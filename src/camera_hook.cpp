// Render-view injection for Titanfall 2 (Respawn's modified Source Engine,
// client.dll, x64).
//
// Hook target: CViewRender::RenderView(renderView*, int clearFlags,
// int whatToDraw)  [MS x64 ABI member call: this in RCX].
//
// Respawn's render view struct is matrix-based, not stock Source's
// origin-plus-QAngle CViewSetup: by the time RenderView runs, the origin
// (+0x00), the 3x4 camera basis (+0x10), the view matrix (+0x40) and the
// view-projection (+0xc0) are all built. The camera is changed by rewriting
// them, not by editing an angle field.
//
// And it is changed in THREE structs, not one. CViewRender::Render preps three
// render views and hands RenderView the third; that one turns out to drive only
// the viewmodel, while the world is drawn from the sibling at CViewRender +
// 0xa13c0 and the 3D skybox from the one at + 0xb55c0. Rewriting only the
// struct RenderView is handed moves the weapon and leaves the world perfectly
// still - which is exactly what it did until this was found.
//
// This hook writes the positional LEAN into those structs. The head ROTATION is
// not applied here and has not been since the culling was fixed: the engine
// decides what is visible before this runs, so a rotation applied at this point
// is drawn through a cone that has already discarded whatever the head turned to
// look at. It goes in upstream instead, into the view angles the whole frame is
// built from (view_angles_hook.h), which aims the frustum as well as the
// picture. So the views arrive here already rotated, and what is left to do is
// the lean and the field of view.
//
// The game's own view angles are back to their clean value before anything that
// reads them for aim, projectile spawning or traces runs, so look and aim
// decouple exactly as they did when the rotation lived here.
//
// The game draws its crosshair in screen space, so it marks where shots land
// only while the view is unrotated. It is moved onto the aim instead
// (crosshair_hook.h), and with the sights up the head delta is faded out and the
// view settles back onto the gun - see the ADS block below.
//
// Engagement is gated twice over. The build-profile registry (see
// build_profile.cpp) means the hook is only installed on a Titanfall 2 build we
// have measured; the level-name gate (game_state.cpp) means it only applies on
// campaign maps, never in multiplayer.

#include "camera_hook.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "cameraunlock/hooks/hook_manager.h"
#include "ads.h"
#include "ads_blend.h"
#include "ads_gate.h"
#include "ads_marker.h"
#include "ads_state.h"
#include "aim_projection.h"
#include "aim_trace.h"
#include "angle_units.h"
#include "build_profile.h"
#include "debug_log.h"
#include "fov_control.h"
#include "game_state.h"
#include "plugin.h"
#include "crosshair_hook.h"
#include "hit_indicator.h"
#include "source_angles.h"
#include "view_matrix.h"
#include "world_marker_hook.h"

