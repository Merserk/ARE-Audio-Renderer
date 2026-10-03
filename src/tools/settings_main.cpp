#include "win/interfaces.hpp"
#include <commctrl.h>

using namespace are::win;
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    const INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    const auto com = OleInitialize(nullptr);
    if (FAILED(com)) return 1;
    HRESULT result = S_OK;
    {
        ComPtr<IBaseFilter> filter;
        result = CoCreateInstance(clsid_renderer,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(filter.GetAddressOf()));
        if (SUCCEEDED(result)) {
            IUnknown* object = filter.Get(); auto page = clsid_settings_page;
            result = OleCreatePropertyFrame(nullptr,0,0,renderer_name,1,&object,1,&page,0,0,nullptr);
        }
        if (FAILED(result)) MessageBoxW(nullptr,L"Could not open settings. Install ARE Audio Renderer for this player architecture first.",renderer_name,MB_OK);
    }
    OleUninitialize(); return FAILED(result) ? 1 : 0;
}
