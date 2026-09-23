#pragma once

#include "cameraunlock/camera/zoom_compensation.h"

namespace headtracking {

// The head pose at the camera boundary: degrees in Source's senses, and the lean
// in Source units along the horizon-locked body basis.
struct HeadPose {
    float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Scales the pose so it moves the picture as far as it would at the player's
// un-zoomed field of view. `factor` is cameraunlock::camera::FovZoomFactor of
// this frame's tangent against the base one, 1.0 at the hip. Roll is left alone:
// it turns the picture about the view axis by the same angle at every FOV.
inline HeadPose CompensateZoom(HeadPose p, float factor) {
    p.pitch = cameraunlock::camera::ScaleAngleForZoom(p.pitch, factor);
    p.yaw   = cameraunlock::camera::ScaleAngleForZoom(p.yaw, factor);
    p.x *= factor;
    p.y *= factor;
    p.z *= factor;
    return p;
}

// With the sights up the lean eases out, because it moves the eye off the sight
// line. `leanScale` is AdsFade's output, 1 at the hip and 0 with the sights up.
// Rotation is never touched: turning the camera about the eye leaves the sight
// line through the eye, so the sights stay lined up wherever the head points.
inline HeadPose EaseLeanForSights(HeadPose p, float leanScale) {
    p.x *= leanScale;
    p.y *= leanScale;
    p.z *= leanScale;
    return p;
}

}  // namespace headtracking
