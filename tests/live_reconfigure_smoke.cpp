// Opt-in real-driver test for the DirectShow rate and live Apply contracts.
// Uses a quiet generated tone; preserves exact renderer registry preferences.
#include "win/interfaces.hpp"
#include "win/settings.hpp"
#include "preferences_lock.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace are;
using namespace are::win;
void checked(HRESULT hr) { if (FAILED(hr)) throw hr; }
void require(bool value) { if (!value) throw E_FAIL; }
struct Preferences : are::test::PreferencesLock {
    struct Value { std::wstring name; DWORD type; std::vector<BYTE> bytes; };
    std::vector<Value> values;
    bool existed{};
    Preferences() {
        RegKey key;
        const auto opened=RegOpenKeyExW(HKEY_CURRENT_USER,settings_registry_path,0,KEY_READ,key.put());
        if (opened==ERROR_FILE_NOT_FOUND) return;
        checked(HRESULT_FROM_WIN32(opened)); existed=true;
        DWORD count{},names{},bytes{};
        checked(HRESULT_FROM_WIN32(RegQueryInfoKeyW(key.get(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&count,&names,&bytes,nullptr,nullptr)));
        for (DWORD i=0;i<count;++i) {
            std::wstring name(names+1,L'\0'); std::vector<BYTE> data(bytes);
            DWORD length=DWORD(name.size()),size=DWORD(data.size()),type{};
            checked(HRESULT_FROM_WIN32(RegEnumValueW(key.get(),i,name.data(),&length,nullptr,&type,data.data(),&size)));
            name.resize(length); data.resize(size); values.push_back({std::move(name),type,std::move(data)});
        }
    }
    ~Preferences() {
        if (!existed) { RegDeleteTreeW(HKEY_CURRENT_USER,settings_registry_path); return; }
        RegDeleteTreeW(HKEY_CURRENT_USER,settings_registry_path);
        RegKey key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,settings_registry_path,0,nullptr,0,KEY_WRITE,nullptr,key.put(),nullptr)!=ERROR_SUCCESS) return;
        for (const auto& value:values)
            RegSetValueExW(key.get(),value.name.c_str(),0,value.type,value.bytes.data(),DWORD(value.bytes.size()));
    }
};
struct Library { HMODULE module; ~Library() { if (module) FreeLibrary(module); } };
struct StopGraph { IMediaControl* control; ~StopGraph() { control->Stop(); } };
struct Fixture {
    std::filesystem::path path=std::filesystem::temp_directory_path()/(L"are-live-reconfigure-"+std::to_wstring(GetCurrentProcessId())+L".wav");
    Fixture() {
        std::ofstream output(path,std::ios::binary|std::ios::trunc);
        const std::uint32_t rate=48000,frames=rate*24,data_size=frames*4,riff_size=36+data_size,fmt_size=16,byte_rate=rate*4;
        const std::uint16_t tag=1,channels=2,align=4,bits=16;
        auto put=[&](const auto& value) { output.write(reinterpret_cast<const char*>(&value),sizeof(value)); };
        output.write("RIFF",4); put(riff_size); output.write("WAVEfmt ",8); put(fmt_size); put(tag); put(channels); put(rate); put(byte_rate); put(align); put(bits); output.write("data",4); put(data_size);
        for (std::uint32_t i=0;i<frames;++i) { const auto sample=std::int16_t(std::round(500*std::sin(2*3.14159265358979323846*1000*i/rate))); put(sample); put(sample); }
        require(output.good());
    }
    ~Fixture() { std::error_code error; std::filesystem::remove(path,error); }
};
ComPtr<IPin> pin(IBaseFilter* filter,PIN_DIRECTION direction) {
    ComPtr<IEnumPins> pins; checked(filter->EnumPins(pins.GetAddressOf()));
    ComPtr<IPin> result;
    while (pins->Next(1,result.ReleaseAndGetAddressOf(),nullptr)==S_OK) {
        PIN_DIRECTION actual{}; checked(result->QueryDirection(&actual)); if (actual==direction) return result;
    }
    throw VFW_E_NOT_FOUND;
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=3) { std::wcerr << L"Usage: live_reconfigure_smoke renderer.dll ASIO-CLSID\n"; return 2; }
    checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
    int result{};
    try {
        Preferences preferences; Library library{LoadLibraryW(argv[1])}; require(library.module!=nullptr);
        using GetClass=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
        const auto get_class=reinterpret_cast<GetClass>(GetProcAddress(library.module,"DllGetClassObject")); require(get_class!=nullptr);
        ComPtr<IClassFactory> factory; checked(get_class(clsid_renderer,IID_PPV_ARGS(factory.GetAddressOf())));
        ComPtr<IBaseFilter> renderer; checked(factory->CreateInstance(nullptr,IID_PPV_ARGS(renderer.GetAddressOf())));
        ComPtr<IASIORenderSettings> settings; checked(renderer.As(&settings));
        ComPtr<IASIORenderStatus> status; checked(renderer.As(&status));
        ComPtr<IASIORenderProcessing> processing; checked(renderer.As(&processing));
        ComPtr<IASIORenderResampling> resampling; checked(renderer.As(&resampling));
        ComPtr<IASIORenderApply> apply; renderer.As(&apply);
        CLSID driver{}; checked(CLSIDFromString(argv[2],&driver));
        const Settings selected{driver,0,0,TRUE}; checked(settings->SetSettings(&selected));
        const ProcessingOptions native{0}; checked(processing->SetProcessingOptions(&native));
        ComPtr<IBasicAudio> volume; checked(renderer.As(&volume)); checked(volume->put_Volume(-3000));
        Fixture fixture;
        ComPtr<IGraphBuilder> graph; checked(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(graph.GetAddressOf())));
        checked(graph->AddFilter(renderer.Get(),renderer_name));
        ComPtr<IBaseFilter> source; checked(graph->AddSourceFilter(fixture.path.c_str(),L"WAV",source.GetAddressOf()));
        auto output=pin(source.Get(),PINDIR_OUTPUT),input=pin(renderer.Get(),PINDIR_INPUT); checked(graph->Connect(output.Get(),input.Get()));
        ComPtr<IMediaControl> control; checked(graph.As(&control)); StopGraph stop{control.Get()};
        ComPtr<IMediaSeeking> seeking; checked(graph.As(&seeking));
        auto wait_delivery=[&] {
            const auto deadline=GetTickCount64()+2000;
            LiveStatus live;
            do {
                checked(status->GetLiveStatus(&live)); checked(live.engine.error);
                if (live.engine.delivered_frames>=live.engine.buffer_frames && live.engine.delivered_frames) return;
                Sleep(1);
            } while (GetTickCount64()<deadline);
            throw HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        };
        using Clock=std::chrono::steady_clock;
        auto elapsed=[](auto start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); };
        for (const auto algorithm:{SrcAlgorithm::r8brain,SrcAlgorithm::sinc}) {
            checked(control->Stop());
            const ResamplingOptions src{algorithm}; checked(resampling->SetResamplingOptions(&src));
            LONGLONG position{}; checked(seeking->SetPositions(&position,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning));
            checked(seeking->SetRate(1.0)); checked(control->Run()); wait_delivery(); Sleep(100);
            for (const auto rate:{0.8,1.2,1.0,0.8,1.2,1.0}) {
                const auto start=Clock::now(); checked(seeking->SetRate(rate)); const auto api_ms=elapsed(start);
                wait_delivery(); const auto delivery_ms=elapsed(start);
                LiveStatus live; checked(status->GetLiveStatus(&live));
                double actual{}; checked(seeking->GetRate(&actual)); require(actual==rate && live.engine.playing);
                std::cout << "RATE src=" << (algorithm==SrcAlgorithm::sinc?"sinc":"r8brain") << " rate=" << rate << " api_ms=" << api_ms << " delivery_ms=" << delivery_ms << " output_hz=" << live.engine.sample_rate << " buffer=" << live.engine.buffer_frames << " underruns=" << live.engine.underruns << " overloads=" << live.engine.overloads << std::endl;
                Sleep(120);
            }
        }
        if (apply) {
            const ResamplingOptions src{SrcAlgorithm::r8brain}; const ProcessingOptions pcm16{16};
            checked(resampling->SetResamplingOptions(&src)); checked(processing->SetProcessingOptions(&pcm16));
            LONGLONG before{},after{}; checked(seeking->GetCurrentPosition(&before));
            const auto start=Clock::now(); checked(apply->ApplyPendingSettings()); wait_delivery();
            checked(seeking->GetCurrentPosition(&after));
            LiveStatus live; checked(status->GetLiveStatus(&live)); ProcessingStatus precision; checked(processing->GetProcessingStatus(&precision));
            ResamplingStatus active; checked(resampling->GetResamplingStatus(&active));
            require(live.engine.playing && !live.settings_pending && precision.pcm_bits==16 && active.algorithm==SrcAlgorithm::r8brain && std::abs(after-before)<10000000);
            std::cout << "APPLY running_ms=" << elapsed(start) << " position_delta_ms=" << (after-before)/10000.0 << " src=r8brain pcm_bits=16 pending=0" << std::endl;
            checked(control->Pause()); OAFilterState state{}; checked(control->GetState(2000,&state));
            const ResamplingOptions sinc{SrcAlgorithm::sinc}; checked(resampling->SetResamplingOptions(&sinc));
            checked(seeking->GetCurrentPosition(&before)); checked(apply->ApplyPendingSettings()); checked(seeking->GetCurrentPosition(&after));
            checked(status->GetLiveStatus(&live)); checked(resampling->GetResamplingStatus(&active));
            require(live.playback==State_Paused && !live.engine.playing && !live.settings_pending && active.algorithm==SrcAlgorithm::sinc && std::abs(after-before)<100000);
            std::cout << "APPLY paused=1 position_preserved=1 src=sinc pending=0" << std::endl;
            checked(control->Run()); wait_delivery();
        } else std::cout << "APPLY unavailable in baseline renderer" << std::endl;
        checked(control->Stop());
        const auto deadline=GetTickCount64()+2000;
        LiveStatus live;
        do { checked(status->GetLiveStatus(&live)); if (!live.engine.opened) break; Sleep(5); } while (GetTickCount64()<deadline);
        require(!live.engine.opened);
        std::cout << "Physical ASIO rate/reconfigure contract passed; device released after Stop." << std::endl;
    } catch (HRESULT hr) { std::cerr << "Live reconfigure test failed: 0x" << std::hex << unsigned(hr) << std::endl; result=1; }
      catch (const std::exception& error) { std::cerr << error.what() << std::endl; result=1; }
    CoUninitialize(); return result;
}
