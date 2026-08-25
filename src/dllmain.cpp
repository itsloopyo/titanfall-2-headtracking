#include "pch.h"

#include "plugin.h"
#include "debug_log.h"
#include "version.h"

namespace {

DWORD WINAPI BootstrapThread(LPVOID) {
    headtracking::OpenLogFile();
    HT_LOG("[main] Titanfall2HeadTracking %s loaded into pid %lu",
           HEADTRACKING_VERSION_STRING, GetCurrentProcessId());

    headtracking::GetPlugin().Initialize();
    return 0;
}

// Makes FreeLibrary on this module a no-op, which is what lets DLL_PROCESS_DETACH
// do nothing at all.
//
// There is no safe teardown from DllMain. Both detach cases hold the loader
// lock, and everything a teardown would have to do is forbidden while holding
// it: joining the hotkey poller and the UDP receiver threads deadlocks, because
// those threads cannot exit without taking the loader lock themselves to run
// DLL_THREAD_DETACH for every other module (DisableThreadLibraryCalls only
// suppressed that for ours), and MinHook's unhook suspends every other thread
// while we hold a lock some of them may be waiting on. Meanwhile the detour
// body lives in this image, so an in-flight render thread would be executing
// code that is about to be unmapped, and no amount of trampoline-preservation
// helps with that.
//
// Pinning removes the whole question: the module stays mapped, the hook stays
// live and valid for the life of the process, and the OS reclaims everything at
// exit. An .asi is loaded once at startup and is not meant to come back out.
void PinSelf(HMODULE self) {
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       reinterpret_cast<LPCWSTR>(self), &pinned);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    UNREFERENCED_PARAMETER(reserved);
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);
            PinSelf(hModule);
            // The thread cannot run until DllMain returns and the loader lock is
            // released, which is exactly why the real work goes on it.
            const HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
            if (thread) {
                CloseHandle(thread);
            } else {
                // The log file is not open yet - it is opened on that thread -
                // so this is the only channel left. Without it a failure here
                // is a mod that loads and then does nothing, silently.
                OutputDebugStringA("Titanfall2HeadTracking: bootstrap thread could not start\n");
            }
            break;
        }

        case DLL_PROCESS_DETACH:
            // Nothing. See PinSelf.
            break;
    }
    return TRUE;
}
