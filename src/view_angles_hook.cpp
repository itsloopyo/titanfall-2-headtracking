#include "view_angles_hook.h"

#include <Windows.h>

#include <cstdint>

#include "cameraunlock/hooks/hook_manager.h"
#include "build_profile.h"
#include "camera_hook.h"
#include "debug_log.h"
#include "source_angles.h"

namespace headtracking {

namespace {

// void BuildRenderView(CViewSetup* setup, renderView* out)
//
// The frame's camera is born here. It reads the setup's origin and QAngle and
// writes the view matrix, the projection, the view-projection and the field of
// view tangents into the render view - everything downstream is derived from
// what this call produces, including the copy that becomes the world view and
// the frustum the engine culls against.
using BuildViewFn = void(*)(void* setup, void* out);
BuildViewFn g_original = nullptr;

bool g_released = false;

// The clean angles of the frame currently being drawn: the setup's own QAngle,
// read on the way past and put straight back before this returns.
float g_clean[3] = { 0.0f, 0.0f, 0.0f };

float* SetupAngles(void* setup) {
    return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(setup)
                                    + ActiveProfile().offsets.setup_angles);
}

// Split out so the __try has no C++ objects to unwind past. The guard is for the
// build where the offset has moved: a fault here would take the render thread
// with it, and the answer "do not rotate this frame" costs one un-tracked frame.
bool RotateSetup(void* setup, const FrameRotation& rot, float saved[3]) {
    __try {
        float* ang = SetupAngles(setup);
        saved[0] = ang[0];
        saved[1] = ang[1];
        saved[2] = ang[2];
        g_clean[0] = ang[0];
        g_clean[1] = ang[1];
        g_clean[2] = ang[2];
        if (rot.worldSpaceYaw) {
            ApplyWorldSpaceRotation(ang, rot.dpitch, rot.dyaw, rot.droll);
        } else {
            ApplyCameraLocalRotation(ang, rot.dpitch, rot.dyaw, rot.droll);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The restore is not optional. The setup struct is the game's, and leaving the
// head delta in it would hand the rotation to whatever reads it next.
void RestoreSetup(void* setup, const float saved[3]) {
    __try {
        float* ang = SetupAngles(setup);
        ang[0] = saved[0];
        ang[1] = saved[1];
        ang[2] = saved[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void CaptureClean(void* setup) {
    __try {
        const float* ang = SetupAngles(setup);
        g_clean[0] = ang[0];
        g_clean[1] = ang[1];
        g_clean[2] = ang[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void Hook_BuildRenderView(void* setup, void* out) {
    // This runs for every view the frame builds, and only the player's are ours:
    // a shadow cascade or a reflection built through here is not a camera anyone
    // looks through. The main view is the one the world view is then copied from,
    // so it is also where the frame's pose is decided.
    const bool isMain = (out != nullptr && out == PlayerMainView());
    const bool isSkybox = (out != nullptr && out == PlayerSkyboxView());
    if (g_released || !setup || (!isMain && !isSkybox)) {
        g_original(setup, out);
        return;
    }

    // BeginFrame pulls the frame's tracker sample, so it runs on the main view
    // only - the skybox is part of the same frame and takes the same pose.
    const FrameRotation rot = isMain ? BeginFrame() : LastFrameRotation();

    float saved[3];
    bool rotated = false;
    if (rot.tracking && !rot.Idle()) {
        rotated = RotateSetup(setup, rot, saved);
    } else if (isMain) {
        CaptureClean(setup);
    }

    g_original(setup, out);

    if (rotated) RestoreSetup(setup, saved);
}

}  // namespace

const float* CleanViewAngles() { return g_clean; }

void ReleaseViewAngles() { g_released = true; }

bool InstallViewAnglesHook() {
    void* target = reinterpret_cast<void*>(ClientBase() + ActiveProfile().offsets.view_build_rva);
    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    const auto st = hooks.CreateHook(target, reinterpret_cast<void*>(&Hook_BuildRenderView),
                                     reinterpret_cast<void**>(&g_original));
    if (st != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[view] CreateHook(BuildRenderView) failed: %s",
               cameraunlock::hooks::HookStatusToString(st));
        return false;
    }
    if (hooks.EnableHook(target) != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[view] EnableHook(BuildRenderView) failed");
        return false;
    }
    HT_LOG("[view] view-build hook installed at %p; the head rotation goes into the angles the "
           "frame's camera is built from, so the engine culls to where the head is looking",
           target);
    return true;
}

}  // namespace headtracking
