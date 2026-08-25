#pragma once

#include <cmath>

#include "angle_units.h"

namespace headtracking {

// The pure arithmetic the field-of-view control runs on, in the header so it
// can be exercised without a game attached. A sign or a constant wrong in here
// produces a picture that is drawn at a plausible-but-wrong field of view, and
// the only thing downstream of it is a write into a live game cvar.
namespace fov {

// Source states a field of view as the horizontal angle of a 4:3 view and
// derives the vertical from it as tan(fov/2) * 3/4, holding the vertical fixed
// and widening the horizontal as the aspect ratio grows. So this constant is a
// property of the engine, not of the player's monitor. Confirmed against a live
// frame at 16:9: 70 degrees gives tanfov (0.934, 0.525), and 0.525 / tan(35) is
// 0.7500 to four figures.
constexpr float kVerticalOfFourThree = 0.75f;

inline float TanYFromFov(float fovDegrees) {
    return std::tan(fovDegrees * 0.5f * kDegToRad) * kVerticalOfFourThree;
}

inline float FovFromTanY(float tanY) {
    return 2.0f * std::atan(tanY / kVerticalOfFourThree) * kRadToDeg;
}

// A scale outside this is not a field-of-view scale, which means either the
// profile's convar_float offset is pointing at the wrong member or something
// else wrote over it.
inline bool PlausibleScale(float scale) {
    return std::isfinite(scale) && scale > 0.05f && scale < 20.0f;
}

// Degrees of field of view per unit of cl_fovScale. Measured, never assumed -
// it is the one number here a patch could move - but the measurement takes the
// NARROWEST base tangent seen over its settling window, so a single frame
// rendered through a scripted narrow view locks the whole session onto a value
// that is out by an order of magnitude. What is then computed from it is the
// multiplier the mod holds a live game cvar at on every frame, so a wrong
// measurement is a permanently wrecked picture rather than a soft failure.
//
// 70.0 is what this build measures. The band is wide enough for any Source
// title's base field of view and narrow enough that the pathological
// measurements above cannot pass.
constexpr float kMinFovPerScale = 30.0f;
constexpr float kMaxFovPerScale = 200.0f;

inline bool PlausibleFovPerScale(float degreesPerUnit) {
    return std::isfinite(degreesPerUnit)
        && degreesPerUnit >= kMinFovPerScale && degreesPerUnit <= kMaxFovPerScale;
}

}  // namespace fov


// Owns the field of view: the one the frame is DRAWN at, the wider one the
// engine is told to CULL at, and the single place either can be read from.
//
// ---- Where the number comes from -------------------------------------------
//
// The render view carries tan(fovX/2) and tan(fovY/2) at +0x180, and at +0x1ac
// the frame's zoom factor - the player's base FOV tangent over this frame's.
// Their product is therefore the player's own field of view, unchanged by
// aiming down sights, and it is measured off the frame that is about to be
// drawn rather than inferred from a cvar, so it is the truth by construction.
//
// Source states a field of view as the HORIZONTAL angle of a 4:3 view and
// derives the vertical from it as tan(fov/2) * 3/4 whatever the aspect ratio,
// widening the horizontal instead. The vertical tangent is therefore the axis
// that converts back to the number on the game's own slider without knowing the
// player's resolution:
//
//     fov_degrees = 2 * atan(tanY / 0.75)
//
// Confirmed on this build: tanfov (0.934, 0.525) at cl_fovScale 1.0 is exactly
// 70 degrees, and (2.264, 1.273) at 1.7 is exactly 119. So cl_fovScale is a
// plain multiplier on the FOV ANGLE at 70 degrees per unit - but that ratio is
// measured at runtime rather than assumed, because it is the one number here
// that a patch could move.
//
// ---- Why it is widened ------------------------------------------------------
//
// Not to cover the head turn - that is the view-angle hook's job, and it does it
// by AIMING the frustum rather than stretching it (view_angles_hook.h). What is
// left for a wider cone is the positional lean, which moves the eye a few tens
// of centimetres without moving the cone it looks through, and `[View]
// FieldOfView` if the player wants to be drawn at an angle the game's own slider
// will not reach. Both default to off.
//
// `cl_fovScale` multiplies the player's field of view and the visibility frustum
// is built from it, so widening means holding the cvar above the player's
// setting and scaling every render view's projection back to the field of view
// they asked for before the frame is drawn. The cost is geometry submitted and
// not drawn, and on a wide monitor that cost is steep for very little reach: the
// frustum grows in TANGENT, and at 32:9 the drawn cone is already about 123
// degrees across, so trebling the tangent buys around 22 degrees each side while
// pushing the projection past 165 degrees, where the near plane and the depth
// range stop behaving and the picture flickers. Hence the 1.0 default.
//
// ---- Why the player can choose it -------------------------------------------
//
// Widening means holding the cvar at a value of our choosing every frame, which
// takes the game's own FOV slider away from the player: whatever they set is
// overwritten before the next frame is drawn. So the drawn field of view is ours
// either way, and the honest thing to do with it is hand it back.
//
// `[View] FieldOfView` draws at any angle asked for, including outside the
// 70-119 degrees the game's slider allows, because the projection scale that
// narrows the widened cone back down does not care which side of the player's
// setting it lands on. Left at 0 the game's own setting is followed LIVE: the
// value the engine pushes into the cvar is read on the way past every frame, so
// moving the in-game slider still works while the mod is running.
class FovControl {
public:
    // Finds cl_fovScale and reads the player's own value. `overrideFovDegrees`
    // is 0 to follow the game's own setting, or the field of view to draw at in
    // the same degrees the in-game slider uses; `cullHeadroom` multiplies that
    // to get the field of view the engine culls to. Returns false if the cvar
    // could not be reached, in which case the mod runs with the game's own field
    // of view and the game's own culling.
    bool Initialize(float overrideFovDegrees, float cullHeadroom);

