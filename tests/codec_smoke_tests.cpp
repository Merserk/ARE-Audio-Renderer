// Opt-in integration test using MPC-HC's actual LAV splitter/decoder binaries.
// Nonzero synthetic fixtures render only to the simulated ASIO driver.
#include "win/interfaces.hpp"
#include "win/settings.hpp"
#include "win/devices.hpp"
#include "fake_asio.hpp"
#include "preferences_lock.hpp"
#include "lav_config.hpp"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace are::win;
void checked(HRESULT hr) { if (FAILED(hr)) throw hr; }
struct Library {
    HMODULE module{};
    explicit Library(const wchar_t* path):module(LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) { if (!module) throw HRESULT_FROM_WIN32(GetLastError()); }
    ~Library() { FreeLibrary(module); }
    ComPtr<IBaseFilter> create(REFCLSID clsid) {
        using GetClass=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
        auto function=reinterpret_cast<GetClass>(GetProcAddress(module,"DllGetClassObject"));
        if (!function) throw E_NOINTERFACE;
        ComPtr<IClassFactory> factory; checked(function(clsid,IID_PPV_ARGS(factory.GetAddressOf())));
        ComPtr<IBaseFilter> filter; checked(factory->CreateInstance(nullptr,IID_PPV_ARGS(filter.GetAddressOf()))); return filter;
    }
};
// Preserve exact value types and absence, including preferences added in future
// versions. Configure only after construction so failures also restore them.
struct Preferences : are::test::PreferencesLock {
    struct Value { std::wstring name; DWORD type{}; std::vector<BYTE> data; };
    bool existed{};
    std::vector<Value> values;
    Preferences() {
        RegKey key;
        const auto opened=RegOpenKeyExW(HKEY_CURRENT_USER,settings_registry_path,0,KEY_READ,key.put());
        if (opened==ERROR_FILE_NOT_FOUND) return;
        checked(HRESULT_FROM_WIN32(opened)); existed=true;
        DWORD count{},name_size{},data_size{};
        checked(HRESULT_FROM_WIN32(RegQueryInfoKeyW(key.get(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&count,&name_size,&data_size,nullptr,nullptr)));
        for (DWORD i=0;i<count;++i) {
            std::wstring name(name_size+1,L'\0'); std::vector<BYTE> data(data_size);
            DWORD characters=DWORD(name.size()),bytes=DWORD(data.size()),type{};
            checked(HRESULT_FROM_WIN32(RegEnumValueW(key.get(),i,name.data(),&characters,nullptr,&type,data.data(),&bytes)));
            name.resize(characters); data.resize(bytes); values.push_back({std::move(name),type,std::move(data)});
        }
    }
    void configure() {
        checked(save_settings(Settings{are::test::fake_clsid,0,0,TRUE}));
        checked(save_processing_options({0})); checked(save_resampling_options({}));
        checked(save_playback_options({FALSE}));
    }
    ~Preferences() {
        if (!existed) { RegDeleteTreeW(HKEY_CURRENT_USER,settings_registry_path); return; }
        RegKey key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,settings_registry_path,0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,key.put(),nullptr)!=ERROR_SUCCESS) return;
        DWORD i=0;
        for (;;) {
            wchar_t name[16384]{}; DWORD size=DWORD(std::size(name));
            if (RegEnumValueW(key.get(),i,name,&size,nullptr,nullptr,nullptr,nullptr)!=ERROR_SUCCESS) break;
            const bool saved=std::any_of(values.begin(),values.end(),[&](const Value& value) { return value.name==name; });
            if (saved) ++i;
            else if (RegDeleteValueW(key.get(),name)!=ERROR_SUCCESS) break;
        }
        for (const auto& value:values) RegSetValueExW(key.get(),value.name.c_str(),0,value.type,value.data.data(),DWORD(value.data.size()));
    }
};
struct DriverRegistration {
    std::wstring path=L"Software\\Classes\\CLSID\\"+guid_string(are::test::fake_clsid);
    explicit DriverRegistration(const wchar_t* dll) {
        RegKey key;
        checked(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CURRENT_USER,(path+L"\\InprocServer32").c_str(),0,nullptr,0,KEY_WRITE,nullptr,key.put(),nullptr)));
        checked(HRESULT_FROM_WIN32(RegSetValueExW(key.get(),nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(dll),DWORD((wcslen(dll)+1)*2))));
        constexpr wchar_t model[]=L"Both";
        checked(HRESULT_FROM_WIN32(RegSetValueExW(key.get(),L"ThreadingModel",0,REG_SZ,reinterpret_cast<const BYTE*>(model),sizeof(model))));
    }
    ~DriverRegistration() { RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str()); }
};
ComPtr<IPin> pin(IBaseFilter* filter,PIN_DIRECTION direction) {
    ComPtr<IEnumPins> pins; checked(filter->EnumPins(pins.GetAddressOf())); ComPtr<IPin> result;
    while (pins->Next(1,result.ReleaseAndGetAddressOf(),nullptr)==S_OK) { PIN_DIRECTION d{}; checked(result->QueryDirection(&d)); if (d==direction) return result; }
    throw VFW_E_NOT_FOUND;
}
int wmain(int argc,wchar_t** argv) {
    if (argc<6 || argc>10) { std::wcerr<<L"Usage: codec_smoke_tests renderer.dll fake_driver.dll LAVSplitter.ax LAVAudio.ax fixtures-directory [device-channels [device-rate [synthetic|real-content [r8brain|sinc]]]]\n"; return 2; }
    if (FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED))) return 1;
    int failed=0;
    try {
        Preferences preferences; preferences.configure();
        if (argc>9) {
            const std::wstring_view algorithm(argv[9]);
            if (algorithm!=L"sinc" && algorithm!=L"r8brain") throw E_INVALIDARG;
            checked(save_resampling_options({algorithm==L"sinc"?are::SrcAlgorithm::sinc:are::SrcAlgorithm::r8brain}));
        }
        DriverRegistration registration(argv[2]); Library driver(argv[2]),renderer(argv[1]),splitter(argv[3]),decoder(argv[4]);
        using Metrics=HRESULT(WINAPI*)(are::test::OutputMetrics*);
        auto metrics=reinterpret_cast<Metrics>(GetProcAddress(driver.module,"GetOutputMetrics")); if (!metrics) throw E_NOINTERFACE;
        using Configure=HRESULT(WINAPI*)(long,double);
        const auto configure=reinterpret_cast<Configure>(GetProcAddress(driver.module,"ConfigureTestDriver")); if (!configure) throw E_NOINTERFACE;
        const auto device_channels=argc>6 ? wcstol(argv[6],nullptr,10) : 8;
        const auto device_rate=argc>7 ? wcstod(argv[7],nullptr) : 48000;
        const bool real_content=argc>8 && std::wstring_view(argv[8])==L"real-content";
        checked(configure(device_channels,device_rate));
        std::wcout<<L"Device: "<<device_channels<<L" channels, "<<device_rate<<L" Hz\n";
        CLSID source_clsid{},decoder_clsid{};
        checked(CLSIDFromString(L"{B98D13E7-55DB-4385-A33D-09FD1BA26338}",&source_clsid));
        checked(CLSIDFromString(L"{E8E73B6B-4CB3-44A4-BE99-4F7BCB96E491}",&decoder_clsid));
        std::vector<std::filesystem::path> files;
        for (const auto& entry:std::filesystem::directory_iterator(argv[5])) {
            const auto extension=entry.path().extension();
            if (entry.is_regular_file() && extension!=L".txt" && extension!=L".json" && extension!=L".md") files.push_back(entry.path());
        }
        std::ranges::sort(files);
        if (files.empty()) throw E_INVALIDARG;
        std::size_t passed=0;
        for (const auto& path:files) {
            EngineStatus status{};
            ComPtr<IMediaControl> control;
            ComPtr<IASIORenderSettings> settings;
            const wchar_t* stage=L"construct";
            std::wcout<<path.filename().wstring()<<L": "<<std::flush;
            try {
                auto sink=renderer.create(clsid_renderer);
                checked(sink.As(&settings));
                // Auto/native output, with the caller's preferences restored
                // by the scope guard after all clips finish.
                Settings current{}; checked(settings->GetSettings(&current));
                if (current.driver!=are::test::fake_clsid) throw E_INVALIDARG;
                ComPtr<IGraphBuilder> graph; checked(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(graph.GetAddressOf())));
                auto source=splitter.create(source_clsid); auto audio=decoder.create(decoder_clsid);
                ComPtr<are::test::LAVSplitterConfiguration> split_config; checked(source.As(&split_config));
                checked(split_config->SetRuntimeConfig(TRUE));
                ComPtr<are::test::LAVAudioConfiguration> audio_config; checked(audio.As(&audio_config));
                checked(audio_config->SetRuntimeConfig(TRUE));
                for (int codec=0;codec<5;++codec) checked(audio_config->SetBitstreamConfig(codec,FALSE));
                checked(audio_config->SetMixingEnabled(FALSE)); checked(audio_config->SetExpandMono(FALSE));
                checked(audio_config->SetExpand61(FALSE)); checked(audio_config->SetOutputStandardLayout(FALSE));
                // MPC-HC's internal LAV decoder also enables these codecs.
                for (const int codec:{are::test::wma2,are::test::wma_pro,are::test::wma_lossless}) checked(audio_config->SetFormatConfiguration(codec,TRUE));
                checked(graph->AddFilter(sink.Get(),renderer_name)); checked(graph->AddFilter(source.Get(),L"LAV Splitter Source")); checked(graph->AddFilter(audio.Get(),L"LAV Audio Decoder"));
                stage=L"load";
                ComPtr<IFileSourceFilter> file; checked(source.As(&file)); checked(file->Load(path.c_str(),nullptr));
                stage=L"connect splitter";
                checked(graph->ConnectDirect(pin(source.Get(),PINDIR_OUTPUT).Get(),pin(audio.Get(),PINDIR_INPUT).Get(),nullptr));
                stage=L"connect decoder";
                checked(graph->ConnectDirect(pin(audio.Get(),PINDIR_OUTPUT).Get(),pin(sink.Get(),PINDIR_INPUT).Get(),nullptr));
                ComPtr<IReferenceClock> clock; checked(sink.As(&clock)); ComPtr<IMediaFilter> graph_filter; checked(graph.As(&graph_filter)); checked(graph_filter->SetSyncSource(clock.Get()));
                stage=L"run"; checked(graph.As(&control)); checked(control->Run());
                stage=L"completion";
                ComPtr<IMediaEvent> events; checked(graph.As(&events)); long complete{}; checked(events->WaitForCompletion(15000,&complete));
                if (complete!=EC_COMPLETE) throw E_FAIL;
                stage=L"validate output samples";
                checked(settings->GetStatus(&status)); are::test::OutputMetrics observed{}; checked(metrics(&observed));
                if (status.delivered_frames<status.sample_rate || status.delivered_frames>std::uint64_t(status.sample_rate)*(real_content?10:3) || FAILED(status.error)) throw E_FAIL;
                ComPtr<IASIORenderChannels> channel_interface; checked(sink.As(&channel_interface));
                ChannelStatus route{}; checked(channel_interface->GetChannelStatus(&route));
                const auto expected_outputs=status.channels==1 && device_channels>=2 ? 2u
                    : status.channels<=UINT(device_channels) ? status.channels : device_channels>=2 ? 2u : 1u;
                if (route.input_channels!=status.channels || route.output_channels!=expected_outputs || route.downmix!=(expected_outputs<status.channels)) throw E_FAIL;
                double total_peak=0;
                for (UINT c=0;c<route.output_channels;++c) {
                    total_peak=std::max(total_peak,observed.peak[c]);
                    if (observed.peak[c]>1.0 || (!real_content && (observed.nonzero[c]<1000 || observed.peak[c]<0.005 || observed.peak[c]>0.2))) throw E_FAIL;
                }
                if (real_content && total_peak<0.001) throw E_FAIL;
                ComPtr<IASIORenderStatus> live_interface; checked(sink.As(&live_interface)); LiveStatus live{}; checked(live_interface->GetLiveStatus(&live));
                ++passed;
                std::wcout<<L"PASS, "<<route.input_channels<<L" -> "<<route.output_channels<<L" channels, "<<(live.input.type==AudioSampleType::integer?L"PCM":L"Float")<<live.input.valid_bits<<L", "<<status.sample_rate<<L" Hz, "<<status.delivered_frames<<L" frames, peak "<<observed.peak[0]<<L", nonzero "<<observed.nonzero[0]<<L", underruns "<<status.underruns<<L'\n';
                checked(control->Stop());
            } catch (HRESULT hr) {
                ++failed;
                if (settings) settings->GetStatus(&status);
                std::wcout<<L"FAIL ("<<stage<<L") 0x"<<std::hex<<unsigned(hr)<<std::dec<<L": "<<status.detail<<L'\n';
                if (control) control->Stop();
            }
        }
        std::wcout<<L"Result: "<<passed<<L" passed, "<<failed<<L" failed.\n";
    } catch (HRESULT hr) { std::wcerr<<L"Codec test setup failed: 0x"<<std::hex<<unsigned(hr)<<L'\n'; ++failed; }
      catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; ++failed; }
    CoUninitialize(); return failed?1:0;
}
