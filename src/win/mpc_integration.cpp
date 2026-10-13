#include "win/mpc_integration.hpp"
#include "win/mpc_ui.hpp"
#include "win/filter.hpp"
#include <commctrl.h>
#include <mutex>
#include <vector>

namespace are::win {
namespace {
std::mutex renderers_mutex;
std::vector<Renderer*> renderers;
struct Binding {
    HWND page, combo, button;
    bool opening{};
    bool originally_enabled{};
    bool removed{};
    bool pressed{};
};
constexpr UINT_PTR subclass_id = 0x415245;
LRESULT CALLBACK page_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK button_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* binding = reinterpret_cast<Binding*>(data);
    if (message == WM_ENABLE && !wp && mpc::selected(binding->combo)) {
        EnableWindow(window, TRUE);
        if (binding->pressed) SetCapture(window);
        return 0;
    }
    if (mpc::selected(binding->combo)) {
        // MPC-HC's idle command updater disables external renderer buttons
        // between mouse-down and mouse-up, cancelling the native button click.
        // Keep the press alive and route the completed click to our page handler.
        if (message == WM_CANCELMODE) return 0;
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
            binding->pressed = true; SetFocus(window); SetCapture(window);
            SendMessageW(window, BM_SETSTATE, TRUE, 0); return 0;
        }
        if (message == WM_LBUTTONUP) {
            const bool pressed = std::exchange(binding->pressed, false);
            if (GetCapture() == window) ReleaseCapture();
            SendMessageW(window, BM_SETSTATE, FALSE, 0);
            RECT client{}; GetClientRect(window, &client);
            const POINT point{short(LOWORD(lp)), short(HIWORD(lp))};
            if (pressed && PtInRect(&client, point))
                SendMessageW(binding->page, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(window), BN_CLICKED), reinterpret_cast<LPARAM>(window));
            return 0;
        }
        if (message == BM_CLICK || (message == WM_KEYUP && (wp == VK_SPACE || wp == VK_RETURN))) {
            SendMessageW(binding->page, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(window), BN_CLICKED), reinterpret_cast<LPARAM>(window));
            return 0;
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, button_proc, subclass_id);
    return DefSubclassProc(window, message, wp, lp);
}
void detach(Binding* binding) noexcept {
    RemovePropW(binding->page, mpc::binding_property);
    RemoveWindowSubclass(binding->page, page_proc, subclass_id);
    RemoveWindowSubclass(binding->button, button_proc, subclass_id);
    if (IsWindow(binding->button) && mpc::selected(binding->combo))
        EnableWindow(binding->button, binding->originally_enabled);
    binding->removed = true;
    if (!binding->opening) delete binding;
}
LRESULT CALLBACK page_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* binding = reinterpret_cast<Binding*>(data);
    if (message == WM_NCDESTROY) {
        detach(binding);
        return DefSubclassProc(window, message, wp, lp);
    }
    if (message == RegisterWindowMessageW(mpc::attach_message) && lp == 0) {
        // A modal property frame still needs the binding until it returns.
        detach(binding);
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wp) == BN_CLICKED &&
        reinterpret_cast<HWND>(lp) == binding->button && mpc::selected(binding->combo)) {
        if (binding->opening || binding->pressed) return 0;
        binding->opening = true;
        const auto com = OleInitialize(nullptr);
        HRESULT result = com;
        if (SUCCEEDED(com)) {
            try {
                auto filter = settings_renderer();
                if (!filter) filter.Attach(new Renderer);
                IUnknown* object = filter.Get(); auto page = clsid_settings_page;
                result = OleCreatePropertyFrame(GetAncestor(window, GA_ROOT), 0, 0,
                    renderer_name, 1, &object, 1, &page, 0, 0, nullptr);
            } catch (...) { result = E_OUTOFMEMORY; }
            OleUninitialize();
        }
        binding->opening = false;
        if (binding->removed) { delete binding; return 0; }
        if (FAILED(result)) MessageBoxW(GetAncestor(window, GA_ROOT),
            L"Could not open ARE settings. Restart MPC-HC after installing the renderer.",
            renderer_name, MB_OK | MB_ICONERROR);
        return 0;
    }
    return DefSubclassProc(window, message, wp, lp);
}
void attach(HWND page, HWND combo, HWND button) noexcept {
    if (GetPropW(page, mpc::binding_property)) return;
    DWORD process{};
    if (GetWindowThreadProcessId(page, &process) != GetCurrentThreadId() || process != GetCurrentProcessId() ||
        GetParent(combo) != page || GetParent(button) != page || mpc::settings_button(combo) != button) return;
    auto* binding = new(std::nothrow) Binding{page, combo, button, false, IsWindowEnabled(button) != FALSE};
    if (!binding) return;
    if (!SetWindowSubclass(page, page_proc, subclass_id, reinterpret_cast<DWORD_PTR>(binding))) { delete binding; return; }
    if (!SetWindowSubclass(button, button_proc, subclass_id, reinterpret_cast<DWORD_PTR>(binding))) {
        RemoveWindowSubclass(page, page_proc, subclass_id); delete binding; return;
    }
    if (!SetPropW(page, mpc::binding_property, binding)) { detach(binding); return; }
    // Windows can unload a hook DLL as soon as its installer removes the hook.
    // Subclass callbacks must remain valid until the player exits.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&attach), &pinned)) { detach(binding); return; }
    EnableWindow(button, TRUE);
}
}
void update_settings_renderer(Renderer* renderer, bool in_graph) {
    std::lock_guard lock(renderers_mutex);
    if (in_graph) {
        if (std::find(renderers.begin(), renderers.end(), renderer) == renderers.end()) renderers.push_back(renderer);
    } else std::erase(renderers, renderer);
}
ULONG release_settings_renderer(Renderer* renderer, std::atomic<ULONG>& refs) noexcept {
    std::lock_guard lock(renderers_mutex);
    const auto count = --refs;
    if (!count) std::erase(renderers, renderer);
    return count;
}
ComPtr<IBaseFilter> settings_renderer() {
    std::lock_guard lock(renderers_mutex);
    return renderers.empty() ? ComPtr<IBaseFilter>{} : ComPtr<IBaseFilter>{renderers.back()};
}
}
extern "C" LRESULT CALLBACK AREMpcSettingsHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && lp) {
        const auto& message = *reinterpret_cast<const CWPSTRUCT*>(lp);
        if (message.message == RegisterWindowMessageW(are::win::mpc::attach_message) && message.lParam) {
            wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
            const auto* name = wcsrchr(path, L'\\'); name = name ? name + 1 : path;
            if (!_wcsicmp(name, L"mpc-hc64.exe") || !_wcsicmp(name, L"mpc-hc.exe"))
                are::win::attach(message.hwnd, reinterpret_cast<HWND>(message.wParam), reinterpret_cast<HWND>(message.lParam));
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
