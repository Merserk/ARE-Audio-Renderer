#pragma once
#include "win/engine.hpp"
#include "win/media.hpp"
#include <mutex>

namespace are::win {
class Renderer;
class InputPin final : public IPin, public IMemInputPin, public IQualityControl {
public:
    explicit InputPin(Renderer& renderer) noexcept : renderer_(renderer) {}
    ~InputPin();
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP Connect(IPin*, const AM_MEDIA_TYPE*) override { return E_UNEXPECTED; }
    STDMETHODIMP ReceiveConnection(IPin* connector, const AM_MEDIA_TYPE* type) override;
    STDMETHODIMP Disconnect() override;
    STDMETHODIMP ConnectedTo(IPin** pin) override;
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* type) override;
    STDMETHODIMP QueryPinInfo(PIN_INFO* info) override;
    STDMETHODIMP QueryDirection(PIN_DIRECTION* direction) override;
    STDMETHODIMP QueryId(LPWSTR* id) override;
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* type) override;
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** types) override;
    STDMETHODIMP QueryInternalConnections(IPin**, ULONG* count) override;
    STDMETHODIMP EndOfStream() override;
    STDMETHODIMP BeginFlush() override;
    STDMETHODIMP EndFlush() override;
    STDMETHODIMP NewSegment(REFERENCE_TIME start, REFERENCE_TIME stop, double rate) override;
    STDMETHODIMP GetAllocator(IMemAllocator** allocator) override;
    STDMETHODIMP NotifyAllocator(IMemAllocator* allocator, BOOL read_only) override;
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* properties) override;
    STDMETHODIMP Receive(IMediaSample* sample) override;
    STDMETHODIMP ReceiveMultiple(IMediaSample** samples, long count, long* processed) override;
    STDMETHODIMP ReceiveCanBlock() override { return S_OK; }
    STDMETHODIMP Notify(IBaseFilter* sender, Quality quality) override;
    STDMETHODIMP SetSink(IQualityControl* sink) override;
private:
    friend class Renderer;
    Renderer& renderer_;
    ComPtr<IPin> peer_;
    ComPtr<IMemAllocator> allocator_;
    AM_MEDIA_TYPE type_{};
    std::optional<SourceFormat> format_;
    std::uint64_t submitted_frames_{};
    REFERENCE_TIME first_time_{};
    bool have_time_{};
};