namespace headtracking {

namespace {

// ----- Reaching the render view's fields --------------------------------------
inline float* Field(void* view, uint32_t offset) {
    return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(view) + offset);
}
inline float* Origin(void* view)         { return Field(view, ActiveProfile().offsets.origin); }
inline float* Basis(void* view)          { return Field(view, ActiveProfile().offsets.basis); }
inline float* ViewMatrix(void* view)     { return Field(view, ActiveProfile().offsets.view_matrix); }
inline float* ProjMatrix(void* view)     { return Field(view, ActiveProfile().offsets.proj_matrix); }
inline float* ViewProjMatrix(void* view) { return Field(view, ActiveProfile().offsets.viewproj_matrix); }
inline float* TanFov(void* view)         { return Field(view, ActiveProfile().offsets.tan_fov); }

// The three render view structs, by their offsets from the CViewRender `this`
// pointer. Named rather than indexed because which is which is load-bearing:
// only the main one carries the zoom field, and the skybox deliberately takes
// no lean.
inline void* MainView(void* self) {
    return reinterpret_cast<uint8_t*>(self) + ActiveProfile().offsets.main_view;
}
inline void* WorldView(void* self) {
    return reinterpret_cast<uint8_t*>(self) + ActiveProfile().offsets.world_view;
}
inline void* SkyboxView(void* self) {
    return reinterpret_cast<uint8_t*>(self) + ActiveProfile().offsets.skybox_view;
}

// ----- Tracker -> Source axis mapping ---------------------------------------
//
// Every sign correction between the tracker frame and Source lives here, at the
// engine boundary, never as an INI `Invert*` default: the position processor
// applies inversion BEFORE its asymmetric Z clamp, so an `InvertZ` used to flip
// a convention silently moves the generous forward allowance onto the backward
// lean. The `Invert*` keys stay pure user preference (applied post-clamp, in
// Plugin::Update).
//
// The pipeline delivers the shared-core frame, documented on
// ViewMatrixModifier.ApplyHeadRotation and matched by the Headcam wire format:
//     yaw   > 0 = head turns right    Source yaw   > 0 = turn left   -> negate
//     pitch > 0 = head looks up       Source pitch > 0 = look down   -> negate
//     roll  > 0 = head tilts left     Source roll  > 0 = tilt right  -> negate
//     x     > 0 = head moves right    Source `right` vector          -> as-is
//     y     > 0 = head moves up       Source `up` vector             -> as-is
//     z     > 0 = head leans forward  Source `fwd` vector            -> as-is
//
// The three Source senses are read straight off AngleVectors (source_angles.h):
// at zero angles `right` is (0,-1,0) - Source world +y is left - `up` is +z, and
// a positive roll tips `up` toward -y, i.e. tilts the view to the right.
//
// A TRACKER that sends x or z the other way round is a different question and
// is NOT fixed here: `[Position] InvertX` / `InvertZ` handle that, applied by the
// processor before its asymmetric Z clamp so the generous 0.40 m allowance
// follows the physical forward lean whichever way they are set. Negating a
// position axis here instead would leave the direction right and the travel
// mirrored - 0.10 m one way, 0.40 m the other - which is the failure that
// survives testing. See .lab/NOTES.md, "The Z clamp".
constexpr float kYawSign   = -1.0f;
constexpr float kPitchSign = -1.0f;
constexpr float kRollSign  = -1.0f;

// Writes the whole camera back into the render view struct. Every representation
// of the camera it carries has to move together: the world pass rebuilds itself
// from the origin and basis, the viewmodel pass uses the matrices, and leaving
// either behind renders a frame built from half a camera.
void WriteCamera(void* view, const float fwd[3], const float right[3], const float up[3],
                 const float org[3]) {
    float* origin = Origin(view);
    float* basis = Basis(view);
    float* viewMat = ViewMatrix(view);
    for (int i = 0; i < 3; ++i) {
        origin[i]     = org[i];
        basis[0 + i]  = fwd[i];    // rows are 4 floats apart, the 4th is padding
        basis[4 + i]  = right[i];
        basis[8 + i]  = up[i];
    }
    ComposeView(viewMat, fwd, right, up, org);
    MulMat4(ProjMatrix(view), viewMat, ViewProjMatrix(view));
}

// ----- The hook -------------------------------------------------------------
using RenderViewFn = void(*)(void* self, void* view, int clearFlags, int whatToDraw);
RenderViewFn g_originalRenderView = nullptr;

// Owns the ADS fade and the pose the sights came up on. Render-thread only,
// like everything else reachable from the detour.
AdsFade g_adsFade;
AdsEntryPose g_adsEntry;

// One-shot float/int dump of the struct RenderView is handed, so the field
// offsets can be read off a live frame when a patch moves them. Enabled with
// [Debug] DumpViewSetup=1; see .lab/NOTES.md for how to read it.
// This covers everything the hook uses: origin, basis, all three matrices,
// tanfov and the zoom factor. Dumped 0x200-0x600 once while chasing the culling
// problem, on the theory that a cached view frustum might live there; it is a
// repeating (1.0, 0.0) array, not planes. See .lab/NOTES.md.
constexpr uint32_t kDumpBytes = 0x200;

void DumpOne(const char* what, const uint8_t* bytes) {
    HT_TRACE("[dump] %s @ %p", what, bytes);
    for (uint32_t off = 0; off < kDumpBytes; off += 16) {
        float f[4];
        int32_t i[4];
        std::memcpy(f, bytes + off, sizeof(f));
        std::memcpy(i, bytes + off, sizeof(i));
        HT_TRACE("[dump] +0x%04X f=(%12.4f %12.4f %12.4f %12.4f) i=(%11d %11d %11d %11d)",
                 off, f[0], f[1], f[2], f[3], i[0], i[1], i[2], i[3]);
    }
}

void DumpViewStruct(void* self, void* view) {
    static bool s_done = false;
    if (s_done || !GetPlugin().GetConfig().dump_view_setup) return;
    s_done = true;

    const auto& off = ActiveProfile().offsets;
    const auto* base = reinterpret_cast<const uint8_t*>(self);
    DumpOne("main render view (RenderView arg)", reinterpret_cast<const uint8_t*>(view));
    DumpOne("world view", base + off.world_view);
    DumpOne("skybox view", base + off.skybox_view);
    HT_TRACE("[dump] main view offset from this = 0x%llX (profile says 0x%X)",
             static_cast<unsigned long long>(reinterpret_cast<const uint8_t*>(view) - base),
             off.main_view);
}

// ----- Detecting that the sights are coming up -------------------------------
//
// The render view carries the frame's zoom factor: the player's base FOV tangent
// divided by this frame's, so the FOV slider cancels out and the field reads
// exactly 1.0 with the sights down and rises as they come up. Measured live on
// sp_training: 1.0000 at the hip, 2.6116 through the gauntlet sniper's scope,
// with tanfov * zoom pinned at 0.93361 through every frame of the transition.
//
// Read for the field-of-view measurement and the diagnostic only. Whether the
// player is AIMING is a separate question with a separate answer - see
// ads_state.h - because a weapon can have sights and no magnification, and this
// field cannot see that at all.
constexpr float kZoomHip = 1.01f;
// Outside this the field is not the zoom factor - a render view that was never
// filled in, or a build that moved it.
constexpr float kZoomMin = 1.0f;
constexpr float kZoomMax = 100.0f;

float ZoomFactor(void* view) {
    const float z = *Field(view, ActiveProfile().offsets.zoom);
    // Reading 1.0 on an implausible value leaves tracking exactly as it was
    // rather than declaring ADS blind.
    return (z >= kZoomMin && z < kZoomMax) ? z : 1.0f;
}

// ----- Diagnostics -----------------------------------------------------------
//
// Dense over the first frames (where install-time faults show), then one line
// per ~2000 frames for the rest of the session, because the failures worth
// catching are the late ones - "it drifted after an hour", "it stopped when I
// loaded a save" - which a burst that goes silent cannot see.
constexpr int kDiagBurstLines = 12;      // lines logged before the rate drops
constexpr int kDiagUnthrottledLines = 6; // lines logged with no throttle at all
constexpr int kDiagBurstInterval = 200;  // frames between lines during the burst
constexpr int kDiagSteadyInterval = 2000;

// Confirms the hook fires, the offsets resolve to a sane camera, and the delta
// is being applied.
void DiagnosticLog(const float* org, const float* cleanAng, const float* tanFov,
                   const char* level, bool tracking, float zoom, bool aiming,
                   const char* adsMode,
                   float dpitch, float dyaw, float droll, float ox, float oy, float oz,
                   float ndcX, float ndcY, float aimDist) {
    if (!VerboseLogging()) return;
    static int s_count = 0;
    static int s_frame = 0;
    static bool s_wasTracking = false;
    static bool s_wasAimed = false;
    ++s_frame;
    // Always log the frame tracking starts or stops on, and the frame the sights
    // come up or go down: those are the frames a test wants, and a feed that
    // connects between two throttled samples would otherwise leave no evidence it
    // was ever applied.
    const bool edge = (tracking != s_wasTracking) || (aiming != s_wasAimed);
    s_wasTracking = tracking;
    s_wasAimed = aiming;
    const int interval = (s_count < kDiagBurstLines) ? kDiagBurstInterval : kDiagSteadyInterval;
    if (!edge && s_count >= kDiagUnthrottledLines && (s_frame % interval) != 0) return;
    ++s_count;
    // The angles are the CLEAN camera, decomposed from the view matrix before
    // the delta goes on. They must stay put while a delta is held: if the
    // rotation ever failed to come back out of the engine's angles it would
    // compound here, frame on frame, into a runaway spin. `drawn` is the camera
    // the render views actually carry, so it is `clean` plus the delta - the
    // pair is the whole proof that the rotation went in where it was meant to
    // and nowhere else.
    // fov is the DRAWN field of view in the degrees the game's own slider uses -
    // during ADS the scoped one, because it is what the frame was rendered at
    // rather than what the player asked for. cull is the wider cone the engine
    // built its visibility frustum from, so the gap between the two is how far
    // the head can turn before geometry starts going missing.
    // The crosshair offset is in NDC, so +-1 is the edge of the frame, and the
    // aim distance is what the parallax correction divides by - a crosshair that
    // has wandered is one or the other, and the two are told apart here rather
    // than by eye.
    const float* clean = CleanViewAngles();
    HT_TRACE("[view] map=%s org=(%.1f,%.1f,%.1f) clean=(p%.2f y%.2f r%.2f) "
             "drawn=(p%.2f y%.2f r%.2f) fov=%.1f cull=%.1f tanfov=(%.3f,%.3f) "
             "zoom=%.3f ads=%d/%s | track=%d delta=(p%.2f y%.2f r%.2f) pos=(%.2f,%.2f,%.2f) "
             "aim=(%.3f,%.3f) dist=%.0f",
             level, org[0], org[1], org[2], clean[0], clean[1], clean[2],
             cleanAng[0], cleanAng[1], cleanAng[2],
             GetFovControl().DrawnFovDegrees(), GetFovControl().CulledFovDegrees(),
             tanFov[0], tanFov[1], zoom, aiming ? 1 : 0, adsMode,
             tracking ? 1 : 0, dpitch, dyaw, droll, ox, oy, oz, ndcX, ndcY, aimDist);
}

// One line per state change, so a player who wanders into multiplayer or opens
// the pause menu sees exactly why the mod went quiet, without one line per
// frame.
void LogSuppression(SessionKind kind, const char* level) {
    static SessionKind s_last = SessionKind::NoLevel;
    static bool s_first = true;
    if (!s_first && kind == s_last) return;
    s_first = false;
    s_last = kind;
    switch (kind) {
        case SessionKind::Multiplayer:
            HT_LOG("[state] multiplayer map '%s' - head tracking suppressed "
                   "(decoupled look and aim is an unfair advantage online)", level);
            break;
        case SessionKind::Paused:
            HT_LOG("[state] game paused - head tracking suppressed");
            break;
        case SessionKind::Campaign:
            HT_LOG("[state] campaign map '%s' - head tracking active", level);
            break;
        case SessionKind::Loading:
        case SessionKind::NoLevel:
            break;
    }
}

// A render view struct that has never been filled in holds zeros, and rewriting
// one of those would hand the renderer a degenerate camera. Every region the
// write touches is checked, not just the one: all three view-matrix rows must
// be unit length and the origin must be finite. Checking one field and then
// writing four is how the 0x1d4 crash happened.
constexpr float kUnitLengthLo = 0.9f;
constexpr float kUnitLengthHi = 1.1f;

bool LooksLikeLiveView(void* view) {
    const float* v = ViewMatrix(view);
    for (int r = 0; r < 3; ++r) {
        const float len = v[r * 4 + 0] * v[r * 4 + 0]
                        + v[r * 4 + 1] * v[r * 4 + 1]
                        + v[r * 4 + 2] * v[r * 4 + 2];
        if (!(len > kUnitLengthLo && len < kUnitLengthHi)) return false;  // NaN fails both ways
    }
    const float* o = Origin(view);
    return std::isfinite(o[0]) && std::isfinite(o[1]) && std::isfinite(o[2]);
}

// The sibling views are reached as `this + 0xa13c0` / `+ 0xb55c0`, roughly 700 KB
// blind of a pointer we were handed. If that pointer is ever something other
// than the CViewRender we measured, those become writes into unrelated heap -
// game-state corruption, not render-state, presenting as a crash somewhere else
// entirely. The profile knows where the main view sits within CViewRender, so
// the cheapest proof that `self` is the object we think it is: the struct we
// were handed must be exactly there.
bool ViewBelongsToThis(void* self, void* view) {
    return view == MainView(self);
}

// Applies the head delta to ONE render view struct, in that struct's own frame.
// The delta is applied per struct rather than copying one camera into all of
// them: a sibling view may legitimately be looking somewhere else, and it should
// still end up rotated by the head, not replaced.
//
// `worldSpaceYaw` is passed in rather than read from the plugin here: the hotkey
// thread can flip it at any moment, and re-reading it per view would let a
// keypress land between the world view and the skybox and compose that one frame
// two different ways.
// What the frame was drawn along, and where the gun was pointing while it was.
// Filled from the vectors ApplyToView actually wrote, so the reticle projection
// cannot encode the head composition differently from the camera - see
// aim_projection.h.
struct AimBasis {
    bool valid = false;
    float aim[3];                      // the clean camera's forward: the aim
    float shotEye[3];                  // where the shot comes from: the clean origin
    float renderEye[3];                // where the frame is drawn from: after the lean
    float fwd[3], right[3], up[3];     // the basis the frame is rendered with
};

void ApplyToView(void* view, bool worldSpaceYaw, float dpitch, float dyaw, float droll,
                 float ox, float oy, float oz, float bodyYaw, AimBasis* outBasis = nullptr) {
    float* viewMat = ViewMatrix(view);
    float fwd[3], right[3], up[3], org[3];
    DecomposeView(viewMat, fwd, right, up, org);

    const float left[3] = { -right[0], -right[1], -right[2] };
    float ang[3];
    BasisToAngles(fwd, left, up, ang);

    // Captured BEFORE the lean below: this is the eye the SHOT comes from, and
    // the whole parallax correction is the difference between it and the eye the
    // frame ends up being drawn from. Taking it after the lean makes the two
    // equal, which silently turns the correction off and reads exactly like the
    // trace not working.
    if (outBasis) {
        outBasis->valid = true;
        for (int i = 0; i < 3; ++i) {
            outBasis->aim[i] = fwd[i];
            outBasis->shotEye[i] = org[i];
        }
    }

    // Positional 6DOF: shift the render origin along a HORIZON-LOCKED basis
    // built from the camera's yaw alone, so the lean follows the body rather
    // than wherever the camera happens to be pointing. Using the camera's own
    // basis would send a forward lean into the floor when the player looks
    // down, and a sideways lean partly vertical while wall-running rolls the
    // camera - which Titanfall does constantly.
    //
    // The tracker frame already matches Source's basis senses, so no sign flips
    // here - see the axis-mapping block above.
    //
    // `bodyYaw` is the CLEAN yaw - where the gun is pointing - not the yaw of the
    // camera this struct carries. Those are no longer the same thing: the head
    // rotation goes in before the view is built, so what is decoded here is
    // already turned. Leaning along the decoded yaw would send the lean wherever
    // the head happened to be looking, which is exactly what a horizon-locked
    // basis exists to avoid - turn your head to the wall and lean in, and the
    // body would step sideways.
    const float flatAng[3] = { 0.0f, bodyYaw, 0.0f };
    float flatFwd[3], flatRight[3], flatUp[3];
    AngleVectors(flatAng, flatFwd, flatRight, flatUp);
    for (int i = 0; i < 3; ++i) {
        org[i] += flatRight[i] * ox + flatUp[i] * oy + flatFwd[i] * oz;
    }
    if (outBasis) for (int i = 0; i < 3; ++i) outBasis->renderEye[i] = org[i];

    if (dpitch == 0.0f && dyaw == 0.0f && droll == 0.0f) {
        // Nothing but the origin moves, so the basis goes back exactly as it was
        // decoded. Rebuilding it from `ang` would put every lean-only frame
        // through a lossy Euler round trip, and at the pole BasisToAngles folds
        // roll into yaw - so the rebuild there is a different camera, not the one
        // that was decoded.
        WriteCamera(view, fwd, right, up, org);
        if (outBasis) {
            for (int i = 0; i < 3; ++i) {
                outBasis->fwd[i] = fwd[i];
                outBasis->right[i] = right[i];
                outBasis->up[i] = up[i];
            }
        }
        return;
    }

    if (worldSpaceYaw) {
        ApplyWorldSpaceRotation(ang, dpitch, dyaw, droll);
    } else {
        ApplyCameraLocalRotation(ang, dpitch, dyaw, droll);
    }

    float newFwd[3], newRight[3], newUp[3];
    AngleVectors(ang, newFwd, newRight, newUp);
    WriteCamera(view, newFwd, newRight, newUp, org);
    if (outBasis) {
        for (int i = 0; i < 3; ++i) {
            outBasis->fwd[i] = newFwd[i];
            outBasis->right[i] = newRight[i];
            outBasis->up[i] = newUp[i];
        }
    }
}

// Scales a render view's projection back down to the field of view the frame is
// meant to be DRAWN at, undoing the widening that keeps the engine from culling
// what the head can turn to look at. That target is the player's own field of
// view unless [View] FieldOfView asks for another one - see fov_control.h.
//
// A perspective projection's [0][0] and [1][1] are 1/tan(halfFov) terms, so
// multiplying both by the ratio the FOV was widened by narrows the drawn cone
// by exactly that much and leaves the aspect alone. tanfov is corrected with
// them so anything else reading it - the ADS zoom check, the diagnostic, the
// drawn field of view published for screen-space projection - sees the field of
// view actually being drawn.
void NarrowProjection(void* view, float ratio) {
    float* proj = ProjMatrix(view);
    proj[0] *= ratio;   // [0][0]
    proj[5] *= ratio;   // [1][1]
    float* tan = TanFov(view);
    tan[0] /= ratio;
    tan[1] /= ratio;
    // The engine built the view-projection from the wide projection, so it has
    // to be rebuilt from the narrow one - otherwise the shaders keep drawing
    // the wide cone and only the diagnostics look right.
    MulMat4(proj, ViewMatrix(view), ViewProjMatrix(view));
}

// The field of view, in four parts, and all of it before anything else touches
// the view.
//
// The measurement is taken ONLY while the campaign is actually being played.
// The logbook screen between checkpoints renders at a noticeably wider field of
// view (base tangent 1.0047 against a resting 0.934), and it sits there for as
// long as the player leaves it - so a measurement that ran during loading or on
// a menu locked onto that and left the player 7% wide for the rest of the
// session.
//
// Enforcing and correcting, by contrast, run on EVERY frame: once the cvar is
// widened it stays widened through pauses and menus, so the projection has to be
// scaled back on those frames too - not only on tracked ones, or putting the
// tracker down would silently leave the player on the culling field of view.
//
// Multiplayer is the exception to that, and it is the whole reason Release()
// exists. "Every frame after it is armed" reaches an mp_ map the moment the
// player quits a campaign to the lobby, and the hold would then carry a widened
// culling frustum and a per-frame projection rewrite into a live match. The
// player would see nothing for it - the projection is scaled straight back down
// to their own field of view - which makes it worse, not better: an
// unadvertised change to what the engine submits in someone else's match, from a
// mod that says multiplayer renders vanilla. It comes off on the same latch
// everything else does, and does not come back.
void UpdateFieldOfView(void* self, void* mainView, SessionKind session, float zoom) {
    auto& fov = GetFovControl();

    if (session == SessionKind::Multiplayer) {
        fov.Release();
        return;
    }

    if (session == SessionKind::Campaign && LooksLikeLiveView(mainView)) {
        // The VERTICAL tangent: Source holds the vertical fixed across aspect
        // ratios and widens the horizontal, so it is the axis that converts back
        // to the degrees the game's own slider shows without knowing the player's
        // resolution. Times the zoom factor, which takes aiming down sights back
        // out of it.
        fov.NoteBaseTangent(TanFov(mainView)[1] * zoom);
    }

    fov.Enforce();

    const float ratio = fov.ProjectionRatio();
    if (ratio > 1.0f) {
        for (void* view : { MainView(self), WorldView(self), SkyboxView(self) }) {
            if (LooksLikeLiveView(view)) NarrowProjection(view, ratio);
        }
    }

    // Published AFTER the narrowing, so that what anything else reads is the
    // field of view the frame is actually DRAWN at rather than the wider one it
    // was culled to. That is the pair a screen-space projection needs - a
    // reticle offset, a world-anchored marker - and getting the two the wrong
    // way round would put them out by the whole headroom multiplier.
    if (LooksLikeLiveView(mainView)) {
        fov.NoteDrawnTangents(TanFov(mainView)[0], TanFov(mainView)[1]);
    }
}

// The camera the frame is DRAWN with, for the diagnostic line. It already
// carries the head rotation - that went into the angles it was built from - so
// against the clean angles it is the delta, and the two together say whether the
// rotation landed.
//
// Sampled from the WORLD view, not the struct RenderView was handed: that one
// drives the viewmodel, and reading the camera off it would say nothing about
// what the world was drawn with.
void ProbeDrawnCamera(void* self, void* mainView, float org[3], float ang[3]) {
    void* probe = WorldView(self);
    if (!LooksLikeLiveView(probe)) probe = mainView;

    float fwd[3], right[3], up[3];
    DecomposeView(ViewMatrix(probe), fwd, right, up, org);
    const float left[3] = { -right[0], -right[1], -right[2] };
    BasisToAngles(fwd, left, up, ang);
}

// This frame's head pose, after everything that governs it: the campaign gate,
// the tracking-loss fade and the ADS fade.
struct FrameDelta {
    bool tracking = false;
    bool worldSpaceYaw = true;
    // The sights are up. Decided once per frame with everything else, so the
    // half of the frame that draws cannot read a different answer from the half
    // that built the camera - and false on every early return, so a menu leaves
    // no stale flag behind for the marker to be placed against.
    bool aiming = false;
    AdsMode adsMode = kDefaultAdsMode;
    float dpitch = 0.0f, dyaw = 0.0f, droll = 0.0f;
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;

