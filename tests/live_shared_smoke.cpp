// Opt-in hardware smoke test. No sound content, files, settings changes, driver
// panels, or registration. Both clients submit only silence. Not part of CTest.
#include "win/engine.hpp"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <future>
#include <iostream>

using namespace are;
using namespace are::win;
void checked(HRESULT hr) { if (FAILED(hr)) throw hr; }
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::wcerr << L"Usage: live_shared_smoke <ASIO driver CLSID>\n"; return 2; }
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    int result = 0;
    try {
        CLSID driver{}; checked(CLSIDFromString(argv[1], &driver));
        ComPtr<IMMDeviceEnumerator> enumerator;
        checked(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(enumerator.GetAddressOf())));
        ComPtr<IMMDevice> device; checked(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,device.GetAddressOf()));
        ComPtr<IAudioClient3> client;
        checked(device->Activate(__uuidof(IAudioClient3),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(client.GetAddressOf())));
        WAVEFORMATEX* raw_format = nullptr; checked(client->GetMixFormat(&raw_format));
        std::unique_ptr<WAVEFORMATEX,decltype(&CoTaskMemFree)> mix(raw_format,CoTaskMemFree);
        UINT32 preferred = 0, fundamental = 0, minimum = 0, maximum = 0;
        checked(client->GetSharedModeEnginePeriod(mix.get(),&preferred,&fundamental,&minimum,&maximum));
        GUID session{}; checked(CoCreateGuid(&session));
        checked(client->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,preferred,mix.get(),&session));
        Handle event(CreateEventW(nullptr,FALSE,FALSE,nullptr)); if (!event) throw E_FAIL;
        checked(client->SetEventHandle(event.get()));
        UINT32 buffer_frames = 0; checked(client->GetBufferSize(&buffer_frames));
        ComPtr<IAudioRenderClient> render; checked(client->GetService(IID_PPV_ARGS(render.GetAddressOf())));
        BYTE* memory = nullptr; checked(render->GetBuffer(buffer_frames,&memory)); checked(render->ReleaseBuffer(buffer_frames,AUDCLNT_BUFFERFLAGS_SILENT));
        checked(client->Start());
        struct StopClient { IAudioClient3* client; ~StopClient() { client->Stop(); } } stop_client{client.Get()};
        auto timeline = std::make_shared<ClockTimeline>(); Engine engine(timeline);
        Settings settings{driver,0,0,FALSE}; SourceFormat source{SampleKind::integer,mix->nSamplesPerSec,2,16,16,0};
        const auto opened = engine.open(source,settings);
        if (FAILED(opened)) { std::wcerr << engine.status().detail << L'\n'; throw opened; }
        const auto preload_frames = std::min<std::size_t>(source.rate/10,4096);
        std::vector<std::byte> preload(preload_frames*4); checked(engine.submit(preload,0));
        checked(engine.start(timeline->now(),timeline->now()));
        const auto total = std::size_t(source.rate)*2;
        std::vector<std::byte> rest((total - preload_frames)*4);
        auto producer = std::async(std::launch::async,[&] { const auto hr = engine.submit(rest,0); if (hr == S_OK) engine.end_of_stream(); return hr; });
        const auto deadline = GetTickCount64()+7000;
        std::uint64_t windows_frames = buffer_frames;
        HRESULT error = S_OK;
        while (!engine.drained() && SUCCEEDED(engine.error()) && GetTickCount64()<deadline) {
            WaitForSingleObject(event.get(),20);
            UINT32 padding = 0; error = client->GetCurrentPadding(&padding); if (FAILED(error)) break;
            if (padding > buffer_frames) { error = E_FAIL; break; }
            const auto available = buffer_frames-padding;
            if (available) {
                error = render->GetBuffer(available,&memory); if (FAILED(error)) break;
                error = render->ReleaseBuffer(available,AUDCLNT_BUFFERFLAGS_SILENT); if (FAILED(error)) break;
                windows_frames += available;
            }
        }
        engine.abort(); const auto submitted = producer.get(); checked(error); checked(engine.error());
        if (submitted != S_OK || !engine.drained()) throw E_FAIL;
        const auto status = engine.status(); if (status.delivered_frames != total || windows_frames < source.rate) throw E_FAIL;
        std::wcout << L"Simultaneous ASIO + Windows shared audio passed (silence only).\n"
                   << L"ASIO: " << status.delivered_frames << L" frames, " << status.underruns << L" underruns.\n"
                   << L"Windows: " << windows_frames << L" frames at " << source.rate << L" Hz.\n";
    } catch (HRESULT hr) { std::wcerr << L"Coexistence test failed: 0x" << std::hex << unsigned(hr) << L'\n'; result=1; }
      catch (const std::exception& e) { std::wcerr << widen(e.what()) << L'\n'; result=1; }
    CoUninitialize(); return result;
}
