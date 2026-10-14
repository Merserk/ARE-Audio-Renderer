#include "win/interfaces.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include "tools/mpc_watcher.hpp"
#include <string_view>
#include <filesystem>

using namespace are::win;
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int count{};
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 1;
    const std::wstring command = count == 2 ? arguments[1] : L"";
    LocalFree(arguments);
    if (count == 2 && command == L"--mpc-integration") return watch_mpc_settings();
    if (count == 2 && command == L"--stop-mpc-integration") return stop_mpc_settings();
    if (count != 1) return 2;
    const INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    const auto com = OleInitialize(nullptr);
    if (FAILED(com)) return 1;
    HRESULT result = S_OK;
    {
        ComPtr<IBaseFilter> filter;
        result = CoCreateInstance(clsid_renderer,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(filter.GetAddressOf()));
        if (SUCCEEDED(result)) {
            wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,DWORD(std::size(path)));
            const auto dll=std::filesystem::path(path).parent_path()/L"ARE-Audio-Renderer.dll";
            const auto module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
            using Show=HRESULT(WINAPI*)(HWND,IUnknown*);
            const auto show=module ? reinterpret_cast<Show>(GetProcAddress(module,"AREShowSettings")) : nullptr;
            result=show ? show(nullptr,filter.Get()) : E_NOINTERFACE;
            if (module) FreeLibrary(module);
        }
        if (FAILED(result)) MessageBoxW(nullptr,L"Could not open settings. Install ARE Audio Renderer for this player architecture first.",renderer_name,MB_OK);
    }
    OleUninitialize(); return FAILED(result) ? 1 : 0;
}
