#include "win/devices.hpp"
#include "win/engine.hpp"
#include "win/settings.hpp"
#include <filesystem>
#include <format>
#include <iostream>
#include <string_view>

using namespace are;
using namespace are::win;
namespace {
struct ComSession { HRESULT result{OleInitialize(nullptr)}; ~ComSession() { if (SUCCEEDED(result)) OleUninitialize(); } };
struct Library { HMODULE module{}; explicit Library(const wchar_t* path) : module(LoadLibraryW(path)) {} ~Library() { if (module) FreeLibrary(module); } };
struct TestDeadline {
    Handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread thread;
    TestDeadline(Engine& engine, DWORD milliseconds) {
        if (!done) throw std::runtime_error("Cannot create test deadline");
        thread = std::thread([this, &engine, milliseconds] { if (WaitForSingleObject(done.get(), milliseconds) == WAIT_TIMEOUT) engine.abort(); });
    }
    ~TestDeadline() { SetEvent(done.get()); if (thread.joinable()) thread.join(); }
};
int report(HRESULT result, std::wstring_view detail = {}) {
    if (FAILED(result)) { std::wcerr << std::format(L"Error 0x{:08X}: {}\n", unsigned(result), detail); return 1; }
    return 0;
}
CLSID parse_guid(const wchar_t* value) {
    CLSID guid{}; if (FAILED(CLSIDFromString(value, &guid))) throw std::runtime_error("Invalid driver GUID"); return guid;
}
int verify_installed() {
    ComPtr<ICreateDevEnum> devices;
    auto hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(devices.GetAddressOf()));
    if (FAILED(hr)) return report(hr, L"Cannot enumerate DirectShow renderers");
    ComPtr<IEnumMoniker> list;
    hr = devices->CreateClassEnumerator(CLSID_AudioRendererCategory, list.GetAddressOf(), 0);
    if (hr != S_OK) return report(VFW_E_NOT_FOUND, L"Audio renderer category is empty");
    ComPtr<IMoniker> moniker;
    while (list->Next(1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
        ComPtr<IPropertyBag> properties;
        if (FAILED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(properties.GetAddressOf())))) continue;
        VARIANT name{}; VariantInit(&name);
        const auto read = properties->Read(L"FriendlyName", &name, nullptr);
        const bool ours = SUCCEEDED(read) && name.vt == VT_BSTR && wcscmp(name.bstrVal, renderer_name) == 0;
        VariantClear(&name);
        if (!ours) continue;
        ComPtr<IBaseFilter> filter;
        hr = moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(filter.GetAddressOf()));
        if (FAILED(hr)) return report(hr, L"The renderer is listed but cannot be loaded");
        CLSID clsid{}; filter->GetClassID(&clsid);
        if (clsid != clsid_renderer) return report(E_FAIL, L"Unexpected renderer CLSID");
        LPOLESTR display = nullptr; moniker->GetDisplayName(nullptr, nullptr, &display);
        std::wcout << renderer_name << L" is installed and loadable.\n" << (display ? display : L"") << L"\n";
        CoTaskMemFree(display); return 0;
    }
    return report(VFW_E_NOT_FOUND, L"ARE Audio Renderer is not installed for this architecture");
}
int show_settings() {
    ComPtr<IBaseFilter> filter;
    auto hr = CoCreateInstance(clsid_renderer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(filter.GetAddressOf()));
    if (FAILED(hr)) return report(hr, L"Install ARE Audio Renderer before opening settings");
    IUnknown* object = filter.Get();
    auto page_clsid = clsid_settings_page;
    hr = OleCreatePropertyFrame(nullptr, 0, 0, renderer_name, 1, &object, 1, &page_clsid, 0, 0, nullptr);
    return report(hr, L"Could not open settings");
}
void print_status(const EngineStatus& s) {
    std::wcout << std::format(L"{}\n{} Hz, {} channels, {} source bits\n{}\nBuffer: {} frames, output latency: {} frames\nDelivered: {}, underruns: {}, overloads: {}\n{}\n",
        s.driver_name, s.sample_rate, s.channels, s.source_bits, s.output_format, s.buffer_frames,
        s.output_latency_frames, s.delivered_frames, s.underruns, s.overloads, s.detail);
}
void help() {
    std::wcout << LR"(ARE Audio Renderer 0.3.0
  ARE-Audio-Renderer-Control.exe --devices
  ARE-Audio-Renderer-Control.exe --settings
  ARE-Audio-Renderer-Control.exe --install <absolute DLL path>
  ARE-Audio-Renderer-Control.exe --uninstall <absolute DLL path>
  ARE-Audio-Renderer-Control.exe --verify-installed
  ARE-Audio-Renderer-Control.exe --select <driver CLSID> [--first-channel 1] [--buffer 0] [--keep-rate on|off] [--output-bits 0|16|24|32] [--src r8brain|sinc]
  ARE-Audio-Renderer-Control.exe --probe <driver CLSID> [--rate 48000] [--bits 16] [--channels 2] [--buffer 0]
  ARE-Audio-Renderer-Control.exe --silence-test <driver CLSID> [--seconds 2] [--rate 48000]

Driver default buffer is 0. Selection changes apply to the next opened file.
Probe and silence-test also accept --keep-rate, --output-bits and --src.
The silence test sends only zeros; it does not establish DAC bit transparency
or test simultaneous browser playback. The probe opens the driver without playback.
)";
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        ComSession com;
        if (FAILED(com.result)) return report(com.result, L"Cannot initialize COM");
        if (argc < 2) { help(); return 0; }
        const std::wstring_view command(argv[1]);
        if (command == L"--help") { help(); return 0; }
        if (command == L"--devices") {
            for (const auto& d : enumerate_devices()) std::wcout << d.name << L"\n  " << guid_string(d.clsid) << L"\n  " << d.dll_path << L"\n";
            return 0;
        }
        if (command == L"--verify-installed") return verify_installed();
        if (command == L"--settings") return show_settings();
        if ((command == L"--install" || command == L"--uninstall") && argc == 3) {
            const auto path = std::filesystem::absolute(argv[2]);
            Library library(path.c_str());
            if (!library.module) return report(HRESULT_FROM_WIN32(GetLastError()), L"Could not load renderer DLL");
            const auto function = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(library.module, command == L"--install" ? "DllRegisterServer" : "DllUnregisterServer"));
            if (!function) return report(E_FAIL, L"Missing registration export");
            const auto hr = function(); if (FAILED(hr)) return report(hr, L"Per-user registration failed");
            std::wcout << (command == L"--install" ? L"Installed for the current user. No administrator rights required.\n" : L"Unregistered for the current user.\n"); return 0;
        }
        if (command != L"--select" && command != L"--probe" && command != L"--silence-test") { help(); return 2; }
        if (argc < 3) return report(E_INVALIDARG, L"Supply a driver CLSID from --devices");
        Settings settings = load_settings(); settings.driver = parse_guid(argv[2]);
        ProcessingOptions processing=load_processing_options();
        ResamplingOptions resampling=load_resampling_options();
        SourceFormat format{SampleKind::integer, 48000, 2, 16, 16, 0};
        UINT seconds = 2;
        for (int i = 3; i < argc; i += 2) {
            if (i + 1 >= argc) return report(E_INVALIDARG, L"Missing option value");
            const std::wstring_view option(argv[i]);
            if (option == L"--src") {
                const std::wstring_view value(argv[i+1]);
                if (value!=L"r8brain" && value!=L"sinc") return report(E_INVALIDARG,L"Use --src r8brain or sinc");
                resampling.algorithm=value==L"sinc" ? SrcAlgorithm::sinc : SrcAlgorithm::r8brain; continue;
            }
            if (option == L"--keep-rate") {
                const std::wstring_view value(argv[i + 1]);
                if (value != L"on" && value != L"off") return report(E_INVALIDARG, L"Use --keep-rate on or off");
                settings.keep_device_rate = value == L"on"; continue;
            }
            std::size_t end = 0; const auto number = std::stoul(argv[i + 1], &end);
            if (end != wcslen(argv[i + 1]) || number > 1000000) return report(E_INVALIDARG, L"Invalid numeric option");
            const auto n = UINT(number);
            if (option == L"--first-channel") { if (n < 1 || n > 1024) return report(E_INVALIDARG, L"Channel must be 1..1024"); settings.first_channel = n - 1; }
            else if (option == L"--buffer") settings.buffer_frames = n;
            else if (option == L"--rate") format.rate = n;
            else if (option == L"--output-bits") {
                if (n!=0 && n!=16 && n!=24 && n!=32) return report(E_INVALIDARG,L"Output PCM bits must be 0 (Auto), 16, 24 or 32");
                processing.output_bits=n;
            }
            else if (option == L"--bits") { if (n != 8 && n != 16 && n != 24 && n != 32) return report(E_INVALIDARG, L"PCM bits must be 8, 16, 24 or 32"); format.valid_bits = format.container_bits = std::uint16_t(n); }
            else if (option == L"--channels") { if (n < 1 || n > 32) return report(E_INVALIDARG, L"Channels must be 1..32"); format.channels = std::uint16_t(n); }
            else if (option == L"--seconds") seconds = n;
            else return report(E_INVALIDARG, L"Unknown option");
        }
        if (command == L"--select") { auto hr = save_settings(settings); if (SUCCEEDED(hr)) hr=save_processing_options(processing); if (SUCCEEDED(hr)) hr=save_resampling_options(resampling); if (SUCCEEDED(hr)) std::wcout << L"Device selection saved. Reopen the file in MPC-HC.\n"; return report(hr); }
        if (!format.valid() || seconds < 1 || seconds > 30) return report(E_INVALIDARG, L"Unsupported PCM format or duration");
        auto timeline = std::make_shared<ClockTimeline>(); Engine engine(timeline);
        auto hr = engine.open(format, settings,processing,resampling);
        if (FAILED(hr)) { print_status(engine.status()); return report(hr); }
        if (command == L"--probe") { print_status(engine.status()); return 0; }
        const std::size_t preload_frames = std::min<std::size_t>(format.rate / 10, 4096);
        const auto zero = format.container_bits == 8 ? std::byte{128} : std::byte{};
        std::vector<std::byte> preload(preload_frames * format.frame_bytes(), zero);
        hr = engine.submit(preload, 0); if (hr != S_OK) return report(FAILED(hr) ? hr : E_ABORT, L"Submission interrupted");
        hr = engine.start(timeline->now(), timeline->now()); if (FAILED(hr)) return report(hr);
        TestDeadline watchdog(engine, (seconds + 5) * 1000);
        const auto total_frames = std::size_t(format.rate) * seconds;
        std::vector<std::byte> chunk(4096 * format.frame_bytes(), zero);
        auto remaining = total_frames - preload_frames;
        while (remaining) {
            const auto frames = std::min<std::size_t>(remaining,4096);
            hr = engine.submit({chunk.data(),frames * format.frame_bytes()},0);
            if (hr != S_OK) { print_status(engine.status()); return report(FAILED(hr) ? hr : E_ABORT, L"Submission interrupted or test timed out"); }
            remaining -= frames;
        }
        hr=engine.end_of_stream(); if (hr!=S_OK) return report(FAILED(hr)?hr:E_ABORT,L"Silent test EOS interrupted");
        const auto deadline = GetTickCount64() + 5000;
        while (!engine.drained() && SUCCEEDED(engine.error()) && GetTickCount64() < deadline) WaitForSingleObject(engine.progress_event(), 20);
        const auto status = engine.status(); engine.pause(); print_status(status);
        if (FAILED(status.error)) return report(status.error);
        const auto expected_frames=(std::uint64_t(total_frames)*status.sample_rate+format.rate/2)/format.rate;
        if (!engine.drained() || status.delivered_frames != expected_frames) return report(E_FAIL, L"The silence test did not drain all frames");
        std::wcout << L"Silent ASIO callback test passed.\n"; return 0;
    } catch (const std::exception& error) { std::wcerr << widen(error.what()) << L"\n"; return 1; }
}
