#pragma once
#include "win/devices.hpp"
#include "win/interfaces.hpp"

namespace are::win {
class SettingsPage final : public IPropertyPage {
public:
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override;
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP SetPageSite(IPropertyPageSite* site) override;
    STDMETHODIMP Activate(HWND parent, LPCRECT rect, BOOL modal) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP GetPageInfo(PROPPAGEINFO* info) override;
    STDMETHODIMP SetObjects(ULONG count, IUnknown** objects) override;
    STDMETHODIMP Show(UINT command) override;
    STDMETHODIMP Move(LPCRECT rect) override;
    STDMETHODIMP IsPageDirty() override { return dirty_ ? S_OK : S_FALSE; }
    STDMETHODIMP Apply() override;
    STDMETHODIMP Help(LPCOLESTR) override { return E_NOTIMPL; }
    STDMETHODIMP TranslateAccelerator(MSG* message) override;
private:
    ~SettingsPage();
    static INT_PTR CALLBACK dialog_proc(HWND window, UINT message, WPARAM wp, LPARAM lp);
    void initialize();
    void refresh_status();
    void dirty();
    ModuleRef module_;
    std::atomic<ULONG> refs_{1};
    ComPtr<IPropertyPageSite> site_;
    ComPtr<IASIORenderSettings> settings_;
    ComPtr<IASIORenderStatus> live_status_;
    ComPtr<IASIORenderPlayback> playback_;
    ComPtr<IASIORenderProcessing> processing_;
    ComPtr<IASIORenderResampling> resampling_;
    ComPtr<IMediaSeeking> seeking_;
    std::wstring apply_message_;
    std::vector<Device> devices_;
    HWND window_{};
    bool dirty_{};
    bool loading_{};
};
} // namespace are::win
