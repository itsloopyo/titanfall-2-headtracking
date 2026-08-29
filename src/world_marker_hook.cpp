#include "world_marker_hook.h"

#include <Windows.h>

#include <atomic>

#include "cameraunlock/hooks/hook_manager.h"
#include "build_profile.h"
#include "debug_log.h"

namespace headtracking {

namespace {

// int WorldToScreen(int* outX, int* outY, const Vector& world, int width,
//                   int height, int offsetX, int offsetY)
//
// Returns non-zero when the point is behind the camera, in which case it writes
// the projection scaled by 100000 rather than a screen position - the callers
// use that to push a marker far enough off screen to be clamped rather than
// drawn.
using WorldToScreenFn = int (*)(int*, int*, const float*, int, int, int, int);
WorldToScreenFn g_original = nullptr;

// The frame's two cameras. Written by the render thread once a frame, read on
// whichever thread asked where a world point is - the client script's, for the
// script native above this.
//
// `g_valid` is released after the fields and cleared before them, so a reader
// that sees it set sees a complete frame. The fields can still be overwritten by
// the next frame while a reader is part way through one, which puts that marker
// a fraction of a head-turn out for a single frame - the same race the game's
// own per-frame HUD data already has, and invisible at any head speed a neck
// produces.
FrameCameras g_cameras;
std::atomic<bool> g_valid{false};

// Rate limit for the diagnostic line below. Per-frame markers are numerous - the
// campaign HUD asks for several every frame - so this is a sample, not a trace.
constexpr ULONGLONG kDiagnosticIntervalMs = 1000;
ULONGLONG g_lastDiagnostic = 0;

// One line saying the correction is live and by how much, sampled about once a
// second while `[Debug] LogToFile` is on.
//
// `game` is where the game would have put the mark and `tracked` is where it now
// goes; with the head centred the two are the same pixel, and the gap between
// them is the head pose. That pair is the whole check: a marker that has not
// moved is either a pose of zero or a correction that is not reaching the path
// the marker is drawn through, and the log tells the two apart without a
// screenshot.
void SampleDiagnostic(const float* world, const float* moved, int trackedX, int trackedY,
                      int w, int h, int offX, int offY) {
    if (!VerboseLogging()) return;
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastDiagnostic < kDiagnosticIntervalMs) return;
    g_lastDiagnostic = now;

    int gameX = 0, gameY = 0;
    const int behind = g_original(&gameX, &gameY, world, w, h, offX, offY);
    HT_TRACE("[marker] world=(%.1f,%.1f,%.1f) game=(%d,%d) tracked=(%d,%d) behind=%d "
             "moved=(%.1f,%.1f,%.1f)",
             world[0], world[1], world[2], gameX, gameY, trackedX, trackedY, behind,
             moved[0], moved[1], moved[2]);
}

// One line, the first time the game asks where a world point is on screen, and
// written whatever `[Debug] LogToFile` says.
//
// It answers the question a screenshot cannot: whether the HUD mark a player is
// looking at is placed through this function at all. Without it a mark that has
// not moved has two explanations - the correction is wrong, or the mark is drawn
// through some other path entirely - and they need different work.
bool g_loggedFirstCall = false;

int Hook_WorldToScreen(int* outX, int* outY, const float* world, int w, int h,
                       int offX, int offY) {
    if (!g_loggedFirstCall) {
        g_loggedFirstCall = true;
        HT_LOG("[marker] the game placed its first world-anchored HUD mark through the hooked "
               "world-to-screen - marks on this path now follow the world");
    }
    if (!g_valid.load(std::memory_order_acquire) || !world || !outX || !outY) {
        return g_original(outX, outY, world, w, h, offX, offY);
    }

    float moved[3];
    ReprojectWorldPoint(g_cameras, world, moved);
    const int behind = g_original(outX, outY, moved, w, h, offX, offY);
    SampleDiagnostic(world, moved, *outX, *outY, w, h, offX, offY);
    return behind;
}

}  // namespace

void PublishFrameCameras(const FrameCameras* cameras) {
    if (!cameras) {
        g_valid.store(false, std::memory_order_release);
        return;
    }
    g_valid.store(false, std::memory_order_relaxed);
    g_cameras = *cameras;
    g_valid.store(true, std::memory_order_release);
}

bool InstallWorldMarkerHook() {
    const uint32_t rva = ActiveProfile().offsets.world_to_screen_rva;
    if (rva == 0) {
        HT_LOG("[marker] no world-to-screen offset for this build - HUD markers stay where the "
               "game puts them, which is where they would be with the head centred");
        return false;
    }
    void* target = reinterpret_cast<void*>(ClientBase() + rva);
    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    const auto st = hooks.CreateHook(target, reinterpret_cast<void*>(&Hook_WorldToScreen),
                                     reinterpret_cast<void**>(&g_original));
    if (st != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[marker] CreateHook failed: %s", cameraunlock::hooks::HookStatusToString(st));
        return false;
    }
    if (hooks.EnableHook(target) != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[marker] EnableHook failed");
        return false;
    }
    HT_LOG("[marker] world marker hook installed at %p - HUD marks follow the world", target);
    return true;
}

}  // namespace headtracking
