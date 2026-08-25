# Changelog

## [Unreleased]

### Added
- Added a single previous log generation: the launch before the current one is
  kept as `Titanfall2HeadTracking.prev.log`. The fault handler asks the user to
  send the log, and relaunching to go find it used to truncate away the crash
  being reported.
- Added the initial scaffold: an x64 Ultimate ASI Loader (`dsound.dll`) C++ mod
  for Titanfall 2 (Respawn's modified Source Engine), built on the CameraUnlock
  shared library.
- Added 3DOF rotation and 6DOF position head tracking via OpenTrack UDP
  (port 4242).
- Added render-view injection into `CViewRender::RenderView`, rewriting the
  camera in all three of the render view structs the frame uses (the world view,
  the viewmodel view and the 3D skybox) and leaving the game's aim / projectile
  / trace path on the clean camera, so look and aim stay decoupled.
- Added campaign-only enforcement: the loaded map name is read every frame and
  tracking is applied only on `sp_*` maps. Multiplayer, Frontier Defense and the
  lobby (`mp_*`) render vanilla, as does anything the mod cannot positively
  identify as campaign.
- Added gameplay-only enforcement: tracking is suppressed whenever the host's
  pause flag is set, which covers the pause menu, the between-mission logbook
  screen and the automatic pause on losing window focus.
- Added ADS handling that hands the view back to the gun: raising the sights
  eases the head delta out over 150 ms, so the frame settles onto the aim -
  which is where the crosshair already was - and both the head rotation and the
  positional lean stay off until the sights come down, when tracking eases back
  in over 250 ms. The player's aim is never touched. Aiming is read from the
  game's own sights flag rather than from a zoom factor, so it is detected on
  weapons that have sights but no magnification - which is most of them, and
  where a moving eye position makes the sights unusable.
- Added parallax correction to the crosshair. Leaning moves the eye the frame is
  drawn from but not the eye the shot comes from, so a crosshair projected as a
  direction slides off whatever the player was aiming at - further the closer the
  target - while the bullet carries on where it was going. The mod now traces the
  aim through the game's own world trace at 15 Hz, smooths the distance, and
  projects the hit POINT from the leaned eye, so leaning changes what you see
  and not what the crosshair is on.
- Added a crosshair that stays on the gun. Titanfall draws its crosshair pinned
  to the centre of the screen, which marks where a shot lands only while the
  view and the aim are the same direction - so once the head turns it points at
  whatever you are looking at rather than where you are aiming. The game's OWN
  crosshair is now moved to where the aim projects into the head-tracked
  picture: same crosshair, same weapon-specific shape and spread, just drawn
  where the gun points. It returns to the centre with the sights up, with
  tracking off and outside the campaign, and is hidden outright if the head
  turns so far that the gun is behind the picture. `[View] MoveCrosshair`
  turns it off.
- Added world-space and camera-local yaw modes, switchable at runtime
  (Page Down / Ctrl+Shift+H).
- Added horizon-locked 6DOF, so the lean follows the body and stays correct
  while the camera is pitched down or rolled by a wall-run.
- Added an ease-out of the held pose on tracking loss instead of snapping the
  view to centre.
- Added Northstar detection: the community multiplayer client runs on the same
  retail `client.dll` and can host campaign maps, so the mod refuses to engage
  under it.
- Added a PE-fingerprint build-profile registry so the mod engages only on known
  `client.dll` AND `engine.dll` builds and stays dormant otherwise, including on
  a profile whose offsets have not been filled in yet, plus
  `pixi run check-fingerprint` to print a paste-ready profile stub for a new
  build.

### Changed
- Removed recentring from the mod. The `Home` / `Ctrl+Shift+T` hotkey and the
  `[Hotkeys] Recenter` key are gone and the tracker pose is applied as sent.
  Every tracker app centres itself, so a mod-side centre sat in series with the
  tracker's own and the two drifted apart. Centre in your tracker app instead:
  OpenTrack's Center bind, or the CENTER button in Headcam.
- Changed `[Position] InvertX` and `InvertZ` to default to on. The trackers this
  mod is used with send sideways and forward the other way round from the shared
  core's frame, so without it a lean left moved the camera right and leaning in
  pulled the view back. Set either to 0 if yours already matches.
- Changed smoothing to two keys in `[Smoothing]`: `LocalSmoothing`
  (default 0.0) for a tracker running on this machine and `RemoteSmoothing`
  (default 0.15) for a remote device on the network, selected per connection
  from the packet source address.

### Fixed
- Fixed the head rotation never reaching the screen. It was being composed onto
  the result of `IVEngineClient::GetViewAngles` for callers inside
  `CViewRender::SetUpView`, on the theory that the whole render pipeline is
  derived from that one read - but SetUpView does not call it at all, so every
  frame computed a rotation, published it and discarded it. The rotation goes
  back into the render views, where it is measured to work; the culling it was
  meant to fix is handled by the field-of-view widening below.
- Fixed geometry being culled out of the head-tracked view. The engine builds
  the frame's camera once, from an origin and a set of angles, and culls against
  the frustum that implies - all of it before the render views the mod used to
  write exist. The head rotation now goes into those angles instead, for exactly
  the span of that one call, so the frustum is AIMED at the head rather than
  drawn through a cone that has already discarded what the head turned to look
  at. There is no limit to how far the head can turn, nothing extra is submitted,
  and it behaves the same at any aspect ratio. The game's own angles are back to
  their clean value before anything reads them for aim, projectiles or traces.
- Removed the field-of-view widening that used to stand in for this.
  `[View] CullFovScale` now defaults to 1.0 (off). Widening was the wrong lever:
  the frustum grows in tangent, so on a 32:9 monitor - where the drawn cone is
  already about 123 degrees across - trebling it bought roughly 22 degrees of
  head turn each side while pushing the projection past 165 degrees, which
  flickered. The setting is kept for the positional lean, which moves the eye
  without moving the cone.
- Fixed the lifecycle log (game build, profile match, hook install, map gate,
  crash report) never being written. `[Debug] LogToFile` closed the log file
  three lines into startup, so the one thing the README asks a user to send was
  a zero-byte file; the key now gates only the per-frame `[view]` diagnostics.
- Fixed `[Position] InvertZ` mirroring the lean envelope. Inversion is applied
  before the asymmetric Z clamp, so a forward lean now gets the generous 0.40 m
  allowance whichever way the key is set; previously turning it on left the
  forward lean with the 0.10 m backward allowance.
- Fixed a profile that matches but is incomplete, or matches `client.dll` but
  not `engine.dll`, stopping the search. It now skips to the next profile, so
  adding a profile for a new patch cannot strand users on an older build.
- Fixed head tracking being able to drive the view past vertical in world-space
  yaw mode.
- Fixed the mod installing its hook under Northstar and only then declining to
  apply the pose; it now refuses to install the hook at all.
- Fixed a deadlock on unload. The module now pins itself instead of tearing
  down; the previous `DLL_PROCESS_DETACH` path joined threads and suspended the
  process from inside the loader lock.
- Fixed the crash handler blocking the render thread. It now logs through a
  lock-free channel rather than the normal logger, which could block forever on
  a mutex orphaned by the fault it was reporting.
- Fixed the tracking-mode cycle being applied from the hotkey thread mid-frame;
  it now runs on the render thread.
- Fixed head tracking snapping to the full pose on the first packet after a
  tracking dropout; it now blends back in.
- Fixed out-of-range position limits, sensitivities and `WorldScale` in a
  hand-edited INI being passed through to the view matrix; they are now bounded.
- Fixed tracking being applied over a loading screen by suppressing it for a
  moment after a level name appears.

### Removed
- Removed `[Smoothing] Amount` and `[Position] Smoothing`; rotation and position
  both use the new `LocalSmoothing` / `RemoteSmoothing` pair.
- Removed the hidden 0.15 baseline smoothing floor, so a local tracker gets
  zero-latency tracking by default.
