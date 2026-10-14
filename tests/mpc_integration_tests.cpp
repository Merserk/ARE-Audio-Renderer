#include "win/interfaces.hpp"
#include "win/mpc_ui.hpp"
#include "win/dialog_theme.hpp"
#include "preferences_lock.hpp"
#include "../resources/resource.h"
#include <commctrl.h>
#include <filesystem>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <fstream>

using namespace are::win;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
int forwarded{}, modal_count{};
std::wstring modal_playback;
bool test_dark{}, modal_dark{};
HBRUSH test_dark_brush{};
COLORREF modal_background{};
void save_dialog(HWND window,const wchar_t* filename) {
    RECT rect{}; GetWindowRect(window,&rect);
    const auto width=rect.right-rect.left, height=rect.bottom-rect.top;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    const auto source=GetWindowDC(window), dc=CreateCompatibleDC(source);
    void* pixels{}; const auto bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (bitmap) {
        const auto previous=SelectObject(dc,bitmap);
        if (PrintWindow(window,dc,0)) {
            BITMAPFILEHEADER header{}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
            const auto bytes=DWORD(width*height*4); header.bfSize=header.bfOffBits+bytes;
            std::ofstream output(std::filesystem::path(filename),std::ios::binary);
            output.write(reinterpret_cast<const char*>(&header),sizeof(header));
            output.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));
            output.write(static_cast<const char*>(pixels),bytes);
        }
        SelectObject(dc,previous); DeleteObject(bitmap);
    }
    DeleteDC(dc); ReleaseDC(window,source);
}
BOOL CALLBACK read_page(HWND window, LPARAM) {
    if (auto label = GetDlgItem(window, IDC_PLAYBACK)) {
        wchar_t value[128]{}; GetWindowTextW(label, value, 128); modal_playback = value;
    }
    return TRUE;
}
BOOL CALLBACK close_modal(HWND window, LPARAM owner) {
    if (IsWindowVisible(window) && GetWindow(window, GW_OWNER) == reinterpret_cast<HWND>(owner) && mpc::class_is(window, L"#32770") && !GetPropW(window, L"ARE.Test.Closing")) {
        SetPropW(window, L"ARE.Test.Closing", reinterpret_cast<HANDLE>(1));
        EnumChildWindows(window, read_page, 0);
        ++modal_count;
        modal_dark=GetPropW(window,dark_property)!=nullptr;
        if (modal_count==4) save_dialog(window,L"mpc-settings-dark.bmp");
        if (modal_count==5) save_dialog(window,L"mpc-settings-light.bmp");
        const auto dc=GetDC(window);
        LOGBRUSH brush{};
        const auto background=reinterpret_cast<HBRUSH>(SendMessageW(window,WM_CTLCOLORDLG,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(window)));
        if (background && GetObjectW(background,sizeof(brush),&brush)) modal_background=brush.lbColor;
        ReleaseDC(window,dc);
        PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
    }
    return TRUE;
}
LRESULT CALLBACK fixture_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message==WM_CTLCOLORDLG) return reinterpret_cast<LRESULT>(test_dark ? test_dark_brush : GetSysColorBrush(COLOR_3DFACE));
    if (message == WM_COMMAND) ++forwarded;
    if (message == WM_TIMER) EnumThreadWindows(GetCurrentThreadId(), close_modal, reinterpret_cast<LPARAM>(window));
    return DefWindowProcW(window, message, wp, lp);
}
void pump_until(const std::function<bool()>& check, const char* failure) {
    const auto deadline = GetTickCount64() + 7000;
    while (!check() && GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(10);
    }
    require(check(), failure);
}
struct Registration {
    HMODULE library{};
    std::wstring previous;
    using Register = HRESULT(WINAPI*)();
    Registration(const wchar_t* path) {
        const auto key = L"Software\\Classes\\CLSID\\" + guid_string(clsid_renderer) + L"\\InprocServer32";
        wchar_t value[32768]{}; DWORD size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS) previous = value;
        library = LoadLibraryW(path); require(library != nullptr, "cannot load integration DLL");
        const auto install = reinterpret_cast<Register>(GetProcAddress(library, "DllRegisterServer"));
        require(install && SUCCEEDED(install()), "test registration failed");
    }
    ~Registration() {
        if (!library) return;
        const auto uninstall = reinterpret_cast<Register>(GetProcAddress(library, "DllUnregisterServer"));
        if (uninstall) uninstall();
        if (!previous.empty()) {
            if (auto old = LoadLibraryW(previous.c_str())) {
                if (auto restore = reinterpret_cast<Register>(GetProcAddress(old, "DllRegisterServer"))) restore();
                FreeLibrary(old);
            }
        }
        FreeLibrary(library);
    }
};
struct Watcher {
    PROCESS_INFORMATION process{};
    Watcher(const wchar_t* path) {
        // Windows PowerShell's Start-Process can add trailing whitespace.
        // Quoted flags must also be accepted by the GUI entry point.
        std::wstring command = L"\"" + std::wstring(path) + L"\" \"--mpc-integration\" \t";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        require(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, "helper launch failed");
    }
    ~Watcher() {
        Handle stop(OpenEventW(EVENT_MODIFY_STATE, FALSE, mpc::stop_name)); if (stop) SetEvent(stop.get());
        if (process.hProcess) { WaitForSingleObject(process.hProcess, 7000); CloseHandle(process.hProcess); CloseHandle(process.hThread); }
    }
};
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    if (FAILED(OleInitialize(nullptr))) return 1;
    int result{};
    try {
        are::test::PreferencesLock preferences;
        Handle running(OpenMutexW(SYNCHRONIZE, FALSE, mpc::mutex_name));
        require(!running, "Stop the installed ARE MPC integration helper before running this UI fixture");
        Registration registration(argv[1]);
        const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
        test_dark_brush=CreateSolidBrush(RGB(32,32,32));
        WNDCLASSW klass{}; klass.lpfnWndProc = fixture_proc; klass.hInstance = GetModuleHandleW(nullptr); klass.lpszClassName = L"ARE.MPC.Test";
        require(RegisterClassW(&klass) != 0, "fixture class failed");
        const auto root = CreateWindowW(klass.lpszClassName, L"MPC integration regression", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 720, 400, nullptr, nullptr, klass.hInstance, nullptr);
        require(root != nullptr, "fixture root failed");
        const auto page = CreateWindowW(klass.lpszClassName, L"Output", WS_CHILD | WS_VISIBLE, 0, 0, 680, 300, root, nullptr, klass.hInstance, nullptr);
        const auto combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 30, 110, 500, 150, page, reinterpret_cast<HMENU>(7341), klass.hInstance, nullptr);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(renderer_name));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System Default"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        RECT input{}; GetWindowRect(combo, &input); MapWindowPoints(nullptr, page, reinterpret_cast<POINT*>(&input), 2);
        const auto button = CreateWindowW(L"BUTTON", L"Gear", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, input.right + 12, input.top, 28, input.bottom - input.top, page, reinterpret_cast<HMENU>(9854), klass.hInstance, nullptr);
        EnableWindow(button, FALSE);
        require(mpc::settings_button(combo) == button, "layout discovery failed");
        {
            Watcher watcher(argv[2]);
            pump_until([&] { return IsWindowEnabled(button) && GetPropW(page, mpc::binding_property); }, "disabled gear was not enabled");
            for (int i = 0; i < 10; ++i) EnableWindow(button, FALSE);
            require(IsWindowEnabled(button) != FALSE, "MPC command updates disabled ARE gear again");
            SetTimer(root, 1, 100, nullptr);
            SendMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
            for (int i = 0; i < 10; ++i) EnableWindow(button, FALSE);
            require(modal_count == 0, "idle disable completed an unfinished mouse press");
            SendMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
            if (modal_count != 1 || modal_playback != L"No playback" || forwarded != 0)
                std::wcerr << L"modal=" << modal_count << L", playback=" << modal_playback << L", forwarded=" << forwarded << L'\n';
            require(modal_count == 1 && modal_playback == L"No playback" && forwarded == 0, "gear did not open ARE configuration before playback");
            using GetClass = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
            auto get_class = reinterpret_cast<GetClass>(GetProcAddress(registration.library, "DllGetClassObject"));
            ComPtr<IClassFactory> factory; require(SUCCEEDED(get_class(clsid_renderer, IID_PPV_ARGS(factory.GetAddressOf()))), "factory failed");
            ComPtr<IBaseFilter> filter; require(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(filter.GetAddressOf()))), "renderer failed");
            ComPtr<IFilterGraph> graph; require(SUCCEEDED(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(graph.GetAddressOf()))), "graph failed");
            require(SUCCEEDED(graph->AddFilter(filter.Get(), renderer_name)), "graph attach failed");
            SendMessageW(button, BM_CLICK, 0, 0);
            require(modal_count == 2 && modal_playback == L"No audio", "gear opened a separate instance instead of the active graph renderer");
            require(SUCCEEDED(graph->RemoveFilter(filter.Get())), "graph detach failed");
            filter.Reset(); graph.Reset();
            SendMessageW(button, WM_KEYUP, VK_SPACE, 0);
            require(modal_count == 3 && modal_playback == L"No playback", "gear retained a stale graph instance");
            require(!modal_dark,"light player opened dark settings");
            test_dark=true;
            SendMessageW(button,BM_CLICK,0,0);
            require(modal_count==4 && modal_dark && modal_background==DialogTheme::background,"dark player did not open a fully dark settings frame");
            test_dark=false;
            SendMessageW(button,BM_CLICK,0,0);
            require(modal_count==5 && !modal_dark,"theme changes were not followed on reopen");
            SendMessageW(combo, CB_SETCURSEL, 1, 0); EnableWindow(button, FALSE);
            require(!IsWindowEnabled(button), "integration changed another renderer's gear");
            SendMessageW(page, WM_COMMAND, MAKEWPARAM(9854, BN_CLICKED), reinterpret_cast<LPARAM>(button));
            require(forwarded == 1, "another renderer's click was intercepted");
            pump_until([&] { return !GetPropW(page, mpc::binding_property); }, "selection change did not detach integration");
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
            pump_until([&] { return GetPropW(page, mpc::binding_property) != nullptr; }, "reselecting ARE did not restore gear");
            Handle stop(OpenEventW(EVENT_MODIFY_STATE, FALSE, mpc::stop_name)); require(stop && SetEvent(stop.get()), "stop event unavailable");
            pump_until([&] { return WaitForSingleObject(watcher.process.hProcess, 0) == WAIT_OBJECT_0; }, "helper shutdown failed");
            require(!GetPropW(page, mpc::binding_property) && !IsWindowEnabled(button), "shutdown did not restore original gear state");
        }
        // Future/ambiguous layouts must be left alone instead of choosing a wrong button.
        const auto duplicate = CreateWindowW(L"BUTTON", L"Other", WS_CHILD | WS_VISIBLE, input.right + 45, input.top, 28, input.bottom - input.top, page, nullptr, klass.hInstance, nullptr);
        require(!mpc::settings_button(combo), "ambiguous layout was accepted");
        DestroyWindow(duplicate); DestroyWindow(root);
        DeleteObject(test_dark_brush);
        std::cout << "MPC settings integration: discovery, repeated disable, modal click, active graph, detach, reselect, shutdown and ambiguous layout passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    OleUninitialize(); return result;
}
