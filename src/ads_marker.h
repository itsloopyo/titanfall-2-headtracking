#pragma once

namespace headtracking {

// The mark that says where the rounds are going while the sights are up.
//
// `marker` mode exists because Titanfall 2 has no aim indicator the mod can move
// during ADS - see ads.h for why this game gets three slots. The drawing itself
// is the shared DX11 marker (cameraunlock/rendering/aim_marker_dx11.h); what is
// here is the wiring: this game's log channel, and the one-shot lazy install
// that keeps the swap chain unpatched for every player who leaves the setting at
// its default.
//
// Returns true once the overlay is drawing. The install runs on a worker thread,
// so the first few frames after a player cycles into `marker` answer false, and
// a mode whose marker never came up behaves exactly like `tracked`.
bool EnsureAdsMarker();

// Called once per rendered frame from the camera hook, on the render thread.
// `visible` is derived per frame and never latched. `ndcX` / `ndcY` are the
// aim's position in the drawn frame, x right, y up, -1..1 - the same projection
// the game's own crosshair is moved by at the hip, never a second one.
void PublishAdsMarker(bool visible, float ndcX, float ndcY);

}  // namespace headtracking
