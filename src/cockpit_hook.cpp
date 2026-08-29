#include "cockpit_hook.h"

#include <Windows.h>

#include <cstdint>

#include "cameraunlock/hooks/hook_manager.h"
#include "build_profile.h"
#include "camera_hook.h"
#include "debug_log.h"
#include "source_angles.h"
#include "view_angles_hook.h"

namespace headtracking {

namespace {

using CockpitCalcViewFn = void(*)(void* cockpit, float* origin, float* angles);
CockpitCalcViewFn g_original = nullptr;

// Split out so the __try has no C++ objects to unwind past. `angles` is the
// game's own buffer, handed to us one call up the stack, so it is as safe as the
// frame is - the guard is for the build where CalcView has moved and this is
// some other function's stack.
bool ReadAngles(const float* angles, float out[3]) {
    __try {
        out[0] = angles[0];
        out[1] = angles[1];
        out[2] = angles[2];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteCarried(float* angles, const float clean[3], const float drawn[3]) {
    __try {
        CarryRotation(clean, drawn, angles);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// One line the first time a cockpit is placed, and one more the first time the
// head delta is actually composed onto it. Between them they say the hook found
// its target and that a pose reached it, which is the pair that tells "not in a
// Titan" apart from "in a Titan and the cockpit is not moving".
bool g_loggedCockpit = false;
bool g_loggedTracked = false;

void Hook_CockpitCalcView(void* cockpit, float* origin, float* angles) {
    // The player's view angles, read BEFORE the original turns them into the
    // cockpit's. This is the reference the head delta is composed onto, and it
    // is read here rather than taken from CleanViewAngles() because that hook
    // has not run yet: the cockpit is placed at SetUpView + 0x247 and the
    // frame's camera is built at SetUpView + 0x9fc, in that order, in the same
    // straight line of the same function.
    float clean[3];
    const bool haveClean = ReadAngles(angles, clean);

    g_original(cockpit, origin, angles);

    if (!haveClean) return;

    if (!g_loggedCockpit) {
        g_loggedCockpit = true;
        HT_LOG("[cockpit] Titan cockpit being placed - it will be turned with the head so you "
               "look THROUGH it rather than around the inside of it");
    }

    // The cockpit is held still in the picture by the CAMERA turning under it by
    // the same amount. So it may only be turned while something is actually
    // turning the camera: with the render hook faulted, or never installed on
    // this build, the world is the game's own and a cockpit turning alone would
    // be worse than leaving it where the game put it. PlayerMainView() is null
    // until the render hook has seen a frame, which covers both.
    if (!ViewRotationLive() || !PlayerMainView()) return;

    // Opens the frame if the view build has not already: this hook runs first,
    // so it is the one that decides the pose, and the view build then draws the
    // world with the same sample. Taking a second sample there would place the
    // cockpit from one pose and the camera it has to stay still against from
    // another, which is a cockpit that swims when the head moves.
    const FrameRotation rot = OpenFrame();
    if (!rot.tracking || rot.Idle()) return;

    float drawn[3] = { clean[0], clean[1], clean[2] };
    if (rot.worldSpaceYaw) {
        ApplyWorldSpaceRotation(drawn, rot.dpitch, rot.dyaw, rot.droll);
    } else {
        ApplyCameraLocalRotation(drawn, rot.dpitch, rot.dyaw, rot.droll);
    }

    if (!WriteCarried(angles, clean, drawn)) return;

    if (!g_loggedTracked) {
        g_loggedTracked = true;
        HT_LOG("[cockpit] head pose reaching the cockpit (delta p%.2f y%.2f r%.2f)",
               rot.dpitch, rot.dyaw, rot.droll);
    }
}

}  // namespace

bool InstallCockpitHook() {
    const uint32_t rva = ActiveProfile().offsets.cockpit_calc_view_rva;
    if (rva == 0) {
        HT_LOG("[cockpit] this build profile has no cockpit transform - Titan cockpits are left "
               "alone and head tracking in a Titan looks around the inside of the cockpit");
        return false;
    }

    void* target = reinterpret_cast<void*>(ClientBase() + rva);
    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    const auto st = hooks.CreateHook(target, reinterpret_cast<void*>(&Hook_CockpitCalcView),
                                     reinterpret_cast<void**>(&g_original));
    if (st != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[cockpit] CreateHook(C_Titan_Cockpit::CalcView) failed: %s",
               cameraunlock::hooks::HookStatusToString(st));
        return false;
    }
    if (hooks.EnableHook(target) != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[cockpit] EnableHook(C_Titan_Cockpit::CalcView) failed");
        return false;
    }
    HT_LOG("[cockpit] Titan cockpit hook installed at %p", target);
    return true;
}

}  // namespace headtracking
