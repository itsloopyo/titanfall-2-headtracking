#pragma once

namespace headtracking {

// NDC (x right, y up, -1..1 across the frame) to the pixels an RUI transform
// block is positioned in, whose y runs DOWN.
//
// Two callers write that block - the game's own crosshair (crosshair_hook.cpp)
// and the hit indicator (hit_indicator.cpp) - and both have to put their mark on
// the same shot. The y flip is the one sign in the path that a log cannot show
// and a screenshot only shows once someone pitches their head, so it is written
// once rather than in each caller.
inline void RuiNdcToPixels(float ndcX, float ndcY, float width, float height,
                           float& px, float& py) {
    px = ndcX * width * 0.5f;
    py = -ndcY * height * 0.5f;
}

}  // namespace headtracking
