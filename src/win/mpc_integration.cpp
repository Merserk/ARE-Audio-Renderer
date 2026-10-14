#include "win/mpc_integration.hpp"
#include "win/mpc_ui.hpp"
#include "win/filter.hpp"
#include "win/settings_frame.hpp"
#include "win/dialog_theme.hpp"
#include "win/mpc_diagnostics.hpp"
#include <commctrl.h>
#include <mutex>
#include <vector>

namespace are::win {
void write_mpc_diagnostic(DWORD caller) noexcept;
namespace {
std::mutex renderers_mutex;
std::vector<Renderer*> renderers;
struct Binding {
    HWND page, combo, button;
    bool opening{};
    bool originally_enabled{};
    bool removed{};
    unsigned callbacks{};
    LONG_PTR original_id{}, managed_id{};
};
struct CallbackScope {
    Binding* binding;
    explicit CallbackScope(Binding* value) : binding(value) { ++binding->callbacks; }
    ~CallbackScope() { if (!--binding->callbacks && binding->removed) delete binding; }
};
constexpr UINT_PTR subclass_id = 0x415245;
LRESULT CALLBACK page_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
void detach(Binding* binding) noexcept {
    if (binding->removed) return;
    binding->removed = true;
    RemovePropW(binding->page, mpc::binding_property);
    RemoveWindowSubclass(binding->page, page_proc, subclass_id);
    if (IsWindow(binding->button) && GetParent(binding->button) == binding->page &&
        GetWindowLongPtrW(binding->button, GWLP_ID) == binding->managed_id) {
        SetWindowLongPtrW(binding->button, GWLP_ID, binding->original_id);
        EnableWindow(binding->button, binding->originally_enabled);
    }
    if (!binding->callbacks) delete binding;
}
LRESULT CALLBACK page_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* binding = reinterpret_cast<Binding*>(data);
    CallbackScope scope(binding);
    if (binding->removed) return DefSubclassProc(window, message, wp, lp);
    if (message == WM_NCDESTROY) {
        detach(binding);
        return DefSubclassProc(window, message, wp, lp);
    }
    static const auto attachment = RegisterWindowMessageW(mpc::attach_message);
    if (attachment && message == attachment && lp == 0) {
        // A modal property frame still needs the binding until it returns.
        detach(binding);
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wp) == CBN_SELCHANGE && reinterpret_cast<HWND>(lp) == binding->combo) {
        const auto result = DefSubclassProc(window, message, wp, lp);
        if (!binding->removed && !mpc::selected(binding->combo)) detach(binding);
        return result;
    }
    if (message == WM_COMMAND && HIWORD(wp) == BN_CLICKED &&
        reinterpret_cast<HWND>(lp) == binding->button && mpc::selected(binding->combo)) {
        if (binding->opening) return 0;
        binding->opening = true;
        const auto com = OleInitialize(nullptr);
        HRESULT result = com;
        if (SUCCEEDED(com)) {
            try {
                auto filter = settings_renderer();
                if (!filter) filter.Attach(new Renderer);
                result = show_settings_frame(GetAncestor(window, GA_ROOT),filter.Get(),dialog_is_dark(window));
            } catch (...) { result = E_OUTOFMEMORY; }
            OleUninitialize();
        }
        binding->opening = false;
        if (binding->removed) return 0;
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
    LONG_PTR managed_id = 0x7ffe;
    while (managed_id >= 0x7000 && GetDlgItem(page, int(managed_id))) --managed_id;
    if (managed_id < 0x7000) return;
    auto* binding = new(std::nothrow) Binding{page, combo, button};
    if (!binding) return;
    binding->originally_enabled = IsWindowEnabled(button) != FALSE;
    binding->original_id = GetWindowLongPtrW(button, GWLP_ID);
    binding->managed_id = managed_id;
    if (!SetWindowSubclass(page, page_proc, subclass_id, reinterpret_cast<DWORD_PTR>(binding))) { delete binding; return; }
    if (!SetPropW(page, mpc::binding_property, binding)) { detach(binding); return; }
    // Windows can unload a hook DLL as soon as its installer removes the hook.
    // Subclass callbacks must remain valid until the player exits.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&attach), &pinned)) { detach(binding); return; }
    // MPC-HC routes its idle enable command by the original control ID and
    // recognizes only internal audio renderers. Temporarily own this command
    // while ARE is selected, rather than fighting EnableWindow or replacing
    // native mouse, focus, capture, keyboard or theme behavior. Other controls
    // keep their normal update handlers; detach restores the original ID/state.
    SetLastError(ERROR_SUCCESS);
    if (!SetWindowLongPtrW(button, GWLP_ID, managed_id) && GetLastError() != ERROR_SUCCESS) { detach(binding); return; }
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
bool is_mpc_process() noexcept {
    static const bool matching = []() noexcept {
        try {
            // Resolve the image once, on the heap. A 32K wchar_t local in the
            // hook reserved 64 KiB for every intercepted UI message, including
            // nested Windows callbacks while the Options page was created.
            std::wstring path(32768, L'\0');
            const auto length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
            if (!length || length >= path.size()) return false;
            path.resize(length);
            const auto separator = path.find_last_of(L'\\');
            const auto* name = path.c_str() + (separator == std::wstring::npos ? 0 : separator + 1);
            return !_wcsicmp(name, L"mpc-hc64.exe") || !_wcsicmp(name, L"mpc-hc.exe");
        } catch (...) { return false; }
    }();
    return matching;
}
}
extern "C" LRESULT CALLBACK AREMpcSettingsHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && lp) {
        static const auto diagnostic = RegisterWindowMessageW(are::win::mpc::diagnostic_message);
        static const auto attachment = RegisterWindowMessageW(are::win::mpc::attach_message);
        const auto& message = *reinterpret_cast<const CWPSTRUCT*>(lp);
        if (diagnostic && message.message == diagnostic) {
            if (are::win::is_mpc_process())
                are::win::write_mpc_diagnostic(DWORD(message.wParam));
        }
        if (attachment && message.message == attachment && message.lParam) {
            if (are::win::is_mpc_process())
                are::win::attach(message.hwnd, reinterpret_cast<HWND>(message.wParam), reinterpret_cast<HWND>(message.lParam));
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
