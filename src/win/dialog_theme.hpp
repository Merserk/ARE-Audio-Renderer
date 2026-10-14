#pragma once
#include "win/common.hpp"
#include <optional>

namespace are::win {
inline constexpr wchar_t dark_property[] = L"ARE.Dialog.Dark";
bool dialog_is_dark(HWND owner) noexcept;
class DialogTheme {
public:
    explicit DialogTheme(bool dark = false) : dark_(dark) {}
    ~DialogTheme();
    DialogTheme(const DialogTheme&) = delete;
    DialogTheme& operator=(const DialogTheme&) = delete;
    void initialize(HWND window);
    std::optional<INT_PTR> message(HWND window, UINT message, WPARAM wp, LPARAM lp);
    bool dark() const noexcept { return dark_; }
    static constexpr COLORREF background = RGB(32,32,32);
    static constexpr COLORREF control = RGB(45,45,45);
    static constexpr COLORREF text = RGB(235,235,235);
private:
    static LRESULT CALLBACK control_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void paint_control(HWND window, HDC dc);
    bool dark_{};
    HBRUSH background_{}, control_{};
};
}
