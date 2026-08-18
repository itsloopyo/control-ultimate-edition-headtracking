# Control: Ultimate Edition Head Tracking

![Mod GIF](https://raw.githubusercontent.com/itsloopyo/control-ultimate-edition-headtracking/main/assets/readme-clip.gif)

Head tracking for Control: Ultimate Edition that lets your head movement steer the camera while your mouse or controller handles the aim, no VR headset required.

## Features

- **Decoupled look and aim** - head movement steers the camera; aim stays on your mouse or controller
- **6DOF positional tracking** - lean and peek by moving your head, horizon-locked so leaning forward travels along the ground

## Requirements

- [Control: Ultimate Edition on Steam](https://store.steampowered.com/app/870780/) (app `870780`). The standard edition is untested and may need a different game-detection entry.
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack) with UDP output, or a phone tracker app that speaks the same protocol (SmoothTrack, HeadTrack, etc.).
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

1. Open OpenTrack.
2. Set **Output** to **UDP over network**.
3. Configure the UDP output to `127.0.0.1`, port `4242`.
4. Start tracking, then sit in your normal playing position and hold still for a
   moment. The mod recenters once, on the first pose you hold steady, so
   whatever you are holding then becomes your centre. `Home` recenters again at
   any time.

### VR Headset Setup

1. Connect your headset to the PC over Air Link or Virtual Desktop and start SteamVR.
2. In OpenTrack, set **Input** to **SteamVR**.
3. Set **Output** to **UDP over network** at `127.0.0.1:4242` as above.

### Webcam Setup

1. In OpenTrack, set **Input** to the **neuralnet tracker**.
2. Pick your webcam and follow OpenTrack's calibration.
3. Set **Output** to **UDP over network** at `127.0.0.1:4242`.

### Phone App Setup

- If your phone app already smooths its output, point it directly at the PC running the mod on port `4242`.
- If you want OpenTrack's curve mapping and filtering, send from the phone into OpenTrack instead, then have OpenTrack output UDP to `127.0.0.1:4242`.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Recenter            | `Home`      | `Ctrl+Shift+T`  |
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
; Win32 Virtual Key codes. Defaults: Home, End, PageUp, PageDown.
Recenter = 36
Toggle = 35
TogglePosition = 33
ToggleYawMode = 34
```

## Troubleshooting

**Mod not loading.**
- Confirm `winmm.dll` and `ControlHeadTracking.asi` both sit next to `Control_DX12.exe`.
- Check `HeadTracking.log` in the same directory for startup diagnostics.
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

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

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
