// Test-only prefixes of LAV's public COM configuration interfaces.
// Interface declarations adapted from LAVFilters src/include/*Settings.h.
// Copyright (C) 2010-2021 Hendrik Leppkes, GPL-2.0-or-later.
// https://github.com/Nevcairiel/LAVFilters/tree/master/src/include
// No LAV code or configuration interface is linked into the renderer.
#pragma once
#include "win/common.hpp"

namespace are::test {
struct __declspec(uuid("774A919D-EA95-4A87-8A1E-F48ABE8499C7")) LAVSplitterConfiguration : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetRuntimeConfig(BOOL enabled)=0;
};
struct __declspec(uuid("4158A22B-6553-45D0-8069-24716F8FF171")) LAVAudioConfiguration : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetRuntimeConfig(BOOL enabled)=0;
    virtual HRESULT STDMETHODCALLTYPE GetDRC(BOOL* enabled,int* level)=0;
    virtual HRESULT STDMETHODCALLTYPE SetDRC(BOOL enabled,int level)=0;
    virtual BOOL STDMETHODCALLTYPE GetFormatConfiguration(int codec)=0;
    virtual HRESULT STDMETHODCALLTYPE SetFormatConfiguration(int codec,BOOL enabled)=0;
    virtual BOOL STDMETHODCALLTYPE GetBitstreamConfig(int codec)=0;
    virtual HRESULT STDMETHODCALLTYPE SetBitstreamConfig(int codec,BOOL enabled)=0;
    virtual BOOL STDMETHODCALLTYPE GetDTSHDFraming()=0;
    virtual HRESULT STDMETHODCALLTYPE SetDTSHDFraming(BOOL enabled)=0;
    virtual BOOL STDMETHODCALLTYPE GetAutoAVSync()=0;
    virtual HRESULT STDMETHODCALLTYPE SetAutoAVSync(BOOL enabled)=0;
    virtual BOOL STDMETHODCALLTYPE GetOutputStandardLayout()=0;
    virtual HRESULT STDMETHODCALLTYPE SetOutputStandardLayout(BOOL enabled)=0;
    virtual BOOL STDMETHODCALLTYPE GetExpandMono()=0;
    virtual HRESULT STDMETHODCALLTYPE SetExpandMono(BOOL enabled)=0;
    virtual BOOL STDMETHODCALLTYPE GetExpand61()=0;
    virtual HRESULT STDMETHODCALLTYPE SetExpand61(BOOL enabled)=0;
    virtual BOOL STDMETHODCALLTYPE GetAllowRawSPDIFInput()=0;
    virtual HRESULT STDMETHODCALLTYPE SetAllowRawSPDIFInput(BOOL enabled)=0;
    virtual BOOL STDMETHODCALLTYPE GetSampleFormat(int format)=0;
    virtual HRESULT STDMETHODCALLTYPE SetSampleFormat(int format,BOOL enabled)=0;
    virtual HRESULT STDMETHODCALLTYPE GetAudioDelay(BOOL* enabled,int* delay)=0;
    virtual HRESULT STDMETHODCALLTYPE SetAudioDelay(BOOL enabled,int delay)=0;
    virtual HRESULT STDMETHODCALLTYPE SetMixingEnabled(BOOL enabled)=0;
    virtual BOOL STDMETHODCALLTYPE GetMixingEnabled()=0;
};
// Stable LAVAudioCodec values; use only these known entries, not Codec_AudioNB.
inline constexpr int wma2=13,wma_pro=14,wma_lossless=17;
}
