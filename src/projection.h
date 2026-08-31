#pragma once

#include "cameraunlock/rendering/aim_ndc_projection.h"
#include "cameraunlock/rendering/world_reprojection.h"

namespace headtracking {

// The two shared projections this game uses, under this mod's own namespace.
//
// Core carries three families and picking between them is a per-game decision,
// so it is recorded here rather than left to whichever header a translation unit
// happened to include:
//
//   ProjectAimToNdc      basis-to-basis. The camera hook composes the head delta
//                        in one of two modes and hands over the vectors it
//                        actually wrote into the view matrix, so there is no
//                        second derivation of the composition to disagree with
//                        the first. The quaternion and per-axis-Euler
//                        alternatives would both need the composition restated.
//   ReprojectWorldPoint  the game's own WorldToScreen places every HUD mark, so
//                        the world point is moved into the frame the game is
//                        projecting with rather than the mark being moved after
//                        the fact - see world_marker_hook.h.
using cameraunlock::rendering::FrameCameras;
using cameraunlock::rendering::ProjectAimToNdc;
using cameraunlock::rendering::ReprojectWorldPoint;

}  // namespace headtracking