    // Feed the frame's base FOV tangent: the VERTICAL tangent times the zoom
    // factor, which is constant for a given FOV setting and so independent of
    // aiming down sights.
    //
    // Call it only on frames the campaign is actually being PLAYED. The logbook
    // screen between checkpoints renders at a noticeably wider field of view and
    // sits there for as long as the player leaves it, so a measurement taken
    // during loading or on a menu locks the whole session onto the wrong number.
    void NoteBaseTangent(float baseTangentY);

    // How much wider than the drawn field of view the projection currently is,
    // i.e. what every render view's projection must be multiplied by. 1.0 until
    // the field of view has been measured, and whenever nothing needs scaling.
    float ProjectionRatio() const;

    // Re-asserts the widened value, and picks the player's own setting up on the
    // way past. Must be called every frame: cl_fovScale is bound to the user
    // setting `setting.cl_fovScale` and the engine pushes that setting back into
    // it, so a single write is reverted before the next frame and looks exactly
    // like a write that never worked. That same push is what makes the in-game
    // slider observable - anything in the cvar that is not the value we last
    // wrote is the player's.
    void Enforce();

    // The field of view the frame was actually drawn at, taken from the render
    // view AFTER the projection was scaled back down. This is what anything
    // projecting into screen space has to use - a reticle offset, a world-anchored
    // marker - and it is deliberately the post-correction number, because the
    // pre-correction one is the cone the engine culled to and nothing is drawn at
    // that. Zero until the first frame has been through.
    void NoteDrawnTangents(float tanX, float tanY);
    float DrawnTanX() const { return m_drawnTanX; }
    float DrawnTanY() const { return m_drawnTanY; }
    float DrawnFovDegrees() const;

    // The field of view the last frame was CULLED to, which is the one the
    // engine built its visibility frustum from. Only ever a diagnostic - what
    // it says is how far the head can turn before geometry starts going missing.
    float CulledFovDegrees() const;

    // Hands cl_fovScale back to the player and stops touching the field of view
    // for the rest of the process. Permanent by design, and called the moment
    // the session is recognised as multiplayer.
    //
    // Everything else in the mod already goes quiet on that latch, but the FOV
    // hold would not have: it is armed on a campaign frame and re-asserted on
    // EVERY frame after it, pauses and menus included, because the projection
    // has to be scaled back down on those too. Quitting a campaign to the lobby
    // is an mp_ map, so without this the mod would carry a widened culling
    // frustum and a per-frame projection rewrite straight into a live match -
    // no picture the player could see for it, and the one thing this mod must
    // never do. Not re-armable: NoteBaseTangent is a campaign-only call, but the
    // gate that matters here is a flag that only ever goes one way.
    void Release();

private:
    void NotePlayerScale(float scale);
    // Says, once per field of view chosen, what was actually settled on. Not a
    // formality: the engine clamps cl_fovScale at 1.7 on every READ whatever is
    // written into it, so a wide FieldOfView can ask for a cone the engine will
    // not build, and nothing else in the mod would tell the player.
    //
    // Deferred until the cone has held steady: the frames between the cvar being
    // written and the engine building a projection from it carry the old value
    // and read exactly like a refusal.
    void ReportCulling();
    // Recomputes the drawn and culled field of view from the player's current
    // setting, the override and the headroom. Called once the FOV-per-scale
    // ratio has been measured, and again whenever the player moves the slider.
    void Recompute();

    void* m_cvar = nullptr;
    float m_overrideFov = 0.0f;   // 0 = follow the game's own setting
    float m_headroom = 1.0f;      // culled field of view / drawn field of view

    // The game's own FOV setting, as the cvar's value and as the degrees per
    // unit of it. The second is measured once (see NoteBaseTangent) and is what
    // lets a field of view in degrees be turned back into a cvar value.
    float m_playerScale = 1.0f;
    float m_fovPerScale = 0.0f;   // 0 until measured

    float m_targetTanY = 0.0f;    // vertical tangent of the drawn field of view
    float m_requestedCulledFov = 0.0f;
    float m_written = 0.0f;       // the value cl_fovScale is held at
    bool m_active = false;
    bool m_released = false;      // set by Release(), never cleared

    float m_currentBaseTanY = 0.0f;
    float m_drawnTanX = 0.0f;
    float m_drawnTanY = 0.0f;

    // Five seconds at 60fps of watching for the NARROWEST field of view before
    // committing to it. Not the first sample and not the first stable run: the
    // frames after a checkpoint load carry a wider transitional FOV that can hold
    // steady for seconds (measured 1.0047 tangent against a resting 0.9336), so
    // both of those locked onto a 7% error. Everything that perturbs the field of
    // view in normal play widens it, which makes the resting value the minimum.
    static constexpr int kSettleFrames = 300;
    float m_candidatePerScale = 0.0f;
    int m_samples = 0;
    // The measurement window re-arms when it produces an implausible answer, so
    // the line saying so is logged once rather than every time it re-arms.
    bool m_perScaleWarned = false;

    // A second of an unchanging cone is enough to call it settled.
    static constexpr int kCullSettleFrames = 60;
    float m_lastBaseTanY = 0.0f;
    int m_cullSettleFrames = 0;
    bool m_cullingReported = false;
};

FovControl& GetFovControl();

}  // namespace headtracking
