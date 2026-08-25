#pragma once

// Source QAngle maths, shared by the two places the head delta meets the
// engine's camera: the view-angle hook, which composes it onto the angles
// SetUpView asks for, and the render hook, which recomposes a render view's
// basis after leaning its origin. Both have to agree on the conventions below
// or the two would encode the same pose differently.

#include <cmath>

#include "angle_units.h"

namespace headtracking {

// Source AngleVectors: build the camera basis from a QAngle (degrees).
inline void AngleVectors(const float* ang, float fwd[3], float right[3], float up[3]) {
    const float p = ang[0] * kDegToRad;
    const float y = ang[1] * kDegToRad;
    const float r = ang[2] * kDegToRad;
    const float sp = std::sin(p), cp = std::cos(p);
    const float sy = std::sin(y), cy = std::cos(y);
    const float sr = std::sin(r), cr = std::cos(r);
    fwd[0]   = cp * cy;            fwd[1]   = cp * sy;            fwd[2]   = -sp;
    right[0] = -sr * sp * cy + cr * sy;
    right[1] = -sr * sp * sy - cr * cy;
    right[2] = -sr * cp;
    up[0]    = cr * sp * cy + sr * sy;
    up[1]    = cr * sp * sy - sr * cy;
    up[2]    = cr * cp;
}

// Source MatrixAngles: recover a QAngle from a camera basis. `left` is the
// negated right vector, matching Source's own column order.
inline void BasisToAngles(const float* fwd, const float* left, const float* up, float* ang) {
    const float xyDist = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    if (xyDist > 0.001f) {
        ang[0] = std::atan2(-fwd[2], xyDist) * kRadToDeg;
        ang[1] = std::atan2(fwd[1], fwd[0]) * kRadToDeg;
        ang[2] = std::atan2(left[2], up[2]) * kRadToDeg;
    } else {
        // Looking straight up or down: yaw and roll are the same axis, so
        // Source folds the whole rotation into yaw and zeroes roll.
        ang[0] = std::atan2(-fwd[2], xyDist) * kRadToDeg;
        ang[1] = std::atan2(-left[0], left[1]) * kRadToDeg;
        ang[2] = 0.0f;
    }
}

inline float ClampPitch(float pitch) {
    if (pitch > 89.0f) return 89.0f;
    if (pitch < -89.0f) return -89.0f;
    return pitch;
}

// World-space yaw, the default: a QAngle is intrinsically horizon-locked - yaw
// is about world up, pitch about the yawed right axis - so adding the head
// delta straight on IS the world-space composition.
//
// Clamped at the pole for the same reason Source clamps player pitch: past 90
// degrees AngleVectors' cos(pitch) goes negative, up flips, and the frame
// renders upside-down with yaw reversed. The game's own camera stops at 89, but
// nothing stops the head delta walking it over.
inline void ApplyWorldSpaceRotation(float* ang, float dpitch, float dyaw, float droll) {
    ang[0] = ClampPitch(ang[0] + dpitch);
    ang[1] += dyaw;
    ang[2] += droll;
}

// Camera-local rotation: compose the head delta about the CAMERA's own axes
// rather than the world's. Adding the delta straight onto the QAngle (the
// world-space path above) yaws about world up, which is right for normal play
// but turns into a spin once the game camera looks steeply up or down. Here the
// head basis is built in the camera's frame and mapped back out through it.
//
// No pole clamp here, and none needed: this composes on the basis, so a
// past-pole rotation re-encodes as an equivalent QAngle rather than an inverted
// one.
inline void ApplyCameraLocalRotation(float* ang, float dpitch, float dyaw, float droll) {
    float fwd[3], right[3], up[3];
    AngleVectors(ang, fwd, right, up);

    const float head[3] = { dpitch, dyaw, droll };
    float hf[3], hr[3], hu[3];
    AngleVectors(head, hf, hr, hu);

    // AngleVectors works in an (x = forward, y = left, z = up) frame, so the
    // head vectors' components are already coordinates in the camera's own
    // frame - mapping them back out is one change of basis.
    const float camLeft[3] = { -right[0], -right[1], -right[2] };
    float outFwd[3], outRight[3], outUp[3], outLeft[3];
    for (int i = 0; i < 3; ++i) {
        outFwd[i]   = hf[0] * fwd[i] + hf[1] * camLeft[i] + hf[2] * up[i];
        outRight[i] = hr[0] * fwd[i] + hr[1] * camLeft[i] + hr[2] * up[i];
        outUp[i]    = hu[0] * fwd[i] + hu[1] * camLeft[i] + hu[2] * up[i];
        outLeft[i]  = -outRight[i];
    }
    BasisToAngles(outFwd, outLeft, outUp, ang);
}

}  // namespace headtracking
