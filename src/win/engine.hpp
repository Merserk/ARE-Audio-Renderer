#pragma once
#include "win/clock.hpp"
#include "win/interfaces.hpp"
#include "core/format.hpp"
#include "core/signal_processor.hpp"
#include "core/resampler.hpp"
#include "core/frame_queue.hpp"
#include "win/asio.hpp"
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <vector>

namespace are::win {
using DriverFactory = std::function<HRESULT(REFCLSID, IASIO**)>;
class Engine {
public:
    explicit Engine(std::shared_ptr<ClockTimeline> clock, DriverFactory factory = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    HRESULT open(const SourceFormat& format, const Settings& settings, ProcessingOptions processing = {}, ResamplingOptions resampling = {}, double rate = 1.0);
    void close();
    HRESULT start(REFERENCE_TIME graph_start, REFERENCE_TIME graph_now);
    void pause();
    // Abort first, then serialize against Receive before reset/close.
    void abort() noexcept;
    void reset();
    // Producer must be aborted and serialized before changing a segment rate.
    HRESULT set_rate(double rate);
    void allow_input() noexcept { accepting_.store(true, std::memory_order_release); }
    HRESULT submit(std::span<const std::byte> input, REFERENCE_TIME timestamp);
    HRESULT end_of_stream();
    [[nodiscard]] bool drained() const noexcept;
    [[nodiscard]] HRESULT error() const noexcept { return error_.load(std::memory_order_acquire); }
    [[nodiscard]] bool opened() const noexcept { return opened_.load(std::memory_order_acquire); }
    [[nodiscard]] HANDLE progress_event() const noexcept { return progress_.get(); }
    void set_volume(long volume_db100) noexcept { volume_db100_.store(volume_db100, std::memory_order_release); }
    void set_mute(bool mute) noexcept { set_volume(mute ? -10000 : 0); }
    void set_smoothing(bool smooth) noexcept { smooth_.store(smooth, std::memory_order_release); }
    [[nodiscard]] SignalStatus signal_status() const noexcept { return {volume_db100_.load(), smooth_.load(), transitioning_.load()}; }
    EngineStatus status();
    LiveStatus live_status();
    ProcessingStatus processing_status();
    ResamplingStatus resampling_status();
private:
    template<class F> auto invoke(F&& action) -> std::invoke_result_t<F> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(action));
        auto result = task->get_future();
        { std::lock_guard lock(tasks_mutex_); tasks_.emplace_back([task] { (*task)(); }); }
        SetEvent(control_.get());
        return result.get();
    }
    void apartment(std::promise<HRESULT> ready);
    HRESULT open_impl(const SourceFormat& format, const Settings& settings, ProcessingOptions processing, ResamplingOptions resampling, double rate);
    void configure_processing(double rate);
    HRESULT enqueue(std::span<const std::byte> input, REFERENCE_TIME timestamp);
    void pause_impl();
    void disable_consumer();
    void close_impl();
    HRESULT fail(HRESULT code, std::wstring message);
    HRESULT check_asio(ASIOError code, const wchar_t* operation);
    void notifications();
    void render(long index, const ASIOTime* time) noexcept;
    static void buffer_switch(long index, ASIOBool direct) noexcept;
    static ASIOTime* buffer_time(ASIOTime* time, long index, ASIOBool direct) noexcept;
    static void rate_changed(ASIOSampleRate rate) noexcept;
    static long asio_message(long selector, long value, void* message, double* opt) noexcept;

    std::shared_ptr<ClockTimeline> clock_;
    DriverFactory factory_;
    Handle control_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Handle quit_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle space_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Handle progress_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::mutex tasks_mutex_;
    std::deque<std::function<void()>> tasks_;
    std::thread thread_;
    ComPtr<IASIO> driver_; // accessed by apartment, plus outputReady in ASIO callback
    HWND driver_window_{};
    SourceFormat source_{};
    SourceFormat queue_format_{};
    UINT output_rate_{};
    ProcessingOptions processing_{};
    ResamplingOptions resampling_{};
    bool precision_reduced_{};
    bool quantize_input_{};
    SampleRateConverter converter_;
    double playback_rate_{1.0};
    REFERENCE_TIME producer_timestamp_{no_timestamp};
    std::vector<double> normalized_; // Preallocated channel-major callback scratch.
    std::vector<ASIOBufferInfo> buffers_;
    // Drivers may retain this address until disposeBuffers; never pass a local.
    ASIOCallbacks callbacks_{buffer_switch, rate_changed, asio_message, buffer_time};
    std::vector<OutputFormat> formats_;
    FrameQueue queue_;
    UINT buffer_frames_{};
    bool buffers_created_{};
    bool started_{};
    bool output_ready_{};
    std::wstring driver_name_;
    std::wstring format_text_;
    std::wstring detail_;
    std::atomic<bool> accepting_{};
    std::atomic<bool> opened_{};
    std::atomic<bool> playing_{};
    std::atomic<long> volume_db100_{};
    std::atomic<bool> smooth_{true};
    std::atomic<bool> transitioning_{};
    // 0 = disabled, 1 = ready, 2 = callback consuming. Reset only after quiescence.
    std::atomic<unsigned> render_gate_{};
    std::atomic<unsigned> restart_generation_{};
    std::atomic<unsigned> silent_callbacks_{};
    std::atomic<bool> eos_{};
    std::atomic<bool> schedule_{};
    std::atomic<HRESULT> error_{S_OK};
    std::atomic<unsigned> notices_{};
    std::atomic<UINT> latency_{};
    std::atomic<UINT> expected_rate_{};
    static constexpr auto no_timestamp = (std::numeric_limits<REFERENCE_TIME>::max)();
    std::atomic<REFERENCE_TIME> first_timestamp_{no_timestamp};
    std::atomic<REFERENCE_TIME> graph_start_{};
    std::atomic<REFERENCE_TIME> graph_offset_{};
    std::atomic<REFERENCE_TIME> last_delivery_end_{};
    std::atomic<std::uint64_t> delivered_{};
    std::atomic<std::uint64_t> underruns_{};
    std::atomic<std::uint64_t> overloads_{};
    std::atomic<std::uint64_t> callback_sequence_{};
    // These are used by the single ASIO callback thread, initialized before start.
    std::uint64_t hardware_origin_{};
    std::uint64_t callback_frames_{};
    REFERENCE_TIME clock_origin_{};
    bool hardware_origin_valid_{};
    SignalProcessor processor_;
    unsigned callback_generation_{};
    bool prerolled_{};
};
} // namespace are::win
