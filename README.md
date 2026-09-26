# Titanfall 2 Head Tracking

![Titanfall 2 running with this mod](https://raw.githubusercontent.com/itsloopyo/titanfall-2-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Titanfall 2 that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

> **Campaign only, uninstall it before you play multiplayer.** Decoupled
> look from aim gives an unfair advantage online, so the mod applies nothing
> outside campaign (`sp_*`) maps. That is enforced in code - but the mod is still
> loaded in the game process while you play, and hooked into `client.dll`. Run
> `uninstall.cmd` before you go online.

## Features

- **Decoupled look and aim** - head tracking moves the camera; your shots still go where the mouse or controller points.
- **6DOF positional tracking** - lean and peek with head position, not just rotation.
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Titanfall 2](https://store.steampowered.com/app/1237970/Titanfall_2/) on Steam.
- A head-tracking source that emits the OpenTrack UDP protocol on port 4242: [OpenTrack](https://github.com/opentrack/opentrack) driven by a webcam or a VR headset, or a phone app such as [Headcam](https://headcam.app).
- Windows 10 or 11, 64-bit. Titanfall 2 is a 64-bit game and the mod ships as a 64-bit ASI plugin.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Titanfall 2**, and click
**Play with head tracking**.

### Standalone Installer

1. Download `Titanfall2HeadTracking-vX.Y.Z-installer.zip` from the [Releases page](https://github.com/itsloopyo/titanfall-2-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds your Steam copy of Titanfall 2 and places the Ultimate ASI Loader (as `dsound.dll`) plus `Titanfall2HeadTracking.asi` next to `Titanfall2.exe`.
4. Configure OpenTrack (or your phone app) to output UDP to `127.0.0.1:4242`. See [Setting Up OpenTrack](#setting-up-opentrack).
5. Launch the game. The mod creates `CameraUnlock.ini` next to `Titanfall2.exe` on its first run. See [Configuration](#configuration).

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
3. Launch the game once. `CameraUnlock.ini` and `Titanfall2HeadTracking.log` are created next to `Titanfall2.exe`.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets, use whichever your keyboard has. These are the
defaults: each action's keys are a list under `[Hotkeys]` in `CameraUnlock.ini`,
chords included, and any of them can be changed or removed.

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

Centre in your tracker app once you are seated normally: OpenTrack's Center bind, the CENTER button in Headcam, or SteamVR's reset.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` switches yaw between world-space (horizon-locked) and camera-local.

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` the moment you
change them, so the next launch starts with the same choice. Toggling tracking on
or off with `End` lasts for the session only: each launch starts with tracking on
or off as `EnableOnStartup` says.

### Aiming down sights

Head tracking stays on while you aim. The weapon stays where your mouse or
controller points it, so with your head turned it sits off to one side with its
sights still lined up, and your rounds land where those sights point. Head
movement is scaled to the zoom, so a scope does not magnify it. Leaning eases out
while the sights are up, because it would move your eye off them.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.

A setting that the defaults below set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it. `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.

Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults below. Every setting they set to `default` then follows `Defaults.ini`.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Titanfall 2 head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[View]
; Field of view in degrees, on the same scale as the game's own slider.
; 0 follows that slider as you move it. Any other value is drawn instead, held to
; 50 to 130, so it can go outside the 70 to 119 the slider allows.
FieldOfView=0.0
; How much wider than the drawn field of view the game is told to cull, 1.0 to 1.7.
; 1.0 adds nothing: the culled view already turns with your head. Raise it only if
; a lean shows missing scenery at the edge of the screen; it costs frames.
CullFovScale=1.0

[Debug]
; true: write a line per frame about the view to Titanfall2HeadTracking.log.
; The game build, the hook install, the map gate and any crash are logged either way.
LogToFile=false
; true: write the game's render view data to the log once, for finding its
; layout again after a game update.
DumpViewSetup=false
```
<!-- /cameraunlock:config -->

The mod has no sensitivity, deadzone, world scale or axis inversion settings. It
applies the pose your tracker sends, so set those in the tracker. The game's own
crosshair always follows your aim, and no setting turns that off.

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
- For micro-jitter around centre, add a deadzone or a filter in your tracker app. The mod has no deadzone of its own.
- A webcam tracker needs light on your face and a frame rate the camera can sustain. A dark room is the usual cause of unstable neuralnet tracking.

**The weapon is off to one side when I aim down sights.** Your head is turned:
the weapon stays on your aim and you are looking past it. Turn back to it, or
move your aim to where you are looking.

**Wrong rotation axis or wrong direction**

- If yaw feels wrong when looking far up or down, toggle between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked is the default and is horizon-stable; camera-local follows the camera's current up axis.
- If an axis moves the opposite way to your head, invert that axis in your tracker app. The mod has no inversion settings of its own.
- If the centre is off after you sit down, centre it in your tracker app (OpenTrack's Center bind, or Headcam's CENTER button).

### Known limitations

- **The crosshair follows the gun, not your head.** The game's own crosshair is moved to where your shot will land in the head-tracked picture, so it stays on the aim however far you look away. The hit mark that flashes when a shot connects rides along with it.
- **Leaning swings the weapon a long way across the screen.** Your gun sits under a meter from your eye, so a real 30 cm lean moves it much further than it moves the world, which is what leaning does to something held in your hands.
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

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

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
