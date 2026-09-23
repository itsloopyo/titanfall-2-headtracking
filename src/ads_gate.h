#pragma once

#include "game_state.h"

namespace headtracking {

// Whether the head pose reaches the view this frame, and why not when it does
// not.
//
// Pulled out of the render hook as a pure function so the walk can be exercised
// without the game. What it decides is one branch wide, and every one of its
// answers is a frame the player either sees their head in or does not.
//
// Aiming down sights is not a verdict. Head tracking carries straight on
// through the aim; the sights only ease the lean out (frame_pose.h).
enum class TrackingVerdict {
    // The head pose is applied in full.
    Active,
    NoLevel,
    Loading,
    Multiplayer,
    GamePaused,
    // Tracking is off, or the tracker has not published a pose.
    NoTracker,
};

struct TrackingState {
    TrackingVerdict verdict = TrackingVerdict::NoLevel;
    // The sights are up. Only reported on an Active frame: it drives the lean
    // easing, and a stale flag through a menu would ease the lean against a
    // weapon that is not raised.
    bool aiming = false;
};

// ADS is taken LAST, so a menu, a loading screen or a multiplayer map still
// reports its own reason when both are true at once, and every earlier return
// leaves `aiming` false.
inline TrackingState DecideTracking(SessionKind session, bool haveRotation, bool aiming) {
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
    s.verdict = TrackingVerdict::Active;
    return s;
}

}  // namespace headtracking
