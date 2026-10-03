#include "win/filter.hpp"
#include "win/settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace are::win {
namespace {
class PinEnum final : public IEnumPins {
public:
    PinEnum(IBaseFilter* filter, IPin* pin, ULONG position = 0) : filter_(filter), pin_(pin), position_(position) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IEnumPins) return E_NOINTERFACE;
        *out = static_cast<IEnumPins*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    STDMETHODIMP Next(ULONG count, IPin** pins, ULONG* fetched) override {
        if (!pins || (!fetched && count != 1)) return E_POINTER;
        if (fetched) *fetched = 0;
        if (count == 0) return S_OK;
        pins[0] = nullptr;
        if (position_ != 0) return S_FALSE;
        pins[0] = pin_; pin_->AddRef(); position_ = 1;
        if (fetched) *fetched = 1;
        return count == 1 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG count) override { const ULONG left = position_ ? 0 : 1; if (count) position_ = 1; return count <= left ? S_OK : S_FALSE; }
    STDMETHODIMP Reset() override { position_ = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumPins** out) override { if (!out) return E_POINTER; *out = new(std::nothrow) PinEnum(filter_.Get(), pin_, position_); return *out ? S_OK : E_OUTOFMEMORY; }
private:
    ModuleRef module_;
    std::atomic<ULONG> refs_{1};
    ComPtr<IBaseFilter> filter_;
    IPin* pin_;
    ULONG position_{};
};
class TypeEnum final : public IEnumMediaTypes {
public:
    explicit TypeEnum(ULONG position = 0) : position_(position) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IEnumMediaTypes) return E_NOINTERFACE;
        *out = static_cast<IEnumMediaTypes*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    STDMETHODIMP Next(ULONG count, AM_MEDIA_TYPE** types, ULONG* fetched) override {
        if (!types || (!fetched && count != 1)) return E_POINTER;
        if (fetched) *fetched = 0;
        ULONG done = 0;
        constexpr WORD bits[]{24, 32, 16, 32, 64};
        while (done < count && position_ < 5) {
            auto* mt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
            if (!mt) return E_OUTOFMEMORY;
            *mt = pcm_media_type(2, 48000, bits[position_]);
            if (!mt->pbFormat) { CoTaskMemFree(mt); return E_OUTOFMEMORY; }
            if (position_ >= 3) { mt->subtype = MEDIASUBTYPE_IEEE_FLOAT; reinterpret_cast<WAVEFORMATEX*>(mt->pbFormat)->wFormatTag = WAVE_FORMAT_IEEE_FLOAT; }
            types[done++] = mt; ++position_;
            if (fetched) *fetched = done;
        }
        return done == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG count) override { const auto left = 5 - position_; position_ += std::min(count, left); return count <= left ? S_OK : S_FALSE; }
    STDMETHODIMP Reset() override { position_ = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumMediaTypes** out) override { if (!out) return E_POINTER; *out = new(std::nothrow) TypeEnum(position_); return *out ? S_OK : E_OUTOFMEMORY; }
