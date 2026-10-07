#include "win/interfaces.hpp"
#include "win/media.hpp"
#include "win/settings.hpp"
#include "win/devices.hpp"
#include "fake_asio.hpp"
#include "preferences_lock.hpp"
#include "../resources/resource.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <format>
#include <barrier>
#include <future>
#include <stdexcept>

using namespace are::win;
void require(bool ok, const char* detail) { if (!ok) throw std::runtime_error(detail); }
void success(HRESULT hr, const char* detail) { if (FAILED(hr)) { std::cerr << std::hex << unsigned(hr) << ' '; throw std::runtime_error(detail); } }
struct Scope : are::test::PreferencesLock {
    Settings previous = load_settings();
    bool existed{};
    bool had_smoothing{};
    bool had_precision{};
    bool had_resampling{};
    ResamplingOptions previous_resampling=load_resampling_options();
    ProcessingOptions previous_precision=load_processing_options();
    PlaybackOptions previous_options = load_playback_options();
    std::wstring driver_key = L"Software\\Classes\\CLSID\\" + guid_string(are::test::fake_clsid);
    std::wstring asio_key = L"Software\\ASIO\\ARE regression driver " + std::to_wstring(GetCurrentProcessId());
    Scope(const wchar_t* driver_path) {
        RegKey key; existed = RegOpenKeyExW(HKEY_CURRENT_USER, settings_registry_path, 0, KEY_READ, key.put()) == ERROR_SUCCESS;
        DWORD value{}, size=sizeof(value);
        had_smoothing = RegGetValueW(HKEY_CURRENT_USER, settings_registry_path, L"SmoothTransitions", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
        size=sizeof(value); had_precision=RegGetValueW(HKEY_CURRENT_USER,settings_registry_path,L"OutputPcmBits",RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS;
        size=sizeof(value); had_resampling=RegGetValueW(HKEY_CURRENT_USER,settings_registry_path,L"SrcAlgorithm",RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS;
        require(RegCreateKeyExW(HKEY_CURRENT_USER, (driver_key + L"\\InprocServer32").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, key.put(), nullptr) == ERROR_SUCCESS, "fake driver registration failed");
        require(RegSetValueExW(key.get(), nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(driver_path), DWORD((wcslen(driver_path)+1)*2)) == ERROR_SUCCESS, "fake driver path failed");
        constexpr wchar_t model[] = L"Both";
        require(RegSetValueExW(key.get(), L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(model), sizeof(model)) == ERROR_SUCCESS, "fake driver model failed");
        DWORD disposition{};
        require(RegCreateKeyExW(HKEY_CURRENT_USER,asio_key.c_str(),0,nullptr,0,KEY_WRITE,nullptr,key.put(),&disposition)==ERROR_SUCCESS && disposition==REG_CREATED_NEW_KEY,"test ASIO enumeration key already exists");
        const auto clsid=guid_string(are::test::fake_clsid);
        require(RegSetValueExW(key.get(),L"CLSID",0,REG_SZ,reinterpret_cast<const BYTE*>(clsid.c_str()),DWORD((clsid.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS,"test ASIO enumeration failed");
        success(save_settings(Settings{are::test::fake_clsid,0,0,FALSE}), "test settings failed");
        success(save_processing_options({0}),"test precision settings failed");
        success(save_resampling_options({}),"test SRC settings failed");
    }
    ~Scope() {
        if (existed) {
            save_settings(previous);
            if (had_smoothing) save_playback_options(previous_options);
            else { RegKey key; if (RegOpenKeyExW(HKEY_CURRENT_USER,settings_registry_path,0,KEY_SET_VALUE,key.put())==ERROR_SUCCESS) RegDeleteValueW(key.get(),L"SmoothTransitions"); }
            if (had_precision) save_processing_options(previous_precision);
            else { RegKey key; if (RegOpenKeyExW(HKEY_CURRENT_USER,settings_registry_path,0,KEY_SET_VALUE,key.put())==ERROR_SUCCESS) RegDeleteValueW(key.get(),L"OutputPcmBits"); }
            if (had_resampling) save_resampling_options(previous_resampling);
            else { RegKey key; if (RegOpenKeyExW(HKEY_CURRENT_USER,settings_registry_path,0,KEY_SET_VALUE,key.put())==ERROR_SUCCESS) RegDeleteValueW(key.get(),L"SrcAlgorithm"); }
        } else RegDeleteTreeW(HKEY_CURRENT_USER, settings_registry_path);
        RegDeleteTreeW(HKEY_CURRENT_USER, driver_key.c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, asio_key.c_str());
    }
};
struct Library { HMODULE module{}; explicit Library(const wchar_t* path) : module(LoadLibraryW(path)) {} ~Library() { if (module) FreeLibrary(module); } };
using GetClass = HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
ComPtr<IPin> first_pin(IBaseFilter* filter, PIN_DIRECTION direction) {
    ComPtr<IEnumPins> list; success(filter->EnumPins(list.GetAddressOf()), "pin enumeration failed");
    ComPtr<IPin> pin;
    while (list->Next(1, pin.ReleaseAndGetAddressOf(), nullptr) == S_OK) { PIN_DIRECTION actual{}; pin->QueryDirection(&actual); if (actual == direction) return pin; }
    return {};
}
std::wstring dialog_text(HWND window, int id) {
    wchar_t text[1024]{}; GetDlgItemTextW(window,id,text,int(std::size(text))); return text;
}
HWND page_window(HWND parent) {
    const auto page = GetWindow(parent,GW_CHILD); require(page != nullptr,"property page window missing"); return page;
}
std::filesystem::path make_wave(std::uint32_t rate=48000, std::uint32_t seconds=1) {
    auto file = std::filesystem::temp_directory_path() / (L"are-filter-test-" + std::to_wstring(GetCurrentProcessId()) + L".wav");
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    const std::uint32_t data_size = rate*seconds*4, riff_size = 36 + data_size, fmt_size = 16;
    const std::uint16_t tag = 1, channels = 2, align = 4, bits = 16;
    const std::uint32_t byte_rate = rate*4;
    auto put = [&](const auto& v) { output.write(reinterpret_cast<const char*>(&v), sizeof(v)); };
    output.write("RIFF",4); put(riff_size); output.write("WAVEfmt ",8); put(fmt_size); put(tag); put(channels); put(rate); put(byte_rate); put(align); put(bits); output.write("data",4); put(data_size);
    for (std::uint32_t i = 0; i < rate*seconds; ++i) { const auto a = std::int16_t(int(i%30000) - 15000), b = std::int16_t(-a); put(a); put(b); }
    require(output.good(), "WAV fixture failed"); return file;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 1;
    int result = 0;
    try {
        Scope scope(argv[2]); Library library(argv[1]); require(library.module != nullptr, "renderer DLL failed to load");
        Library driver_library(argv[2]); require(driver_library.module != nullptr,"fake driver failed to load");
        const auto driver_metrics=reinterpret_cast<HRESULT(WINAPI*)(are::test::DriverMetrics*)>(GetProcAddress(driver_library.module,"GetDriverMetrics"));
        require(driver_metrics!=nullptr,"driver lifecycle metrics unavailable");
        auto get_class = reinterpret_cast<GetClass>(GetProcAddress(library.module,"DllGetClassObject"));
        auto can_unload = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(library.module,"DllCanUnloadNow"));
        require(get_class && can_unload, "missing COM exports");
        {
            ComPtr<IClassFactory> factory;
            success(get_class(clsid_renderer, IID_PPV_ARGS(factory.GetAddressOf())), "renderer factory failed");
            ComPtr<IBaseFilter> filter; success(factory->CreateInstance(nullptr, IID_PPV_ARGS(filter.GetAddressOf())), "renderer construction failed");
            ComPtr<IAMFilterMiscFlags> flags; success(filter.As(&flags), "missing renderer flags"); require(flags->GetMiscFlags() == AM_FILTER_MISC_FLAGS_IS_RENDERER, "wrong renderer flags");
            ComPtr<IReferenceClock> clock; success(filter.As(&clock), "missing audio clock");
            ComPtr<IUnknown> filter_identity, clock_identity; filter.As(&filter_identity); clock.As(&clock_identity);
            require(filter_identity.Get() == clock_identity.Get(), "COM clock identity does not match filter");
            success(filter->SetSyncSource(clock.Get()), "own sync clock failed");
            ComPtr<IBasicAudio> audio; success(filter.As(&audio), "missing volume interface");
            ComPtr<IMediaSeeking> renderer_seeking; success(filter.As(&renderer_seeking),"missing renderer seeking interface");
            double normal_rate{}; success(renderer_seeking->GetRate(&normal_rate),"initial rate unavailable");
            require(normal_rate==1.0 && renderer_seeking->GetRate(nullptr)==E_POINTER,"initial rate or null validation wrong");
            for (const auto rate : {0.0,-1.0,0.049,128.001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
                require(renderer_seeking->SetRate(rate)==E_INVALIDARG,"invalid playback rate accepted");
            require(audio->put_Volume(1) == E_INVALIDARG && audio->put_Volume(-10001) == E_INVALIDARG, "invalid volume accepted");
            success(audio->put_Volume(-1200), "ordinary player volume rejected");
            long volume{}; success(audio->get_Volume(&volume), "volume read failed"); require(volume == -1200, "volume not remembered");
            success(audio->put_Volume(-10000), "mute rejected"); success(audio->put_Volume(-2795), "unmute to 20 percent failed");
            success(audio->get_Volume(&volume), "unmute volume read failed"); require(volume == -2795, "unmute retained silence");
            success(audio->put_Volume(0), "unity volume rejected");
            auto pin = first_pin(filter.Get(), PINDIR_INPUT); require(bool(pin), "missing input pin");
            require(pin->NewSegment(0,10000000,0)==E_INVALIDARG,"invalid segment speed accepted");
            ComPtr<IQualityControl> pin_quality; success(pin.As(&pin_quality), "missing input quality interface");
            ComPtr<IUnknown> pin_identity, quality_identity; pin.As(&pin_identity); pin_quality.As(&quality_identity);
            require(pin_identity.Get() == quality_identity.Get(), "input pin quality interface has wrong COM identity");
            auto mt = pcm_media_type(2,48000,16); require(pin->QueryAccept(&mt) == S_OK, "PCM input rejected");
            mt.cbFormat = 1; require(pin->QueryAccept(&mt) == S_FALSE, "malformed media accepted"); free_media_type(mt);
            ComPtr<IASIORenderSettings> settings; success(filter.As(&settings), "missing settings interface");
            Settings stored{}; success(settings->GetSettings(&stored), "settings read failed"); require(stored.driver == are::test::fake_clsid, "wrong driver selection");
            ComPtr<IASIORenderStatus> live_status; success(filter.As(&live_status),"missing live status interface");
            ComPtr<IASIORenderPlayback> playback; success(filter.As(&playback),"missing playback options interface");
            ComPtr<IASIORenderProcessing> processing; success(filter.As(&processing),"missing processing options interface");
            ComPtr<IASIORenderResampling> resampling; success(filter.As(&resampling),"missing SRC options interface");
            ComPtr<IASIORenderChannels> channels; success(filter.As(&channels),"missing channel status interface");
            require(channels->GetChannelStatus(nullptr)==E_POINTER,"channel status accepted null pointer");
            ComPtr<IUnknown> channel_identity; channels.As(&channel_identity);
            require(channel_identity.Get()==filter_identity.Get(),"channel interface broke COM identity");
            ComPtr<IUnknown> src_identity; resampling.As(&src_identity);
            require(src_identity.Get()==filter_identity.Get(),"SRC interface has a different COM identity");
            require(resampling->GetResamplingOptions(nullptr)==E_POINTER && resampling->SetResamplingOptions(nullptr)==E_POINTER && resampling->GetResamplingStatus(nullptr)==E_POINTER,"SRC interface accepted null pointers");
            const ResamplingOptions invalid_src{are::SrcAlgorithm(2)};
            require(resampling->SetResamplingOptions(&invalid_src)==E_INVALIDARG,"invalid SRC choice accepted");
            ResamplingOptions src_options; success(resampling->GetResamplingOptions(&src_options),"SRC options read failed");
            require(src_options.algorithm==are::SrcAlgorithm::r8brain,"default SRC changed");
            require(processing->GetProcessingOptions(nullptr)==E_POINTER && processing->SetProcessingOptions(nullptr)==E_POINTER && processing->GetProcessingStatus(nullptr)==E_POINTER,"processing interface accepted null pointers");
            const ProcessingOptions invalid_precision{20}; require(processing->SetProcessingOptions(&invalid_precision)==E_INVALIDARG,"invalid PCM precision accepted");
            require(playback->GetPlaybackOptions(nullptr)==E_POINTER && playback->SetPlaybackOptions(nullptr)==E_POINTER && playback->GetSignalStatus(nullptr)==E_POINTER,"playback options accepted null pointers");
            const PlaybackOptions strict{FALSE}; success(playback->SetPlaybackOptions(&strict),"strict output option failed");
            PlaybackOptions options{}; success(playback->GetPlaybackOptions(&options),"playback options read failed");
            require(!options.smooth_transitions,"strict option did not persist");
            const PlaybackOptions smooth{TRUE}; success(playback->SetPlaybackOptions(&smooth),"smooth output option failed");
            success(playback->GetPlaybackOptions(&options),"smooth playback options read failed");
            require(options.smooth_transitions,"smooth option did not persist");
            require(live_status->GetLiveStatus(nullptr) == E_POINTER,"live status accepted null pointer");
            LiveStatus live{}; success(live_status->GetLiveStatus(&live),"initial live status failed");
            require(!live.in_graph && !live.connected && !live.engine.opened && !live.input.valid_bits && !live.output.valid_bits,"inactive renderer reported active output");
            ComPtr<IClassFactory> page_factory; success(get_class(clsid_settings_page, IID_PPV_ARGS(page_factory.GetAddressOf())), "property factory failed");
            ComPtr<IPropertyPage> page; success(page_factory->CreateInstance(nullptr, IID_PPV_ARGS(page.GetAddressOf())), "property page construction failed");
            IUnknown* object = filter.Get(); success(page->SetObjects(1,&object), "property objects failed");
            PROPPAGEINFO info{}; success(page->GetPageInfo(&info), "page info failed"); require(info.pszTitle && wcscmp(info.pszTitle,renderer_name)==0, "property page title wrong"); CoTaskMemFree(info.pszTitle);
            const auto parent = CreateWindowExW(0,L"STATIC",L"Renderer test",WS_OVERLAPPED,0,0,700,720,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            require(parent != nullptr, "test parent creation failed"); const RECT rect{0,0,info.size.cx,info.size.cy};
            success(page->Activate(parent,&rect,FALSE), "settings dialog activation failed");
            require(dialog_text(page_window(parent),IDC_PLAYBACK) == L"No playback","inactive page status wrong");
            RECT footer{},client{}; const auto settings_window=page_window(parent);
            GetWindowRect(GetDlgItem(settings_window,IDC_STATUS),&footer);
            MapWindowPoints(nullptr,settings_window,reinterpret_cast<POINT*>(&footer),2);
            GetClientRect(settings_window,&client);
            require(footer.right<=client.right && footer.bottom<=client.bottom,"status footer clipped at advertised page size");
            success(page->Deactivate(), "settings dialog deactivation failed");
            auto wav = make_wave();
            {
                ComPtr<IGraphBuilder> graph; success(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(graph.GetAddressOf())), "graph creation failed");
                success(graph->AddFilter(filter.Get(),renderer_name), "adding renderer to graph failed");
                ComPtr<IBaseFilter> source; success(graph->AddSourceFilter(wav.c_str(),L"WAV",source.GetAddressOf()), "WAV source failed");
                auto out = first_pin(source.Get(),PINDIR_OUTPUT); require(bool(out), "source output missing");
                success(graph->Connect(out.Get(),pin.Get()), "WAV -> ASIO graph connection failed");
                success(live_status->GetLiveStatus(&live),"connected stopped status failed");
                require(live.connected && !live.engine.opened,"connecting graph pins opened the DAC before playback");
                ComPtr<IMediaControl> control; graph.As(&control);
                success(control->Pause(), "graph pause/preroll failed");
                OAFilterState state{}; success(control->GetState(2000,&state), "graph preroll state failed");
                success(live_status->GetLiveStatus(&live),"paused live status failed");
                require(live.in_graph && live.connected && live.engine.opened && live.playback == State_Paused && !live.engine.playing,"paused status wrong");
                require(live.input.valid_bits == 16 && live.input.container_bits == 16 && live.output.valid_bits == 32 && live.output.container_bits == 32,"live input/output depth wrong");
                require(live.input.type == AudioSampleType::integer && live.output.type == AudioSampleType::integer && live.engine.sample_rate == 48000 && live.engine.channels == 2,"live format wrong");
                ChannelStatus route{}; success(channels->GetChannelStatus(&route),"live channel status failed");
                require(route.input_channels==2 && route.output_channels==2 && !route.downmix && !route.mono_duplicate,"native live channels wrong");
                // Saving identical settings must not interrupt or reopen the active device.
                success(settings->SetSettings(&stored),"active settings save failed");
                success(live_status->GetLiveStatus(&live),"unchanged settings status failed");
                require(!live.settings_pending && live.engine.opened,"identical settings scheduled a reopen");
                success(control->Run(), "graph run failed");
                Sleep(200);
                success(live_status->GetLiveStatus(&live),"playing live status failed");
                require(live.playback == State_Running && live.engine.playing && live.engine.delivered_frames > 0 && !live.completed,"playing status wrong");
                success(page->Activate(parent,&rect,FALSE),"active property page failed");
                const auto window = page_window(parent);
                require(dialog_text(window,IDC_PLAYBACK).find(L"Playing") == 0,"page did not use active renderer");
                require(dialog_text(window,IDC_INPUT_FORMAT) == L"16-bit PCM" && dialog_text(window,IDC_OUTPUT_FORMAT) == L"32-bit PCM","page bit depths wrong");
                  require(dialog_text(window,IDC_SAMPLE_RATE) == L"48 kHz \u00b7 Stereo","page rate wrong");
                  require(SendDlgItemMessageW(window,IDC_PCM_BITS,CB_GETCOUNT,0,0)==4,"PCM precision choices missing");
                  require(SendDlgItemMessageW(window,IDC_SRC_ALGORITHM,CB_GETCOUNT,0,0)==2,"SRC choices missing");
                  const ResamplingOptions sinc{are::SrcAlgorithm::sinc},r8brain{};
                  success(resampling->SetResamplingOptions(&sinc),"deferring SRC failed");
                  ResamplingStatus src; success(resampling->GetResamplingStatus(&src),"pending SRC status failed");
                  require(src.settings_pending && src.algorithm==are::SrcAlgorithm::r8brain && !src.active,"pending SRC was applied to the current stream");
                  success(live_status->GetLiveStatus(&live),"pending SRC live status failed"); require(live.settings_pending,"live status omitted pending SRC");
                  success(resampling->SetResamplingOptions(&r8brain),"canceling pending SRC failed");
                  success(resampling->GetResamplingStatus(&src),"canceled SRC status failed"); require(!src.settings_pending,"SRC cancellation stayed pending");
                  const ProcessingOptions pcm24{24},native{0};
                  success(processing->SetProcessingOptions(&pcm24),"deferring PCM precision failed");
                  ProcessingStatus conversion; success(processing->GetProcessingStatus(&conversion),"processing live status failed");
                  require(conversion.settings_pending && !conversion.pcm_bits && !conversion.resampling,"pending PCM precision was applied to active samples");
                  success(processing->SetProcessingOptions(&native),"canceling pending PCM precision failed");
                  require(IsDlgButtonChecked(window,IDC_SMOOTH)==BST_CHECKED,"page smoothing setting wrong");
                  success(audio->put_Volume(-2795),"live non-unity volume failed");
                  SendMessageW(window,WM_TIMER,1,0);
                  require(dialog_text(window,IDC_STATUS).find(L"volume adjustment")!=std::wstring::npos,"page claimed bit-perfect at reduced volume");
                  success(audio->put_Volume(0),"live unity restoration failed");
                const auto activity = dialog_text(window,IDC_ACTIVITY);
                Sleep(80); SendMessageW(window,WM_TIMER,1,0);
                require(dialog_text(window,IDC_ACTIVITY) != activity,"page activity did not update");
                Settings next = stored; next.first_channel = 1;
                success(settings->SetSettings(&next),"saving device settings during playback failed");
                success(live_status->GetLiveStatus(&live),"pending settings status failed");
                require(live.settings_pending && live.engine.playing && live.engine.delivered_frames > 0,"settings save interrupted playback");
                SendMessageW(window,WM_TIMER,1,0);
                require(dialog_text(window,IDC_STATUS).find(L"Settings saved.") == 0,"page did not explain pending settings");
                // Restore the original routing while still running; pending changes cancel.
                success(settings->SetSettings(&stored),"canceling pending settings failed");
                ComPtr<IMediaSeeking> running_seek; success(graph.As(&running_seek), "graph seeking interface missing");
                LONGLONG current = 0; success(running_seek->GetCurrentPosition(&current), "running position unavailable");
                require(current > 0 && current < 8000000, "running position did not track playback");
                ComPtr<IMediaEvent> events; graph.As(&events); long complete = 0;
                success(events->WaitForCompletion(5000,&complete), "graph completion failed"); require(complete == EC_COMPLETE, "graph ended with error event");
                EngineStatus status{}; success(settings->GetStatus(&status), "status read failed");
                require(status.delivered_frames == 48000, "WAV graph lost frames");
                success(live_status->GetLiveStatus(&live),"completion live status failed");
                require(live.completed,"completed status missing");
                Settings after_stop = stored; after_stop.keep_device_rate = true;
                success(settings->SetSettings(&after_stop),"scheduling settings for stop failed");
                success(live_status->GetLiveStatus(&live),"deferred stop status failed");
                require(live.settings_pending,"device settings were not deferred until stop");
                success(control->Stop(), "graph stop failed");
                success(live_status->GetLiveStatus(&live),"stopped live status failed");
                require(live.playback == State_Stopped && !live.engine.opened && !live.input.valid_bits && !live.output.valid_bits && !live.settings_pending,"stopped status retained active formats");
                Settings applied{}; success(settings->GetSettings(&applied),"applied settings read failed");
                require(applied.keep_device_rate && applied.driver == stored.driver,"stop did not apply deferred settings");
                SendMessageW(window,WM_TIMER,1,0);
                require(dialog_text(window,IDC_PLAYBACK) == L"Stopped" && dialog_text(window,IDC_OUTPUT_FORMAT) == L"Inactive","page stopped status wrong");
                success(page->Deactivate(),"active page deactivation failed");
                // Replay after a seek exercises flush and end-of-stream reset.
                ComPtr<IMediaSeeking> seek; graph.As(&seek); LONGLONG position = 5000000;
                success(seek->SetPositions(&position,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning), "graph seeking failed");
                success(control->Run(), "graph replay failed");
                success(events->WaitForCompletion(5000,&complete), "graph replay completion failed"); require(complete == EC_COMPLETE, "replay ended with error event");
                success(settings->GetStatus(&status), "replay status read failed"); require(status.delivered_frames > 0 && status.delivered_frames <= 48000, "seek replay did not render");
                success(control->Stop(), "replay stop failed");
                success(graph->RemoveFilter(filter.Get()), "graph renderer removal failed");
            }
            std::filesystem::remove(wav);
            // Apply through the actual property page while a producer is
            // blocked on the full queue. Restart once for all changed fields,
            // preserving position, speed, volume and running/paused state.
            wav=make_wave(44100,6);
            {
                Settings device=stored; device.keep_device_rate=TRUE;
                success(settings->SetSettings(&device),"Apply test device setup failed");
                const ProcessingOptions native{0}; success(processing->SetProcessingOptions(&native),"Apply test precision setup failed");
                const ResamplingOptions r8brain{}; success(resampling->SetResamplingOptions(&r8brain),"Apply test SRC setup failed");
                ComPtr<IASIORenderApply> apply; success(filter.As(&apply),"missing live Apply interface");
                ComPtr<IGraphBuilder> graph; success(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(graph.GetAddressOf())),"Apply graph creation failed");
                success(graph->AddFilter(filter.Get(),renderer_name),"Apply renderer addition failed");
                ComPtr<IBaseFilter> source; success(graph->AddSourceFilter(wav.c_str(),L"WAV",source.GetAddressOf()),"Apply WAV source failed");
                auto out=first_pin(source.Get(),PINDIR_OUTPUT); success(graph->Connect(out.Get(),pin.Get()),"Apply graph connection failed");
                ComPtr<IMediaControl> control; graph.As(&control);
                ComPtr<IMediaSeeking> seeking; graph.As(&seeking);
                success(seeking->SetRate(1.25),"Apply speed setup failed");
                success(audio->put_Volume(-2795),"Apply volume setup failed");
                success(control->Run(),"Apply graph run failed"); Sleep(100);
                success(page->Activate(parent,&rect,FALSE),"Apply page activation failed");
                const auto window=page_window(parent);
                are::test::DriverMetrics before{},after{}; driver_metrics(&before);
                success(page->Apply(),"unchanged page Apply failed"); driver_metrics(&after);
                require(after.opens==before.opens && after.disposals==before.disposals,"unchanged Apply reopened ASIO");
                LONGLONG position_before{},position_after{}; seeking->GetCurrentPosition(&position_before);
                SendDlgItemMessageW(window,IDC_SRC_ALGORITHM,CB_SETCURSEL,1,0);
                SendDlgItemMessageW(window,IDC_PCM_BITS,CB_SETCURSEL,2,0);
                SendDlgItemMessageW(window,IDC_BUFFER,CB_SETCURSEL,3,0);
                SetDlgItemInt(window,IDC_CHANNEL,2,FALSE);
                SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDC_SRC_ALGORITHM,CBN_SELCHANGE),0);
                success(page->Apply(),"running property-page Apply failed");
                driver_metrics(&after);
                require(after.opens==before.opens+1 && after.disposals==before.disposals+1,"Apply did not batch changes into one ASIO restart");
                ResamplingStatus src; success(resampling->GetResamplingStatus(&src),"applied SRC status failed");
                ProcessingStatus precision; success(processing->GetProcessingStatus(&precision),"applied precision status failed");
                success(live_status->GetLiveStatus(&live),"applied live status failed");
                require(src.algorithm==are::SrcAlgorithm::sinc && src.active && !src.settings_pending && precision.pcm_bits==24 && !live.settings_pending,"Apply left the new converter or precision pending");
                if (live.playback!=State_Running || !live.engine.playing || live.engine.buffer_frames!=256)
                    std::cerr << "Apply state " << live.playback << ", playing " << live.engine.playing << ", buffer " << live.engine.buffer_frames << ", error " << std::hex << unsigned(live.engine.error) << std::dec << '\n';
                require(live.playback==State_Running && live.engine.playing && live.engine.buffer_frames==256,"Apply did not resume playback with the new buffer size");
                success(settings->GetSettings(&device),"applied routing unavailable"); require(device.first_channel==1,"Apply did not update ASIO routing");
                double rate{}; renderer_seeking->GetRate(&rate); audio->get_Volume(&volume);
                require(rate==1.25 && volume==-2795,"Apply changed playback speed or volume");
                seeking->GetCurrentPosition(&position_after);
                require(std::abs(position_after-position_before)<5000000,"Apply jumped to a different playback position");
                Sleep(100); success(live_status->GetLiveStatus(&live),"post-Apply status failed");
                require(live.engine.delivered_frames>0 && SUCCEEDED(live.engine.error),"Apply failed to deliver audio after reopening");
                success(control->Pause(),"Apply graph pause failed");
                OAFilterState state{}; success(control->GetState(2000,&state),"Apply graph pause did not settle");
                seeking->GetCurrentPosition(&position_before);
                SendDlgItemMessageW(window,IDC_SRC_ALGORITHM,CB_SETCURSEL,0,0);
                SendDlgItemMessageW(window,IDC_PCM_BITS,CB_SETCURSEL,1,0);
                success(page->Apply(),"paused property-page Apply failed");
                success(live_status->GetLiveStatus(&live),"paused applied live status failed");
                success(resampling->GetResamplingStatus(&src),"paused applied SRC status failed");
                seeking->GetCurrentPosition(&position_after);
                require(live.playback==State_Paused && !live.engine.playing && !live.settings_pending && src.algorithm==are::SrcAlgorithm::r8brain,"Apply resumed a paused graph or deferred SRC");
                require(std::abs(position_after-position_before)<100000,"paused Apply changed playback position");
                success(control->Run(),"resume after paused Apply failed"); Sleep(80);
                // Invalid device settings roll back to the last working stream.
                SetDlgItemInt(window,IDC_CHANNEL,1024,FALSE);
                require(FAILED(page->Apply()),"Apply accepted unavailable ASIO output");
                success(live_status->GetLiveStatus(&live),"failed Apply status unavailable");
                success(settings->GetSettings(&device),"rolled-back routing unavailable");
                if (!live.engine.opened || !live.engine.playing || FAILED(live.engine.error) || live.settings_pending || device.first_channel!=1)
                    std::cerr << "Rollback state " << live.playback << ", open " << live.engine.opened << ", playing " << live.engine.playing << ", pending " << live.settings_pending << ", channel " << device.first_channel << ", error " << std::hex << unsigned(live.engine.error) << std::dec << '\n';
                require(live.engine.opened && live.engine.playing && SUCCEEDED(live.engine.error) && !live.settings_pending && device.first_channel==1,"failed Apply did not restore the working stream");
                require(page->IsPageDirty()==S_OK,"failed Apply cleared unsaved page edits");
                SetDlgItemInt(window,IDC_CHANNEL,2,FALSE); success(page->Apply(),"corrected Apply failed");
                success(audio->put_Volume(-10000),"Apply mute setup failed");
                SendDlgItemMessageW(window,IDC_SRC_ALGORITHM,CB_SETCURSEL,1,0); success(page->Apply(),"muted Apply failed");
                audio->get_Volume(&volume); require(volume==-10000,"Apply unmuted playback");
                success(audio->put_Volume(0),"Apply test unity restore failed");
                ComPtr<IMediaEvent> events; graph.As(&events); long complete{};
                // Failed Apply must not post EC_ERRORABORT to the player when
                // the previous stream has been restored successfully.
                long event{}; LONG_PTR first{},second{};
                while (events->GetEvent(&event,&first,&second,0)==S_OK) {
                    require(event!=EC_ERRORABORT,"failed Apply posted a fatal graph error despite rollback");
                    events->FreeEventParams(event,first,second);
                }
                success(events->WaitForCompletion(8000,&complete),"applied graph failed to finish"); require(complete==EC_COMPLETE,"applied graph ended in error");
                driver_metrics(&before); success(control->Stop(),"Apply graph stop failed");
                const auto deadline=GetTickCount64()+1500;
                do { success(live_status->GetLiveStatus(&live),"stop-release status failed"); if (!live.engine.opened) break; Sleep(10); } while (GetTickCount64()<deadline);
                driver_metrics(&after);
                require(!live.engine.opened && after.stops==before.stops+1 && after.disposals==before.disposals+1,"real Stop failed to release retained ASIO device");
                // Race the graph's Run transition against a producer's
                // NewSegment, and separately against a flush pair. All calls
                // must finish with a running consumer, not a stalled queue.
                success(filter->Pause(),"transition race setup failed");
                for (int iteration=0;iteration<24;++iteration) {
                    success(filter->Pause(),"transition race pause failed");
                    REFERENCE_TIME now{}; success(clock->GetTime(&now),"transition race clock failed");
                    std::barrier begin(3);
                    auto run=std::async(std::launch::async,[&] {
                        CoInitializeEx(nullptr,COINIT_MULTITHREADED); begin.arrive_and_wait();
                        const auto hr=filter->Run(now); CoUninitialize(); return hr;
                    });
                    auto segment=std::async(std::launch::async,[&] {
                        CoInitializeEx(nullptr,COINIT_MULTITHREADED); begin.arrive_and_wait();
                        auto hr=iteration%2 ? pin->NewSegment(0,60000000,iteration%3==0 ? 1.0 : 1.25) : pin->BeginFlush();
                        if (SUCCEEDED(hr) && iteration%2==0) hr=pin->EndFlush();
                        CoUninitialize(); return hr;
                    });
                    begin.arrive_and_wait(); success(run.get(),"racing Run failed"); success(segment.get(),"racing segment/flush failed");
                    success(live_status->GetLiveStatus(&live),"transition race status failed");
                    require(live.playback==State_Running && live.engine.playing,"segment/flush race left a running graph with a paused engine");
                }
                success(filter->Stop(),"transition race stop failed");
                success(page->Deactivate(),"Apply page deactivation failed");
                success(graph->RemoveFilter(filter.Get()),"Apply renderer removal failed");
                std::cout << "Live Apply: batched running/paused restart, position/speed/volume/mute, unchanged Apply, rollback, idle release and Run/segment/flush races passed.\n";
            }
            std::filesystem::remove(wav);
            // Real DirectShow producer, back-pressure, converter tail and seek
            // at a different device rate, through the same public COM contract.
            wav=make_wave(44100);
            Settings keep=stored; keep.keep_device_rate=TRUE;
            success(settings->SetSettings(&keep),"SRC graph settings failed");
            const ProcessingOptions pcm16{16}; success(processing->SetProcessingOptions(&pcm16),"SRC graph PCM precision failed");
            for (const auto algorithm : {are::SrcAlgorithm::r8brain,are::SrcAlgorithm::sinc}) {
                const ResamplingOptions selected_src{algorithm}; success(resampling->SetResamplingOptions(&selected_src),"SRC graph algorithm selection failed");
                ComPtr<IGraphBuilder> graph; success(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(graph.GetAddressOf())),"SRC graph creation failed");
                success(graph->AddFilter(filter.Get(),renderer_name),"SRC renderer addition failed");
                ComPtr<IBaseFilter> source; success(graph->AddSourceFilter(wav.c_str(),L"WAV",source.GetAddressOf()),"SRC WAV source failed");
                auto out=first_pin(source.Get(),PINDIR_OUTPUT); success(graph->Connect(out.Get(),pin.Get()),"SRC graph connection failed");
                ComPtr<IMediaControl> control; graph.As(&control); success(control->Run(),"SRC graph run failed");
                Sleep(150);
                ProcessingStatus conversion; success(processing->GetProcessingStatus(&conversion),"SRC graph processing status failed");
                require(conversion.input_rate==44100 && conversion.output_rate==48000 && conversion.resampling && conversion.pcm_bits==16,"SRC graph reported the wrong rates or precision");
                success(page->Activate(parent,&rect,FALSE),"SRC property page failed");
                auto window=page_window(parent);
                require(dialog_text(window,IDC_SAMPLE_RATE).find(L"44.1 \u2192 48 kHz") == 0,"page omitted SRC input/output rates");
                const auto src_name=algorithm==are::SrcAlgorithm::sinc ? L"Sinc" : L"r8brain";
                require(dialog_text(window,IDC_SAMPLE_RATE).find(src_name)!=std::wstring::npos && dialog_text(window,IDC_STATUS).find(src_name)!=std::wstring::npos,"page reported a different converter than the active SRC");
                ResamplingStatus active_src; success(resampling->GetResamplingStatus(&active_src),"active SRC status failed");
                require(active_src.active && active_src.algorithm==algorithm && !active_src.settings_pending,"SRC graph algorithm status wrong");
                require(dialog_text(window,IDC_OUTPUT_FORMAT).find(L"16-bit PCM (ASIO: 32-bit PCM)") == 0,"page conflated PCM precision and ASIO container");
                require(dialog_text(window,IDC_STATUS).find(L"resampling")!=std::wstring::npos,"page claimed exact samples during SRC");
                ComPtr<IMediaEvent> events; graph.As(&events); long complete{};
                success(events->WaitForCompletion(5000,&complete),"SRC graph did not drain"); require(complete==EC_COMPLETE,"SRC graph ended with error");
                EngineStatus status; success(settings->GetStatus(&status),"SRC EOS status failed"); require(status.delivered_frames==48000,"SRC graph duration or final tail was wrong");
                ComPtr<IMediaSeeking> seeking; graph.As(&seeking); LONGLONG position=5000000;
                success(seeking->SetPositions(&position,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning),"SRC graph seek failed");
                success(control->Run(),"SRC graph replay failed"); success(events->WaitForCompletion(5000,&complete),"SRC replay did not finish");
                success(settings->GetStatus(&status),"SRC replay status failed"); require(status.delivered_frames==24000,"SRC seek retained history or changed duration");
                success(control->Stop(),"SRC graph stop failed"); success(page->Deactivate(),"SRC property page deactivation failed");
                success(graph->RemoveFilter(filter.Get()),"SRC renderer removal failed");
            }
            std::filesystem::remove(wav);
            // Exercise the player's real graph-rate contract: frame counts
            // change with tempo, timestamps must not insert gaps at slow rates,
            // and seeks must drain the tempo and SRC tails together.
            for (const auto input_rate : {48000u,44100u}) {
                wav=make_wave(input_rate);
                for (const auto algorithm : {are::SrcAlgorithm::r8brain,are::SrcAlgorithm::sinc}) {
                    const ResamplingOptions selected{algorithm}; success(resampling->SetResamplingOptions(&selected),"speed SRC selection failed");
                    ComPtr<IGraphBuilder> graph; success(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(graph.GetAddressOf())),"speed graph creation failed");
                    success(graph->AddFilter(filter.Get(),renderer_name),"speed renderer addition failed");
                    ComPtr<IBaseFilter> source; success(graph->AddSourceFilter(wav.c_str(),L"WAV",source.GetAddressOf()),"speed WAV source failed");
                    auto out=first_pin(source.Get(),PINDIR_OUTPUT); success(graph->Connect(out.Get(),pin.Get()),"speed graph connection failed");
                    ComPtr<IMediaControl> control; graph.As(&control);
                    ComPtr<IMediaSeeking> seeking; graph.As(&seeking);
                    ComPtr<IMediaEvent> events; graph.As(&events);
                    for (const auto rate : {0.5,1.001,1.25,2.0,1.0}) {
                        LONGLONG position=0; success(seeking->SetPositions(&position,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning),"speed start seek failed");
                        success(seeking->SetRate(rate),"graph playback rate rejected");
                        double actual{}; success(seeking->GetRate(&actual),"graph rate read failed"); require(actual==rate,"graph ignored playback rate");
                        success(control->Run(),"speed graph run failed"); Sleep(100);
                        success(renderer_seeking->GetRate(&actual),"renderer rate read failed"); require(actual==rate,"renderer ignored segment rate");
                        success(page->Activate(parent,&rect,FALSE),"speed property page failed"); const auto window=page_window(parent);
                        const auto rate_text=std::format(L" \u00b7 {:g}\u00d7",rate);
                        require(dialog_text(window,IDC_PLAYBACK).find(rate_text)!=std::wstring::npos,"live page omitted playback speed");
                        if (rate!=1.0) require(dialog_text(window,IDC_STATUS).find(L"playback speed")!=std::wstring::npos,"page claimed exact samples during speed processing");
                        if (rate!=1.0) require(dialog_text(window,IDC_STATUS).find(L"Pitch follows speed")!=std::wstring::npos,"page reported the wrong speed/pitch behavior");
                        long complete{}; const auto finished=events->WaitForCompletion(6000,&complete);
                        if (FAILED(finished)) {
                            LiveStatus diagnostic; live_status->GetLiveStatus(&diagnostic);
                            std::cerr << "Speed timeout: input " << input_rate << ", SRC " << unsigned(algorithm) << ", rate " << rate << ", state " << diagnostic.playback << ", playing " << diagnostic.engine.playing << ", delivered " << diagnostic.engine.delivered_frames << ", queued " << diagnostic.engine.queued_frames << ", error " << std::hex << unsigned(diagnostic.engine.error) << std::dec << '\n';
                        }
                        success(finished,"speed graph did not finish"); require(complete==EC_COMPLETE,"speed graph ended with error");
                        EngineStatus status; success(settings->GetStatus(&status),"speed status read failed");
                        // One conversion combines speed and device rate;
                        // intermediate rounding would lose a frame at 1.001x.
                        const auto output_frames=std::uint64_t(std::round(48000/rate));
                        if (status.delivered_frames!=output_frames) std::cerr << "Speed " << rate << ", input " << input_rate << ": " << status.delivered_frames << " vs " << output_frames << '\n';
                        require(status.delivered_frames==output_frames && status.sample_rate==48000,"speed duration, timing or fixed output rate wrong");
                        success(page->Deactivate(),"speed page deactivation failed");
                        position=5000000; success(seeking->SetPositions(&position,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning),"speed graph seek failed");
                        success(control->Run(),"speed graph replay failed"); success(events->WaitForCompletion(6000,&complete),"speed seek did not finish");
                        success(settings->GetStatus(&status),"speed seek status read failed");
                        require(status.delivered_frames==std::uint64_t(std::round(24000/rate)),"speed seek retained history or changed duration");
                        success(control->Stop(),"speed graph stop failed");
                    }
                    // Change speed during running playback, as the toolbar does.
                    LONGLONG position=0; success(seeking->SetPositions(&position,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning),"running rate seek failed");
                    success(control->Run(),"running rate start failed"); Sleep(80);
                    are::test::DriverMetrics before{},after{}; driver_metrics(&before);
                    const auto rate_started=GetTickCount64();
                    success(seeking->SetRate(0.8),"running rate decrease failed");
                    const auto rate_elapsed=GetTickCount64()-rate_started; driver_metrics(&after);
                    std::cout << "Rate change: " << input_rate << " Hz, SRC " << unsigned(algorithm) << ", " << rate_elapsed << " ms; opens " << after.opens-before.opens << ", starts " << after.starts-before.starts << ", stops " << after.stops-before.stops << ", disposals " << after.disposals-before.disposals << '\n';
                    require(after.opens==before.opens && after.starts==before.starts && after.stops==before.stops && after.disposals==before.disposals && after.rate_changes==before.rate_changes,"toolbar rate decrease restarted ASIO hardware");
                    require(rate_elapsed<200,"toolbar rate decrease blocked too long");
                    Sleep(80);
                    success(renderer_seeking->GetRate(&normal_rate),"running rate read failed"); require(normal_rate==0.8,"running decrease ignored");
                    success(seeking->SetRate(2.0),"running rate increase failed"); Sleep(80);
                    driver_metrics(&after); require(after.opens==before.opens && after.starts==before.starts && after.stops==before.stops && after.disposals==before.disposals,"toolbar rate increase restarted ASIO");
                    success(renderer_seeking->GetRate(&normal_rate),"running increased rate read failed"); require(normal_rate==2.0,"running increase ignored");
                    success(seeking->SetRate(1.0),"running normal speed restoration failed");
                    driver_metrics(&after); require(after.opens==before.opens && after.starts==before.starts && after.stops==before.stops && after.disposals==before.disposals,"toolbar rate reset restarted ASIO");
                    long complete{}; success(events->WaitForCompletion(6000,&complete),"rate change playback did not finish"); require(complete==EC_COMPLETE,"rate change playback errored");
                    success(control->Stop(),"running rate stop failed"); success(graph->RemoveFilter(filter.Get()),"speed renderer removal failed");
                }
                std::filesystem::remove(wav);
            }
            DestroyWindow(parent); success(page->SetObjects(0,nullptr), "page detach failed");
            filter->SetSyncSource(nullptr);
        }
        require(can_unload() == S_OK, "renderer leaked COM objects or worker threads");
        std::cout << "DLL, COM identity, live property page, deferred settings, DirectShow WAV graph, seek/replay and unload passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    CoUninitialize(); return result;
}

