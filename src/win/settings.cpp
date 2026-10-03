#include "win/settings.hpp"
#include "win/devices.hpp"

namespace are::win {
namespace {
DWORD read_dword(const wchar_t* name, DWORD fallback = 0) noexcept {
    DWORD result = fallback, bytes = sizeof(result);
    RegGetValueW(HKEY_CURRENT_USER, settings_registry_path, name, RRF_RT_REG_DWORD, nullptr, &result, &bytes);
    return result;
}
}
Settings load_settings() {
    Settings result;
    const auto guid = registry_string(HKEY_CURRENT_USER, settings_registry_path, L"Driver");
    if (!guid.empty()) CLSIDFromString(guid.c_str(), &result.driver);
    result.first_channel = read_dword(L"FirstChannel");
    result.buffer_frames = read_dword(L"BufferFrames");
    result.keep_device_rate = read_dword(L"KeepDeviceRate", 1) ? TRUE : FALSE;
    if (result.first_channel > 1023) result.first_channel = 0;
    if (result.buffer_frames > 65536) result.buffer_frames = 0;
    return result;
}
HRESULT save_settings(const Settings& s) noexcept {
    try {
        if (s.first_channel > 1023 || s.buffer_frames > 65536) return E_INVALIDARG;
        RegKey key;
        auto code = RegCreateKeyExW(HKEY_CURRENT_USER, settings_registry_path, 0, nullptr, 0, KEY_WRITE, nullptr, key.put(), nullptr);
        if (code != ERROR_SUCCESS) return win_error(code);
        const auto guid = guid_string(s.driver);
        code = RegSetValueExW(key.get(), L"Driver", 0, REG_SZ, reinterpret_cast<const BYTE*>(guid.c_str()), DWORD((guid.size() + 1) * sizeof(wchar_t)));
        if (code != ERROR_SUCCESS) return win_error(code);
        for (const auto& entry : {std::pair{L"FirstChannel", DWORD(s.first_channel)},
                                 std::pair{L"BufferFrames", DWORD(s.buffer_frames)},
                                 std::pair{L"KeepDeviceRate", DWORD(s.keep_device_rate != FALSE)}}) {
            code = RegSetValueExW(key.get(), entry.first, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&entry.second), sizeof(DWORD));
            if (code != ERROR_SUCCESS) return win_error(code);
        }
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
PlaybackOptions load_playback_options() noexcept {
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, settings_registry_path, L"SmoothTransitions", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return {value != 0};
}
HRESULT save_playback_options(const PlaybackOptions& options) noexcept {
    RegKey key;
    const auto created = RegCreateKeyExW(HKEY_CURRENT_USER, settings_registry_path, 0, nullptr, 0, KEY_WRITE, nullptr, key.put(), nullptr);
    if (created != ERROR_SUCCESS) return win_error(created);
    const DWORD value = options.smooth_transitions != FALSE;
    return win_error(RegSetValueExW(key.get(), L"SmoothTransitions", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)));
}
ProcessingOptions load_processing_options() noexcept {
    const auto bits=read_dword(L"OutputPcmBits");
    return {bits==16 || bits==24 || bits==32 ? bits : 0};
}
HRESULT save_processing_options(const ProcessingOptions& options) noexcept {
    if (options.output_bits!=0 && options.output_bits!=16 && options.output_bits!=24 && options.output_bits!=32) return E_INVALIDARG;
    RegKey key;
    const auto created=RegCreateKeyExW(HKEY_CURRENT_USER,settings_registry_path,0,nullptr,0,KEY_WRITE,nullptr,key.put(),nullptr);
    if (created!=ERROR_SUCCESS) return win_error(created);
    const DWORD bits=options.output_bits;
    return win_error(RegSetValueExW(key.get(),L"OutputPcmBits",0,REG_DWORD,reinterpret_cast<const BYTE*>(&bits),sizeof(bits)));
}
ResamplingOptions load_resampling_options() noexcept {
    return {read_dword(L"SrcAlgorithm")==UINT(SrcAlgorithm::sinc) ? SrcAlgorithm::sinc : SrcAlgorithm::r8brain};
}
HRESULT save_resampling_options(const ResamplingOptions& options) noexcept {
    if (!valid_src(options.algorithm)) return E_INVALIDARG;
    RegKey key;
    const auto created=RegCreateKeyExW(HKEY_CURRENT_USER,settings_registry_path,0,nullptr,0,KEY_WRITE,nullptr,key.put(),nullptr);
    if (created!=ERROR_SUCCESS) return win_error(created);
    const DWORD algorithm=DWORD(options.algorithm);
    return win_error(RegSetValueExW(key.get(),L"SrcAlgorithm",0,REG_DWORD,reinterpret_cast<const BYTE*>(&algorithm),sizeof(algorithm)));
}
} // namespace are::win