private:
    ModuleRef module_;
    std::atomic<ULONG> refs_{1};
    ULONG position_{};
};
REFERENCE_TIME frames_time(std::uint64_t frames, UINT rate) noexcept {
    return REFERENCE_TIME(frames / rate) * 10000000 + REFERENCE_TIME(frames % rate) * 10000000 / rate;
}
}
Renderer::Renderer() : settings_(load_settings()), processing_(load_processing_options()), resampling_(load_resampling_options()), timeline_(std::make_shared<ClockTimeline>()), engine_(timeline_) {
    if (!ready_ || !monitor_stop_) throw std::bad_alloc();
    own_clock_.Attach(new ReferenceClock(timeline_));
    engine_.set_smoothing(load_playback_options().smooth_transitions != FALSE);
    monitor_thread_ = std::thread([this] { monitor(); });
}
Renderer::~Renderer() {
    engine_.abort(); SetEvent(monitor_stop_.get());
    if (monitor_thread_.joinable()) monitor_thread_.join();
    engine_.close();
}
STDMETHODIMP Renderer::QueryInterface(REFIID iid, void** out) {
    if (!out) return E_POINTER; *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_IPersist || iid == IID_IMediaFilter || iid == IID_IBaseFilter) *out = static_cast<IBaseFilter*>(this);
    else if (iid == IID_IAMFilterMiscFlags) *out = static_cast<IAMFilterMiscFlags*>(this);
    else if (iid == IID_ISpecifyPropertyPages) *out = static_cast<ISpecifyPropertyPages*>(this);
    else if (iid == __uuidof(IASIORenderSettings)) *out = static_cast<IASIORenderSettings*>(this);
    else if (iid == __uuidof(IASIORenderStatus)) *out = static_cast<IASIORenderStatus*>(this);
    else if (iid == __uuidof(IASIORenderPlayback)) *out = static_cast<IASIORenderPlayback*>(this);
    else if (iid == __uuidof(IASIORenderProcessing)) *out = static_cast<IASIORenderProcessing*>(this);
    else if (iid == __uuidof(IASIORenderResampling)) *out = static_cast<IASIORenderResampling*>(this);
    else if (iid == IID_IBasicAudio || iid == IID_IDispatch) *out = static_cast<IBasicAudio*>(this);
    else if (iid == IID_IMediaSeeking) *out = static_cast<IMediaSeeking*>(this);
    else if (iid == IID_IQualityControl) *out = static_cast<IQualityControl*>(this);
    else if (iid == IID_IReferenceClock) *out = static_cast<IReferenceClock*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
