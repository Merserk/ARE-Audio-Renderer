#include "win/settings_frame.hpp"
#include "win/property_page.hpp"
#include "win/dialog_theme.hpp"
#include "../../resources/resource.h"

namespace are::win {
namespace {
class Frame final : public IPropertyPageSite {
public:
    explicit Frame(bool dark) : theme(dark) {}
    ComPtr<IPropertyPage> page;
    DialogTheme theme;
    HWND window{};
    HRESULT result{S_OK};
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_IPropertyPageSite) return E_NOINTERFACE;
        *out=static_cast<IPropertyPageSite*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override { return --refs; } // Stack lifetime contains the modal page.
    STDMETHODIMP OnStatusChange(DWORD flags) override {
        if (window) EnableWindow(GetDlgItem(window,ID_APPLY),(flags&PROPPAGESTATUS_DIRTY)!=0); return S_OK;
    }
    STDMETHODIMP GetLocaleID(LCID* out) override { if (!out) return E_POINTER; *out=GetUserDefaultLCID(); return S_OK; }
    STDMETHODIMP GetPageContainer(IUnknown** out) override { if (!out) return E_POINTER; *out=nullptr; return E_NOINTERFACE; }
    STDMETHODIMP TranslateAccelerator(MSG*) override { return S_FALSE; }
    static INT_PTR CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        auto* frame=reinterpret_cast<Frame*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message==WM_INITDIALOG) {
            frame=reinterpret_cast<Frame*>(lp); frame->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,lp);
            frame->theme.initialize(window);
            RECT rect{8,20,352,362}; MapDialogRect(window,&rect);
            frame->result=frame->page->Activate(window,&rect,TRUE);
            if (FAILED(frame->result)) { EndDialog(window,IDCANCEL); return TRUE; }
            frame->page->Show(SW_SHOW); EnableWindow(GetDlgItem(window,ID_APPLY),FALSE);
            RECT bounds{}, owner{}; GetWindowRect(window,&bounds);
            const auto parent=GetWindow(window,GW_OWNER);
            if (parent && GetWindowRect(parent,&owner)) SetWindowPos(window,nullptr,
                owner.left+(owner.right-owner.left-(bounds.right-bounds.left))/2,
                owner.top+(owner.bottom-owner.top-(bounds.bottom-bounds.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
            return TRUE;
        }
        if (!frame) return FALSE;
        if (const auto handled=frame->theme.message(window,message,wp,lp)) return *handled;
        if (message==WM_COMMAND) {
            if (LOWORD(wp)==IDOK || LOWORD(wp)==ID_APPLY) {
                if (frame->page->IsPageDirty()==S_OK && FAILED(frame->page->Apply())) return TRUE;
                if (LOWORD(wp)==IDOK) EndDialog(window,IDOK);
                return TRUE;
            }
            if (LOWORD(wp)==IDCANCEL) { EndDialog(window,IDCANCEL); return TRUE; }
        }
        if (message==WM_CLOSE) { EndDialog(window,IDCANCEL); return TRUE; }
        return FALSE;
    }
private:
    std::atomic<ULONG> refs{1};
};
}
HRESULT show_settings_frame(HWND owner,IUnknown* object,bool dark) {
    if (!object) return E_POINTER;
    Frame frame(dark); frame.page.Attach(new(std::nothrow) SettingsPage);
    if (!frame.page) return E_OUTOFMEMORY;
    auto result=frame.page->SetObjects(1,&object); if (FAILED(result)) return result;
    result=frame.page->SetPageSite(&frame); if (FAILED(result)) return result;
    const auto modal=DialogBoxParamW(module_handle,MAKEINTRESOURCEW(IDD_SETTINGS_FRAME),owner,Frame::proc,reinterpret_cast<LPARAM>(&frame));
    frame.page->Deactivate(); frame.page->SetObjects(0,nullptr); frame.page->SetPageSite(nullptr);
    return modal==-1 ? HRESULT_FROM_WIN32(GetLastError()) : frame.result;
}
}
extern "C" HRESULT WINAPI AREShowSettings(HWND owner,IUnknown* object) {
    try { return are::win::show_settings_frame(owner,object,are::win::dialog_is_dark(owner)); }
    catch (...) { return E_OUTOFMEMORY; }
}