    bool Idle() const {
        return dpitch == 0.0f && dyaw == 0.0f && droll == 0.0f
            && ox == 0.0f && oy == 0.0f && oz == 0.0f;
    }
};

// One line each way, so a player who wonders what the view did when they raised
// the sights can see that it was meant to, and which mode did it.
void LogAdsEdge(bool aiming, AdsMode mode) {
    static bool s_was = false;
    static AdsMode s_mode = kDefaultAdsMode;
    if (aiming == s_was && mode == s_mode) return;
    s_was = aiming;
    s_mode = mode;
    if (!aiming) {
        HT_LOG("[ads] sights down - easing head tracking back to your head");
        return;
    }
    switch (mode) {
        case AdsMode::Paused:
            HT_LOG("[ads] sights up - head tracking paused, view settling onto the aim; a head "
                   "tilt still rolls it");
            break;
        case AdsMode::Marker:
            HT_LOG("[ads] sights up - view settling onto the aim, head tracking carries on "
                   "from there with the aim marker drawn");
            break;
        case AdsMode::Tracked:
            HT_LOG("[ads] sights up - view settling onto the aim, head tracking carries on "
                   "from there with nothing drawn");
            break;
    }
}

// Works out the frame's pose, after the campaign / pause gate, the tracker's own
// state and the ADS mode. The caller writes it into the render views.
FrameDelta ComputeFrameDelta(Plugin& plugin, SessionKind session, bool aiming) {
    FrameDelta d;
    d.adsMode = plugin.GetAdsMode();

    float yaw_r, pitch_r, roll_r;
    const bool live = plugin.GetRotationRadians(yaw_r, pitch_r, roll_r);
    const TrackingState state = DecideTracking(session, live, aiming, d.adsMode);
    if (!PoseApplies(state.verdict)) {
        // No pose to apply this frame - tracking off, no tracker, or not a live
        // campaign. Drop the ADS fade, the pose the sights came up on and the
        // traced aim distance, so the next aim re-enters cleanly instead of
        // resuming against a pose from before the suppression.
        g_adsFade.Reset();
        g_adsEntry.Reset();
        ResetAimDistance();
        return d;
    }

    d.tracking = true;
    d.aiming = state.aiming;
    plugin.GetPositionOffset(d.ox, d.oy, d.oz);

    d.dpitch = pitch_r * kRadToDeg * kPitchSign;
    d.dyaw   = yaw_r   * kRadToDeg * kYawSign;
    d.droll  = roll_r  * kRadToDeg * kRollSign;

    // Raising the sights hands the view back to the gun: the head pose eases out
    // over a fraction of a second and the frame settles onto the aim, which is
    // where the crosshair already was. All three ADS modes make that same swing,
    // and differ in where the fade lands.
    //
    //   paused           it lands on nothing, and stays there until the sights
    //                    drop, so the sight picture is exactly the game's.
    //   marker, tracked  it lands on the pose measured from the entry frame,
    //                    which is identity at that moment, so head tracking
    //                    carries on from the aim rather than from centre.
    //
    // Roll is in neither fade, in any mode - see BlendAdsPose, which is where
    // that and the rest of the shape live.
    //
    // Both are asked in every mode, so the entry pose is dropped when the weapon
    // comes down whichever mode was live while it was up, and a mode cycled
    // mid-aim takes effect on that aim.
    LogAdsEdge(d.aiming, d.adsMode);
    const float scale = g_adsFade.Update(d.aiming, GetTickCount64());
    const AdsEntryPose::Pose absolute{ d.dpitch, d.dyaw, d.droll, d.ox, d.oy, d.oz };
    const AdsEntryPose::Pose relative = g_adsEntry.Relative(d.aiming, live, absolute);
    const AdsEntryPose::Pose blended = BlendAdsPose(d.adsMode, scale, absolute, relative);
    d.dpitch = blended.pitch;
    d.dyaw   = blended.yaw;
    d.droll  = blended.roll;
    d.ox     = blended.x;
    d.oy     = blended.y;
    d.oz     = blended.z;

    // Which of the two yaw compositions the write uses is read here, once per
    // frame: the hotkey thread can flip it at any moment, and re-reading it per
    // struct would let a keypress land between the world view and the skybox and
    // compose that one frame two different ways.
    d.worldSpaceYaw = plugin.IsWorldSpaceYaw();
    return d;
}

// The frame's pose, decided at the top of the render phase and consumed here.
// `g_fresh` is what stops a stale one being drawn: BeginFrame runs from the
// render-phase hook, and a frame that never reached it (the hook did not
// install, the render phase was skipped) must render vanilla rather than reuse
// whatever the last one decided.
FrameDelta g_frame;
bool g_fresh = false;
FrameRotation g_lastRotation;

// The CViewRender the frame is drawn through, captured the first time the render
// hook sees it. The view-build hook runs earlier in the frame and has no way to
// reach it otherwise, and it needs it to tell the player's camera apart from a
// shadow cascade or a reflection.
void* g_viewRender = nullptr;

FrameDelta TakeFrame() {
    if (!g_fresh) return FrameDelta();
    g_fresh = false;
    return g_frame;
}

// Writes the frame's delta into the render views. NOT idempotent - it decomposes
// what is there, adds the delta and recomposes - so it must run once per frame
// per struct.
//
// Called only when there is something to apply. An idle delta returns WITHOUT
// touching the structs rather than writing an identity: ApplyToView is not the
// identity even at zero, because it rebuilds the view matrix and the
// view-projection from the decoded camera rather than leaving them. Skipping the
// write is what makes a suspended frame the frame the game would have drawn on
// its own, which is the whole promise of handing the view back to the gun - and
// with the sights up in `paused` the only delta left is the head tilt, so a head
// held level takes that path every frame.
void ApplyRenderViews(void* self, const FrameDelta& d, float bodyYaw, AimBasis& mainBasis) {
    void* main = MainView(self);
    void* world = WorldView(self);

    // Rotation is NOT passed here. Every one of these structs was built from the
    // view angles the render-phase hook had already rotated, so they arrive
    // pointing where the head is looking; adding the delta again would double it.
    // What is left is the lean, which the frustum upstream knows nothing about -
    // and does not need to, because a few tens of centimetres of eye movement is
    // nothing against the extent of a view frustum.
    if (LooksLikeLiveView(main)) {
        ApplyToView(main, d.worldSpaceYaw, 0.0f, 0.0f, 0.0f, d.ox, d.oy, d.oz, bodyYaw,
                    &mainBasis);
    }
    if (LooksLikeLiveView(world)) {
        ApplyToView(world, d.worldSpaceYaw, 0.0f, 0.0f, 0.0f, d.ox, d.oy, d.oz, bodyYaw);
    }
    // The 3D skybox is not touched at all. It is drawn from its own origin at its
    // own scale, so a translation that is right for the world would read as the
    // sky sliding when you lean - and its rotation, like everything else's, came
    // in upstream.
}

// The direction from the eye the frame is DRAWN from to the point the shot will
// land on, normalised. That is the whole parallax correction: with no lean the
// two eyes are the same point and this is just the aim direction, and with a
// lean it swings by however much the eye moved relative to the target - which is
// exactly how far the crosshair has to move to stay on it.
//
// Falls back to the aim direction when the trace found nothing, which is the
// right answer for a shot at the sky: a point at infinity has no parallax.
const float* AimVector(const AimBasis& b, float& outDistance) {
    static float s_vec[3];
    float distance = 0.0f;
    outDistance = 0.0f;
    if (!AimDistance(b.shotEye, b.aim, distance)) return b.aim;
    outDistance = distance;

    float len = 0.0f;
    for (int i = 0; i < 3; ++i) {
        s_vec[i] = b.shotEye[i] + b.aim[i] * distance - b.renderEye[i];
        len += s_vec[i] * s_vec[i];
    }
    len = std::sqrt(len);
    if (!(len > 0.0f)) return b.aim;
    for (int i = 0; i < 3; ++i) s_vec[i] /= len;
    return s_vec;
}

void ApplyTracking(void* self) {
    void* view = MainView(self);

    DumpViewStruct(self, view);

    const SessionKind session = CurrentSession();
    LogSuppression(session, CurrentLevelName());

    // The zoom factor comes from `view`, the struct the offset was measured on.
    // The 0x1a0-0x1d4 region is main-struct-only layout - 0x1d0 in the siblings
    // is a 64-bit heap pointer, which is what made writing 0x1d4 there crash the
    // game - so 0x1ac is only known to mean anything here. Reading it off a
    // sibling would be reading an unrelated float, and the failure is silent:
    // any value in [1.0, 100.0) reads as "aiming", which would leave the mod
    // suppressing tracking for the whole session while the log printed a
    // perfectly plausible zoom.
    //
    // Read before the projection is narrowed, though nothing below touches the
    // field, so the FOV measurement and the ADS check are the same number by
    // construction rather than by two reads happening to agree.
    const float zoom = ZoomFactor(view);

    UpdateFieldOfView(self, view, session, zoom);

    float drawnOrg[3], drawnAng[3];
    ProbeDrawnCamera(self, view, drawnOrg, drawnAng);

    // The frame's pose was decided at the top of the render phase, where the
    // rotation went into the angles everything else was built from. Taking a
    // second tracker sample here would draw a different pose to the one the world
    // was culled to.
    const FrameDelta delta = TakeFrame();
    // The frame's ADS answer, decided in BeginFrame along with everything else.
    // Asking the game again here would be a second read of a flag that can change
    // between the two, so the half of the frame that draws could disagree with the
    // half that built the camera. A frame that never reached BeginFrame reports
    // "not aiming", which is the safe direction: it fails toward stock.
    const bool aiming = delta.aiming;
    AimBasis mainBasis;
    FrameCameras cameras;
    bool haveCameras = false;
    if (delta.tracking && !delta.Idle()) {
        ApplyRenderViews(self, delta, CleanViewAngles()[1], mainBasis);
        // The aim is not recoverable from the camera any more: the view these
        // structs carry is the head-rotated one, so its forward vector is where
        // the head is looking, not where the gun is pointing. The clean angles
        // the rotation was composed onto are the only copy left of it. Only the
        // aim comes from them - right and up must stay the basis the frame was
        // DRAWN with, or the projection would resolve the offset in the wrong
        // frame.
        float cleanFwd[3], cleanRight[3], cleanUp[3];
        AngleVectors(CleanViewAngles(), cleanFwd, cleanRight, cleanUp);
        for (int i = 0; i < 3; ++i) mainBasis.aim[i] = cleanFwd[i];

        // The same two cameras the HUD's world-anchored marks need. The game
        // places those through its own world-to-screen, which projects with the
        // clean camera and leaves them sitting still on the glass while the world
        // turns; giving it both cameras lets the world point be moved into the
        // one it is projecting with (world_marker_hook.h).
        for (int i = 0; i < 3; ++i) {
            cameras.cleanEye[i]   = mainBasis.shotEye[i];
            cameras.cleanFwd[i]   = cleanFwd[i];
            cameras.cleanRight[i] = cleanRight[i];
            cameras.cleanUp[i]    = cleanUp[i];
            cameras.drawnEye[i]   = mainBasis.renderEye[i];
            cameras.drawnFwd[i]   = mainBasis.fwd[i];
            cameras.drawnRight[i] = mainBasis.right[i];
            cameras.drawnUp[i]    = mainBasis.up[i];
        }
        haveCameras = mainBasis.valid;
    }
    PublishFrameCameras(haveCameras ? &cameras : nullptr);

    // Where the gun is pointing in the picture that was just built. ONE
    // projection, two consumers - never a second formula for the ADS case, which
    // is how two marks come to disagree about the same shot. At the hip it moves
    // the game's own crosshair onto the gun; with the sights up in a tracked ADS
    // mode it does the same for whatever crosshair the game still submits, and in
    // `marker` mode it also places the mod's own mark, which is the only thing on
    // screen saying where the rounds go once head tracking has moved the eye off
    // the sight line.
    //
    // Nothing is placed in `paused`: the view has settled back onto the aim, so
    // the game's own sight picture is the truth again, and the centre of the
    // frame is where the aim is - which is where the zero-initialised offset
    // leaves it. The head tilt that survives the fade there does not change that:
    // a pure roll leaves the camera's forward vector where it was, so the aim
    // point stays at the centre and the crosshair the game draws there is still
    // marking it.
    const bool adsTracked = aiming && delta.adsMode != AdsMode::Paused;
    const bool haveAim = delta.tracking && (!aiming || adsTracked);
    float ndcX = 0.0f, ndcY = 0.0f;
    bool offScreen = false;
    float aimDist = 0.0f;
    if (haveAim && mainBasis.valid) {
        const float* tan = TanFov(view);
        offScreen = !ProjectAimToNdc(AimVector(mainBasis, aimDist), mainBasis.fwd,
                                     mainBasis.right, mainBasis.up, tan[0], tan[1], ndcX, ndcY);
    }
    PublishAim(haveAim, offScreen, ndcX, ndcY);
    // The mark the game flashes on a hit rides the same offset: it is drawn in the
    // middle of the frame by an asset that takes no position, so without this it
    // reports the hit where the head is looking (hit_indicator.h).
    PublishHitIndicator(haveAim && !offScreen, ndcX, ndcY);

    // Derived here, every frame, and never latched. A projection that was
    // rejected - the gun behind the picture, an unreadable field of view - draws
    // nothing at all rather than parking the mark where it was, which would put
    // it somewhere the rounds are not going. A mode whose overlay has not come up
    // yet behaves exactly like `tracked`.
    bool markerVisible = false;
    if (delta.tracking && aiming && delta.adsMode == AdsMode::Marker && EnsureAdsMarker()) {
        markerVisible = !offScreen;
    }
    PublishAdsMarker(markerVisible, ndcX, ndcY);

    DiagnosticLog(drawnOrg, drawnAng, TanFov(view), CurrentLevelName(), delta.tracking, zoom,
                  aiming, AdsModeValue(delta.adsMode),
                  delta.dpitch, delta.dyaw, delta.droll, delta.ox, delta.oy, delta.oz,
                  ndcX, ndcY, aimDist);
}

// Set when the detour has faulted once. A wrong offset on some future build
// faults every frame; engaging again after that would be a guaranteed crash
// loop, so the write path stays off for the rest of the process and the game
// carries on vanilla.
bool g_faulted = false;

void Hook_RenderView(void* self, void* view, int clearFlags, int whatToDraw) {
    // Only the frame's main render view is ours to touch. Anything else through
    // this vtable slot - a second view-render object, a render-to-texture pass -
    // is left entirely alone.
    if (self && view && !g_faulted && ViewBelongsToThis(self, view)) {
        g_viewRender = self;
        // SEH, not catch(...): the risk in a detour that walks game structs by
        // pinned offset is an access violation, and under MSVC's default /EHsc
        // a C++ handler does not catch those at all. Nothing here throws a C++
        // exception, so catch(...) would have been a handler for the one thing
        // that cannot happen and no handler for the thing that can.
        //
        // Swallowing is right here, and only here: unwinding out of the detour
        // would skip the original RenderView and leave the engine with a frame
        // it never drew. The fault is not hidden - it is logged once and the
        // mod disables itself for the rest of the process.
        //
        // The handler logs through the EMERGENCY channel. /EHsc means SEH
        // unwinding runs no C++ destructors, so a fault taken anywhere inside
        // the logger orphans its mutex - and the normal logger would then block
        // this thread forever on a lock nothing will ever release. A hung render
        // thread is strictly worse than the crash the handler exists to avoid,
        // on the one path whose whole promise is "the game keeps running".
        __try {
            ApplyTracking(self);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_faulted = true;
            ReleaseViewAngles();
            // Retiring this hook is not enough on its own: the rotation goes in
            // through the view-angle hook, which keeps applying whatever was
            // last published. Without this the view would stay frozen at the
            // delta that was live when the fault landed, for the rest of the
            // session, on a mod that has just declared itself off.
            HT_LOG_FAULT("[hook] faulted while applying the head pose - head tracking disabled "
                         "for this session. The frame it faulted on may render from a partly "
                         "written camera; every frame after it is the game's own. Please report "
                         "this log with your game version.");
        }
    }

    g_originalRenderView(self, view, clearFlags, whatToDraw);
}

// Set when a hook that runs EARLIER in the frame than the view build has already
// decided this frame's pose. The view build clears it the moment it uses it, so
// it lives for exactly the span between the two calls and cannot latch on: a
// frame whose view build never runs costs one stale frame, not a pose that never
// moves again.
bool g_frameOpened = false;

// The frame's pose, decided once: a tracker sample through the campaign gate,
// the tracking-loss fade and the ADS fade. Everything the frame does with the
// head - the culling frustum, the render views, the skybox, the cockpit - comes
// from this one answer, because two samples of a live tracker are two different
// poses and the frame would then be culled to one and drawn to the other.
FrameRotation DecideFrame() {
    Plugin& plugin = GetPlugin();
    plugin.Update();

    g_frame = ComputeFrameDelta(plugin, CurrentSession(), PlayerIsAiming());
    g_fresh = true;

    FrameRotation r;
    r.tracking = g_frame.tracking;
    r.worldSpaceYaw = g_frame.worldSpaceYaw;
    r.dpitch = g_frame.dpitch;
    r.dyaw = g_frame.dyaw;
    r.droll = g_frame.droll;
    g_lastRotation = r;
    return r;
}

}  // namespace

