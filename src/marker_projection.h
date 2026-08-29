#pragma once

namespace headtracking {

// The two cameras a tracked frame has: the one the game still believes it is
// drawing, and the one the picture was actually drawn with.
//
// They differ by exactly the head pose. The rotation goes into the angles the
// view is built from and comes straight back out again (view_angles_hook.h), and
// the lean is written into the render views (camera_hook.cpp) - so every part of
// the game that reads the camera for itself, rather than off a render view, is
// still looking at the clean one.
struct FrameCameras {
    float cleanEye[3], cleanFwd[3], cleanRight[3], cleanUp[3];
    float drawnEye[3], drawnFwd[3], drawnRight[3], drawnUp[3];
};

// Moves a world point into the frame the game is about to project it with, so
// that the game's own world-to-screen puts it where it belongs in the picture
// that was DRAWN.
//
// The point is resolved in the drawn camera's basis and rebuilt in the clean
// one, which is the world point that - seen from the clean camera - appears
// exactly where the real point appears from the drawn one. Distances and depths
// are preserved, so everything the game does downstream of the projection (the
// perspective divide, the behind-the-camera test) is answering about the drawn
// camera without knowing it exists.
//
// This is deliberately a move of the INPUT rather than a projection of our own:
// a second projection would have to reproduce the game's field of view, aspect
// handling and viewport letterboxing, and would drift away from the game's the
// moment any of those changed.
inline void ReprojectWorldPoint(const FrameCameras& c, const float p[3], float out[3]) {
    const float v[3] = { p[0] - c.drawnEye[0], p[1] - c.drawnEye[1], p[2] - c.drawnEye[2] };
    const float x = v[0] * c.drawnRight[0] + v[1] * c.drawnRight[1] + v[2] * c.drawnRight[2];
    const float y = v[0] * c.drawnUp[0]    + v[1] * c.drawnUp[1]    + v[2] * c.drawnUp[2];
    const float z = v[0] * c.drawnFwd[0]   + v[1] * c.drawnFwd[1]   + v[2] * c.drawnFwd[2];
    for (int i = 0; i < 3; ++i) {
        out[i] = c.cleanEye[i] + x * c.cleanRight[i] + y * c.cleanUp[i] + z * c.cleanFwd[i];
    }
}

}  // namespace headtracking
