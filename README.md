# Control: Ultimate Edition Head Tracking

![Control: Ultimate Edition running with this mod](https://raw.githubusercontent.com/itsloopyo/control-ultimate-edition-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Control: Ultimate Edition that moves the camera with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - head movement steers the camera; aim stays on your mouse or controller
- **6DOF positional tracking** - lean and peek by moving your head, horizon-locked so leaning forward travels along the ground

## Requirements

- [Control: Ultimate Edition on Steam](https://store.steampowered.com/app/870780/) (app `870780`). The standard edition is untested and may need a different game-detection entry.
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack) with its UDP output, or a phone app that sends OpenTrack UDP pose data to port 4242.
- Windows 10 or 11, 64-bit.

## Installation

1. Download the latest `ControlHeadTracking-v<version>-installer.zip` from the [Releases](https://github.com/itsloopyo/control-ultimate-edition-headtracking/releases) page.
2. Extract the ZIP anywhere.
3. Double-click `install.cmd`. It auto-detects your Steam install, installs the Ultimate ASI Loader if it is not already present, and deploys `ControlHeadTracking.asi` next to `Control_DX12.exe`.
4. Configure OpenTrack (or your phone app) to send UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the install folder explicitly with either an environment variable or a positional argument:

```powershell
# Environment variable override
$env:CONTROL_PATH = "D:\Games\Control"; .\install.cmd

# Or pass the path directly
.\install.cmd "D:\Games\Control"
```

### Manual Installation

If you prefer to place files by hand:

1. Install the Ultimate ASI Loader yourself by copying its DLL (renamed to `winmm.dll`) next to `Control_DX12.exe`.
2. Download `ControlHeadTracking-v<version>-nexus.zip` and extract `ControlHeadTracking.asi` into the same directory as `Control_DX12.exe`.

The Nexus ZIP does not bundle the ASI loader; you manage that yourself.

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

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay.
2. Positional tracking disabled, rotational tracking enabled.
3. Rotational tracking disabled, positional tracking enabled.
4. Back to normal.

## Configuration

The mod writes `HeadTracking.ini` next to `Control_DX12.exe` on first launch.
Edit it and restart the game to apply changes. Hotkey values are Win32
Virtual Key codes in decimal.

A comment has to sit on its own line, above the key. The parser hands the whole
text after `=` to the value reader. For a `true`/`false` or text setting that
text is compared as a whole, so a trailing `; note` makes the comparison fail
and the setting silently keeps its default. Numeric settings survive a trailing
comment because the number is read off the front of the text, which is why some
lines below still carry one. Putting every comment on its own line always works.

```ini
[Network]
UdpPort = 4242            ; OpenTrack UDP port. All interfaces are listened on.

[General]
; start with tracking active
EnableOnStartup = true
; enable 6DOF positional tracking
PositionEnabled = true
; true = horizon-locked yaw, false = camera-local
WorldSpaceYaw = true

[Camera]
; Field of view multiplier. This is the same multiplier as Control's FOV Scale
; slider under Options > Graphics, written past the 0.75-1.25 range the game
; clamps that slider to and re-applied every frame. 0 leaves Control's own
; slider in charge; otherwise 0.5 to 2.0. It scales the tangent of the
; half-angle, so from Control's default 70 degrees horizontal: 1.25 gives 82,
; 1.5 gives 93, 2.0 gives 109. Aim zoom still works, and cutscenes keep their
; authored framing.
FovScale = 0.00

[Rotation]
YawSensitivity = 1.00     ; multiplier for left/right
PitchSensitivity = 1.00   ; multiplier for up/down
RollSensitivity = 1.00    ; multiplier for tilt
InvertYaw = false
InvertPitch = false
InvertRoll = false
; Smoothing covers rotation and position alike. Which of the two is used is
; picked per connection from the packet's source address, and only LOOPBACK
; counts as local: a tracker running on this PC but sending to this machine's
; LAN address is treated as remote. Nothing floors either value; 0 is the
; lightest setting, a 20 ms time constant, not literally instant.
LocalSmoothing = 0.00     ; tracker sending to 127.0.0.1. 0 = responsive, 1 = heavy
RemoteSmoothing = 0.15    ; tracker on the network, e.g. a phone over WiFi
YawDeadzone = 0.00        ; degrees of dead center
PitchDeadzone = 0.00
RollDeadzone = 0.00

[Position]
SensitivityX = 1.00       ; lean left/right
SensitivityY = 1.00       ; up/down
SensitivityZ = 1.00       ; forward/back
LimitX = 0.30             ; meters, symmetric
LimitY = 0.20             ; meters, symmetric
LimitZ = 0.40             ; meters forward
LimitZBack = 0.10         ; meters back

[Hotkeys]
; Win32 Virtual Key codes. Defaults: End, PageUp, PageDown.
Toggle = 35
TogglePosition = 33
ToggleYawMode = 34
```

## Troubleshooting

**Mod not loading.**
- Confirm `winmm.dll` and `ControlHeadTracking.asi` both sit next to `Control_DX12.exe`.
- Check `HeadTracking.log` in the same directory for startup diagnostics. It is rewritten from scratch on every launch; the previous launch is kept as `HeadTracking.prev.log`, which is the one to send if the game crashed and you have relaunched since.
- Look for `Build profile ... matched` in the log. If it instead says the EXE is
  newer or older than any known build, the game has been patched and the mod has
  deliberately stayed dormant; check the releases page for an update.

**No tracking response.**
- Confirm OpenTrack is outputting UDP to `127.0.0.1:4242`.
- Check the game holds the port: `Get-Process -Id (Get-NetUDPEndpoint -LocalPort 4242).OwningProcess` in PowerShell should name `Control_DX12` (or `Control_DX11`).
- Tracking may be disabled. Press `End` (or `Ctrl+Shift+Y`) and watch `HeadTracking.log` for the `Head tracking ENABLED` line.
- Head tracking only drives the camera during gameplay. It is off on the title
  screen, in the main menu, while a level loads, during cutscenes and in the
  pause menu.

**Jittery or unstable tracking.**
- Raise the smoothing value your tracker actually uses in the `[Rotation]` section toward `0.30` or higher: `LocalSmoothing` if it sends to `127.0.0.1`, `RemoteSmoothing` otherwise. Only loopback counts as local, so OpenTrack running on this PC but pointed at your LAN address gets `RemoteSmoothing`. The log records which of the two is in effect once tracking is running.
- Wireless and webcam trackers are noisier; a little extra smoothing helps.

**Wrong rotation axis or inverted view.**
- Flip the relevant `Invert` toggle in the `[Rotation]` section. They all ship as
  `false`: the directions Control needs are already applied inside the mod, so a
  toggle only ever compensates for a tracker whose own axes run the other way.
- If yaw feels wrong at extreme up/down angles, toggle yaw mode with `Page Down` (or `Ctrl+Shift+H`).

## Updating

Download the new release and run `install.cmd` again. Your `HeadTracking.ini`
config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLL and leaves your
`HeadTracking.ini` alone. The Ultimate ASI Loader (`winmm.dll`) is removed only
if the installer put it there (tracked via `.headtracking-state.json`). Use
`uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires Visual Studio 2022 with the C++ workload, CMake 3.20 or newer, and
[pixi](https://pixi.sh) for task orchestration.

```powershell
git clone --recursive https://github.com/itsloopyo/control-ultimate-edition-headtracking
cd control-ultimate-edition-headtracking
pixi run build
```

Output: `bin/Release/ControlHeadTracking.asi`.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. Third-party components bundled
or linked into the mod keep their own licenses, listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- **Remedy Entertainment** and **505 Games** for Control.
- **ThirteenAG** for the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (MIT).
- **TsudaKageyu** for [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause).
- The [OpenTrack](https://github.com/opentrack/opentrack) contributors for the UDP wire format (ISC).
- [CameraUnlock Core](https://github.com/itsloopyo/cameraunlock-core) (MIT) for the shared head tracking pipeline.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Remedy
Entertainment or 505 Games. Use at your own risk.
