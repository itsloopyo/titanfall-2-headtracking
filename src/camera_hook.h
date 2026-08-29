#pragma once

#include "view_angles_hook.h"

namespace headtracking {

// Decides the frame's pose: pulls a tracker sample, runs it through the campaign
// gate and the ADS fade, and returns the rotation half of the answer.
//
// Called once per frame from the render-phase hook, which is upstream of
// everything - the culling frustum, the render views, the skybox. The rotation
// goes in there, and the render hook below draws with the SAME sample rather
// than taking its own: two samples of a live tracker are two different poses,
// and the frame would then be culled to one and drawn to the other, leaving a
// sliver of missing world down whichever edge the head was moving towards.
FrameRotation BeginFrame();

// The same decision, taken from the Titan cockpit hook instead. That hook runs
// EARLIER in the frame than the view build - the cockpit is placed at SetUpView
// + 0x247, the camera is built at + 0x9fc - so when there is a cockpit it is the
// first thing in the frame that needs the pose, and BeginFrame above then reuses
// what this decided rather than taking a second sample.
FrameRotation OpenFrame();

// The rotation BeginFrame last decided, for the second view of the same frame -
// the 3D skybox is built from its own origin but has to take the head pose the
// world took, or the sky and the ground disagree about where the player looked.
FrameRotation LastFrameRotation();

// The frame's own view structs, which is how the view-build hook tells the
// player's camera apart from a shadow cascade or a reflection. Null until the
// render hook has seen a frame.
void* PlayerMainView();
void* PlayerSkyboxView();

// Installs a MinHook detour on CViewRender::RenderView (client.dll).
//
// The detour writes the positional lean into the frame's render view structs and
// scales their projections back to the field of view the frame is meant to be
// drawn at. The head ROTATION is not applied here - it went into the angles the
// whole view was built from, before any of this existed (view_angles_hook.h).
// The game's own view angles, which aim, projectile spawning and raycasts read,
// are back to their clean value by the time any of that runs, so look and aim
// stay decoupled.
//
// The hook is gated on a PE-fingerprint build-profile registry: it engages only
// on a Titanfall 2 client.dll build it has offsets for, and stays dormant (game
// runs vanilla) on any other build. See camera_hook.cpp.
class CameraHook {
public:
    CameraHook() = default;
    ~CameraHook();

    bool Install();
    void Uninstall();
};

}  // namespace headtracking