class Renderer final : public IBaseFilter, public IAMFilterMiscFlags,
                       public ISpecifyPropertyPages, public IASIORenderSettings, public IASIORenderStatus, public IASIORenderPlayback, public IASIORenderProcessing, public IASIORenderResampling,
                       public IBasicAudio, public IMediaSeeking, public IQualityControl, public IReferenceClock {
public:
    Renderer();
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override;
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetClassID(CLSID* clsid) override;
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME start) override;
    STDMETHODIMP GetState(DWORD timeout, FILTER_STATE* state) override;
    STDMETHODIMP SetSyncSource(IReferenceClock* clock) override;
    STDMETHODIMP GetSyncSource(IReferenceClock** clock) override;
    STDMETHODIMP EnumPins(IEnumPins** pins) override;
    STDMETHODIMP FindPin(LPCWSTR id, IPin** pin) override;
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* info) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph* graph, LPCWSTR name) override;
    STDMETHODIMP QueryVendorInfo(LPWSTR* vendor) override;
    STDMETHODIMP_(ULONG) GetMiscFlags() override { return AM_FILTER_MISC_FLAGS_IS_RENDERER; }
    STDMETHODIMP GetPages(CAUUID* pages) override;
    STDMETHODIMP GetSettings(Settings* settings) override;
    STDMETHODIMP SetSettings(const Settings* settings) override;
    STDMETHODIMP GetStatus(EngineStatus* status) override;
    STDMETHODIMP GetLiveStatus(LiveStatus* status) override;
    STDMETHODIMP GetPlaybackOptions(PlaybackOptions* options) override;
    STDMETHODIMP SetPlaybackOptions(const PlaybackOptions* options) override;
    STDMETHODIMP GetSignalStatus(SignalStatus* status) override;
    STDMETHODIMP GetProcessingOptions(ProcessingOptions* options) override;
    STDMETHODIMP SetProcessingOptions(const ProcessingOptions* options) override;
    STDMETHODIMP GetProcessingStatus(ProcessingStatus* status) override;
    STDMETHODIMP GetResamplingOptions(ResamplingOptions* options) override;
    STDMETHODIMP SetResamplingOptions(const ResamplingOptions* options) override;
    STDMETHODIMP GetResamplingStatus(ResamplingStatus* status) override;
    STDMETHODIMP GetTypeInfoCount(UINT* count) override;
    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }
    STDMETHODIMP put_Volume(long volume) override;
    STDMETHODIMP get_Volume(long* volume) override;
    STDMETHODIMP put_Balance(long balance) override { return balance == 0 ? S_OK : E_INVALIDARG; }
    STDMETHODIMP get_Balance(long* balance) override;
    STDMETHODIMP GetCapabilities(DWORD* caps) override;
    STDMETHODIMP CheckCapabilities(DWORD* caps) override;
    STDMETHODIMP IsFormatSupported(const GUID* format) override;
    STDMETHODIMP QueryPreferredFormat(GUID* format) override;
    STDMETHODIMP GetTimeFormat(GUID* format) override;
    STDMETHODIMP IsUsingTimeFormat(const GUID* format) override;
    STDMETHODIMP SetTimeFormat(const GUID* format) override;
    STDMETHODIMP GetDuration(LONGLONG* value) override;
    STDMETHODIMP GetStopPosition(LONGLONG* value) override;
    STDMETHODIMP GetCurrentPosition(LONGLONG* value) override;
    STDMETHODIMP ConvertTimeFormat(LONGLONG* target, const GUID* tf, LONGLONG source, const GUID* sf) override;
    STDMETHODIMP SetPositions(LONGLONG* current, DWORD cf, LONGLONG* stop, DWORD sf) override;
    STDMETHODIMP GetPositions(LONGLONG* current, LONGLONG* stop) override;
    STDMETHODIMP GetAvailable(LONGLONG* earliest, LONGLONG* latest) override;
    STDMETHODIMP SetRate(double rate) override;
    STDMETHODIMP GetRate(double* rate) override;
    STDMETHODIMP GetPreroll(LONGLONG* preroll) override;
    STDMETHODIMP Notify(IBaseFilter*, Quality) override { return S_OK; }
    STDMETHODIMP SetSink(IQualityControl*) override { return S_OK; }
    STDMETHODIMP GetTime(REFERENCE_TIME* time) override { return own_clock_->GetTime(time); }
    STDMETHODIMP AdviseTime(REFERENCE_TIME base, REFERENCE_TIME stream, HEVENT event, DWORD_PTR* cookie) override { return own_clock_->AdviseTime(base, stream, event, cookie); }
    STDMETHODIMP AdvisePeriodic(REFERENCE_TIME start, REFERENCE_TIME period, HSEMAPHORE semaphore, DWORD_PTR* cookie) override { return own_clock_->AdvisePeriodic(start, period, semaphore, cookie); }
    STDMETHODIMP Unadvise(DWORD_PTR cookie) override { return own_clock_->Unadvise(cookie); }
private:
    friend class InputPin;
    ~Renderer();
    ComPtr<IMediaSeeking> upstream_seeking();
    REFERENCE_TIME graph_now();
    void monitor();
    void notify_event(long event, LONG_PTR first, LONG_PTR second);
    ModuleRef module_;
    std::atomic<ULONG> refs_{1};
    std::recursive_mutex transition_mutex_;
    std::mutex info_mutex_;
    std::mutex receive_mutex_;
    Settings settings_;
    std::optional<Settings> pending_settings_;
    ProcessingOptions processing_;
    std::optional<ProcessingOptions> pending_processing_;
    ResamplingOptions resampling_;
    std::optional<ResamplingOptions> pending_resampling_;
    std::shared_ptr<ClockTimeline> timeline_;
    ComPtr<IReferenceClock> own_clock_;
    ComPtr<IReferenceClock> sync_clock_;
    bool sync_is_own_{};
    Engine engine_;
    InputPin input_{*this};
    std::atomic<FILTER_STATE> state_{State_Stopped};
    std::atomic<bool> flushing_{};
    std::atomic<bool> eos_{};
    std::atomic<bool> complete_sent_{};
    std::atomic<bool> error_sent_{};
    std::atomic<long> volume_db100_{};
    std::atomic<REFERENCE_TIME> run_start_{};
    std::atomic<double> playback_rate_{1.0};
    Handle ready_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle monitor_stop_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread monitor_thread_;
    IFilterGraph* graph_{}; // non-owning, per IBaseFilter::JoinFilterGraph contract
    std::wstring name_{renderer_name};
};
} // namespace are::win
