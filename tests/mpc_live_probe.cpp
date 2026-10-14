#include "win/mpc_diagnostics.hpp"
#include <iostream>
#include <filesystem>
using namespace are::win;
struct Target { DWORD pid{}; HWND window{}; DWORD thread{}; };
BOOL CALLBACK find_player(HWND window,LPARAM data) {
    auto& target=*reinterpret_cast<Target*>(data); DWORD pid{};
    const auto thread=GetWindowThreadProcessId(window,&pid);
    wchar_t name[64]{}; GetClassNameW(window,name,64);
    if (pid==target.pid && wcsstr(name,L"MediaPlayerClassic")) { target.window=window; target.thread=thread; return FALSE; }
    return TRUE;
}
int wmain(int argc,wchar_t** argv) {
    if (argc<3) return 2;
    Target target{std::wcstoul(argv[1],nullptr,10)}; EnumWindows(find_player,reinterpret_cast<LPARAM>(&target));
    if (!target.window) { std::wcerr << L"Player main window not found.\n"; return 1; }
    const auto dll=LoadLibraryExW(argv[2],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) return 1;
    const auto callback=reinterpret_cast<HOOKPROC>(GetProcAddress(dll,"AREMpcSettingsHook"));
    const auto name=mpc::diagnostic_mapping(target.pid,GetCurrentProcessId());
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(mpc::Diagnostic),name.c_str()));
    auto* result=static_cast<mpc::Diagnostic*>(MapViewOfFile(mapping.get(),FILE_MAP_ALL_ACCESS,0,0,sizeof(mpc::Diagnostic)));
    if (!result || !callback) return 1;
    *result=mpc::Diagnostic{};
    const auto hook=SetWindowsHookExW(WH_CALLWNDPROC,callback,dll,target.thread);
    DWORD_PTR response{};
    const auto sent=hook && SendMessageTimeoutW(target.window,RegisterWindowMessageW(mpc::diagnostic_message),GetCurrentProcessId(),0,SMTO_ABORTIFHUNG|SMTO_BLOCK,10000,&response);
    if (hook) UnhookWindowsHookEx(hook);
    std::wcout << std::format(L"response={} result={:08X}, connected={}, playing={}, in graph={}, ASIO={}, driver={}, frames={}, errors={:08X}\n",
        sent!=0,UINT(result->result),result->live.connected,result->live.playback==State_Running,result->live.in_graph,result->live.engine.opened,
        result->live.engine.driver_name,result->live.engine.delivered_frames,UINT(result->live.engine.error));
    for (UINT i=0;i<result->filters;++i) std::wcout << L"Filter: " << result->filter_names[i] << L"\n";
    const bool passed=sent && SUCCEEDED(result->result) && result->live.connected && result->live.engine.opened && result->live.engine.delivered_frames>0 && SUCCEEDED(result->live.engine.error);
    UnmapViewOfFile(result); FreeLibrary(dll);
    return argc>3 && !passed ? 1 : 0;
}
