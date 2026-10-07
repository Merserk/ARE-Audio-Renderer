#pragma once
#include "win/common.hpp"
#include "core/resampler.hpp"
#include <cstdint>

namespace are::win {
inline constexpr CLSID clsid_renderer{0x8a793ba1,0xcf7e,0x4f24,{0x93,0x9f,0x28,0x8d,0x13,0xca,0x57,0x87}};
inline constexpr CLSID clsid_settings_page{0x78f0f9fd,0x1857,0x45de,{0xb7,0xb1,0xe9,0x86,0x86,0x28,0x98,0xc2}};
inline constexpr wchar_t renderer_name[] = L"ARE Audio Renderer";
struct Settings {
    CLSID driver{};
    UINT first_channel{}; // zero based; first contiguous ASIO output
    UINT buffer_frames{}; // zero means driver's preferred value
    BOOL keep_device_rate{TRUE};
};
struct EngineStatus {
    BOOL opened{};
    BOOL playing{};
    BOOL muted{};
    UINT sample_rate{};
    UINT channels{};
    UINT source_bits{};
    UINT buffer_frames{};
    UINT output_latency_frames{};
    UINT queued_frames{};
    std::uint64_t delivered_frames{};
    std::uint64_t underruns{};
    std::uint64_t overloads{};
    HRESULT error{S_OK};
    wchar_t driver_name[128]{};
    wchar_t output_format[256]{};
    wchar_t detail[512]{};
};
// Keep the original settings/status ABI intact for existing tools.
enum class AudioSampleType : UINT { integer, float32, float64 };
struct AudioFormatStatus {
    AudioSampleType type{AudioSampleType::integer};
    UINT valid_bits{};
    UINT container_bits{};
};
struct LiveStatus {
    EngineStatus engine{};
    FILTER_STATE playback{State_Stopped};
    BOOL in_graph{};
    BOOL connected{};
    BOOL completed{};
    BOOL settings_pending{};
    BOOL mixed_output{};
    AudioFormatStatus input{};
    AudioFormatStatus output{};
};
MIDL_INTERFACE("0943C49B-E374-4DC2-9521-59C8FB09A1FA")
IASIORenderSettings : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetSettings(Settings* settings) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSettings(const Settings* settings) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStatus(EngineStatus* status) = 0;
};
MIDL_INTERFACE("B5D3BE9C-CFB7-4B68-A75F-49759DE9B10A")
IASIORenderStatus : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetLiveStatus(LiveStatus* status) = 0;
};
struct PlaybackOptions { BOOL smooth_transitions{TRUE}; };
struct SignalStatus {
    long volume_db100{};
    BOOL smooth_transitions{};
    BOOL transitioning{};
};
// A separate interface preserves the 0.1/0.2 settings and live-status layouts.
MIDL_INTERFACE("A098F620-B17B-4E60-9DD8-15EB2C6C41A3")
IASIORenderPlayback : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackOptions(PlaybackOptions* options) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPlaybackOptions(const PlaybackOptions* options) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSignalStatus(SignalStatus* status) = 0;
};
struct ProcessingOptions { UINT output_bits{}; }; // 0 = ASIO native, or PCM 16/24/32.
struct ProcessingStatus {
    UINT input_rate{}, output_rate{}, pcm_bits{};
    BOOL resampling{}, precision_reduced{}, settings_pending{};
};
// New interface; existing settings, playback and status ABIs stay unchanged.
MIDL_INTERFACE("D1C40D67-227A-47F9-8A34-8B2ADFF25796")
IASIORenderProcessing : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetProcessingOptions(ProcessingOptions* options) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingOptions(const ProcessingOptions* options) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingStatus(ProcessingStatus* status) = 0;
};
struct ResamplingOptions { SrcAlgorithm algorithm{SrcAlgorithm::r8brain}; };
struct ResamplingStatus {
    SrcAlgorithm algorithm{SrcAlgorithm::r8brain}; // Active choice, not a pending selection.
    BOOL active{}, settings_pending{};
};
// Keep all earlier COM interface layouts intact for installed clients.
MIDL_INTERFACE("6CB9943B-3EB1-4AE6-B461-787D73624C85")
IASIORenderResampling : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetResamplingOptions(ResamplingOptions* options) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetResamplingOptions(const ResamplingOptions* options) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResamplingStatus(ResamplingStatus* status) = 0;
};
struct ChannelStatus {
    UINT input_channels{}, output_channels{}, input_mask{}, output_mask{};
    BOOL downmix{}, mono_duplicate{};
};
// Separate interface keeps every existing settings/status ABI intact.
MIDL_INTERFACE("9C411DD0-2F46-4385-9D3C-2ED27E91B731")
IASIORenderChannels : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetChannelStatus(ChannelStatus* status) = 0;
};
// Batch the existing setters into one restart without changing earlier ABIs.
MIDL_INTERFACE("7B5125EA-1553-46D1-A624-FC7EF51994BD")
IASIORenderApply : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ApplyPendingSettings() = 0;
};
} // namespace are::win
