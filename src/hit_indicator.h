#pragma once

namespace headtracking {

// Moves the game's hit indicator - the mark that flashes when a shot connects -
// onto the crosshair the mod has already moved.
//
// It is not part of the crosshair. The crosshair submitter draws the weapon's
// own reticle elements and nothing else (crosshair_hook.h); the hit indicator is
// a separate RUI the client script creates on each hit, from
// `DamageFlyout()` in cl_hud.gnut:
//
//     RuiCreate( $"ui/hit_indicator.rpak", clGlobal.topoFullScreen, RUI_DRAW_HUD, 0 )
//
// with no position argument at all - the asset draws it in the middle of the
// topology it was created in, and topoFullScreen is the whole frame. In stock
// Titanfall that is exactly right, because the crosshair is in the middle of the
// frame too. Once head tracking moves the crosshair onto the gun the two come
// apart, and the mark saying "you hit something" sits where the player is
// LOOKING while the mark saying where the rounds go sits on the gun.
//
// So the same offset goes on both. `RuiCreate` is detoured (client.dll+0x3092e0,
// the client's RUI instance allocator, which every script RUI goes through) and
// an instance of the hit indicator asset is handed a private copy of its
// topology's transform block, with the crosshair's pixel offset added. Nothing
// the game draws through the topology itself moves - the rest of the HUD shares
// that block, and a HUD that swims with the head is not what anyone asked for.
bool InstallHitIndicatorHook();

// Called once per rendered frame from the camera hook, on the render thread.
// `ndcX` / `ndcY` are the aim's position in the drawn frame, x right, y up,
// -1..1 - the same projection the crosshair is moved by, never a second one.
// `visible` false puts the indicator back in the middle of the frame, which is
// where the game's own crosshair is in every case that turns it off.
void PublishHitIndicator(bool visible, float ndcX, float ndcY);

}  // namespace headtracking
