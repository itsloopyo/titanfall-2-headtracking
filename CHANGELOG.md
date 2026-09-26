# Changelog

## [Unreleased]

### Added
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- Added Titan cockpit tracking: the cockpit now turns with your head instead of
  standing still while the view sweeps across the inside of it. It is a
  viewmodel, so the game placed it from the clean view angles like a pilot's
  weapon - right for a gun you look away from, useless for a shell that wraps
  you. The head rotation is composed onto `C_Titan_Cockpit::CalcView`, so you
  look through the cockpit rather than around it. The positional lean is left
  off it deliberately, so leaning still gives parallax against the canopy. Aim
  is untouched: the game's own view angles never see any of this.
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
- Added ADS handling. Head tracking stays on while you aim down sights, and the
  positional lean eases out over 150 ms as the sights come up, because it moves
  your eye off them, then back in over 250 ms when they come down. Head movement
  is scaled to the zoom, so a scope does not magnify it. The player's aim is
  never touched. Aiming is read from the game's own sights flag rather than from
  a zoom factor, so it is detected on weapons that have sights but no
  magnification.
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
  where the gun points, with the sights up or down. It is hidden outright if the
  head turns so far that the gun is behind the picture.
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
- Settings move to `CameraUnlock.ini`, next to `Titanfall2.exe`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings: `[View] MoveCrosshair=false`. The crosshair now always follows the aim.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] Toggle`, `ModeCycle` and `YawMode`, which held key codes, are now `ToggleKey`, `CycleTrackingModeKey` and `YawModeKey`, and the chords, fixed in code before, can be changed or removed like any other key.
- The tracking mode (`Page Up`) and the yaw mode (`Page Down`) are saved to `CameraUnlock.ini` the moment you change them, so the next launch starts with the same choice. They lasted for the session only before. `End` still changes the session only.
- `[Network] Port` is now `UdpPort`, `[Network] EnableOnStartup` and `[View] WorldSpaceYaw` move to `[General]`, and `[Position] LimitX`, `LimitY`, `LimitZ` and `LimitZBack` are now `PositionLimitX`, `PositionLimitY`, `PositionLimitZ` and `PositionLimitZBack`. The import writes `LimitY` into both `PositionLimitY` and the new `PositionLimitYDown`, since it limits leaning down as well as up (see the next item). `[Position] Enabled` chose the tracking mode the game started in and is now that mode, as `RotationEnabled` and `PositionEnabled`. `[View] FieldOfView` and `CullFovScale` and `[Debug] LogToFile` and `DumpViewSetup` keep their names.
- `[Position] LimitY` now limits leaning down as well as up (0859563). The dev build held leaning down to 0.20 m whatever `LimitY` said, so this changes nothing unless you set `LimitY` to something other than 0.20.
- Removed recentring from the mod. The `Home` / `Ctrl+Shift+T` hotkey and the
  `[Hotkeys] Recenter` key are gone and the tracker pose is applied as sent.
  Every tracker app centres itself, so a mod-side centre sat in series with the
  tracker's own and the two drifted apart. Centre in your tracker app instead:
  OpenTrack's Center bind, or the CENTER button in Headcam.
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
- Fixed an out-of-range position limit in a hand-edited config being passed
  through to the view matrix. A value outside the range a setting accepts now
  keeps that setting's default.
- Fixed tracking being applied over a loading screen by suppressing it for a
  moment after a level name appears.

### Removed
- The sensitivity, scale, deadzone, response curve and axis inversion settings: `[Sensitivity] Yaw`, `Pitch`, `Roll`, `InvertYaw`, `InvertPitch` and `InvertRoll`, `[Deadzone] Yaw`, `Pitch` and `Roll`, and `[Position] WorldScale`, `SensX`, `SensY`, `SensZ`, `InvertX`, `InvertY` and `InvertZ`. Set these in your tracker app instead. The x and z inversions every earlier version shipped switched on (`InvertX=true`, `InvertZ=true`) and the shipped `WorldScale` of 39.37 Source units per metre are now part of how the mod converts the tracker's axes to the game's, so leaning goes the same way and as far as it did. The z flip is applied before the lean is clamped, so leaning in is limited by `PositionLimitZ` and pulling back by `PositionLimitZBack`.
- With these settings at their shipped defaults the camera moves as it did before.
- `[View] MoveCrosshair`. The game's crosshair, and the hit mark that flashes on a connecting shot, always follow the aim.
- `[View] AdsMode` is no longer read: head tracking carries on through the sights in every case, and the lean eases out while they are up (ea74da9).
- `[Hotkeys] AdsMode` is no longer read, and neither it nor `Ctrl+Shift+U` cycles an ADS mode (ea74da9).
- Removed `[Smoothing] Amount` and `[Position] Smoothing`; rotation and position
  both use the new `LocalSmoothing` / `RemoteSmoothing` pair.
- Removed the hidden 0.15 baseline smoothing floor, so a local tracker gets
  zero-latency tracking by default.
