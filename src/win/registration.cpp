#include "win/registration.hpp"
#include "win/interfaces.hpp"

namespace are::win {
namespace {
// Published DirectShow registry serializer (Fil_data.h in the SDK sample).
MIDL_INTERFACE("97F7C4D4-547B-4A5F-8332-536430AD2E4D")
FilterData : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ParseFilterData(BYTE*, ULONG, BYTE**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFilterData(REGFILTER2*, BYTE**, ULONG*) = 0;
};
HRESULT set_value(const std::wstring& path, const wchar_t* name, DWORD type, const BYTE* data, DWORD size) {
    RegKey key;
    const auto code = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, key.put(), nullptr);
    if (code != ERROR_SUCCESS) return win_error(code);
    return win_error(RegSetValueExW(key.get(), name, 0, type, data, size));
}
HRESULT set_string(const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    return set_value(path, name, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
}
HRESULT register_class(REFCLSID clsid, const std::wstring& path, const wchar_t* model) {
    const auto key = L"Software\\Classes\\CLSID\\" + guid_string(clsid);
    auto hr = set_string(key, nullptr, renderer_name); if (FAILED(hr)) return hr;
    hr = set_string(key + L"\\InprocServer32", nullptr, path); if (FAILED(hr)) return hr;
    return set_string(key + L"\\InprocServer32", L"ThreadingModel", model);
}
}
HRESULT register_renderer() noexcept {
    try {
        wchar_t path[32768]{};
        const auto length = GetModuleFileNameW(module_handle, path, 32768);
        if (!length || length == 32768) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        ComPtr<FilterData> serializer;
        const auto created = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(serializer.GetAddressOf()));
        if (FAILED(created)) return created;
        const REGPINTYPES media[]{{&MEDIATYPE_Audio, &MEDIASUBTYPE_PCM}, {&MEDIATYPE_Audio, &MEDIASUBTYPE_IEEE_FLOAT}};
        REGFILTERPINS2 pin{}; pin.dwFlags = REG_PINFLAG_B_RENDERER; pin.nMediaTypes = 2; pin.lpMediaType = media;
        REGFILTER2 filter{}; filter.dwVersion = 2; filter.dwMerit = MERIT_DO_NOT_USE; filter.cPins2 = 1; filter.rgPins2 = &pin;
        BYTE* bytes = nullptr; ULONG size = 0;
        auto hr = serializer->CreateFilterData(&filter, &bytes, &size);
        if (FAILED(hr)) return hr;
        std::unique_ptr<BYTE, decltype(&CoTaskMemFree)> data(bytes, CoTaskMemFree);
        hr = register_class(clsid_renderer, path, L"Both"); if (FAILED(hr)) return hr;
        hr = register_class(clsid_settings_page, path, L"Apartment"); if (FAILED(hr)) return hr;
        for (const auto& category : {CLSID_AudioRendererCategory, CLSID_LegacyAmFilterCategory}) {
            const auto key = L"Software\\Classes\\CLSID\\" + guid_string(category) + L"\\Instance\\" + guid_string(clsid_renderer);
            hr = set_string(key, L"CLSID", guid_string(clsid_renderer)); if (FAILED(hr)) return hr;
            hr = set_string(key, L"FriendlyName", renderer_name); if (FAILED(hr)) return hr;
            hr = set_value(key, L"FilterData", REG_BINARY, bytes, size); if (FAILED(hr)) return hr;
        }
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
HRESULT unregister_renderer() noexcept {
    try {
        HRESULT result = S_OK;
        for (const auto& category : {CLSID_AudioRendererCategory, CLSID_LegacyAmFilterCategory}) {
            const auto key = L"Software\\Classes\\CLSID\\" + guid_string(category) + L"\\Instance\\" + guid_string(clsid_renderer);
            const auto code = RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
            if (code != ERROR_SUCCESS && code != ERROR_FILE_NOT_FOUND) result = win_error(code);
        }
        for (const auto& clsid : {clsid_renderer, clsid_settings_page}) {
            const auto key = L"Software\\Classes\\CLSID\\" + guid_string(clsid);
            const auto code = RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
            if (code != ERROR_SUCCESS && code != ERROR_FILE_NOT_FOUND) result = win_error(code);
        }
        return result;
    } catch (...) { return E_OUTOFMEMORY; }
}
} // namespace are::win
