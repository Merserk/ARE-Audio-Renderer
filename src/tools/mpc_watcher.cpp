#include "tools/mpc_watcher.hpp"
#include "win/mpc_ui.hpp"
#include "win/common.hpp"
#include <filesystem>
#include <vector>

namespace {
using namespace are::win;
struct Target { HWND page, combo, button; DWORD thread; HHOOK hook{}; };
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
BOOL CALLBACK find_combo(HWND window, LPARAM data) {
    if (!IsWindowVisible(window) || !mpc::class_is(window, L"ComboBox")) return TRUE;
    if (auto button = mpc::settings_button(window)) {
        auto& targets = *reinterpret_cast<std::vector<Target>*>(data);
        targets.push_back({GetParent(window), window, button, GetWindowThreadProcessId(window, nullptr)});
    }
    return TRUE;
}
BOOL CALLBACK find_player(HWND window, LPARAM data) {
    if (!IsWindowVisible(window)) return TRUE;
    DWORD id{}; GetWindowThreadProcessId(window, &id);
    if (matching_player(id)) EnumChildWindows(window, find_combo, data);
    return TRUE;
}
void detach(Target& target, UINT message) {
    DWORD_PTR result{};
    SendMessageTimeoutW(target.page, message, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result);
    if (target.hook) UnhookWindowsHookEx(target.hook);
}
void attach(const Target& target, UINT message) {
    DWORD_PTR result{};
    SendMessageTimeoutW(target.page, message, reinterpret_cast<WPARAM>(target.combo), reinterpret_cast<LPARAM>(target.button),
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
    if (!callback) { FreeLibrary(module); return 1; }
    const auto message = RegisterWindowMessageW(mpc::attach_message);
    std::vector<Target> attached;
    while (WaitForSingleObject(stop.get(), 750) == WAIT_TIMEOUT) {
        std::vector<Target> found;
        EnumWindows(find_player, reinterpret_cast<LPARAM>(&found));
        for (auto it = attached.begin(); it != attached.end();) {
            if (std::none_of(found.begin(), found.end(), [&](const Target& target) {
                return target.page == it->page && target.combo == it->combo && target.button == it->button && target.thread == it->thread;
            })) { detach(*it, message); it = attached.erase(it); } else ++it;
        }
        for (auto& target : found) {
            const auto exists = std::find_if(attached.begin(), attached.end(), [&](const Target& item) { return item.page == target.page; });
            if (exists != attached.end()) continue;
            target.hook = SetWindowsHookExW(WH_CALLWNDPROC, callback, module, target.thread);
            if (target.hook) {
                attached.push_back(target);
                attach(target, message);
            }
        }
        // Retry attachment if the player's page was briefly busy during discovery.
        for (const auto& target : attached) if (!GetPropW(target.page, mpc::binding_property))
            attach(target, message);
    }
    for (auto& target : attached) detach(target, message);
    FreeLibrary(module);
    return 0;
}
