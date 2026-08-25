#include "crosshair_hook.h"

#include <Windows.h>

#include <cstdint>

#include "cameraunlock/hooks/hook_manager.h"
#include "build_profile.h"
#include "debug_log.h"
#include "rui_transform.h"

namespace headtracking {

namespace {

// void FillCrosshairArgs(RuiInstance*, C_BaseEntity* weapon, C_Player*,
//                        uint16_t* argBase, uint32_t)
using FillArgsFn = void(*)(void* inst, void* ent, void* player, uint16_t* argBase, uint32_t u);
FillArgsFn g_original = nullptr;

// The three values the game's crosshair-state global takes, in the order the
// CROSSHAIR_STATE_* script constants are registered. HIT_INDICATORS_ONLY is the
// one to want when the crosshair has to go: it keeps the hit markers, which are
// feedback the player would otherwise lose for no reason.
constexpr uint32_t kCrosshairShowAll = 0;
constexpr uint32_t kCrosshairHitIndicatorsOnly = 1;

// Written by the render hook, read in the detour. Both run on the render thread,
// inside the same RenderView call - the crosshair submitter is called from it -
// so there is nothing here to synchronise.
bool g_visible = false;
bool g_offScreen = false;
float g_ndcX = 0.0f;
float g_ndcY = 0.0f;

// True while the game's crosshair is hidden BY US. Without it, restoring would
// be a blind write of SHOW_ALL over whatever the game had put there.
bool g_hiding = false;

void SetGameCrosshairHidden(bool hide) {
    if (hide == g_hiding) return;
    auto* state = reinterpret_cast<uint32_t*>(ClientBase()
                                              + ActiveProfile().offsets.crosshair_state);
    if (hide) {
        // Only ours to take when the game is showing the crosshair. If a script
        // has already hidden the HUD for a cutscene, leave that alone.
        if (*state != kCrosshairShowAll) return;
        *state = kCrosshairHitIndicatorsOnly;
        g_hiding = true;
    } else {
        if (*state == kCrosshairHitIndicatorsOnly) *state = kCrosshairShowAll;
        g_hiding = false;
    }
}

void Hook_FillCrosshairArgs(void* inst, void* ent, void* player, uint16_t* argBase, uint32_t u) {
    g_original(inst, ent, player, argBase, u);
    if (!g_visible || !inst) return;

    const auto& off = ActiveProfile().offsets;
    auto* transform = *reinterpret_cast<float**>(reinterpret_cast<uint8_t*>(inst)
                                                 + off.rui_instance_transform);
    const auto* size = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(inst)
                                                      + off.rui_instance_size);
    if (!transform) return;

    // The frame's render size, straight off the instance - not the window's, and
    // not a cvar. The offset has to be in the pixels this frame was rendered at
    // or it lands somewhere else at any resolution but the one it was tuned on.
    float* origin = transform + off.rui_transform_origin / sizeof(float);
    RuiNdcToPixels(g_ndcX, g_ndcY, size[0], size[1], origin[0], origin[1]);
}

}  // namespace

void PublishAim(bool visible, bool offScreen, float ndcX, float ndcY) {
    g_visible = visible && !offScreen;
    g_offScreen = offScreen;
    g_ndcX = ndcX;
    g_ndcY = ndcY;
    // Turned so far that the gun is behind the picture there is no honest place
    // to put the crosshair: it would have to be drawn at an edge it is not
    // pointing at. Hide it instead, and let it come back with the head.
    SetGameCrosshairHidden(visible && offScreen);
}

bool InstallCrosshairHook() {
    void* target = reinterpret_cast<void*>(ClientBase()
                                           + ActiveProfile().offsets.crosshair_args_rva);
    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    const auto st = hooks.CreateHook(target, reinterpret_cast<void*>(&Hook_FillCrosshairArgs),
                                     reinterpret_cast<void**>(&g_original));
    if (st != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[crosshair] CreateHook failed: %s",
               cameraunlock::hooks::HookStatusToString(st));
        return false;
    }
    if (hooks.EnableHook(target) != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[crosshair] EnableHook failed");
        return false;
    }
    HT_LOG("[crosshair] hook installed at %p; the game's own crosshair now follows the gun "
           "instead of the head", target);
    return true;
}

}  // namespace headtracking
