#include "win/clock.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace are::win {
REFERENCE_TIME ClockTimeline::qpc_time() noexcept {
    static const LONGLONG frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return (value.QuadPart / frequency) * 10000000 + (value.QuadPart % frequency) * 10000000 / frequency;
}
ClockTimeline::ClockTimeline() noexcept {
    const auto time = qpc_time();
    anchor_time_.store(time);
    anchor_qpc_.store(time);
    last_.store(time);
}
void ClockTimeline::anchor(REFERENCE_TIME time) noexcept {
    sequence_.fetch_add(1, std::memory_order_acq_rel);
    anchor_qpc_.store(qpc_time(), std::memory_order_relaxed);
    anchor_time_.store(time, std::memory_order_relaxed);
    sequence_.fetch_add(1, std::memory_order_release);
}
REFERENCE_TIME ClockTimeline::now() const noexcept {
    REFERENCE_TIME time{};
    for (;;) {
        const auto seq = sequence_.load(std::memory_order_acquire);
        if (seq & 1) continue;
        const auto aq = anchor_qpc_.load(std::memory_order_relaxed);
        const auto at = anchor_time_.load(std::memory_order_relaxed);
        time = at + qpc_time() - aq;
        if (sequence_.load(std::memory_order_acquire) == seq) break;
    }
    auto last = last_.load(std::memory_order_relaxed);
    while (time > last && !last_.compare_exchange_weak(last, time, std::memory_order_relaxed)) {}
    return std::max(time, last);
}
ReferenceClock::ReferenceClock(std::shared_ptr<ClockTimeline> timeline) : timeline_(std::move(timeline)) {
    if (!wake_ || !stop_) throw std::runtime_error("Cannot create reference-clock events");
    thread_ = std::thread([this] { worker(); });
}
ReferenceClock::~ReferenceClock() { SetEvent(stop_.get()); if (thread_.joinable()) thread_.join(); }
STDMETHODIMP ReferenceClock::QueryInterface(REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_IReferenceClock) return E_NOINTERFACE;
    *out = static_cast<IReferenceClock*>(this); AddRef(); return S_OK;
}
STDMETHODIMP_(ULONG) ReferenceClock::Release() { const auto n = --refs_; if (!n) delete this; return n; }
STDMETHODIMP ReferenceClock::GetTime(REFERENCE_TIME* time) { if (!time) return E_POINTER; *time = timeline_->now(); return S_OK; }
STDMETHODIMP ReferenceClock::AdviseTime(REFERENCE_TIME base, REFERENCE_TIME stream, HEVENT event, DWORD_PTR* cookie) {
    if (!cookie) return E_POINTER;
    *cookie = 0;
    if (!event || (stream > 0 && base > (std::numeric_limits<REFERENCE_TIME>::max)() - stream) ||
        (stream < 0 && base < (std::numeric_limits<REFERENCE_TIME>::min)() - stream)) return E_INVALIDARG;
    try { std::lock_guard lock(mutex_); const auto id = next_cookie_++; advice_.emplace(id, Advice{base + stream, 0, reinterpret_cast<HANDLE>(event)}); *cookie = id; }
    catch (...) { return E_OUTOFMEMORY; }
    SetEvent(wake_.get()); return S_OK;
}
STDMETHODIMP ReferenceClock::AdvisePeriodic(REFERENCE_TIME start, REFERENCE_TIME period, HSEMAPHORE sem, DWORD_PTR* cookie) {
    if (!cookie) return E_POINTER;
    *cookie = 0;
    if (!sem || period <= 0 || start <= 0) return E_INVALIDARG;
    try { std::lock_guard lock(mutex_); const auto id = next_cookie_++; advice_.emplace(id, Advice{start, period, reinterpret_cast<HANDLE>(sem)}); *cookie = id; }
    catch (...) { return E_OUTOFMEMORY; }
    SetEvent(wake_.get()); return S_OK;
}
STDMETHODIMP ReferenceClock::Unadvise(DWORD_PTR cookie) {
    std::lock_guard lock(mutex_);
    const auto removed = advice_.erase(cookie);
    SetEvent(wake_.get()); return removed ? S_OK : S_FALSE;
}
void ReferenceClock::worker() {
    Handle timer(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS));
    if (!timer) timer.reset(CreateWaitableTimerW(nullptr, FALSE, nullptr));
    const HANDLE handles[]{stop_.get(), wake_.get(), timer.get()};
    for (;;) {
        auto delay = REFERENCE_TIME(10000000);
        bool pending = false;
        {
            std::lock_guard lock(mutex_);
            const auto now = timeline_->now();
            for (auto it = advice_.begin(); it != advice_.end();) {
                auto& a = it->second;
                if (a.due <= now) {
                    if (!a.period) { SetEvent(a.handle); it = advice_.erase(it); continue; }
                    const auto count = 1 + (now - a.due) / a.period;
                    ReleaseSemaphore(a.handle, LONG(std::min<REFERENCE_TIME>(count, LONG_MAX)), nullptr);
                    a.due += count * a.period;
                }
                delay = std::min(delay, std::max<REFERENCE_TIME>(1, a.due - now));
                ++it;
            }
            pending = !advice_.empty();
        }
        if (!pending) {
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) return;
            continue;
        }
        LARGE_INTEGER due{}; due.QuadPart = -delay;
        if (timer) SetWaitableTimer(timer.get(), &due, 0, nullptr, nullptr, FALSE);
        const DWORD result = WaitForMultipleObjects(timer ? 3 : 2, handles, FALSE, timer ? INFINITE : DWORD(std::max<REFERENCE_TIME>(1, delay / 10000)));
        if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) return;
    }
}
} // namespace are::win
