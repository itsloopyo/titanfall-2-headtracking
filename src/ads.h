#pragma once

#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/ads/entry_pose.h"

namespace headtracking {

// The shared aim-down-sights module, under this mod's own namespace.
//
// The cycle, its value strings, its toast wording and the transition shape are a
// fleet-wide contract rather than a Titanfall decision: a player who learns
// Insert in one shooter has to find the same three slots in the same order here.
// So they live in cameraunlock-core and this file only says which of them this
// game gets.
//
// **Titanfall 2 ships all three slots**, not the two-slot shape, because it has
// no aim indicator the mod can move while the sights are up. The sight picture
// is the weapon's own irons, holo or optic drawn on the gun model, and the
// crosshair the hip-fire path moves (crosshair_hook.h) is not submitted through
// an aim. Scoped weapons settle it either way: the gauntlet sniper's reticle is
// only honest while the eye sits exactly on the optic, which is precisely what
// head tracking breaks. So `marker` earns its slot - it is the only thing on
// screen saying where the rounds go once tracking has moved the eye off the
// sight line.
using cameraunlock::ads::AdsEntryPose;
using cameraunlock::ads::AdsFade;
using cameraunlock::ads::AdsMode;
using cameraunlock::ads::AdsModeToast;
using cameraunlock::ads::AdsModeValue;
using cameraunlock::ads::kDefaultAdsMode;
using cameraunlock::ads::NextAdsMode;
using cameraunlock::ads::ParseAdsMode;

}  // namespace headtracking
