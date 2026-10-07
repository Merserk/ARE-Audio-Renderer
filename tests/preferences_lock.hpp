#pragma once
#include "win/common.hpp"
#include <stdexcept>

namespace are::test {
// Integration/measurement programs temporarily save per-user preferences and
// register the simulated driver. Serialize processes of the same architecture
// before taking their snapshots, including independently built source trees.
struct PreferencesLock {
#ifdef _WIN64
    win::Handle mutex{CreateMutexW(nullptr,FALSE,L"Local\\AREAudioRenderer.Validation.x64")};
#else
    win::Handle mutex{CreateMutexW(nullptr,FALSE,L"Local\\AREAudioRenderer.Validation.x86")};
#endif
    PreferencesLock() {
        if (!mutex) throw std::runtime_error("Cannot create test preferences lock");
        const auto result=WaitForSingleObject(mutex.get(),INFINITE);
        if (result!=WAIT_OBJECT_0 && result!=WAIT_ABANDONED) throw std::runtime_error("Cannot acquire test preferences lock");
    }
    ~PreferencesLock() { ReleaseMutex(mutex.get()); }
};
} // namespace are::test
