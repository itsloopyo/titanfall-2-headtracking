#include "ads_state.h"

#include <Windows.h>

#include <cstdint>

#include "cameraunlock/hooks/hook_manager.h"
#include "build_profile.h"
#include "debug_log.h"

namespace headtracking {

namespace {

using SubmitCrosshairFn = void(*)(void* player);
SubmitCrosshairFn g_original = nullptr;

// Written and read on the render thread only: the submitter is called from
// RenderView, and so is everything that asks.
void* g_player = nullptr;

void Hook_SubmitCrosshair(void* player) {
    g_player = player;
    g_original(player);
}

}  // namespace

bool PlayerIsAiming() {
    if (!g_player || !HasActiveProfile()) return false;
    const auto* flag = reinterpret_cast<const uint8_t*>(g_player)
                     + ActiveProfile().offsets.player_ads_flag;
    // The player pointer is the game's own, handed to us one call up the stack,
    // so this read is as safe as the frame is. The guard is for the build where
    // the offset is wrong: a fault here would otherwise take the render thread
    // with it, and the answer "not aiming" costs nothing.
    __try {
        return *flag != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InstallAdsStateHook() {
    void* target = reinterpret_cast<void*>(ClientBase()
                                           + ActiveProfile().offsets.crosshair_submit_rva);
    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    const auto st = hooks.CreateHook(target, reinterpret_cast<void*>(&Hook_SubmitCrosshair),
                                     reinterpret_cast<void**>(&g_original));
    if (st != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[ads] CreateHook failed: %s", cameraunlock::hooks::HookStatusToString(st));
        return false;
    }
    if (hooks.EnableHook(target) != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[ads] EnableHook failed");
        return false;
    }
    HT_LOG("[ads] sights detector installed at %p; aiming is read from the game's own flag, so "
           "it works on weapons whose sights do not magnify", target);
    return true;
}

}  // namespace headtracking
