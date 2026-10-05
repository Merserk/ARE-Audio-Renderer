#include "win/media.hpp"
#include <ks.h>
#include <ksmedia.h>
#include <bit>
#include <cstring>

namespace are::win {
std::optional<SourceFormat> parse_media_type(const AM_MEDIA_TYPE& mt) noexcept {
    constexpr auto pcm_header_size = offsetof(WAVEFORMATEX, cbSize);
    if (mt.majortype != MEDIATYPE_Audio || mt.formattype != FORMAT_WaveFormatEx || !mt.pbFormat || mt.cbFormat < pcm_header_size) return {};
    WAVEFORMATEX wave{};
    if (mt.cbFormat < sizeof(wave)) {
        if (mt.cbFormat != pcm_header_size) return {};
        std::memcpy(&wave, mt.pbFormat, pcm_header_size);
        if (wave.wFormatTag != WAVE_FORMAT_PCM) return {};
    } else {
        std::memcpy(&wave, mt.pbFormat, sizeof(wave));
        if (std::size_t(wave.cbSize) + sizeof(wave) > mt.cbFormat) return {};
    }
    SourceFormat result;
    result.rate = wave.nSamplesPerSec;
    result.channels = wave.nChannels;
    result.container_bits = wave.wBitsPerSample;
    result.valid_bits = wave.wBitsPerSample;
    GUID subtype{};
    if (wave.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (wave.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) || mt.cbFormat < sizeof(WAVEFORMATEXTENSIBLE)) return {};
        WAVEFORMATEXTENSIBLE ext{};
        std::memcpy(&ext, mt.pbFormat, sizeof(ext));
        subtype = ext.SubFormat;
        result.valid_bits = ext.Samples.wValidBitsPerSample;
        result.channel_mask = ext.dwChannelMask;
        if (result.channel_mask && std::popcount(result.channel_mask) != result.channels) return {};
    } else if (wave.wFormatTag == WAVE_FORMAT_PCM) subtype = KSDATAFORMAT_SUBTYPE_PCM;
    else if (wave.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) subtype = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    else return {};
    if (subtype == KSDATAFORMAT_SUBTYPE_PCM && mt.subtype == MEDIASUBTYPE_PCM) result.kind = SampleKind::integer;
    else if (subtype == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && mt.subtype == MEDIASUBTYPE_IEEE_FLOAT) {
        result.kind = result.container_bits == 64 ? SampleKind::float64 : SampleKind::float32;
    } else return {};
    if (!result.valid() || wave.nBlockAlign != result.frame_bytes() ||
        std::uint64_t(wave.nAvgBytesPerSec) != std::uint64_t(result.rate) * result.frame_bytes()) return {};
    return result;
}
void free_media_type(AM_MEDIA_TYPE& mt) noexcept {
    CoTaskMemFree(mt.pbFormat);
    if (mt.pUnk) mt.pUnk->Release();
    mt = {};
}
HRESULT copy_media_type(AM_MEDIA_TYPE& to, const AM_MEDIA_TYPE& from) noexcept {
    to = from;
    to.pbFormat = nullptr;
    to.pUnk = nullptr;
    if (from.cbFormat) {
        if (!from.pbFormat) { to = {}; return E_INVALIDARG; }
        to.pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(from.cbFormat));
        if (!to.pbFormat) { to = {}; return E_OUTOFMEMORY; }
        std::memcpy(to.pbFormat, from.pbFormat, from.cbFormat);
    }
    to.pUnk = from.pUnk;
    if (to.pUnk) to.pUnk->AddRef();
    return S_OK;
}
AM_MEDIA_TYPE pcm_media_type(WORD channels, DWORD rate, WORD bits) {
    AM_MEDIA_TYPE mt{};
    mt.majortype = MEDIATYPE_Audio;
    mt.subtype = MEDIASUBTYPE_PCM;
    mt.bFixedSizeSamples = TRUE;
    mt.lSampleSize = channels * (bits / 8);
    mt.formattype = FORMAT_WaveFormatEx;
    mt.cbFormat = sizeof(WAVEFORMATEX);
    mt.pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(mt.cbFormat));
    if (!mt.pbFormat) { mt.cbFormat = 0; return mt; }
    const WAVEFORMATEX wave{WAVE_FORMAT_PCM, channels, rate, rate * channels * (bits / 8), WORD(channels * (bits / 8)), bits, 0};
    std::memcpy(mt.pbFormat, &wave, sizeof(wave));
    return mt;
}
} // namespace are::win
