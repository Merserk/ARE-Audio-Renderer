#pragma once
#include "win/common.hpp"
#include "core/format.hpp"
#include <optional>

namespace are::win {
[[nodiscard]] std::optional<SourceFormat> parse_media_type(const AM_MEDIA_TYPE& type) noexcept;
void free_media_type(AM_MEDIA_TYPE& type) noexcept;
HRESULT copy_media_type(AM_MEDIA_TYPE& to, const AM_MEDIA_TYPE& from) noexcept;
AM_MEDIA_TYPE pcm_media_type(WORD channels, DWORD rate, WORD bits);
} // namespace are::win
