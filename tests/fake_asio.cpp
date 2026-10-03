#include "fake_asio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace are::test {
namespace {
std::array<std::atomic<std::uint64_t>,32> nonzero{};
std::array<std::atomic<double>,32> peaks{};
}
OutputMetrics output_metrics() noexcept {
    OutputMetrics result;
    for (std::size_t c=0;c<32;++c) { result.nonzero[c]=nonzero[c].load(); result.peak[c]=peaks[c].load(); }
    return result;
}
FakeASIO::~FakeASIO() { running_.store(false); if (callback_thread_.joinable()) { callback_thread_.request_stop(); callback_thread_.join(); } }
STDMETHODIMP FakeASIO::QueryInterface(REFIID iid, void** out) { if (!out) return E_POINTER; *out = nullptr; if (iid != IID_IUnknown && iid != fake_clsid) return E_NOINTERFACE; *out = static_cast<IASIO*>(this); AddRef(); return S_OK; }
STDMETHODIMP_(ULONG) FakeASIO::Release() { const auto n = --refs_; if (!n) delete this; return n; }
ASIOBool FakeASIO::init(void* handle) { initial_thread_ = GetCurrentThreadId(); return config_.init_ok && handle ? ASIOTrue : ASIOFalse; }
void FakeASIO::getDriverName(char* name) { check_thread(); strcpy_s(name, 32, "Simulated ASIO test driver"); }
void FakeASIO::getErrorMessage(char* text) { check_thread(); strcpy_s(text, 124, "Simulated driver failure"); }
ASIOError FakeASIO::start() {
    check_thread(); ++starts;
    for (const auto& channel : data_) for (const auto& buffer : channel) for (const auto sample : buffer) if (sample != std::byte{}) { ++stale_start_buffers; break; }
    if (config_.start_result != ASE_OK) return config_.start_result;
    running_.store(true);
    if (config_.automatic_callbacks) {
        callback_thread_ = std::jthread([this](std::stop_token stop) {
            auto next = std::chrono::steady_clock::now();
            const auto period = std::chrono::nanoseconds(std::int64_t(1e9 * buffer_size_ / config_.sample_rate));
            while (!stop.stop_requested()) { pump(); next += period; std::this_thread::sleep_until(next); }
        });
    }
    return ASE_OK;
}
ASIOError FakeASIO::stop() { check_thread(); ++stops; running_.store(false); if (callback_thread_.joinable()) { callback_thread_.request_stop(); callback_thread_.join(); } return ASE_OK; }
ASIOError FakeASIO::getChannels(long* in, long* out) { check_thread(); *in = 0; *out = config_.output_channels; return ASE_OK; }
ASIOError FakeASIO::getLatencies(long* in, long* out) { check_thread(); *in = 0; *out = config_.latency; return ASE_OK; }
ASIOError FakeASIO::getBufferSize(long* min, long* max, long* preferred, long* granularity) { check_thread(); *min = 16; *max = 4096; *preferred = config_.buffer_size; *granularity = -1; return ASE_OK; }
ASIOError FakeASIO::canSampleRate(ASIOSampleRate) { check_thread(); return config_.rates_supported ? ASE_OK : ASE_NoClock; }
ASIOError FakeASIO::getSampleRate(ASIOSampleRate* rate) { check_thread(); *rate = config_.sample_rate; return ASE_OK; }
ASIOError FakeASIO::setSampleRate(ASIOSampleRate rate) { check_thread(); ++rate_changes; if (!config_.rates_supported) return ASE_NoClock; config_.sample_rate = rate; return ASE_OK; }
ASIOError FakeASIO::getChannelInfo(ASIOChannelInfo* info) { check_thread(); info->type = config_.type; info->isActive = ASIOTrue; strcpy_s(info->name, "Test output"); return ASE_OK; }
ASIOError FakeASIO::createBuffers(ASIOBufferInfo* buffers, long channels, long size, ASIOCallbacks* callbacks) {
    for (std::size_t c=0;c<32;++c) { nonzero[c].store(0); peaks[c].store(0); }
    check_thread(); buffer_size_ = size; callbacks_ = callbacks; next_index_ = 0; position_ = 0;
    const auto format = asio_format(config_.type); if (!format) return ASE_InvalidMode;
    data_.resize(std::size_t(channels));
    for (long c = 0; c < channels; ++c) for (std::size_t b = 0; b < 2; ++b) { auto& data = data_[std::size_t(c)][b]; data.resize(std::size_t(size) * format->sample_bytes()); buffers[c].buffers[b] = data.data(); }
    return ASE_OK;
}
ASIOError FakeASIO::disposeBuffers() { check_thread(); ++disposals; data_.clear(); return ASE_OK; }
void FakeASIO::pump() {
    if (!running_.load()) return;
    ASIOTime time{}; time.timeInfo.flags = kSamplePositionValid | kSampleRateValid; time.timeInfo.sampleRate = config_.sample_rate;
    time.timeInfo.samplePosition = {static_cast<unsigned long>(position_ >> 32), static_cast<unsigned long>(position_ & 0xffffffff)};
    last_index_ = next_index_; callbacks_->bufferSwitchTimeInfo(&time, next_index_, ASIOTrue);
    // The codec smoke test verifies actual rendered content, not only frames
    // or timestamps. The simulator's default transport is Int32LSB.
    if (config_.type==ASIOSTInt32LSB) for (std::size_t c=0;c<data_.size();++c) {
        std::uint64_t count{}; double peak=peaks[c].load();
        const auto& buffer=data_[c][std::size_t(next_index_)];
        for (long i=0;i<buffer_size_;++i) {
            std::int32_t sample{}; std::memcpy(&sample,buffer.data()+std::size_t(i)*4,4);
            if (sample) ++count;
            peak=std::max(peak,std::abs(double(sample)/2147483648.0));
        }
        nonzero[c].fetch_add(count); peaks[c].store(peak);
    }
    next_index_ ^= 1; position_ += std::uint64_t(buffer_size_);
}
} // namespace are::test
