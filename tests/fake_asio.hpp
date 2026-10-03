#pragma once
#include "win/common.hpp"
#include "core/format.hpp"
#include "win/asio.hpp"
#include <array>
#include <thread>
#include <vector>

namespace are::test {
inline constexpr CLSID fake_clsid{0x6ce814a9,0x235b,0x40cb,{0xa6,0x74,0x1b,0xd1,0xa4,0x5b,0x08,0x79}};
struct OutputMetrics { std::uint64_t nonzero[32]{}; double peak[32]{}; };
OutputMetrics output_metrics() noexcept;
struct FakeConfig {
    long type{ASIOSTInt32LSB};
    long output_channels{8};
    double sample_rate{48000};
    long buffer_size{64};
    long latency{64};
    bool init_ok{true};
    bool rates_supported{true};
    bool automatic_callbacks{};
    ASIOError start_result{ASE_OK};
};
class FakeASIO final : public IASIO {
public:
    explicit FakeASIO(FakeConfig config = {}) : config_(config) {}
    ~FakeASIO();
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override;
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override;
    ASIOBool init(void* handle) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override { check_thread(); return 1; }
    void getErrorMessage(char* text) override;
    ASIOError start() override;
    ASIOError stop() override;
    ASIOError getChannels(long* inputs, long* outputs) override;
    ASIOError getLatencies(long* in, long* out) override;
    ASIOError getBufferSize(long* min, long* max, long* preferred, long* granularity) override;
    ASIOError canSampleRate(ASIOSampleRate rate) override;
    ASIOError getSampleRate(ASIOSampleRate* rate) override;
    ASIOError setSampleRate(ASIOSampleRate rate) override;
    ASIOError getClockSources(ASIOClockSource*, long*) override { return ASE_NotPresent; }
    ASIOError setClockSource(long) override { return ASE_NotPresent; }
    ASIOError getSamplePosition(ASIOSamples*, ASIOTimeStamp*) override { return ASE_NotPresent; }
    ASIOError getChannelInfo(ASIOChannelInfo* info) override;
    ASIOError createBuffers(ASIOBufferInfo* buffers, long channels, long size, ASIOCallbacks* callbacks) override;
    ASIOError disposeBuffers() override;
    ASIOError controlPanel() override { ++panel_calls; return ASE_OK; }
    ASIOError future(long, void*) override { return ASE_NotPresent; }
    ASIOError outputReady() override { return ASE_OK; }
    void pump();
    void notify_rate(double rate) { callbacks_->sampleRateDidChange(rate); }
    [[nodiscard]] const std::vector<std::byte>& last_output(std::size_t channel) const { return data_.at(channel).at(std::size_t(last_index_)); }
    [[nodiscard]] long message(long selector) { return callbacks_->asioMessage(selector, 0, nullptr, nullptr); }
    std::atomic<unsigned> panel_calls{};
    std::atomic<unsigned> wrong_thread_calls{};
    std::atomic<unsigned> starts{};
    std::atomic<unsigned> rate_changes{};
    std::atomic<unsigned> stops{};
    std::atomic<unsigned> disposals{};
    std::atomic<unsigned> stale_start_buffers{};
private:
    void check_thread() { if (initial_thread_ && GetCurrentThreadId() != initial_thread_) ++wrong_thread_calls; }
    std::atomic<ULONG> refs_{1};
    FakeConfig config_;
    DWORD initial_thread_{};
    ASIOCallbacks* callbacks_{}; // Match drivers that retain the host's table.
    std::vector<std::array<std::vector<std::byte>,2>> data_;
    long buffer_size_{};
    long next_index_{};
    long last_index_{};
    std::uint64_t position_{};
    std::atomic<bool> running_{};
    std::jthread callback_thread_;
};
} // namespace are::test
