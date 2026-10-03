#pragma once
#include "win/common.hpp"
#include <map>
#include <mutex>
#include <thread>

namespace are::win {
// QPC interpolation between hardware sample-position observations. All clock
// state touched by the ASIO callback is atomic, independent of advisory locks.
class ClockTimeline {
public:
    ClockTimeline() noexcept;
    [[nodiscard]] REFERENCE_TIME now() const noexcept;
    void anchor(REFERENCE_TIME time) noexcept;
    static REFERENCE_TIME qpc_time() noexcept;
private:
    std::atomic<unsigned> sequence_{};
    std::atomic<REFERENCE_TIME> anchor_time_{};
    std::atomic<REFERENCE_TIME> anchor_qpc_{};
    mutable std::atomic<REFERENCE_TIME> last_{};
};
class ReferenceClock final : public IReferenceClock {
public:
    explicit ReferenceClock(std::shared_ptr<ClockTimeline> timeline);
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override;
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetTime(REFERENCE_TIME* time) override;
    STDMETHODIMP AdviseTime(REFERENCE_TIME base, REFERENCE_TIME stream, HEVENT event, DWORD_PTR* cookie) override;
    STDMETHODIMP AdvisePeriodic(REFERENCE_TIME start, REFERENCE_TIME period, HSEMAPHORE semaphore, DWORD_PTR* cookie) override;
    STDMETHODIMP Unadvise(DWORD_PTR cookie) override;
private:
    ~ReferenceClock();
    struct Advice { REFERENCE_TIME due; REFERENCE_TIME period; HANDLE handle; };
    void worker();
    ModuleRef module_;
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<ClockTimeline> timeline_;
    Handle wake_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Handle stop_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::mutex mutex_;
    std::map<DWORD_PTR, Advice> advice_;
    DWORD_PTR next_cookie_{1};
    std::thread thread_;
};
} // namespace are::win
