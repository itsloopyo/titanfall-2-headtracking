#pragma once

// Row-major 4x4 view maths, with no knowledge of the game, the hook or the
// render view struct. Split out from camera_hook.cpp so the arithmetic that
// every rendered frame passes through can be exercised on its own: a sign error
// in here is a camera that points somewhere plausible-but-wrong, which is
// exactly the kind of defect that survives being looked at in game.

#include <cstring>

namespace headtracking {

// The view matrix rows ARE the camera axes: row 0 = right, row 1 = up,
// row 2 = -forward (the camera looks down its own -z), and each row's fourth
// element is -dot(axis, origin). Everything the hook needs about the clean
// camera is therefore recoverable from the one matrix the renderer consumes,
// which is what keeps the two from ever disagreeing.
inline void DecomposeView(const float* v, float fwd[3], float right[3], float up[3],
                          float org[3]) {
    for (int i = 0; i < 3; ++i) {
        right[i] =  v[0 + i];
        up[i]    =  v[4 + i];
        fwd[i]   = -v[8 + i];
    }
    // The rows are orthonormal, so origin = sum over rows of -w * row.
    for (int i = 0; i < 3; ++i) {
        org[i] = -(v[3] * v[0 + i] + v[7] * v[4 + i] + v[11] * v[8 + i]);
    }
}

inline void ComposeView(float* v, const float fwd[3], const float right[3], const float up[3],
                        const float org[3]) {
    const float back[3] = { -fwd[0], -fwd[1], -fwd[2] };
    const float* rows[3] = { right, up, back };
    for (int r = 0; r < 3; ++r) {
        float dot = 0.0f;
        for (int i = 0; i < 3; ++i) {
            v[r * 4 + i] = rows[r][i];
            dot += rows[r][i] * org[i];
        }
        v[r * 4 + 3] = -dot;
    }
    v[12] = v[13] = v[14] = 0.0f;
    v[15] = 1.0f;
}

// out = a * b, row-major. Writes through a temporary so `out` may alias.
inline void MulMat4(const float* a, const float* b, float* out) {
    float t[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[r * 4 + k] * b[k * 4 + c];
            t[r * 4 + c] = sum;
        }
    }
    std::memcpy(out, t, sizeof(t));
}

}  // namespace headtracking
