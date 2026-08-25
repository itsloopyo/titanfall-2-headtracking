#pragma once

namespace headtracking {

// The head rotation the frame is drawn with, decided once at the top of the
// client's render phase.
//
// ---- Why the rotation goes in here and not in the render hook ---------------
//
// The engine decides what is VISIBLE long before RenderView runs. The frame's
// camera is built once, from a setup struct holding an origin and a QAngle; the
// world view is a wholesale copy of the result, and the frustum follows from it.
// A rotation applied later - in CViewRender::RenderView, where the mod used to
// put it - moves the picture but cannot un-cull what was already thrown away, so
// turning the head far enough showed holes where walls and terrain should be.
//
// Widening the cone instead of aiming it was tried and shipped, and it is the
// wrong lever. `cl_fovScale` widens the frustum in TANGENT, and a wide monitor
// is already far up the tangent curve: at 32:9 the drawn cone is about 123
// degrees across, so trebling the tangent buys roughly 22 degrees of turn each
// side while dragging the projection out past 165 degrees, where the near plane
// and the depth range stop behaving and the frame flickers. It also submits
// several times the geometry for a picture that is then scaled straight back
// down.
//
// So the frustum is AIMED instead. The player's view angles are rotated by the
// head delta for exactly the span of the client's render phase and restored
// before it returns, so the frame's view - and every frustum, render view and
// skybox basis built from it - points where the head is looking. Nothing is
// widened, nothing extra is submitted, and the headroom is the same at any
// aspect ratio, because the cone is not being stretched to reach.
//
// The engine's own copy of the angles is untouched outside that span. Aim,
// projectile spawning and world traces all run outside it, on the clean value,
// so look and aim stay decoupled exactly as before.
struct FrameRotation {
    bool tracking = false;
    bool worldSpaceYaw = true;
    float dpitch = 0.0f, dyaw = 0.0f, droll = 0.0f;

    bool Idle() const { return dpitch == 0.0f && dyaw == 0.0f && droll == 0.0f; }
};

// Detours the function that builds the frame's camera. Returns false if the hook
// could not be installed, in which case nothing rotates the view at all - say so
// rather than quietly shipping a mod that only leans.
bool InstallViewAnglesHook();

// The angles the frame's view was built from, BEFORE the head delta went on.
// This is the aim, and it is the only place it survives: once the rotation is in
// the view the render hook is handed, the camera it can decode is the rotated
// one. Valid only while a frame is being drawn.
const float* CleanViewAngles();

// Stops rotating for the rest of the process. Called when the render hook
// faults: the two halves are one camera, and a mod that has declared itself off
// must not leave the frustum aimed at a pose nothing is drawing any more.
void ReleaseViewAngles();

}  // namespace headtracking
