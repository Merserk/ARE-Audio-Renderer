#pragma once
#include <windows.h>
#include <cwchar>
#include <algorithm>

namespace are::win::mpc {
inline constexpr wchar_t attach_message[] = L"ARE.AudioRenderer.MPC.Settings.Attach.v1";
inline constexpr wchar_t binding_property[] = L"ARE.AudioRenderer.MPC.Settings.Binding.v1";
#ifdef _WIN64
inline constexpr wchar_t mutex_name[] = L"Local\\ARE.AudioRenderer.MPC.Integration.x64";
inline constexpr wchar_t stop_name[] = L"Local\\ARE.AudioRenderer.MPC.Integration.Stop.x64";
#else
inline constexpr wchar_t mutex_name[] = L"Local\\ARE.AudioRenderer.MPC.Integration.x86";
inline constexpr wchar_t stop_name[] = L"Local\\ARE.AudioRenderer.MPC.Integration.Stop.x86";
#endif
inline bool class_is(HWND window, const wchar_t* name) noexcept {
    wchar_t value[64]{};
    return GetClassNameW(window, value, 64) && !_wcsicmp(value, name);
}
inline bool selected(HWND combo) noexcept {
    wchar_t value[128]{}; DWORD_PTR result{};
    return SendMessageTimeoutW(combo, WM_GETTEXT, std::size(value), reinterpret_cast<LPARAM>(value),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result) && !wcscmp(value, L"ARE Audio Renderer");
}
// Find the gear by its relationship to the selected audio combobox. This uses
// neither MPC-HC resource IDs, translated captions, nor executable versions.
inline HWND settings_button(HWND combo) noexcept {
    if (!class_is(combo, L"ComboBox") || !selected(combo)) return nullptr;
    RECT input{}; if (!GetWindowRect(combo, &input)) return nullptr;
    HWND found{};
    for (auto child = GetWindow(GetParent(combo), GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!class_is(child, L"Button") || !IsWindowVisible(child)) continue;
        if ((GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) continue;
        RECT button{}; if (!GetWindowRect(child, &button)) continue;
        const auto height = input.bottom - input.top;
        const auto center = (input.top + input.bottom) / 2;
        if (button.left >= input.right && button.left - input.right <= height * 3 &&
            button.top <= center && button.bottom > center &&
            button.right - button.left <= height * 3) {
            if (found) return nullptr; // Refuse ambiguous or redesigned layouts.
            found = child;
        }
    }
    return found;
}
} // namespace are::win::mpc