// Runs from the Titan cockpit hook, which is the earliest thing in the frame
// that needs the pose: the cockpit is placed at SetUpView + 0x247 and the
// frame's camera is built at SetUpView + 0x9fc. Decides the pose and marks the
// frame as decided, so the view build below reuses it instead of sampling again.
FrameRotation OpenFrame() {
    const FrameRotation r = DecideFrame();
    g_frameOpened = true;
    return r;
}

// Runs from the render-phase hook, upstream of everything this file draws.
FrameRotation BeginFrame() {
    if (g_frameOpened) {
        // The Titan cockpit was placed earlier in this same SetUpView call and
        // has already decided the pose. Reuse it rather than sampling the
        // tracker again: the cockpit is held still in the picture by the camera
        // turning by exactly what the cockpit turned by, and two samples of a
        // live tracker are two different poses - which is a cockpit that swims
        // against the frame whenever the head moves.
        g_frameOpened = false;
        return g_lastRotation;
    }
    return DecideFrame();
}

FrameRotation LastFrameRotation() { return g_lastRotation; }

void* PlayerMainView() {
    return g_viewRender ? MainView(g_viewRender) : nullptr;
}

void* PlayerSkyboxView() {
    return g_viewRender ? SkyboxView(g_viewRender) : nullptr;
}

