#pragma once

#include "ads.h"
#include "game_state.h"

namespace headtracking {

// Whether the head pose reaches the view this frame, and why not when it does
// not.
//
// Pulled out of the render hook as a pure function so the walk can be exercised
// without the game. What it decides is one branch wide, and every one of its
// answers is a frame the player either sees their head in or does not.
enum class TrackingVerdict {
    // The head pose is applied in full.
    Active,
    // The sights are up in `paused` mode. The pose is still fed to the camera,
    // because it is being EASED off rather than switched off - see AdsFade - and
    // once it has gone the frame is the frame the game would have drawn on its
    // own, bar the head tilt: roll is left out of the fade in every mode, since
    // it moves neither the eye off the barrel nor the aim off the middle of the
    // frame (ads_blend.h).
    AdsSuspended,
    NoLevel,
    Loading,
    Multiplayer,
    GamePaused,
    // Tracking is off, or the tracker has not published a pose.
    NoTracker,
};

struct TrackingState {
    TrackingVerdict verdict = TrackingVerdict::NoLevel;
    // The sights are up. Reported in EVERY mode, including `paused` where the
    // gate is closed: the gate says whether tracking applies, this says what the
    // weapon is doing, and the per-frame code needs both - the marker and the
    // crosshair are placed from it.
    bool aiming = false;
};

// ADS is tested LAST, so a menu, a loading screen or a multiplayer map still
// reports its own reason when both are true at once - and every earlier return
// leaves `aiming` false, because a stale flag through a menu would keep the
// render-side placement running against a weapon that is not raised.
inline TrackingState DecideTracking(SessionKind session, bool haveRotation, bool aiming,
                                    AdsMode mode) {
    TrackingState s;
    switch (session) {
        case SessionKind::NoLevel:     s.verdict = TrackingVerdict::NoLevel;     return s;
        case SessionKind::Loading:     s.verdict = TrackingVerdict::Loading;     return s;
        case SessionKind::Multiplayer: s.verdict = TrackingVerdict::Multiplayer; return s;
        case SessionKind::Paused:      s.verdict = TrackingVerdict::GamePaused;  return s;
        case SessionKind::Campaign:    break;
    }
    if (!haveRotation) {
        s.verdict = TrackingVerdict::NoTracker;
        return s;
    }
    s.aiming = aiming;
    s.verdict = (aiming && mode == AdsMode::Paused) ? TrackingVerdict::AdsSuspended
                                                    : TrackingVerdict::Active;
    return s;
}

// A pose is fed to the camera in both of the first two verdicts. AdsSuspended
// needs it because suspending is an ease-out, not a switch: dropping the pose on
// the falling edge into ADS would throw away the smoothing state, and lowering
// the weapon would then swing the view back through the whole head angle.
inline bool PoseApplies(TrackingVerdict verdict) {
    return verdict == TrackingVerdict::Active || verdict == TrackingVerdict::AdsSuspended;
}

}  // namespace headtracking
