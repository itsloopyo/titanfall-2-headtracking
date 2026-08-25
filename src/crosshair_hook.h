#pragma once

namespace headtracking {

// Moves the GAME's own crosshair to where the gun is pointing.
//
// Titanfall draws its crosshair in screen space, pinned to the centre of the
// frame - held a 20 degree head pose and the four reticle dashes stayed on the
// same pixels while the world rotated under them. That is correct only while the
// rendered view is the aim. As soon as the head turns the view off the gun, a
// centred crosshair points at whatever the head is looking at rather than where
// a shot goes, which is the exact confusion decoupled look and aim exists to
// avoid.
//
// The crosshair is drawn through RUI, Respawn's UI system. `CViewRender::
// RenderView` calls the crosshair submitter (client.dll+0x15ef90), which builds
// one RUI instance per crosshair element, hangs a shared transform off each
// instance at +0x38, and fills each instance's arguments through
// client.dll+0x158eb0 - a function nothing else calls. Hooking that filler
// therefore sees every crosshair element of the frame and nothing else, and the
// transform's origin (block+0x10) is the whole crosshair's position in pixels.
// Measured: 200 into its x moves the crosshair 200 px right, into its y 200 px
// down. The second record at block+0x40 has the same shape and no visible
// effect.
//
// So the mark the player follows is the game's own - right weapon, right spread,
// right state changes - just drawn where the gun points.
bool InstallCrosshairHook();

// Called once per rendered frame from the camera hook, on the render thread.
// `visible` is false whenever the frame is being drawn along the aim anyway
// (sights up, tracking off, not a campaign map), and the crosshair is left where
// the game put it. `ndcX` / `ndcY` are the aim's position in the drawn frame,
// x right, y up, -1..1; `offScreen` says the aim is behind the rendered view,
// where there is no honest place to draw it and it is hidden instead.
void PublishAim(bool visible, bool offScreen, float ndcX, float ndcY);

}  // namespace headtracking
