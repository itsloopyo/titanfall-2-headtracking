#pragma once

namespace headtracking {

// How far away the thing the gun is pointing at is, in Source units.
//
// The crosshair marks a POINT in the world, not a direction. While the eye is
// where the game put it those are the same thing to project, which is why this
// did not exist at first. A positional lean breaks that: the frame is drawn from
// an eye a few tens of centimetres to one side of the one the shot comes from,
// so a fixed direction projects to a screen position that slides off the thing
// it is supposed to be marking - parallax, and worse the closer the target. The
// player leans and the crosshair drifts off what they were aiming at, while the
// bullet keeps going exactly where it was going.
//
// Fixing that needs the distance, and the game will give it: client.dll+0x348ae0
// is a thin wrapper over IEngineTrace's world trace (`EngineTraceClient004`,
// held at client.dll+0xc3d9c0) taking (start, end, mins, maxs, mask, trace_out).
//
// Traced at 15 Hz, not per frame: it is a world query, the answer changes slowly
// compared to a frame, and the result is smoothed anyway. Smoothed because the
// distance JUMPS when the aim crosses the edge of anything - a doorway, a
// railing - and an unsmoothed jump moves the crosshair across the screen in one
// frame for no reason the player can see.
//
// Returns false when the trace could not run, when nothing was hit, or before
// the first sample. The caller then projects the aim as a pure direction, which
// is exactly right for a target at infinity and is the honest answer for a shot
// into the sky.
bool AimDistance(const float eye[3], const float fwd[3], float& outUnits);

// Drops the smoothed distance. Call when tracking stops, so the next lean does
// not start from a distance measured in a different place.
void ResetAimDistance();

}  // namespace headtracking
