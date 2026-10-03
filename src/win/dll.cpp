#include "win/filter.hpp"
#include "win/property_page.hpp"
#include "win/registration.hpp"

namespace are::win {
class Factory final : public IClassFactory {
public:
    explicit Factory(bool page) : page_(page) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        try {
            IUnknown* object = page_ ? static_cast<IUnknown*>(static_cast<IPropertyPage*>(new SettingsPage)) : static_cast<IUnknown*>(static_cast<IBaseFilter*>(new Renderer));
            const auto hr = object->QueryInterface(iid, out); object->Release(); return hr;
        } catch (...) { return E_OUTOFMEMORY; }
    }
    STDMETHODIMP LockServer(BOOL lock) override { if (lock) ++module_locks; else --module_locks; return S_OK; }
private:
    ModuleRef module_;
    std::atomic<ULONG> refs_{1};
    bool page_;
};
}
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) are::win::module_handle = instance;
    return TRUE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER; *out = nullptr;
    if (clsid != are::win::clsid_renderer && clsid != are::win::clsid_settings_page) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new(std::nothrow) are::win::Factory(clsid == are::win::clsid_settings_page);
    if (!factory) return E_OUTOFMEMORY;
    const auto hr = factory->QueryInterface(iid, out); factory->Release(); return hr;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return are::win::module_objects.load() == 0 && are::win::module_locks.load() == 0 ? S_OK : S_FALSE; }
extern "C" HRESULT WINAPI DllRegisterServer() {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return initialized;
    const auto hr = are::win::register_renderer(); if (SUCCEEDED(initialized)) CoUninitialize(); return hr;
}
extern "C" HRESULT WINAPI DllUnregisterServer() { return are::win::unregister_renderer(); }