STDMETHODIMP_(ULONG) Renderer::Release() { const auto n = --refs_; if (!n) delete this; return n; }
STDMETHODIMP Renderer::GetClassID(CLSID* clsid) { if (!clsid) return E_POINTER; *clsid = clsid_renderer; return S_OK; }
STDMETHODIMP Renderer::Stop() {
    std::lock_guard transition(transition_mutex_);
    state_.store(State_Stopped); flushing_.store(false); engine_.abort(); SetEvent(ready_.get());
    std::lock_guard stream(receive_mutex_);
    engine_.close(); eos_.store(false); complete_sent_.store(false); error_sent_.store(false);
    if (pending_settings_) { settings_ = *pending_settings_; pending_settings_.reset(); }
    if (pending_processing_) { processing_ = *pending_processing_; pending_processing_.reset(); }
    if (pending_resampling_) { resampling_ = *pending_resampling_; pending_resampling_.reset(); }
    input_.have_time_ = false; input_.submitted_frames_ = 0;
    return S_OK;
}
STDMETHODIMP Renderer::Pause() {
    try {
        std::lock_guard transition(transition_mutex_);
        if (state_.load() == State_Paused) return S_OK;
        if (state_.load() == State_Stopped) {
            std::lock_guard stream(receive_mutex_);
            if (input_.format_ && !engine_.opened()) { const auto hr = engine_.open(*input_.format_, settings_,processing_,resampling_,playback_rate_.load()); if (FAILED(hr)) return hr; }
            input_.have_time_ = false; input_.submitted_frames_ = 0;
            eos_.store(false); complete_sent_.store(false); error_sent_.store(false); flushing_.store(false);
            ResetEvent(ready_.get()); engine_.allow_input();
        } else engine_.pause();
        state_.store(State_Paused);
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP Renderer::Run(REFERENCE_TIME start) {
    try {
        std::lock_guard transition(transition_mutex_);
        if (state_.load() == State_Stopped) { const auto hr = Pause(); if (FAILED(hr)) return hr; }
        run_start_.store(start);
        if (engine_.opened()) { const auto hr = engine_.start(start, graph_now()); if (FAILED(hr)) return hr; }
        state_.store(State_Running); SetEvent(engine_.progress_event()); return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP Renderer::GetState(DWORD timeout, FILTER_STATE* state) {
    if (!state) return E_POINTER;
    *state = state_.load();
    if (*state == State_Stopped) return S_OK;
    bool connected = false;
    { std::lock_guard info(info_mutex_); connected = input_.peer_ != nullptr; }
    if (!connected) return S_OK;
    if (WaitForSingleObject(ready_.get(), timeout) == WAIT_TIMEOUT) return VFW_S_STATE_INTERMEDIATE;
    *state = state_.load(); return FAILED(engine_.error()) ? engine_.error() : S_OK;
}
STDMETHODIMP Renderer::SetSyncSource(IReferenceClock* clock) {
    std::lock_guard lock(info_mutex_);
    sync_is_own_ = clock == static_cast<IReferenceClock*>(this);
    sync_clock_ = sync_is_own_ ? nullptr : clock;
    return S_OK;
}
STDMETHODIMP Renderer::GetSyncSource(IReferenceClock** clock) {
    if (!clock) return E_POINTER;
    std::lock_guard lock(info_mutex_);
    if (sync_is_own_) { *clock = static_cast<IReferenceClock*>(this); AddRef(); return S_OK; }
    return sync_clock_.CopyTo(clock);
}
STDMETHODIMP Renderer::EnumPins(IEnumPins** pins) { if (!pins) return E_POINTER; *pins = new(std::nothrow) PinEnum(static_cast<IBaseFilter*>(this), &input_); return *pins ? S_OK : E_OUTOFMEMORY; }
STDMETHODIMP Renderer::FindPin(LPCWSTR id, IPin** pin) { if (!pin) return E_POINTER; *pin = nullptr; if (!id) return E_POINTER; if (wcscmp(id, L"Input")) return VFW_E_NOT_FOUND; *pin = &input_; AddRef(); return S_OK; }
STDMETHODIMP Renderer::QueryFilterInfo(FILTER_INFO* info) {
    if (!info) return E_POINTER;
    std::lock_guard lock(info_mutex_); wcsncpy_s(info->achName, name_.c_str(), _TRUNCATE); info->pGraph = graph_; if (graph_) graph_->AddRef(); return S_OK;
}
STDMETHODIMP Renderer::JoinFilterGraph(IFilterGraph* graph, LPCWSTR name) {
    try { std::lock_guard lock(info_mutex_); graph_ = graph; name_ = name ? name : renderer_name; return S_OK; }
    catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP Renderer::QueryVendorInfo(LPWSTR* vendor) { if (!vendor) return E_POINTER; *vendor = nullptr; return E_NOTIMPL; }
STDMETHODIMP Renderer::GetPages(CAUUID* pages) {
    if (!pages) return E_POINTER; *pages = {};
    pages->pElems = static_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID)));
    if (!pages->pElems) return E_OUTOFMEMORY;
    pages->cElems = 1; pages->pElems[0] = clsid_settings_page; return S_OK;
}
STDMETHODIMP Renderer::GetSettings(Settings* settings) { if (!settings) return E_POINTER; std::lock_guard lock(transition_mutex_); *settings = pending_settings_.value_or(settings_); return S_OK; }
STDMETHODIMP Renderer::SetSettings(const Settings* settings) {
    if (!settings) return E_POINTER;
    try {
        std::lock_guard transition(transition_mutex_);
        const auto hr = save_settings(*settings); if (FAILED(hr)) return hr;
        if (state_.load() != State_Stopped) {
            const bool same = settings->driver == settings_.driver && settings->first_channel == settings_.first_channel &&
                settings->buffer_frames == settings_.buffer_frames && settings->keep_device_rate == settings_.keep_device_rate;
            pending_settings_ = same ? std::nullopt : std::optional<Settings>(*settings);
        } else {
            std::lock_guard stream(receive_mutex_); engine_.close(); settings_ = *settings; pending_settings_.reset();
        }
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP Renderer::GetStatus(EngineStatus* status) { if (!status) return E_POINTER; try { *status = engine_.status(); return S_OK; } catch (...) { return E_OUTOFMEMORY; } }
STDMETHODIMP Renderer::GetLiveStatus(LiveStatus* status) {
    if (!status) return E_POINTER;
    try {
        // Never take receive_mutex_: a paused producer can be waiting for buffer space.
        std::lock_guard transition(transition_mutex_);
        auto live = engine_.live_status();
        live.playback = state_.load(); live.completed = complete_sent_.load();
        live.settings_pending = pending_settings_.has_value() || pending_processing_.has_value() || pending_resampling_.has_value();
        { std::lock_guard info(info_mutex_); live.in_graph = graph_ != nullptr; live.connected = input_.peer_ != nullptr; }
        *status = live; return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP Renderer::GetTypeInfoCount(UINT* count) { if (!count) return E_POINTER; *count = 0; return S_OK; }
STDMETHODIMP Renderer::put_Volume(long volume) {
    if (volume < -10000 || volume > 0) return E_INVALIDARG;
    volume_db100_.store(volume); engine_.set_volume(volume); return S_OK;
}
STDMETHODIMP Renderer::get_Volume(long* volume) { if (!volume) return E_POINTER; *volume = volume_db100_.load(); return S_OK; }
STDMETHODIMP Renderer::GetPlaybackOptions(PlaybackOptions* options) {
    if (!options) return E_POINTER; options->smooth_transitions = engine_.signal_status().smooth_transitions; return S_OK;
}
STDMETHODIMP Renderer::SetPlaybackOptions(const PlaybackOptions* options) {
    if (!options) return E_POINTER;
    const auto hr = save_playback_options(*options);
    if (SUCCEEDED(hr)) engine_.set_smoothing(options->smooth_transitions != FALSE);
    return hr;
}
STDMETHODIMP Renderer::GetSignalStatus(SignalStatus* status) { if (!status) return E_POINTER; *status = engine_.signal_status(); return S_OK; }
STDMETHODIMP Renderer::GetProcessingOptions(ProcessingOptions* options) {
    if (!options) return E_POINTER;
    std::lock_guard transition(transition_mutex_); *options=pending_processing_.value_or(processing_); return S_OK;
}
STDMETHODIMP Renderer::SetProcessingOptions(const ProcessingOptions* options) {
    if (!options) return E_POINTER;
    try {
        std::lock_guard transition(transition_mutex_);
        const auto result=save_processing_options(*options); if (FAILED(result)) return result;
        if (state_.load()!=State_Stopped)
            pending_processing_=options->output_bits==processing_.output_bits ? std::nullopt : std::optional<ProcessingOptions>(*options);
        else {
            std::lock_guard stream(receive_mutex_); engine_.close(); processing_=*options; pending_processing_.reset();
        }
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP Renderer::GetProcessingStatus(ProcessingStatus* status) {
    if (!status) return E_POINTER;
    try {
        std::lock_guard transition(transition_mutex_); *status=engine_.processing_status();
        status->settings_pending=pending_processing_.has_value() || pending_settings_.has_value() || pending_resampling_.has_value(); return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP Renderer::get_Balance(long* balance) { if (!balance) return E_POINTER; *balance = 0; return S_OK; }
STDMETHODIMP Renderer::GetResamplingOptions(ResamplingOptions* options) {
    if (!options) return E_POINTER;
    std::lock_guard transition(transition_mutex_); *options=pending_resampling_.value_or(resampling_); return S_OK;
}
STDMETHODIMP Renderer::SetResamplingOptions(const ResamplingOptions* options) {
    if (!options) return E_POINTER;
    try {
        std::lock_guard transition(transition_mutex_);
        const auto result=save_resampling_options(*options); if (FAILED(result)) return result;
        if (state_.load()!=State_Stopped)
            pending_resampling_=options->algorithm==resampling_.algorithm ? std::nullopt : std::optional<ResamplingOptions>(*options);
        else {
            std::lock_guard stream(receive_mutex_); engine_.close(); resampling_=*options; pending_resampling_.reset();
        }
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP Renderer::GetResamplingStatus(ResamplingStatus* status) {
    if (!status) return E_POINTER;
    try {
        std::lock_guard transition(transition_mutex_); *status=engine_.resampling_status();
        if (!engine_.opened()) status->algorithm=resampling_.algorithm;
        status->settings_pending=pending_resampling_.has_value(); return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
REFERENCE_TIME Renderer::graph_now() {
    ComPtr<IReferenceClock> clock;
    { std::lock_guard lock(info_mutex_); clock = sync_clock_; }
    REFERENCE_TIME now = timeline_->now(); if (clock) clock->GetTime(&now); return now;
}
void Renderer::notify_event(long event, LONG_PTR first, LONG_PTR second) {
    ComPtr<IMediaEventSink> sink;
    { std::lock_guard lock(info_mutex_); if (graph_) graph_->QueryInterface(IID_PPV_ARGS(sink.GetAddressOf())); }
    if (sink) sink->Notify(event, first, second);
}
void Renderer::monitor() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const HANDLE handles[]{monitor_stop_.get(), engine_.progress_event()};
    for (;;) {
        const auto timeout = eos_.load() && state_.load() == State_Running && !complete_sent_.load() ? 10u : INFINITE;
        if (WaitForMultipleObjects(2, handles, FALSE, timeout) == WAIT_OBJECT_0) break;
        if (state_.load() == State_Stopped || flushing_.load()) continue;
        const auto error = engine_.error();
        if (FAILED(error)) {
            if (!error_sent_.exchange(true)) { SetEvent(ready_.get()); notify_event(EC_ERRORABORT, error, 0); }
            continue;
        }
        if (state_.load() == State_Running && eos_.load() && engine_.drained() && !complete_sent_.exchange(true)) notify_event(EC_COMPLETE, S_OK, reinterpret_cast<LONG_PTR>(static_cast<IBaseFilter*>(this)));
    }
    CoUninitialize();
}
ComPtr<IMediaSeeking> Renderer::upstream_seeking() {
    ComPtr<IPin> peer;
    { std::lock_guard lock(info_mutex_); peer = input_.peer_; }
    ComPtr<IMediaSeeking> seeking;
    if (peer) {
        if (SUCCEEDED(peer.As(&seeking))) return seeking;
        PIN_INFO info{};
        if (SUCCEEDED(peer->QueryPinInfo(&info)) && info.pFilter) { info.pFilter->QueryInterface(IID_PPV_ARGS(seeking.GetAddressOf())); info.pFilter->Release(); }
    }
    return seeking;
}
#define ARE_FORWARD_SEEK(method, parameters) do { auto upstream_interface = upstream_seeking(); return upstream_interface ? upstream_interface->method parameters : VFW_E_NOT_CONNECTED; } while (false)
STDMETHODIMP Renderer::GetCapabilities(DWORD* value) { ARE_FORWARD_SEEK(GetCapabilities, (value)); }
STDMETHODIMP Renderer::CheckCapabilities(DWORD* value) { ARE_FORWARD_SEEK(CheckCapabilities, (value)); }
STDMETHODIMP Renderer::IsFormatSupported(const GUID* value) { ARE_FORWARD_SEEK(IsFormatSupported, (value)); }
STDMETHODIMP Renderer::QueryPreferredFormat(GUID* value) { ARE_FORWARD_SEEK(QueryPreferredFormat, (value)); }
STDMETHODIMP Renderer::GetTimeFormat(GUID* value) { ARE_FORWARD_SEEK(GetTimeFormat, (value)); }
STDMETHODIMP Renderer::IsUsingTimeFormat(const GUID* value) { ARE_FORWARD_SEEK(IsUsingTimeFormat, (value)); }
STDMETHODIMP Renderer::SetTimeFormat(const GUID* value) { ARE_FORWARD_SEEK(SetTimeFormat, (value)); }
STDMETHODIMP Renderer::GetDuration(LONGLONG* value) { ARE_FORWARD_SEEK(GetDuration, (value)); }
STDMETHODIMP Renderer::GetStopPosition(LONGLONG* value) { ARE_FORWARD_SEEK(GetStopPosition, (value)); }
STDMETHODIMP Renderer::GetCurrentPosition(LONGLONG* value) { ARE_FORWARD_SEEK(GetCurrentPosition, (value)); }
STDMETHODIMP Renderer::ConvertTimeFormat(LONGLONG* t, const GUID* tf, LONGLONG s, const GUID* sf) { ARE_FORWARD_SEEK(ConvertTimeFormat, (t, tf, s, sf)); }
STDMETHODIMP Renderer::SetPositions(LONGLONG* c, DWORD cf, LONGLONG* s, DWORD sf) { ARE_FORWARD_SEEK(SetPositions, (c, cf, s, sf)); }
STDMETHODIMP Renderer::GetPositions(LONGLONG* c, LONGLONG* s) { ARE_FORWARD_SEEK(GetPositions, (c, s)); }
STDMETHODIMP Renderer::GetAvailable(LONGLONG* e, LONGLONG* l) { ARE_FORWARD_SEEK(GetAvailable, (e, l)); }
STDMETHODIMP Renderer::SetRate(double rate) {
    if (!valid_playback_rate(rate)) return E_INVALIDARG;
    auto upstream=upstream_seeking();
    if (!upstream) return VFW_E_NOT_CONNECTED;
    const auto result=upstream->SetRate(rate);
    if (SUCCEEDED(result)) playback_rate_.store(rate);
    return result;
}
STDMETHODIMP Renderer::GetRate(double* rate) { if (!rate) return E_POINTER; *rate = playback_rate_.load(); return S_OK; }
STDMETHODIMP Renderer::GetPreroll(LONGLONG* value) { ARE_FORWARD_SEEK(GetPreroll, (value)); }
#undef ARE_FORWARD_SEEK

InputPin::~InputPin() { free_media_type(type_); }
STDMETHODIMP InputPin::QueryInterface(REFIID iid, void** out) {
    if (!out) return E_POINTER; *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_IPin) *out = static_cast<IPin*>(this);
    else if (iid == IID_IMemInputPin) *out = static_cast<IMemInputPin*>(this);
    else if (iid == IID_IQualityControl) *out = static_cast<IQualityControl*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
STDMETHODIMP_(ULONG) InputPin::AddRef() { return renderer_.AddRef(); }
STDMETHODIMP_(ULONG) InputPin::Release() { return renderer_.Release(); }
STDMETHODIMP InputPin::Notify(IBaseFilter* sender, Quality quality) { return renderer_.Notify(sender, quality); }
STDMETHODIMP InputPin::SetSink(IQualityControl* sink) { return renderer_.SetSink(sink); }
STDMETHODIMP InputPin::ReceiveConnection(IPin* connector, const AM_MEDIA_TYPE* type) {
    if (!connector || !type) return E_POINTER;
    try {
        std::lock_guard transition(renderer_.transition_mutex_);
        if (renderer_.state_.load() != State_Stopped) return VFW_E_NOT_STOPPED;
        { std::lock_guard info(renderer_.info_mutex_); if (peer_) return VFW_E_ALREADY_CONNECTED; }
        PIN_DIRECTION direction{};
        if (FAILED(connector->QueryDirection(&direction)) || direction != PINDIR_OUTPUT) return VFW_E_INVALID_DIRECTION;
        const auto format = parse_media_type(*type); if (!format) return VFW_E_TYPE_NOT_ACCEPTED;
        std::lock_guard stream(renderer_.receive_mutex_);
        // Negotiate graph pins without initializing/resetting the physical DAC.
        // Open ASIO once when the connected graph enters Pause/Run.
        AM_MEDIA_TYPE copy{};
        const auto copied = copy_media_type(copy, *type); if (FAILED(copied)) return copied;
        std::lock_guard info(renderer_.info_mutex_); free_media_type(type_); type_ = copy; format_ = format; peer_ = connector; return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP InputPin::Disconnect() {
    std::lock_guard transition(renderer_.transition_mutex_);
    if (renderer_.state_.load() != State_Stopped) return VFW_E_NOT_STOPPED;
    renderer_.engine_.abort(); std::lock_guard stream(renderer_.receive_mutex_); renderer_.engine_.close();
    std::lock_guard info(renderer_.info_mutex_);
    if (!peer_) return S_FALSE;
    peer_.Reset(); allocator_.Reset(); free_media_type(type_); format_.reset(); return S_OK;
}
STDMETHODIMP InputPin::ConnectedTo(IPin** pin) { if (!pin) return E_POINTER; std::lock_guard lock(renderer_.info_mutex_); *pin = nullptr; return peer_ ? peer_.CopyTo(pin) : VFW_E_NOT_CONNECTED; }
STDMETHODIMP InputPin::ConnectionMediaType(AM_MEDIA_TYPE* type) { if (!type) return E_POINTER; *type = {}; std::lock_guard lock(renderer_.info_mutex_); return peer_ ? copy_media_type(*type, type_) : VFW_E_NOT_CONNECTED; }
STDMETHODIMP InputPin::QueryPinInfo(PIN_INFO* info) { if (!info) return E_POINTER; info->pFilter = &renderer_; renderer_.AddRef(); info->dir = PINDIR_INPUT; wcscpy_s(info->achName, L"Input"); return S_OK; }
STDMETHODIMP InputPin::QueryDirection(PIN_DIRECTION* direction) { if (!direction) return E_POINTER; *direction = PINDIR_INPUT; return S_OK; }
STDMETHODIMP InputPin::QueryId(LPWSTR* id) { if (!id) return E_POINTER; *id = static_cast<LPWSTR>(CoTaskMemAlloc(6 * sizeof(wchar_t))); if (!*id) return E_OUTOFMEMORY; wcscpy_s(*id, 6, L"Input"); return S_OK; }
STDMETHODIMP InputPin::QueryAccept(const AM_MEDIA_TYPE* type) { if (!type) return E_POINTER; return parse_media_type(*type) ? S_OK : S_FALSE; }
STDMETHODIMP InputPin::EnumMediaTypes(IEnumMediaTypes** types) { if (!types) return E_POINTER; *types = new(std::nothrow) TypeEnum; return *types ? S_OK : E_OUTOFMEMORY; }
STDMETHODIMP InputPin::QueryInternalConnections(IPin**, ULONG* count) { if (!count) return E_POINTER; *count = 0; return E_NOTIMPL; }
STDMETHODIMP InputPin::GetAllocator(IMemAllocator** allocator) {
    if (!allocator) return E_POINTER; *allocator = nullptr;
    std::lock_guard lock(renderer_.info_mutex_);
    if (!allocator_) { const auto hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(allocator_.GetAddressOf())); if (FAILED(hr)) return hr; }
    return allocator_.CopyTo(allocator);
}
STDMETHODIMP InputPin::NotifyAllocator(IMemAllocator* allocator, BOOL) { if (!allocator) return E_POINTER; std::lock_guard lock(renderer_.info_mutex_); allocator_ = allocator; return S_OK; }
STDMETHODIMP InputPin::GetAllocatorRequirements(ALLOCATOR_PROPERTIES* properties) { if (!properties) return E_POINTER; *properties = {}; return E_NOTIMPL; }
STDMETHODIMP InputPin::Receive(IMediaSample* sample) {
    if (!sample) return E_POINTER;
    try {
        std::lock_guard stream(renderer_.receive_mutex_);
        if (renderer_.flushing_.load()) return S_FALSE;
        if (renderer_.state_.load() == State_Stopped) return VFW_E_WRONG_STATE;
        if (!format_) return VFW_E_NOT_CONNECTED;
        if (renderer_.eos_.load()) return VFW_E_SAMPLE_REJECTED_EOS;
        AM_MEDIA_TYPE* dynamic = nullptr;
        if (sample->GetMediaType(&dynamic) == S_OK && dynamic) {
            const auto format = parse_media_type(*dynamic);
            AM_MEDIA_TYPE copy{}; const auto copied = copy_media_type(copy, *dynamic);
            free_media_type(*dynamic); CoTaskMemFree(dynamic);
            if (!format || FAILED(copied)) { free_media_type(copy); return VFW_E_TYPE_NOT_ACCEPTED; }
            if (*format != *format_) {
                renderer_.engine_.abort(); renderer_.engine_.close();
                const auto hr = renderer_.engine_.open(*format, renderer_.settings_,renderer_.processing_,renderer_.resampling_,renderer_.playback_rate_.load());
                if (FAILED(hr)) { free_media_type(copy); return hr; }
                if (renderer_.state_.load() == State_Running) { const auto started = renderer_.engine_.start(renderer_.run_start_.load(), renderer_.graph_now()); if (FAILED(started)) { free_media_type(copy); return started; } }
                format_ = format; have_time_ = false; submitted_frames_ = 0;
            }
            { std::lock_guard info(renderer_.info_mutex_); free_media_type(type_); type_ = copy; }
        }
        if (sample->IsPreroll() == S_OK) return S_OK;
        BYTE* data = nullptr; const auto hr = sample->GetPointer(&data); if (FAILED(hr)) return hr;
        const auto length = sample->GetActualDataLength();
        if (length < 0 || length > sample->GetSize() || (length && !data) || std::size_t(length) % format_->frame_bytes()) return VFW_E_TYPE_NOT_ACCEPTED;
        if (!length) return S_OK;
        REFERENCE_TIME start = 0, stop = 0;
        const auto timed = sample->GetTime(&start, &stop);
        // DirectShow timestamps already reflect playback speed. PCM frames do
        // not: compare timestamps with source duration divided by segment rate.
        const auto rate=renderer_.playback_rate_.load();
        const auto duration=REFERENCE_TIME(frames_time(submitted_frames_,format_->rate)/rate);
        if (timed != S_OK && timed != VFW_S_NO_STOP_TIME) start = have_time_ ? first_time_ + duration : 0;
        if (!have_time_) { first_time_ = start; submitted_frames_ = 0; have_time_ = true; }
        SetEvent(renderer_.ready_.get()); // preroll ready before back-pressure can block
        const auto expected = first_time_ + duration;
        const auto gap = start - expected;
        if (gap > frames_time(2, format_->rate)/rate) {
            if (gap > 6000000000LL) return VFW_E_TYPE_NOT_ACCEPTED;
            auto missing = std::uint64_t(std::round(static_cast<long double>(gap)*format_->rate*rate/10000000));
            std::vector<std::byte> silence(4096 * format_->frame_bytes(), format_->container_bits == 8 ? std::byte{128} : std::byte{});
            while (missing) {
                const auto count = std::min<std::uint64_t>(missing, 4096);
                const auto result = renderer_.engine_.submit({silence.data(), std::size_t(count) * format_->frame_bytes()}, first_time_);
                if (result != S_OK) return result;
                submitted_frames_ += count; missing -= count;
            }
        }
        const auto result = renderer_.engine_.submit({reinterpret_cast<const std::byte*>(data), std::size_t(length)}, first_time_);
        if (result == S_OK) submitted_frames_ += std::size_t(length) / format_->frame_bytes();
        return result;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP InputPin::ReceiveMultiple(IMediaSample** samples, long count, long* processed) {
    if (!processed || (count && !samples)) return E_POINTER;
    *processed = 0; if (count < 0) return E_INVALIDARG;
    for (long i = 0; i < count; ++i) { const auto hr = Receive(samples[i]); if (hr != S_OK) return hr; ++*processed; }
    return S_OK;
}
STDMETHODIMP InputPin::EndOfStream() {
    std::lock_guard stream(renderer_.receive_mutex_);
    if (renderer_.flushing_.load()) return S_FALSE;
    if (!format_) return VFW_E_NOT_CONNECTED;
    const auto result=renderer_.engine_.end_of_stream(); if (result!=S_OK) return result;
    renderer_.eos_.store(true); SetEvent(renderer_.ready_.get()); return S_OK;
}
STDMETHODIMP InputPin::BeginFlush() {
    renderer_.flushing_.store(true); renderer_.engine_.abort();
    std::lock_guard stream(renderer_.receive_mutex_);
    renderer_.engine_.reset(); renderer_.eos_.store(false); renderer_.complete_sent_.store(false); renderer_.error_sent_.store(false);
    have_time_ = false; submitted_frames_ = 0; ResetEvent(renderer_.ready_.get()); return S_OK;
}
STDMETHODIMP InputPin::EndFlush() {
    try {
        std::lock_guard stream(renderer_.receive_mutex_);
        renderer_.flushing_.store(false); renderer_.engine_.allow_input();
        if (renderer_.state_.load() == State_Running && renderer_.engine_.opened()) return renderer_.engine_.start(renderer_.run_start_.load(), renderer_.graph_now());
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
STDMETHODIMP InputPin::NewSegment(REFERENCE_TIME, REFERENCE_TIME, double rate) {
    if (!valid_playback_rate(rate)) return E_INVALIDARG;
    renderer_.engine_.abort();
    try {
        std::lock_guard stream(renderer_.receive_mutex_); renderer_.engine_.reset();
        if (renderer_.engine_.opened()) {
            const auto result=renderer_.engine_.set_rate(rate); if (FAILED(result)) return result;
        }
        renderer_.playback_rate_.store(rate);
        renderer_.eos_.store(false); renderer_.complete_sent_.store(false); have_time_ = false; submitted_frames_ = 0;
        if (renderer_.state_.load() == State_Running && renderer_.engine_.opened()) return renderer_.engine_.start(renderer_.run_start_.load(), renderer_.graph_now());
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
} // namespace are::win
