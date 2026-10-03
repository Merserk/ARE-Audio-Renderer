#pragma once
#include "win/interfaces.hpp"

namespace are::win {
#ifdef _WIN64
// Retain the original key so existing installations keep their preferences.
inline constexpr wchar_t settings_registry_path[] = L"Software\\ASIO Render Engine\\x64";
#else
// Retain the original key so existing installations keep their preferences.
inline constexpr wchar_t settings_registry_path[] = L"Software\\ASIO Render Engine\\x86";
#endif
[[nodiscard]] Settings load_settings();
HRESULT save_settings(const Settings& settings) noexcept;
[[nodiscard]] PlaybackOptions load_playback_options() noexcept;
HRESULT save_playback_options(const PlaybackOptions& options) noexcept;
[[nodiscard]] ProcessingOptions load_processing_options() noexcept;
HRESULT save_processing_options(const ProcessingOptions& options) noexcept;
[[nodiscard]] ResamplingOptions load_resampling_options() noexcept;
HRESULT save_resampling_options(const ResamplingOptions& options) noexcept;
} // namespace are::win
