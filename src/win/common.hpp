#pragma once
#include <windows.h>
#include <mmreg.h>
#include <dshow.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace are::win {
using Microsoft::WRL::ComPtr;
inline std::atomic<long> module_objects{};
inline std::atomic<long> module_locks{};
inline HMODULE module_handle{};
struct ModuleRef {
    ModuleRef() noexcept { ++module_objects; }
    ~ModuleRef() { --module_objects; }
    ModuleRef(const ModuleRef&) = delete;
    ModuleRef& operator=(const ModuleRef&) = delete;
};
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE h) noexcept : h_(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : h_(std::exchange(other.h_, nullptr)) {}
    [[nodiscard]] HANDLE get() const noexcept { return h_; }
    explicit operator bool() const noexcept { return h_ && h_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE h = nullptr) noexcept { if (*this) CloseHandle(h_); h_ = h; }
private:
    HANDLE h_{};
};
class RegKey {
public:
    ~RegKey() { if (key_) RegCloseKey(key_); }
    [[nodiscard]] HKEY get() const noexcept { return key_; }
    HKEY* put() noexcept { if (key_) RegCloseKey(key_); key_ = nullptr; return &key_; }
private:
    HKEY key_{};
};
inline std::wstring guid_string(REFGUID guid) {
    wchar_t text[40]{};
    StringFromGUID2(guid, text, 40);
    return text;
}
inline std::wstring widen(const char* text) {
    if (!text || !*text) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 0) return L"ASIO driver error";
    std::wstring result(std::size_t(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), len);
    result.pop_back();
    return result;
}
inline HRESULT win_error(LSTATUS code) noexcept { return HRESULT_FROM_WIN32(code); }
} // namespace are::win
