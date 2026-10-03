#include "fake_asio.hpp"
using namespace are::test;
namespace {
class FakeFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override { if (!out) return E_POINTER; *out = nullptr; if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE; *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** out) override { if (outer) return CLASS_E_NOAGGREGATION; FakeConfig config; config.automatic_callbacks = true; auto* driver = new FakeASIO(config); const auto hr = driver->QueryInterface(iid, out); driver->Release(); return hr; }
    STDMETHODIMP LockServer(BOOL) override { return S_OK; }
private:
    std::atomic<ULONG> refs_{1};
};
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) { if (clsid != fake_clsid) return CLASS_E_CLASSNOTAVAILABLE; auto* factory = new FakeFactory; const auto hr = factory->QueryInterface(iid, out); factory->Release(); return hr; }
// COM retains the simulator module for the test process, including its callback code.
extern "C" HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
extern "C" HRESULT WINAPI GetOutputMetrics(OutputMetrics* result) { if (!result) return E_POINTER; *result=output_metrics(); return S_OK; }
