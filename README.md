# Titanfall 2 Head Tracking

![Titanfall 2 running with this mod](https://raw.githubusercontent.com/itsloopyo/titanfall-2-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Titanfall 2 that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

> **Campaign only, uninstall it before you play multiplayer.** Decoupled
> look from aim gives an unfair advantage online, so the mod applies nothing
> outside campaign (`sp_*`) maps. That is enforced in code - but the mod is still
> loaded in the game process while you play, and hooked into `client.dll`. Run
> `uninstall.cmd` before you go online.

## Features

- **Decoupled look and aim** - head tracking moves the camera; your shots still go where the mouse or controller points.
- **6DOF positional tracking** - lean and peek with head position, not just rotation.

## Requirements

- [Titanfall 2](https://store.steampowered.com/app/1237970/Titanfall_2/) on Steam.
- A head-tracking source that emits the OpenTrack UDP protocol on port 4242: [OpenTrack](https://github.com/opentrack/opentrack) driven by a webcam or a VR headset, or a phone app such as [Headcam](https://headcam.app).
- Windows 10 or 11, 64-bit. Titanfall 2 is a 64-bit game and the mod ships as a 64-bit ASI plugin.

## Installation

1. Download `Titanfall2HeadTracking-vX.Y.Z-installer.zip` from the [Releases page](https://github.com/itsloopyo/titanfall-2-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds your Steam copy of Titanfall 2 and places the Ultimate ASI Loader (as `dsound.dll`) plus `Titanfall2HeadTracking.asi` next to `Titanfall2.exe`.
4. Configure OpenTrack (or your phone app) to output UDP to `127.0.0.1:4242`. See [Setting Up OpenTrack](#setting-up-opentrack).
5. Launch the game. The mod writes a default `HeadTracking.ini` next to `Titanfall2.exe` on its first run.

If the installer cannot find your game, point it at the install folder yourself. Either set the environment variable:

```powershell
$env:TITANFALL_2_PATH = "D:\Games\Titanfall2"
.\install.cmd
```

or pass the path as the first argument:

```powershell
.\install.cmd "D:\Games\Titanfall2"
```

### Manual Installation

For placing the files by hand:

1. Copy `plugins\Titanfall2HeadTracking.asi` from the installer ZIP into your Titanfall 2 folder, next to `Titanfall2.exe`. The `-nexus.zip` release asset contains this file plus `Titanfall2HeadTracking-LICENSE.txt`, laid out for extracting straight into the game folder.
2. Copy `vendor\ultimate-asi-loader\dinput8.dll` from the installer ZIP into the same folder, renamed to `dsound.dll`. Titanfall 2 imports `dsound.dll`, so that is the proxy name the loader has to use here. The Nexus ZIP does not carry the loader; get it from [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) if you are installing that way.
3. Launch the game once. `HeadTracking.ini` and `Titanfall2HeadTracking.log` are created next to `Titanfall2.exe`.

## Setting Up OpenTrack

1. Set **Output** to `UDP over network`, then open its options and set the remote address to `127.0.0.1` and the port to `4242`.
2. Pick your **Input** (tracker) from the sections below.
3. Map yaw, pitch and roll, plus X, Y and Z if you want 6DOF, on the Curves tab.
4. Press **Start**, then centre it with OpenTrack's Center bind once you are seated normally.

### VR Headset Setup

1. Connect the headset to the PC with Air Link, Virtual Desktop, or a cable link.
2. Launch SteamVR and confirm the headset is tracking.
3. In OpenTrack, set **Input** to `SteamVR`, then Start.
4. Centre the headset in SteamVR or OpenTrack while you are looking straight at the screen.

### Webcam Setup

1. In OpenTrack, set **Input** to `neuralnet tracker`.
2. Open its options, pick your webcam, and set a resolution and frame rate the camera can sustain.
3. Sit at your normal playing distance with your face fully in frame, then Start.

### Phone App Setup

The mod takes the OpenTrack UDP protocol on port `4242` and nothing else, so a phone app works here if it can send that protocol, either itself or through a companion app on the PC. For one that can, what decides how you wire it up is how much filtering it does before the packet leaves the phone.

- **Send directly to port `4242`** when the app filters its own signal on the device: point it at this PC's IP address, UDP port `4242`, using the OpenTrack packet format. A raw or lightly filtered feed sent straight here will jitter, because the mod's smoothing is sized to take the edge off a clean signal rather than to rescue a noisy one. I made [Headcam](https://headcam.app) so that decent tracking was free for anybody with a phone already in their pocket, and it filters on-device, so it can send direct. Any other app that filters enough noise works exactly the same way.
- **Relay through OpenTrack** when the app sends a raw feed, or when you want OpenTrack's curve mapping: set OpenTrack's **Input** to `UDP over network` on a different port, have the phone send to that port, and leave OpenTrack's **Output** on `127.0.0.1:4242`. OpenTrack's filters clean the feed up before it reaches the game.

Not sure which yours is? Try direct first, then hold your head still and watch the view. If it drifts or shakes, route it through OpenTrack.

Traffic from another device on the network is treated as a remote connection and gets `RemoteSmoothing` rather than `LocalSmoothing`. A tracker on this PC that sends to the machine's LAN address instead of `127.0.0.1` counts as remote too, because the mod classifies by packet source address.

## Controls

Two equivalent binding sets, use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |
| Cycle ADS mode      | `Insert`    | `Ctrl+Shift+U`  |

Centre in your tracker app once you are seated normally: OpenTrack's Center bind, the CENTER button in Headcam, or SteamVR's reset.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` switches yaw between world-space (horizon-locked) and camera-local.

`Insert` / `Ctrl+Shift+U` cycles what happens when you aim down sights. All
three start the same way - raising the sights swings the view onto the point the
reticle was marking, so your shot lands where you had it lined up - and they
differ in what happens for the rest of the aim:

1. **Tracking paused** (default) - the game keeps the camera for as long as the
   sights are up. The sight picture is exactly the game's, and turning or leaning
   your head does nothing until you lower the weapon. Tilting it still rolls the
   view, in this mode and the other two: a tilt does not move your eye off the
   barrel or the aim off the middle of the screen, so there is nothing to hand
   back to the gun.
2. **Tracking on, with an aim marker** - head tracking carries on from the
   snapped position, and a small white crosshair is drawn wherever your rounds
   will actually land. This white marker is authoritative, including with scoped
   weapons. A scope's built-in reticle is only accurate while your eye is
   exactly aligned with the optic, so the two reticles separate when head
   tracking moves your view off that sight line.
3. **Tracking on, no aim marker** - the same as 2 without the marker, for a
   cleaner screen when you are happy reading the sights themselves.

The choice is saved to `HeadTracking.ini`, so it survives a restart. Pressing the
key writes the mode you switched to into `Titanfall2HeadTracking.log`.

## Configuration

`HeadTracking.ini` is created next to `Titanfall2.exe` on first launch. Edit it with any text editor and restart the game to apply. Delete it to reset to defaults.

```ini
; Titanfall 2 head tracking - default config

[Network]
Port=4242
EnableOnStartup=1

[Sensitivity]
Yaw=1
Pitch=1
Roll=1
InvertYaw=0
InvertPitch=0
InvertRoll=0

[Smoothing]
; Smoothing applied when the tracker runs on this machine (loopback).
; 0 = no smoothing, 1 = heavy. Covers rotation and position.
LocalSmoothing=0
; Smoothing applied when the tracker is a remote device on the network.
; 0 = no smoothing, 1 = heavy. Covers rotation and position.
RemoteSmoothing=0.15

[Deadzone]
Yaw=0
Pitch=0
Roll=0

[Position]
; 6DOF head position, applied to the render view origin only
Enabled=1
; WorldScale = Source units per metre of head movement (1 unit = 1 inch; 39.37 = 1:1)
WorldScale=39.37
SensX=1
SensY=1
SensZ=1
; X and Z are inverted by default: the trackers this mod is used with send
; sideways and forward the other way round from the mod's own frame. Set them
; to 0 if leaning moves the camera the wrong way for yours.
InvertX=1
InvertY=0
InvertZ=1
; Movement envelope in metres before world scaling
LimitX=0.3
LimitY=0.2
LimitZ=0.4
LimitZBack=0.1

[Hotkeys]
Toggle=0x23
YawMode=0x22
; Page Up: cycle 6DOF -> rotation-only -> position-only
ModeCycle=0x21
; Insert: cycle what head tracking does while the sights are up
AdsMode=0x2D

[View]
; true = horizon-locked yaw (default), false = camera-local yaw
WorldSpaceYaw=1
; Field of view in degrees, the same numbers the game's own slider uses.
; 0 follows that slider live. Anything else overrides it and can go outside
; the 70-119 the slider allows - 50 to 130 is accepted.
FieldOfView=0
; How much wider than the drawn FOV the engine is told to cull. The head turn
; itself needs none of this - the culling frustum is aimed where you are
; looking - so this only covers a positional lean at the very edge of the
; frame. Above 1.0 costs frames for geometry that is submitted and not drawn.
CullFovScale=1.0
; Move the game's own crosshair to where the gun is pointing, and the hit mark
; that flashes on a connecting shot along with it. false leaves both pinned to
; the centre of the screen, where they mark the aim only while your head is
; centred.
MoveCrosshair=1
; What head tracking does while the sights are up. Cycled in game with Insert
; or Ctrl+Shift+U, which writes the new value back here.
;   paused  = tracking stands down until you lower the weapon (default)
;   marker  = tracking stays live and a white cross marks where rounds land
;   tracked = tracking stays live with nothing drawn
; A head TILT rolls the view in all three: it moves neither your eye off the
; barrel nor the aim off the middle of the screen, so there is nothing to
; hand back to the gun.
AdsMode=paused

[Debug]
; Per-frame view diagnostics. The lifecycle lines - game build, profile
; match, hook install, map gate, crash report - are always written.
LogToFile=0
; One-shot render view field dump, for rederiving offsets on a new build
DumpViewSetup=0
```

`WorldScale` is the main lean tuning knob: lower it if leaning swings the weapon further across the screen than you want.

## Troubleshooting

Start with `Titanfall2HeadTracking.log` next to `Titanfall2.exe`. It always records whether the loader engaged, whether the camera hook matched your game build, which map the campaign-only gate saw, and whether tracking packets are arriving. It is rewritten from scratch on every launch, and the launch before it is kept as `Titanfall2HeadTracking.prev.log` - so if the game crashed and you relaunched before fetching the log, send the `.prev.log` too.

**Mod not loading**

- Confirm `dsound.dll` and `Titanfall2HeadTracking.asi` sit next to `Titanfall2.exe`. If there is no log file at all, the loader never engaged; re-run `install.cmd`.
- If the log says the mod is "staying dormant", your game build is not in the mod's profile registry yet, usually because the game patched. The game runs vanilla and nothing is hooked; check the Releases page for an updated build.
- Check your antivirus has not quarantined the `.asi`. It is an unsigned DLL loaded into a game process, which some scanners flag.

**No tracking response**

- Confirm OpenTrack is running with Output set to `UDP over network` at `127.0.0.1:4242`, and that the tracker is producing motion in OpenTrack's own preview.
- If the log says `UDP port 4242 busy, receiver will retry in background`, another app (a second game, or a second copy of OpenTrack) already holds the port. Close it and keep playing; the mod re-checks twice a second and starts tracking within about half a second, with no restart needed.
- Nothing applies in multiplayer, by design. The log records `head tracking suppressed` with the map name, and the gate latches for the rest of the session, so restart the game before playing the campaign again. Uninstall before playing multiplayer anyway.
- The view stops moving in the pause menu, and while the game is alt-tabbed out, because Titanfall pauses the campaign when it loses focus. Tracking resumes when you do.
- Allow the game through Windows Firewall if your tracker is a phone or another PC on the network.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` if the tracker is a phone or another device on the network. `0.15` is the default and `0.3` is noticeably heavier.
- Raise `LocalSmoothing` above `0` only if a tracker on this PC is genuinely noisy. It defaults to `0` because a wired source is already stable and smoothing only costs latency.
- Add a small `[Deadzone]` value in degrees to kill micro-jitter around center.
- A webcam tracker needs light on your face and a frame rate the camera can sustain. A dark room is the usual cause of unstable neuralnet tracking.

**Wrong rotation axis or wrong direction**

- If yaw feels wrong when looking far up or down, toggle between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked is the default and is horizon-stable; camera-local follows the camera's current up axis.
- If an axis moves the opposite way to your head, set the matching `InvertYaw` / `InvertPitch` / `InvertRoll` (or `InvertX` / `InvertY` / `InvertZ`) to `1`.
- If the centre is off after you sit down, centre it in your tracker app (OpenTrack's Center bind, or Headcam's CENTER button).

### Known limitations

- **The crosshair follows the gun, not your head.** The game's own crosshair is moved to where your shot will land in the head-tracked picture, so it stays on the aim however far you look away. The hit mark that flashes when a shot connects rides along with it.
- **Leaning swings the weapon a long way across the screen.** Your gun sits under a meter from your eye, so a real 30 cm lean moves it much further than it moves the world, which is what leaning does to something held in your hands. Lower `[Position] WorldScale` if you want less of it.
- **The 3D skybox takes head rotation but not lean.** Not noticeable in normal play; a lean at skybox scale would need the sky's own scale factor applied.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files. The Ultimate ASI Loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

Do this before playing multiplayer. The mod applies nothing in a match either way, but uninstalling is what takes it out of the game process entirely.

## Building from Source

Requires [pixi](https://pixi.sh) and the Visual Studio C++ toolchain (x64). No game install is needed to build.

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/titanfall-2-headtracking.git
cd titanfall-2-headtracking
pixi run build-release
pixi run test
pixi run package
```

Release ZIPs land in `release/`.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- Respawn Entertainment and Electronic Arts for Titanfall 2.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (MIT) - loads the mod into the game process.
- [OpenTrack](https://github.com/opentrack/opentrack) (ISC) - the tracking source and UDP protocol.
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause) - inline hooking.
- [CameraUnlock Core](https://github.com/itsloopyo/cameraunlock-core) (MIT) - shared tracking pipeline.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Respawn Entertainment or Electronic Arts. Use at your own risk.
