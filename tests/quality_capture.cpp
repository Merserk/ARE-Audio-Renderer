// MIT License. Copyright (c) 2026 Merserk.
// Opt-in audible measurement: DirectShow renderer -> default endpoint loopback.
// No default device, mixer volume, or audio enhancement settings are changed.
#include "win/interfaces.hpp"
#include "win/settings.hpp"
#include "core/format.hpp"
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace are;
using namespace are::win;
void checked(HRESULT hr) { if (FAILED(hr)) throw hr; }
struct StopGraph {
    IMediaControl* control;
    ~StopGraph() { control->Stop(); }
};
// Preserve exact registry values/types/absence across the optional ARE run.
struct Preferences {
    struct Value { std::wstring name; DWORD type; std::vector<BYTE> data; };
    std::vector<Value> values;
    bool existed{};
    Preferences() {
        RegKey key;
        const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, settings_registry_path, 0, KEY_READ, key.put());
        if (opened == ERROR_FILE_NOT_FOUND) return;
        checked(HRESULT_FROM_WIN32(opened)); existed = true;
        DWORD count{}, names{}, bytes{};
        checked(HRESULT_FROM_WIN32(RegQueryInfoKeyW(key.get(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            &count, &names, &bytes, nullptr, nullptr)));
        for (DWORD i = 0; i < count; ++i) {
            std::wstring name(names + 1, L'\0'); std::vector<BYTE> data(bytes);
            DWORD length = DWORD(name.size()), size = DWORD(data.size()), type{};
            checked(HRESULT_FROM_WIN32(RegEnumValueW(key.get(), i, name.data(), &length, nullptr, &type, data.data(), &size)));
            name.resize(length); data.resize(size); values.push_back({std::move(name), type, std::move(data)});
        }
    }
    ~Preferences() {
        if (!existed) { RegDeleteTreeW(HKEY_CURRENT_USER, settings_registry_path); return; }
        RegKey key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, settings_registry_path, 0, nullptr, 0, KEY_READ | KEY_WRITE,
            nullptr, key.put(), nullptr) != ERROR_SUCCESS) return;
        DWORD i = 0;
        for (;;) {
            wchar_t name[16384]{}; DWORD size = DWORD(std::size(name));
            if (RegEnumValueW(key.get(), i, name, &size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            const auto saved = std::any_of(values.begin(), values.end(), [&](const Value& v) { return v.name == name; });
            if (saved) ++i; else if (RegDeleteValueW(key.get(), name) != ERROR_SUCCESS) break;
        }
        for (const auto& v : values) RegSetValueExW(key.get(), v.name.c_str(), 0, v.type, v.data.data(), DWORD(v.data.size()));
    }
};
int active_sessions() {
    checked(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    try {
        ComPtr<IMMDeviceEnumerator> enumerator;
        checked(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(enumerator.GetAddressOf())));
        ComPtr<IMMDevice> device;
        checked(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf()));
        ComPtr<IAudioSessionManager2> manager;
        checked(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(manager.GetAddressOf())));
        ComPtr<IAudioSessionEnumerator> sessions; checked(manager->GetSessionEnumerator(sessions.GetAddressOf()));
        int count{}; checked(sessions->GetCount(&count));
        for (int i=0; i<count; ++i) {
            ComPtr<IAudioSessionControl> session; checked(sessions->GetSession(i,session.GetAddressOf()));
            AudioSessionState state{}; checked(session->GetState(&state));
            if (state!=AudioSessionStateActive) continue;
            ComPtr<IAudioSessionControl2> detail; checked(session.As(&detail));
            DWORD process{}; checked(detail->GetProcessId(&process));
            std::cout << "Active audio process: " << process << '\n';
        }
    } catch (...) { CoUninitialize(); throw; }
    CoUninitialize(); return 0;
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--sessions") {
        try { return active_sessions(); } catch (HRESULT hr) { std::cerr << "Session query failed: " << std::hex << unsigned(hr) << '\n'; return 1; }
    }
    if (argc < 5 || argc > 8) {
        std::wcerr << L"Usage: quality_capture system|are input.wav output-prefix renderer.dll [ASIO-CLSID [r8brain|sinc [volume-db100]]]\n";
        return 2;
    }
    const auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init)) return 1;
    int result = 0;
    HMODULE library = nullptr;
    try {
        const bool is_are = std::wstring_view(argv[1]) == L"are";
        // Construct before loading a renderer, which reads these preferences.
        std::unique_ptr<Preferences> preferences = is_are ? std::make_unique<Preferences>() : nullptr;
        ComPtr<IMMDeviceEnumerator> enumerator;
        checked(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(enumerator.GetAddressOf())));
        ComPtr<IMMDevice> device;
        checked(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf()));
        ComPtr<IAudioClient> client;
        checked(device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(client.GetAddressOf())));
        WAVEFORMATEX* raw = nullptr;
        checked(client->GetMixFormat(&raw));
        std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> mix(raw, CoTaskMemFree);
        bool floating = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        UINT bits = mix->wBitsPerSample;
        if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix.get());
            floating = ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            bits = ext->Samples.wValidBitsPerSample;
        }
        const SourceFormat capture_format{floating ? SampleKind::float32 : SampleKind::integer,
            mix->nSamplesPerSec, mix->nChannels, mix->wBitsPerSample, WORD(bits), 0};
        if (!capture_format.valid() || (floating && mix->wBitsPerSample != 32)) throw E_INVALIDARG;
        checked(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, mix.get(), nullptr));
        ComPtr<IAudioCaptureClient> capture;
        checked(client->GetService(IID_PPV_ARGS(capture.GetAddressOf())));
        ComPtr<IGraphBuilder> graph;
        checked(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(graph.GetAddressOf())));
        ComPtr<IBaseFilter> renderer;
        ComPtr<IASIORenderSettings> settings;
        if (is_are) {
            if (argc < 6) throw E_INVALIDARG;
            library = LoadLibraryW(argv[4]);
            if (!library) throw HRESULT_FROM_WIN32(GetLastError());
            using GetClass = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
            const auto create = reinterpret_cast<GetClass>(GetProcAddress(library, "DllGetClassObject"));
            if (!create) throw E_NOINTERFACE;
            ComPtr<IClassFactory> factory;
            checked(create(clsid_renderer, IID_PPV_ARGS(factory.GetAddressOf())));
            checked(factory->CreateInstance(nullptr, IID_PPV_ARGS(renderer.GetAddressOf())));
            checked(renderer.As(&settings));
            CLSID driver{}; checked(CLSIDFromString(argv[5], &driver));
            const Settings selected{driver, 0, 0, TRUE};
            checked(settings->SetSettings(&selected));
            ComPtr<IASIORenderPlayback> playback; checked(renderer.As(&playback));
            const PlaybackOptions options{FALSE}; checked(playback->SetPlaybackOptions(&options));
            ComPtr<IASIORenderProcessing> processing; checked(renderer.As(&processing));
            const ProcessingOptions precision{0}; checked(processing->SetProcessingOptions(&precision));
            ComPtr<IASIORenderResampling> resampling; checked(renderer.As(&resampling));
            const auto algorithm = argc >= 7 ? std::wstring_view(argv[6]) : L"r8brain";
            if (algorithm != L"r8brain" && algorithm != L"sinc") throw E_INVALIDARG;
            const ResamplingOptions src{algorithm == L"sinc" ? SrcAlgorithm::sinc : SrcAlgorithm::r8brain};
            checked(resampling->SetResamplingOptions(&src));
        } else {
            if (std::wstring_view(argv[1]) != L"system") throw E_INVALIDARG;
            checked(CoCreateInstance(CLSID_DSoundRender, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(renderer.GetAddressOf())));
        }
        checked(graph->AddFilter(renderer.Get(), is_are ? L"ARE Audio Renderer" : L"System Default (DirectSound)"));
        checked(graph->RenderFile(argv[2], nullptr));
        // Ensure graph building actually connected the requested renderer.
        ComPtr<IEnumPins> pins; checked(renderer->EnumPins(pins.GetAddressOf()));
        ComPtr<IPin> input; bool connected = false;
        while (pins->Next(1, input.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
            ComPtr<IPin> peer;
            if (input->ConnectedTo(peer.GetAddressOf()) == S_OK) connected = true;
        }
        if (!connected) throw VFW_E_NOT_CONNECTED;
        ComPtr<IMediaControl> control; checked(graph.As(&control));
        StopGraph stop{control.Get()};
        ComPtr<IBasicAudio> volume;
        const auto volume_db100 = argc >= 8 ? wcstol(argv[7], nullptr, 10) : 0;
        if (volume_db100 < -10000 || volume_db100 > 0) throw E_INVALIDARG;
        if (graph.As(&volume) == S_OK) checked(volume->put_Volume(volume_db100));
        ComPtr<IMediaEvent> events; checked(graph.As(&events));
        std::vector<float> samples;
        std::uint64_t discontinuities{}, timestamp_errors{}, silent_frames{};
        bool first_packet = true;
        const auto collect = [&] {
            UINT packet = 0; checked(capture->GetNextPacketSize(&packet));
            while (packet) {
                BYTE* bytes = nullptr; UINT frames = 0; DWORD flags = 0;
                checked(capture->GetBuffer(&bytes, &frames, &flags, nullptr, nullptr));
                if (!first_packet && (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)) ++discontinuities;
                if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ++timestamp_errors;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) silent_frames += frames;
                first_packet = false;
                for (std::size_t i = 0; i < std::size_t(frames) * mix->nChannels; ++i) {
                    samples.push_back((flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0f :
                        float(normalized_sample(capture_format, reinterpret_cast<std::byte*>(bytes) + i * capture_format.sample_bytes())));
                }
                checked(capture->ReleaseBuffer(frames)); checked(capture->GetNextPacketSize(&packet));
            }
        };
        checked(client->Start());
        const auto lead = GetTickCount64();
        while (GetTickCount64() - lead < 1000) { collect(); Sleep(2); }
        double preroll_peak{};
        for (const auto sample : samples) preroll_peak = std::max(preroll_peak, std::abs(double(sample)));
        const auto preroll_frames = samples.size() / mix->nChannels;
        checked(control->Run());
        const auto start = GetTickCount64();
        bool complete = false; ULONGLONG finish = 0;
        while (GetTickCount64() - start < 60000) {
            collect();
            long code{}; LONG_PTR a{}, b{};
            while (events->GetEvent(&code, &a, &b, 0) == S_OK) {
                events->FreeEventParams(code, a, b);
                if (code == EC_ERRORABORT) throw HRESULT(a);
                if (code == EC_COMPLETE) { complete = true; finish = GetTickCount64(); }
            }
            if (complete && GetTickCount64() - finish > 500) break;
            Sleep(2);
        }
        EngineStatus engine{};
        if (settings) checked(settings->GetStatus(&engine));
        ChannelStatus routing{};
        if (is_are) {
            ComPtr<IASIORenderChannels> channels;
            if (renderer.As(&channels) == S_OK) checked(channels->GetChannelStatus(&routing));
        }
        collect(); checked(control->Stop()); checked(client->Stop());
        if (!complete) throw HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        const std::filesystem::path base(argv[3]);
        std::ofstream output(base.wstring() + L".f32", std::ios::binary);
        output.write(reinterpret_cast<const char*>(samples.data()), std::streamsize(samples.size() * sizeof(float)));
        std::ofstream metadata(base.wstring() + L".json");
        metadata << "{\"rate\":" << mix->nSamplesPerSec << ",\"channels\":" << mix->nChannels
            << ",\"container_bits\":" << mix->wBitsPerSample << ",\"valid_bits\":" << bits
            << ",\"floating_point\":" << (floating ? "true" : "false")
            << ",\"volume_db100\":" << volume_db100
            << ",\"input_channels\":" << routing.input_channels << ",\"output_channels\":" << routing.output_channels
            << ",\"downmix\":" << (routing.downmix ? "true" : "false")
            << ",\"mono_duplicate\":" << (routing.mono_duplicate ? "true" : "false")
            << ",\"frames\":" << samples.size() / mix->nChannels
            << ",\"asio_rate\":" << (is_are ? std::to_string(engine.sample_rate) : "null")
            << ",\"delivered_frames\":" << (is_are ? std::to_string(engine.delivered_frames) : "null")
            << ",\"underruns\":" << (is_are ? std::to_string(engine.underruns) : "null")
            << ",\"overloads\":" << (is_are ? std::to_string(engine.overloads) : "null")
            << ",\"buffer_frames\":" << (is_are ? std::to_string(engine.buffer_frames) : "null")
            << ",\"output_latency_frames\":" << (is_are ? std::to_string(engine.output_latency_frames) : "null")
            << ",\"preroll_frames\":" << preroll_frames << ",\"preroll_peak\":" << preroll_peak
            << ",\"loopback_discontinuities\":" << discontinuities << ",\"timestamp_errors\":" << timestamp_errors
            << ",\"silent_frames\":" << silent_frames << "}\n";
        if (!output || !metadata) throw E_FAIL;
        std::wcout << argv[1] << L": " << samples.size() / mix->nChannels << L" captured frames at " << mix->nSamplesPerSec
            << L" Hz; ASIO rate " << engine.sample_rate << L"; underruns " << engine.underruns << L'\n';
    } catch (HRESULT error) {
        std::cerr << "Capture failed: HRESULT 0x" << std::hex << unsigned(error) << '\n'; result = 1;
    }
    if (library) FreeLibrary(library);
    CoUninitialize(); return result;
}
