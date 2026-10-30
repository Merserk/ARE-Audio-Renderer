#include "tools/mpc_watcher.hpp"
#include "win/mpc_ui.hpp"
#include "win/common.hpp"
#include <filesystem>
#include <vector>

namespace {
using namespace are::win;
struct Target { HWND root; DWORD thread, process; HHOOK before{}, after{}; };
constexpr UINT rescan_message = WM_APP + 1;
DWORD watcher_thread{};
bool rescan_pending{};
void CALLBACK window_shown(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD) {
    if (!window || object != OBJID_WINDOW || child != CHILDID_SELF || rescan_pending) return;
    // Out-of-context WinEvents are delivered on the installer's message loop.
    // Coalesce bursts of child-window creation; do not inspect another UI here.
    rescan_pending = PostThreadMessageW(watcher_thread, rescan_message, 0, 0) != FALSE;
}
bool matching_player(DWORD id) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id));
    if (!process) return false;
    wchar_t path[32768]{}; DWORD length = DWORD(std::size(path));
    if (!QueryFullProcessImageNameW(process.get(), 0, path, &length)) return false;
    const auto name = std::filesystem::path(path).filename().wstring();
    if (_wcsicmp(name.c_str(), L"mpc-hc.exe") && _wcsicmp(name.c_str(), L"mpc-hc64.exe")) return false;
    USHORT machine{}, native{};
    if (!IsWow64Process2(process.get(), &machine, &native)) return false;
#ifdef _WIN64
    return machine == IMAGE_FILE_MACHINE_UNKNOWN && native == IMAGE_FILE_MACHINE_AMD64;
#else
    return machine == IMAGE_FILE_MACHINE_I386 || (machine == IMAGE_FILE_MACHINE_UNKNOWN && native == IMAGE_FILE_MACHINE_I386);
#endif
}
BOOL CALLBACK find_player(HWND window, LPARAM data) {
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    DWORD id{}; const auto thread = GetWindowThreadProcessId(window, &id);
    if (!matching_player(id)) return TRUE;
    auto& targets = *reinterpret_cast<std::vector<Target>*>(data);
    if (std::none_of(targets.begin(), targets.end(), [&](const Target& item) { return item.thread == thread && item.process == id; }))
        targets.push_back({window, thread, id});
    return TRUE;
}
void detach(Target& target, UINT message) {
    DWORD_PTR result{};
    DWORD process{};
    if (GetWindowThreadProcessId(target.root, &process) == target.thread && process == target.process)
        SendMessageTimeoutW(target.root, message, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result);
    if (target.after) UnhookWindowsHookEx(target.after);
    if (target.before) UnhookWindowsHookEx(target.before);
}
void attach(const Target& target, UINT message) {
    DWORD_PTR result{};
    SendMessageTimeoutW(target.root, message, 1, 0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result);
}
}
int stop_mpc_settings() {
    are::win::Handle event(OpenEventW(EVENT_MODIFY_STATE, FALSE, are::win::mpc::stop_name));
    return event && !SetEvent(event.get()) ? 1 : 0;
}
int watch_mpc_settings() {
    using namespace are::win;
    Handle mutex(CreateMutexW(nullptr, FALSE, mpc::mutex_name));
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, mpc::stop_name));
    if (!stop) return 1;
    wchar_t executable[32768]{};
    if (!GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)))) return 1;
    const auto dll = std::filesystem::path(executable).parent_path() / L"ARE-Audio-Renderer.dll";
    const auto module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) return 1;
    const auto callback = reinterpret_cast<HOOKPROC>(GetProcAddress(module, "AREMpcSettingsHook"));
    const auto returned = reinterpret_cast<HOOKPROC>(GetProcAddress(module, "AREMpcSettingsReturnHook"));
    if (!callback || !returned) { FreeLibrary(module); return 1; }
    const auto message = RegisterWindowMessageW(mpc::discover_message);
    watcher_thread = GetCurrentThreadId();
    MSG initial{}; PeekMessageW(&initial, nullptr, 0, 0, PM_NOREMOVE);
    const auto shown = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, window_shown,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    std::vector<Target> attached;
    // A Windows hook installer must service its message queue, including when
    // Windows delivers hook work back to the installing thread.
    const HANDLE events[]{stop.get()};
    auto next_scan = GetTickCount64();
    for (;;) {
        const auto now = GetTickCount64();
        const auto wait = MsgWaitForMultipleObjectsEx(1, events,
            DWORD(next_scan > now ? next_scan - now : 0), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        MSG pending{};
        while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
            if (pending.message == rescan_message) { rescan_pending = false; next_scan = 0; continue; }
            TranslateMessage(&pending); DispatchMessageW(&pending);
        }
        if (GetTickCount64() < next_scan) continue;
        // Only a safety fallback. Output creation and selection are handled
        // synchronously inside the already-watched player's UI thread.
        next_scan = GetTickCount64() + 2000;
        std::vector<Target> found;
        EnumWindows(find_player, reinterpret_cast<LPARAM>(&found));
        for (auto it = attached.begin(); it != attached.end();) {
            if (std::none_of(found.begin(), found.end(), [&](const Target& target) {
                return target.root == it->root &&
                    target.thread == it->thread && target.process == it->process;
            })) { detach(*it, message); it = attached.erase(it); } else ++it;
        }
        for (auto& target : found) {
            const auto exists = std::find_if(attached.begin(), attached.end(), [&](const Target& item) {
                return item.root == target.root && item.thread == target.thread && item.process == target.process;
            });
            if (exists != attached.end()) continue;
            target.before = SetWindowsHookExW(WH_CALLWNDPROC, callback, module, target.thread);
            target.after = SetWindowsHookExW(WH_CALLWNDPROCRET, returned, module, target.thread);
            if (target.before && target.after) {
                attached.push_back(target);
                attach(target, message);
            } else detach(target, message);
        }
        // Recover if the player's thread was temporarily hung during setup.
        for (const auto& target : attached) if (!GetPropW(target.root, mpc::watching_property))
            attach(target, message);
    }
    if (shown) UnhookWinEvent(shown);
    for (auto& target : attached) detach(target, message);
    FreeLibrary(module);
    return 0;
}
