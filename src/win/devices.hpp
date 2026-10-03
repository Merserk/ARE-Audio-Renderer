#pragma once
#include "win/common.hpp"
#include <vector>

namespace are::win {
struct Device { CLSID clsid{}; std::wstring name; std::wstring dll_path; };
[[nodiscard]] std::vector<Device> enumerate_devices();
[[nodiscard]] std::wstring registry_string(HKEY root, const std::wstring& path, const wchar_t* name);
} // namespace are::win