CameraHook::~CameraHook() { Uninstall(); }

bool CameraHook::Install() {
    if (!HasActiveProfile()) return false;

    if (cameraunlock::hooks::HookManager::Instance().Initialize()
            != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[hook] MinHook init failed");
        return false;
    }

    void* target = reinterpret_cast<void*>(ClientBase() + ActiveProfile().offsets.render_view_rva);
    const auto st = cameraunlock::hooks::HookManager::Instance().CreateHook(
        target, reinterpret_cast<void*>(&Hook_RenderView),
        reinterpret_cast<void**>(&g_originalRenderView));
    if (st != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[hook] CreateHook(RenderView) failed: %s",
               cameraunlock::hooks::HookStatusToString(st));
        return false;
    }
    if (cameraunlock::hooks::HookManager::Instance().EnableHook(target)
            != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[hook] EnableHook(RenderView) failed");
        return false;
    }
    HT_LOG("[hook] RenderView hook installed at %p", target);
    return true;
}

void CameraHook::Uninstall() {
    // Disable, but do NOT remove the hook or null the trampoline pointer.
    // Disabling restores the original bytes with the threads frozen, so no
    // thread can enter the detour afterwards - but a thread already INSIDE the
    // detour body is in our module, not the patched function, so MinHook does
    // not relocate it. It will still run to the tail call. Removing the hook
    // frees the trampoline and nulling the pointer turns that tail call into a
    // jump to address zero, which is a crash on exactly the path this is
    // supposed to make safe. Leaking one trampoline costs nothing.
    if (g_originalRenderView) {
        cameraunlock::hooks::HookManager::Instance().DisableAllHooks();
    }
}

}  // namespace headtracking
