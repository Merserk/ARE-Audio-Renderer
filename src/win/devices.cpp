#include "win/devices.hpp"
#include <algorithm>

namespace are::win {
std::wstring registry_string(HKEY root, const std::wstring& path, const wchar_t* name) {
    DWORD bytes = 0;
    constexpr DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ;
    if (RegGetValueW(root, path.c_str(), name, flags, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes > 65536) return {};
    std::wstring result(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, path.c_str(), name, flags, nullptr, result.data(), &bytes) != ERROR_SUCCESS) return {};
    while (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}
std::vector<Device> enumerate_devices() {
    std::vector<Device> result;
    for (const auto root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        RegKey key;
        if (RegOpenKeyExW(root, L"SOFTWARE\\ASIO", 0, KEY_READ, key.put()) != ERROR_SUCCESS) continue;
        for (DWORD i = 0;; ++i) {
            wchar_t name[256]{};
            DWORD length = 256;
            const auto code = RegEnumKeyExW(key.get(), i, name, &length, nullptr, nullptr, nullptr, nullptr);
            if (code == ERROR_NO_MORE_ITEMS) break;
            if (code != ERROR_SUCCESS) continue;
            const auto path = std::wstring(L"SOFTWARE\\ASIO\\") + name;
            const auto clsid_text = registry_string(root, path, L"CLSID");
            CLSID clsid{};
            if (FAILED(CLSIDFromString(clsid_text.c_str(), &clsid))) continue;
            if (std::ranges::any_of(result, [&](const Device& d) { return d.clsid == clsid; })) continue;
            auto description = registry_string(root, path, L"Description");
            if (description.empty()) description = name;
            const auto dll_path = registry_string(HKEY_CLASSES_ROOT, L"CLSID\\" + guid_string(clsid) + L"\\InprocServer32", nullptr);
            if (!dll_path.empty()) result.push_back({clsid, std::move(description), dll_path});
        }
    }
    std::ranges::sort(result, {}, &Device::name);
    return result;
}
} // namespace are::win
