#pragma once

#include "marker_projection.h"

namespace headtracking {

// Puts the game's world-anchored HUD marks back on the things they mark: the
// BT-7274 marker, objective waypoints, anything the HUD draws over a place in
// the world.
//
// Reported from the chair as the BT marker staying fixed in screen space while
// the head turns - the marker sits on the glass and BT slides out from under it.
//
// ---- Why they do not follow -------------------------------------------------
//
// They are placed by the client's own world-to-screen,
// `WorldToScreen(int* x, int* y, const Vector& world, int w, int h, int offX,
// int offY)` at client.dll+0x19a9f0, which projects through the matrix the
// CViewRender holds and converts the result to pixels. Every HUD marker path
// goes through it: the screen-indicator placement at 0x199330 (the one that
// clamps an off-screen target onto the `screen_indicator_ellipse_*` ellipse) and
// 0x199830, and the `GetEntScreenSpaceBounds` script native above them
// (0x13ed70 -> 0x15c050 -> 0x14d370), which is how a script asks where an entity
// is on screen before it puts a marker there.
//
// What it projects with is the camera the GAME thinks it is drawing. The head
// rotation is composed onto the setup's angles for the span of the view build
// and taken straight back out (view_angles_hook.h), and the lean is written into
// the render views afterwards - so anything that reads the camera for itself is
// still reading the clean one. The marker therefore lands where its target would
// be if the head had never moved, which is a fixed screen position while the
// world turns underneath it.
//
// ---- The fix ----------------------------------------------------------------
//
// The mark is not moved after the fact. The world point is moved BEFORE the game
// projects it, out of the frame the picture was drawn in and into the frame the
// game is projecting with (marker_projection.h). The game's own projection then
// produces the head-tracked answer, and the off-screen ellipse clamp and the
// behind-the-camera test that read that answer come along with it.
//
// Nothing of the mod's own maths reaches the screen here, which is the point: a
// projection of ours would have to reproduce the game's field of view, aspect
// and letterboxing exactly, and would disagree with it the first time any of the
// three moved.
bool InstallWorldMarkerHook();

// The frame's two cameras, published once per frame by the render hook. A null
// pointer - tracking off, suppressed by a gate, or a frame carrying no delta -
// leaves every marker exactly where the game put it.
void PublishFrameCameras(const FrameCameras* cameras);

}  // namespace headtracking
