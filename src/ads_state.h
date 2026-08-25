#pragma once

namespace headtracking {

// Is the player aiming down sights, as the game itself understands it.
//
// The mod used to answer this from the render view's zoom factor crossing 1.01,
// which is only true of a weapon that MAGNIFIES. The P2011 pistol holds
// zoom=1.000 and fov=70.0 through a held aim, so with that weapon the mod never
// saw an ADS at all: the view kept following the head and, worse, the positional
// lean kept moving the eye off the sight line, which makes aiming impossible.
//
// The game keeps the answer explicitly. `player + 0x1698` is the byte the
// zoom-fraction computation branches on (client.dll+0x2c8440, reached from the
// `player_zoomFrac` RUI argument): non-zero and the fraction ramps toward 1,
// zero and it decays to 0. It is a statement about the sights, not about
// magnification, so it is right for every weapon.
//
// The player pointer comes from a detour on the crosshair submitter
// (client.dll+0x15ef90), which CViewRender::RenderView calls once per frame with
// the local player and which runs whatever the HUD is doing - the crosshair's
// own visibility gate is inside it, after the call.
bool InstallAdsStateHook();

// False when the hook is not installed or no frame has been seen yet, which is
// the right answer: no player, no sights.
bool PlayerIsAiming();

}  // namespace headtracking
