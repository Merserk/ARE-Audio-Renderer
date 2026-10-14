#include "win/mpc_diagnostics.hpp"
#include "win/mpc_integration.hpp"
#include <cwctype>
#include <algorithm>
namespace are::win {
void write_mpc_diagnostic(DWORD caller) noexcept {
    try {
        const auto name=mpc::diagnostic_mapping(GetCurrentProcessId(),caller);
        Handle mapping(OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name.c_str()));
        if (!mapping) return;
        auto* shared=static_cast<mpc::Diagnostic*>(MapViewOfFile(mapping.get(),FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(mpc::Diagnostic)));
        if (!shared) return;
        if (shared->size!=sizeof(mpc::Diagnostic) || shared->signature!=0x41524531) { UnmapViewOfFile(shared); return; }
        mpc::Diagnostic result;
        auto active=settings_renderer();
        ComPtr<IASIORenderStatus> status;
        if (active && SUCCEEDED(active.As(&status))) result.result=status->GetLiveStatus(&result.live);
        ComPtr<IRunningObjectTable> rot;
        if (SUCCEEDED(GetRunningObjectTable(0,rot.GetAddressOf()))) {
            ComPtr<IEnumMoniker> entries; rot->EnumRunning(entries.GetAddressOf());
            ComPtr<IMoniker> moniker;
            auto match=std::format(L"pid {:08x} (mpc-hc)",GetCurrentProcessId());
            while (entries && entries->Next(1,moniker.ReleaseAndGetAddressOf(),nullptr)==S_OK) {
                ComPtr<IBindCtx> context; CreateBindCtx(0,context.GetAddressOf());
                LPOLESTR display{}; if (FAILED(moniker->GetDisplayName(context.Get(),nullptr,&display))) continue;
                std::wstring graph_name(display); CoTaskMemFree(display);
                std::transform(graph_name.begin(),graph_name.end(),graph_name.begin(),[](wchar_t ch){return wchar_t(std::towlower(ch));});
                if (graph_name.find(match)==std::wstring::npos) continue;
                ComPtr<IUnknown> object; if (FAILED(rot->GetObject(moniker.Get(),object.GetAddressOf()))) continue;
                ComPtr<IGraphBuilder> graph; if (FAILED(object.As(&graph))) continue;
                ComPtr<IEnumFilters> filters; graph->EnumFilters(filters.GetAddressOf());
                ComPtr<IBaseFilter> filter;
                while (filters && result.filters<std::size(result.filter_names) && filters->Next(1,filter.ReleaseAndGetAddressOf(),nullptr)==S_OK) {
                    FILTER_INFO info{}; filter->QueryFilterInfo(&info); if (info.pGraph) info.pGraph->Release();
                    wcsncpy_s(result.filter_names[result.filters++],info.achName,_TRUNCATE);
                    // An extracted diagnostic tool can load a second copy of
                    // the DLL. Query the real graph filter rather than assuming
                    // its instance registry lives in our particular module.
                    CLSID clsid{}; filter->GetClassID(&clsid);
                    if (clsid==clsid_renderer && !active && SUCCEEDED(filter.As(&status)))
                        result.result=status->GetLiveStatus(&result.live);
                }
            }
        }
        *shared=result; UnmapViewOfFile(shared);
    } catch (...) { }
}
}
