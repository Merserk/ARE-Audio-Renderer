#include "win/engine.hpp"
#include "win/devices.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <stdexcept>

namespace are::win {
namespace {
std::atomic<Engine*> active_engine{}; // ASIO has no callback user-data parameter.
constexpr unsigned notice_latency = 1, notice_rate = 2, notice_reset = 4;
REFERENCE_TIME frames_time(std::uint64_t frames, UINT rate) noexcept {
    return REFERENCE_TIME(frames / rate) * 10000000 + REFERENCE_TIME(frames % rate) * 10000000 / rate;
}
std::uint64_t sample_position(const ASIOSamples& samples) noexcept {
    return (std::uint64_t(samples.hi) << 32) | samples.lo;
}
}
Engine::Engine(std::shared_ptr<ClockTimeline> clock, DriverFactory factory)
    : clock_(std::move(clock)), factory_(std::move(factory)) {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    if (!control_ || !quit_ || !space_ || !progress_) throw std::runtime_error("Cannot create ASIO engine events");
    if (!factory_) factory_ = [](REFCLSID clsid, IASIO** out) {
        // ASIO drivers use their own CLSID as the IASIO interface identifier.
        return CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(out));
    };
    std::promise<HRESULT> ready;
    auto result = ready.get_future();
    thread_ = std::thread([this, ready = std::move(ready)]() mutable { apartment(std::move(ready)); });
    if (FAILED(result.get())) { SetEvent(quit_.get()); thread_.join(); throw std::runtime_error("Cannot initialize ASIO COM apartment"); }
}
Engine::~Engine() {
    abort();
    if (thread_.joinable()) { invoke([this] { close_impl(); }); SetEvent(quit_.get()); thread_.join(); }
}
void Engine::apartment(std::promise<HRESULT> ready) {
    const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { ready.set_value(hr); return; }
    driver_window_ = CreateWindowExW(0, L"STATIC", renderer_name, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    ready.set_value(driver_window_ ? S_OK : E_FAIL);
    const HANDLE handles[]{quit_.get(), control_.get()};
    for (;;) {
        const auto wait = MsgWaitForMultipleObjectsEx(2, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        if (wait == WAIT_OBJECT_0 + 1) {
            for (;;) {
                std::function<void()> task;
                { std::lock_guard lock(tasks_mutex_); if (tasks_.empty()) break; task = std::move(tasks_.front()); tasks_.pop_front(); }
                task();
            }
            notifications();
        }
        if (wait == WAIT_OBJECT_0 + 2) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    close_impl();
    if (driver_window_) DestroyWindow(driver_window_);
    driver_window_ = nullptr;
    CoUninitialize();
}
HRESULT Engine::fail(HRESULT code, std::wstring message) {
    detail_ = std::move(message);
    error_.store(code, std::memory_order_release);
    SetEvent(progress_.get()); SetEvent(space_.get());
    return code;
}
HRESULT Engine::check_asio(ASIOError code, const wchar_t* operation) {
    if (code == ASE_OK || code == ASE_SUCCESS) return S_OK;
    char text[256]{};
    if (driver_) driver_->getErrorMessage(text);
    text[sizeof(text) - 1] = '\0';
    return fail(E_FAIL, std::format(L"{} failed (ASIO {}): {}", operation, code, widen(text)));
}
HRESULT Engine::open(const SourceFormat& format, const Settings& settings, ProcessingOptions processing, ResamplingOptions resampling, double rate) {
    return invoke([this, format, settings, processing, resampling, rate] { return open_impl(format, settings, processing, resampling, rate); });
}
HRESULT Engine::open_impl(const SourceFormat& format, const Settings& settings, ProcessingOptions processing, ResamplingOptions resampling, double playback_rate) {
    close_impl();
    error_.store(S_OK); notices_.store(0); detail_.clear(); driver_name_.clear(); format_text_.clear();
    if (!format.valid()) return fail(VFW_E_TYPE_NOT_ACCEPTED, L"Unsupported PCM format.");
    if (!valid_playback_rate(playback_rate)) return fail(E_INVALIDARG,L"Playback speed must be between 0.05x and 128x.");
    if (processing.output_bits && processing.output_bits!=16 && processing.output_bits!=24 && processing.output_bits!=32)
        return fail(E_INVALIDARG,L"Output PCM precision must be Auto, 16, 24 or 32 bits.");
    processing_=processing;
    if (!valid_src(resampling.algorithm)) return fail(E_INVALIDARG,L"SRC algorithm must be r8brain or Sinc.");
    resampling_=resampling;
    if (settings.driver == CLSID_NULL) return fail(VFW_E_NOT_FOUND, L"Select an ASIO device in ARE Audio Renderer settings first.");
    expected_rate_.store(format.rate, std::memory_order_release);
    Engine* expected = nullptr;
    if (!active_engine.compare_exchange_strong(expected, this)) return fail(HRESULT_FROM_WIN32(ERROR_BUSY), L"Another ARE Audio Renderer instance is using the ASIO callback slot in this player.");
    const auto hr = factory_(settings.driver, driver_.GetAddressOf());
    if (FAILED(hr)) { close_impl(); return fail(hr, L"Could not load the selected ASIO driver. Check the device connection and player/driver architecture."); }
    if (!driver_->init(driver_window_)) {
        char text[256]{}; driver_->getErrorMessage(text); text[255] = '\0';
        auto message = L"ASIO initialization failed: " + widen(text);
        close_impl(); return fail(E_FAIL, std::move(message));
    }
    char name[64]{}; driver_->getDriverName(name); name[63] = '\0'; driver_name_ = widen(name);
    source_ = format;
    auto fail_close = [this](HRESULT code) { close_impl(); return code; };
    long inputs = 0, outputs = 0;
    if (FAILED(check_asio(driver_->getChannels(&inputs, &outputs), L"Read channels"))) return fail_close(error());
    if (outputs < 0 || std::uint64_t(settings.first_channel) + format.channels > std::uint64_t(outputs)) return fail_close(fail(VFW_E_TYPE_NOT_ACCEPTED, L"The selected output range does not have enough channels. No automatic downmix is performed."));
    ASIOSampleRate rate = 0;
    if (FAILED(check_asio(driver_->getSampleRate(&rate), L"Read sample rate"))) return fail_close(error());
    if (!settings.keep_device_rate && (!std::isfinite(rate) || std::abs(rate - format.rate) > 0.01)) {
        if (FAILED(check_asio(driver_->canSampleRate(format.rate), L"Check source sample rate")) ||
            FAILED(check_asio(driver_->setSampleRate(format.rate), L"Set exact source sample rate"))) return fail_close(error());
        if (FAILED(check_asio(driver_->getSampleRate(&rate), L"Verify sample rate"))) return fail_close(error());
        if (!std::isfinite(rate) || std::abs(rate - format.rate) > 0.01) return fail_close(fail(VFW_E_TYPE_NOT_ACCEPTED, L"The ASIO driver did not accept the exact source sample rate."));
    }
    if (!std::isfinite(rate) || rate<8000 || rate>768000 || std::abs(rate-std::round(rate))>0.01)
        return fail_close(fail(VFW_E_TYPE_NOT_ACCEPTED,L"The device reported an unsupported sample rate."));
    output_rate_=UINT(std::round(rate)); expected_rate_.store(output_rate_);
    precision_reduced_=processing.output_bits && (format.kind!=SampleKind::integer || format.valid_bits>processing.output_bits);
    // Rate-change notifications from our own setSampleRate have been verified.
    notices_.fetch_and(~notice_rate);
    long min_size = 0, max_size = 0, preferred = 0, granularity = 0;
    if (FAILED(check_asio(driver_->getBufferSize(&min_size, &max_size, &preferred, &granularity), L"Read buffer sizes"))) return fail_close(error());
    const auto requested = settings.buffer_frames ? long(settings.buffer_frames) : preferred;
    bool valid_size = min_size > 0 && max_size >= min_size && requested >= min_size && requested <= max_size && requested <= 65536;
    if (granularity == -1) valid_size = valid_size && requested > 0 && (requested & (requested - 1)) == 0;
    else if (granularity > 0) valid_size = valid_size && (requested - min_size) % granularity == 0;
    else if (granularity == 0) valid_size = valid_size && requested == preferred;
    else valid_size = false;
    if (!valid_size) return fail_close(fail(E_INVALIDARG, L"This buffer size is not supported by the driver. Select Driver default or a supported size."));
    buffer_frames_ = UINT(requested);
    buffers_.resize(format.channels);
    formats_.clear(); formats_.reserve(format.channels);
    for (UINT c = 0; c < format.channels; ++c) {
        ASIOChannelInfo info{}; info.channel = long(settings.first_channel + c); info.isInput = ASIOFalse;
        if (FAILED(check_asio(driver_->getChannelInfo(&info), L"Read output format"))) return fail_close(error());
        const auto output = asio_format(info.type);
        if (!output) return fail_close(fail(VFW_E_TYPE_NOT_ACCEPTED,L"Unsupported ASIO channel format."));
        const UINT capacity=output->kind==SampleKind::integer ? output->valid_bits : output->kind==SampleKind::float32 ? 24u : 32u;
        if (processing.output_bits>capacity) return fail_close(fail(VFW_E_TYPE_NOT_ACCEPTED,
            std::format(L"{}-bit PCM is not supported by this ASIO channel ({} bits available). Choose Auto or a lower precision.",processing.output_bits,capacity)));
        // Auto chooses the driver's native format. LAV decodes Opus and many
        // other codecs to Float32, while most hardware ASIO outputs use integer
        // PCM. Keep the exact path where possible and otherwise use the existing
        // normalized, dithered conversion instead of rejecting decoded audio.
        precision_reduced_=precision_reduced_ || !is_lossless(format,*output);
        formats_.push_back(*output);
        buffers_[c] = ASIOBufferInfo{ASIOFalse, long(settings.first_channel + c), {nullptr, nullptr}};
    }
    try {
        configure_processing(playback_rate);
        normalized_.assign(std::size_t(buffer_frames_)*format.channels,0);
    } catch (const std::exception& error) {
        return fail_close(fail(E_FAIL,L"Cannot initialize sample processing: "+widen(error.what())));
    }
    producer_timestamp_=no_timestamp;
    if (FAILED(check_asio(driver_->createBuffers(buffers_.data(), format.channels, requested, &callbacks_), L"Create ASIO buffers"))) return fail_close(error());
    buffers_created_ = true;
    for (std::size_t c = 0; c < buffers_.size(); ++c) {
        for (const auto p : buffers_[c].buffers) {
            if (!p) return fail_close(fail(E_FAIL, L"The ASIO driver returned an invalid audio buffer."));
            std::memset(p, 0, buffer_frames_ * formats_[c].sample_bytes());
        }
    }
    long input_latency = 0, output_latency = 0;
    if (FAILED(check_asio(driver_->getLatencies(&input_latency, &output_latency), L"Read output latency"))) return fail_close(error());
    if (output_latency < 0 || output_latency > long(output_rate_) * 10) return fail_close(fail(E_FAIL, L"The ASIO driver reported invalid latency."));
    latency_.store(UINT(output_latency));
    output_ready_ = driver_->outputReady() == ASE_OK;
    const auto& output = formats_.front();
    const auto type = output.kind == SampleKind::integer ? L"PCM" : output.kind == SampleKind::float32 ? L"Float32" : L"Float64";
    format_text_ = std::format(L"{} {} valid bits / {}-bit container{}", type, output.valid_bits, output.container_bits, output.big_endian ? L" / big endian" : L"");
    if (std::ranges::any_of(formats_, [&](const OutputFormat& f) { return f.kind != output.kind || f.valid_bits != output.valid_bits || f.container_bits != output.container_bits; })) format_text_ += L" (mixed channel formats)";
    first_timestamp_.store(no_timestamp); delivered_.store(0); underruns_.store(0); overloads_.store(0);
    last_delivery_end_.store(0); eos_.store(false); accepting_.store(true); opened_.store(true);
    processor_.configure(output_rate_); transitioning_.store(false); silent_callbacks_.store(0);
    callback_generation_ = restart_generation_.load(); prerolled_ = false;
    detail_ = converter_.active() ? resampling.algorithm==SrcAlgorithm::sinc
        ? L"Sinc (SoX) Float64 linear-phase SRC: 33-bit design precision / 0.2% Nyquist transition band. Conversion is not bit-perfect."
        : L"r8brain Float64 linear-phase SRC: 0.5% transition band / 218 dB stop-band target. Conversion is not bit-perfect."
        : precision_reduced_ ? L"Output format conversion. Integer quantization uses TPDF dither; converted audio is not bit-perfect."
        : L"Matching rates: SRC bypassed. Unity gain preserves steady samples; lower volume or smoothing modifies samples.";
    return S_OK;
}
void Engine::disable_consumer() {
    const auto previous = render_gate_.exchange(0, std::memory_order_acq_rel);
    if (previous == 2) {
        // New callbacks cannot enter the queue after the gate is disabled.
        // Wait on the control thread; the ASIO callback never waits or locks.
        while (callback_sequence_.load(std::memory_order_acquire) & 1)
            WaitForSingleObject(space_.get(), 1);
    }
}
void Engine::pause_impl() {
    disable_consumer();
    playing_.store(false, std::memory_order_release);
    // Keep the device clock and ASIO stream alive through pause/seek. The
    // callback fades to silence without consuming or replaying queued samples.
}
void Engine::close_impl() {
    accepting_.store(false); pause_impl();
    if (started_ && driver_) {
        silent_callbacks_.store(0);
        const auto frames = std::uint64_t(output_rate_ / 200) + buffer_frames_ * 2ull + latency_.load();
        const auto wait_ms = std::min<std::uint64_t>(250, (frames * 1000 + output_rate_ - 1) / output_rate_ + 20);
        const auto deadline = GetTickCount64() + wait_ms;
        const auto drain_until = clock_->now() + frames_time(frames, output_rate_);
        while (GetTickCount64() < deadline &&
               (silent_callbacks_.load() < 2 || clock_->now() < drain_until)) {
            // Some drivers dispatch callbacks through this apartment's window.
            const HANDLE progress = progress_.get();
            if (MsgWaitForMultipleObjectsEx(1, &progress, 2, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0 + 1) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
        }
        driver_->stop(); started_ = false;
        clock_->anchor(clock_->now());
    }
    if (buffers_created_ && driver_) driver_->disposeBuffers();
    buffers_created_ = false; opened_.store(false);
    Engine* expected = this; active_engine.compare_exchange_strong(expected, nullptr);
    driver_.Reset(); buffers_.clear(); formats_.clear();
    SetEvent(space_.get()); SetEvent(progress_.get());
}
void Engine::close() { invoke([this] { close_impl(); }); }
void Engine::pause() { invoke([this] { pause_impl(); }); }
HRESULT Engine::start(REFERENCE_TIME graph_start, REFERENCE_TIME graph_now) {
    return invoke([this, graph_start, graph_now] {
        if (!opened()) return fail(VFW_E_NOT_CONNECTED, L"No ASIO device is open.");
        if (FAILED(error())) return error();
        if (playing_.load()) return S_OK;
        graph_start_.store(graph_start); graph_offset_.store(graph_now - clock_->now()); schedule_.store(true);
        restart_generation_.fetch_add(1, std::memory_order_release);
        if (started_) {
            playing_.store(true, std::memory_order_release); render_gate_.store(1, std::memory_order_release); return S_OK;
        }
        hardware_origin_valid_ = false; callback_frames_ = 0; clock_origin_ = clock_->now();
        // A resumed driver may play its second buffer before the first callback.
        // Never replay audio left in the hardware buffers by the prior run.
        for (std::size_t c = 0; c < buffers_.size(); ++c) for (auto p : buffers_[c].buffers) std::memset(p, 0, buffer_frames_ * formats_[c].sample_bytes());
        playing_.store(true, std::memory_order_release);
        render_gate_.store(1, std::memory_order_release);
        const auto code = driver_->start();
        if (code != ASE_OK && code != ASE_SUCCESS) { disable_consumer(); playing_.store(false); driver_->stop(); return check_asio(code, L"Start ASIO"); }
        started_ = true; return S_OK;
    });
}
void Engine::abort() noexcept { accepting_.store(false, std::memory_order_release); SetEvent(space_.get()); SetEvent(progress_.get()); }
void Engine::reset() {
    invoke([this] {
        pause_impl(); queue_.reset(); first_timestamp_.store(no_timestamp); delivered_.store(0);
        last_delivery_end_.store(0); eos_.store(false); accepting_.store(opened());
        converter_.clear(); producer_timestamp_=no_timestamp;
        restart_generation_.fetch_add(1, std::memory_order_release);
    });
}
void Engine::configure_processing(double rate) {
    // One Float64 conversion handles both playback speed and the device rate.
    // No overlap/time stretching: the pitch follows speed like DirectSound.
    converter_.configure(source_,output_rate_,resampling_.algorithm,rate);
    queue_format_=converter_.active()
        ? SourceFormat{SampleKind::float64,output_rate_,source_.channels,64,64,source_.channel_mask} : source_;
    quantize_input_=precision_reduced_ || converter_.active();
    queue_.configure(std::max<std::size_t>(output_rate_ / 4,std::size_t(buffer_frames_)*8),queue_format_.frame_bytes());
    playback_rate_=rate;
}
HRESULT Engine::set_rate(double rate) {
    if (!valid_playback_rate(rate)) return E_INVALIDARG;
    return invoke([this,rate] {
        if (!opened()) return VFW_E_NOT_CONNECTED;
        if (rate==playback_rate_) return S_OK;
        pause_impl();
        try { configure_processing(rate); }
        catch (const std::exception& exception) { return fail(E_FAIL,L"Cannot change playback speed: "+widen(exception.what())); }
        return S_OK;
    });
}
HRESULT Engine::submit(std::span<const std::byte> input, REFERENCE_TIME timestamp) {
    if (!opened()) return VFW_E_NOT_CONNECTED;
    if (input.size() % source_.frame_bytes()) return VFW_E_TYPE_NOT_ACCEPTED;
    if (eos_.load(std::memory_order_acquire)) return VFW_E_SAMPLE_REJECTED_EOS;
    if (producer_timestamp_==no_timestamp) producer_timestamp_=timestamp;
    if (!converter_.active()) return enqueue(input,timestamp);
    try {
        while (!input.empty()) {
            if (!accepting_.load(std::memory_order_acquire)) return S_FALSE;
            if (FAILED(error())) return error();
            const auto bytes=std::min(input.size(),converter_.block_frames()*source_.frame_bytes());
            const auto output=converter_.process(input.first(bytes));
            const auto result=enqueue(std::as_bytes(output),producer_timestamp_);
            if (result!=S_OK) return result;
            input=input.subspan(bytes);
        }
        return S_OK;
    } catch (const std::exception& exception) {
        return invoke([this,message=std::string(exception.what())]{return fail(E_FAIL,L"Sample rate conversion failed: "+widen(message.c_str()));});
    }
}
HRESULT Engine::end_of_stream() {
    if (converter_.active()) {
        try {
            do {
                if (!accepting_.load(std::memory_order_acquire)) return S_FALSE;
                const auto output=converter_.drain();
                const auto result=enqueue(std::as_bytes(output),producer_timestamp_);
                if (result!=S_OK) return result;
            } while (!converter_.done());
        } catch (const std::exception& exception) {
            return invoke([this,message=std::string(exception.what())]{return fail(E_FAIL,L"Resampler drain failed: "+widen(message.c_str()));});
        }
    }
    eos_.store(true,std::memory_order_release); SetEvent(progress_.get()); return S_OK;
}
HRESULT Engine::enqueue(std::span<const std::byte> input, REFERENCE_TIME timestamp) {
    while (!input.empty()) {
        if (!accepting_.load(std::memory_order_acquire)) return S_FALSE;
        if (FAILED(error())) return error();
        const auto count = queue_.push(input);
        if (count) {
            auto expected = no_timestamp;
            first_timestamp_.compare_exchange_strong(expected, timestamp, std::memory_order_release);
        }
        input = input.subspan(count * queue_format_.frame_bytes());
        if (!input.empty() && count == 0) WaitForSingleObject(space_.get(), 20);
    }
    return S_OK;
}
bool Engine::drained() const noexcept {
    const auto sequence = callback_sequence_.load(std::memory_order_acquire);
    if ((sequence & 1) || !eos_.load(std::memory_order_acquire)) return false;
    const bool done = queue_.size() == 0 && clock_->now() >= last_delivery_end_.load(std::memory_order_acquire);
    return done && callback_sequence_.load(std::memory_order_acquire) == sequence;
}
EngineStatus Engine::status() {
    return live_status().engine;
}
LiveStatus Engine::live_status() {
    return invoke([this] {
        LiveStatus live;
        auto& s = live.engine;
        s.opened = opened(); s.playing = playing_.load(); s.muted = volume_db100_.load() == -10000; s.sample_rate = output_rate_;
        s.channels = source_.channels; s.source_bits = source_.valid_bits; s.buffer_frames = buffer_frames_;
        s.output_latency_frames = latency_.load(); s.queued_frames = opened() ? UINT(queue_.size()) : 0;
        s.delivered_frames = delivered_.load(); s.underruns = underruns_.load(); s.overloads = overloads_.load(); s.error = error();
        wcsncpy_s(s.driver_name, driver_name_.c_str(), _TRUNCATE); wcsncpy_s(s.output_format, format_text_.c_str(), _TRUNCATE); wcsncpy_s(s.detail, detail_.c_str(), _TRUNCATE);
        if (s.opened) {
            live.input = {static_cast<AudioSampleType>(source_.kind), source_.valid_bits, source_.container_bits};
            if (!formats_.empty()) {
                const auto& output = formats_.front();
                live.output = {static_cast<AudioSampleType>(output.kind), output.valid_bits, output.container_bits};
                live.mixed_output = std::ranges::any_of(formats_, [&](const OutputFormat& format) {
                    return format.kind != output.kind || format.valid_bits != output.valid_bits || format.container_bits != output.container_bits;
                });
            }
        }
        return live;
    });
}
ProcessingStatus Engine::processing_status() {
    return invoke([this]{ return ProcessingStatus{opened()?source_.rate:0,opened()?output_rate_:0,opened()?processing_.output_bits:0,
        opened() && converter_.active(),opened() && precision_reduced_,FALSE}; });
}
ResamplingStatus Engine::resampling_status() {
    return invoke([this]{ return ResamplingStatus{resampling_.algorithm,opened() && converter_.active(),FALSE}; });
}
void Engine::notifications() {
    const auto flags = notices_.exchange(0);
    if ((flags & notice_latency) && driver_) {
        long in = 0, out = 0;
        if (driver_->getLatencies(&in, &out) == ASE_OK && out >= 0 && out <= long(output_rate_) * 10) latency_.store(UINT(out));
    }
    if ((flags & (notice_rate | notice_reset)) && opened()) {
        abort(); pause_impl();
        fail(VFW_E_TYPE_NOT_ACCEPTED, L"The ASIO device changed rate or requested a reset. Stop and reopen the file to renegotiate the exact format.");
    }
}
void Engine::render(long index, const ASIOTime* time) noexcept {
    if ((index != 0 && index != 1) || !buffers_created_) return;
    struct ProgressGuard {
        std::atomic<std::uint64_t>& sequence;
        explicit ProgressGuard(std::atomic<std::uint64_t>& value) noexcept : sequence(value) { sequence.fetch_add(1, std::memory_order_acq_rel); }
        ~ProgressGuard() { sequence.fetch_add(1, std::memory_order_release); }
    } progress_guard(callback_sequence_);
    for (std::size_t c = 0; c < buffers_.size(); ++c) std::memset(buffers_[c].buffers[index], 0, std::size_t(buffer_frames_) * formats_[c].sample_bytes());
    if (FAILED(error())) { if (output_ready_) driver_->outputReady(); return; }
    std::uint64_t hardware_frames = callback_frames_;
    if (time && (time->timeInfo.flags & kSamplePositionValid)) {
        const auto position = sample_position(time->timeInfo.samplePosition);
        if (!hardware_origin_valid_) { hardware_origin_ = position; hardware_origin_valid_ = true; }
        if (position >= hardware_origin_) hardware_frames = position - hardware_origin_;
        if ((time->timeInfo.flags & kSampleRateValid) && (!std::isfinite(time->timeInfo.sampleRate) || std::abs(time->timeInfo.sampleRate - output_rate_) > 0.01)) { rate_changed(time->timeInfo.sampleRate); if (output_ready_) driver_->outputReady(); return; }
    }
    clock_->anchor(clock_origin_ + frames_time(hardware_frames, output_rate_));
    callback_frames_ += buffer_frames_;
    const auto now = clock_->now();
    unsigned ready = 1;
    const bool consuming = render_gate_.compare_exchange_strong(ready, 2, std::memory_order_acquire);
    const auto generation = restart_generation_.load(std::memory_order_acquire);
    if (callback_generation_ != generation) {
        processor_.restart(); prerolled_ = false; callback_generation_ = generation;
    }
    std::size_t first_offset = 0, offset = 0;
    if (consuming && playing_.load(std::memory_order_acquire)) {
        const auto timestamp = first_timestamp_.load(std::memory_order_acquire);
        if (!prerolled_ && (queue_.size() >= buffer_frames_ || eos_.load(std::memory_order_acquire))) prerolled_ = true;
        if (timestamp != no_timestamp && prerolled_) {
            if (schedule_.load(std::memory_order_acquire)) {
                const auto due = graph_start_.load() + timestamp + frames_time(delivered_.load(), output_rate_) - graph_offset_.load();
                const auto lead = due - now - frames_time(latency_.load(), output_rate_);
                if (lead > 0) {
                    const auto frames = (std::uint64_t(lead) * output_rate_ + 9999999) / 10000000;
                    first_offset = offset = std::size_t(std::min<std::uint64_t>(frames, buffer_frames_));
                }
                if (offset < buffer_frames_) schedule_.store(false);
            }
            while (offset < buffer_frames_) {
                const auto data = queue_.front(buffer_frames_ - offset);
                if (data.empty()) break;
                const auto count = data.size() / queue_format_.frame_bytes();
                for (std::size_t c = 0; c < buffers_.size(); ++c) {
                    const auto bytes = formats_[c].sample_bytes();
                    auto* destination = static_cast<std::byte*>(buffers_[c].buffers[index]) + offset * bytes;
                    if (is_lossless(queue_format_,formats_[c]))
                        convert_channel(queue_format_, formats_[c], data, std::uint16_t(c), {destination, count * bytes});
                    else
                        for (std::size_t i=0; i<count; ++i)
                            normalized_[c*buffer_frames_+offset+i]=normalized_sample(queue_format_,data.data()+i*queue_format_.frame_bytes()+c*queue_format_.sample_bytes());
                }
                queue_.consume(count); delivered_.fetch_add(count, std::memory_order_release); offset += count;
            }
            if (offset < buffer_frames_ && !eos_.load(std::memory_order_acquire)) {
                underruns_.fetch_add(1, std::memory_order_relaxed); prerolled_ = false;
            }
        }
    }
    std::array<PlanarBuffer, 32> planar{};
    for (std::size_t c = 0; c < buffers_.size(); ++c)
        planar[c] = {formats_[c], {static_cast<std::byte*>(buffers_[c].buffers[index]), std::size_t(buffer_frames_) * formats_[c].sample_bytes()},
            !consuming || is_lossless(queue_format_,formats_[c]) ? std::span<const double>{} : std::span<const double>{normalized_.data()+c*buffer_frames_,buffer_frames_}};
    processor_.process({planar.data(), buffers_.size()}, buffer_frames_, first_offset, offset, volume_db100_.load(), smooth_.load(),
        std::uint16_t(processing_.output_bits),consuming && quantize_input_);
    transitioning_.store(processor_.transitioning(), std::memory_order_release);
    if (processor_.silent()) silent_callbacks_.fetch_add(1); else silent_callbacks_.store(0);
    if (offset > first_offset) last_delivery_end_.store(now + frames_time(offset + latency_.load(), output_rate_), std::memory_order_release);
    if (!processor_.silent()) last_delivery_end_.store(now + frames_time(buffer_frames_ + latency_.load(), output_rate_), std::memory_order_release);
    if (consuming) {
        unsigned busy = 2;
        render_gate_.compare_exchange_strong(busy, 1, std::memory_order_release);
    }
    SetEvent(space_.get()); SetEvent(progress_.get());
    if (output_ready_) driver_->outputReady();
}
void Engine::buffer_switch(long index, ASIOBool) noexcept { if (auto* e = active_engine.load(std::memory_order_acquire)) e->render(index, nullptr); }
ASIOTime* Engine::buffer_time(ASIOTime* time, long index, ASIOBool) noexcept { if (auto* e = active_engine.load(std::memory_order_acquire)) e->render(index, time); return time; }
void Engine::rate_changed(ASIOSampleRate rate) noexcept {
    if (auto* e = active_engine.load(std::memory_order_acquire)) {
        if (!std::isfinite(rate) || std::abs(rate - e->expected_rate_.load(std::memory_order_acquire)) > 0.01) { e->notices_.fetch_or(notice_rate); e->playing_.store(false); SetEvent(e->control_.get()); }
    }
}
long Engine::asio_message(long selector, long value, void*, double*) noexcept {
    if (selector == kAsioSelectorSupported) return value == kAsioEngineVersion || value == kAsioSupportsTimeInfo || value == kAsioLatenciesChanged || value == kAsioOverload;
    if (selector == kAsioEngineVersion) return 2;
    if (selector == kAsioSupportsTimeInfo) return 1;
    auto* e = active_engine.load(std::memory_order_acquire);
    if (!e) return 0;
    if (selector == kAsioLatenciesChanged) { e->notices_.fetch_or(notice_latency); SetEvent(e->control_.get()); return 1; }
    if (selector == kAsioOverload) { e->overloads_.fetch_add(1); return 1; }
    if (selector == kAsioResetRequest || selector == kAsioResyncRequest) { e->notices_.fetch_or(notice_reset); e->playing_.store(false); SetEvent(e->control_.get()); }
    return 0;
}
} // namespace are::win
