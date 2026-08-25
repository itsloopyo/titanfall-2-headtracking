#pragma once

#include <cmath>

namespace headtracking {

// Where the gun is pointing, in the picture the head is looking at.
//
// The game draws its crosshair at a fixed screen position, so it marks where
// shots land only while the rendered view IS the aim. Once the head turns the
// view away from the aim, the two are different directions and the crosshair is
// a lie - which is the whole reason this exists.
//
// The projection takes a unit vector FROM THE EYE THE FRAME IS DRAWN FROM TO THE
// POINT THE SHOT WILL LAND ON, and the basis the frame is actually rendered
// with, and asks where the first lands in the second. With the head centred that
// vector is just the clean camera's forward; with a positional lean it swings by
// the parallax, which is the whole reason it is a vector to a point and not a
// direction (aim_trace.h).
//
// It is deliberately basis-to-basis rather than a formula in yaw /
// pitch / roll: the camera hook composes the head delta in one of two modes and
// hands the vectors it actually wrote into the view matrix straight to here, so
// there is no second derivation of the composition to disagree with the first.
// A per-axis tangent formula would agree with it on single-axis poses and drift
// on combined ones, which is precisely the bug that survives testing.
//
// `tanX` / `tanY` are the DRAWN half-field tangents (tan(fovX/2), tan(fovY/2)),
// read back out of the render view after the projection has been narrowed - not
// the wider one the engine culls against.
//
// Returns false when the aim points behind the rendered view (the head has
// turned past 90 degrees off the gun), where there is no screen position to
// draw at. NDC is x right, y up, both -1..1 across the frame.
inline bool ProjectAimToNdc(const float aim[3],
                            const float fwd[3], const float right[3], const float up[3],
                            float tanX, float tanY, float& ndcX, float& ndcY) {
    const float z = aim[0] * fwd[0] + aim[1] * fwd[1] + aim[2] * fwd[2];
    // Not just "behind the camera": as z goes to zero the projection goes to
    // infinity, and a reticle at 1e30 is a NaN waiting to be handed to a vertex
    // buffer. A tenth of the depth range is far outside any frame anyway.
    if (!(z > 0.1f) || !(tanX > 0.0f) || !(tanY > 0.0f)) return false;

    const float x = aim[0] * right[0] + aim[1] * right[1] + aim[2] * right[2];
    const float y = aim[0] * up[0] + aim[1] * up[1] + aim[2] * up[2];
    ndcX = (x / z) / tanX;
    ndcY = (y / z) / tanY;
    return std::isfinite(ndcX) && std::isfinite(ndcY);
}

}  // namespace headtracking
