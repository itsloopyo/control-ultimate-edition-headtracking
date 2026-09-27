# Changelog

All notable changes to this project will be documented in this file. Format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- The tracking mode (`PageUp` / `Ctrl+Shift+G`) and the yaw mode (`PageDown` / `Ctrl+Shift+H`) are saved to `CameraUnlock.ini` the moment you change them, and the game starts in them next time. `End` still turns head tracking on or off for the current session only; `EnableOnStartup` says whether it is on when the game starts.
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- Added the camera hook that makes head tracking move the view. The mod hooks
  Control's camera-manager tick, rotates the camera pose for the duration of
  that call and restores it before returning, so the renderer draws the
  head-tracked view while the camera state the game reads afterwards is the one
  it set.
- Added decoupled look and aim, measured in game: holding a 45 degree head
  turn, walking forward moved the player in the same world direction as with
  the head centred, to three decimal places.
- Added full 6DOF. Leaning is mapped against a horizon-locked basis built from
  the clean camera rotation, so it follows your body rather than your gaze and
  leaning forward travels along the ground even with the camera pitched steeply
  down.
- Added a build profile for the DX11 executable alongside the DX12 one, so the
  mod routes on whichever Control launches.
- Added pose and configuration validation. The camera hook rejects a non-finite
  pose instead of handing it to the engine, and every float in the INI is
  checked for being finite and in range when it is read.
- Added a scripted-camera standdown. Head tracking stands down whenever the
  game takes the camera off you, and eases back in over a third of a second
  when you get it back. That covers the establishing shot on every save load,
  cutscenes, and any other moment the view is handed to a scripted camera. The
  establishing shot is what made it necessary: it starts fully upside down, and
  while the camera is inverted horizon-locked yaw turns about world up, so
  turning your head steered the view backwards.

  It reads Control's own count of how many camera entities the player camera is
  currently blending toward. Gameplay leaves that list empty. Nothing about the
  camera's movement is involved, so it works on a cutscene whose camera sits
  perfectly still, and it cannot be fooled by you standing still or by combat
  camera shake.
- Added crosshair compensation, so the crosshair follows your aim rather than
  sitting at screen centre. Head tracking moves what you see but not where you
  shoot, so at any appreciable head angle a centred crosshair stops marking
  where shots land. Control's own crosshair and ammo counter are moved to the
  point the game is actually aiming at, which keeps their art, their
  weapon-specific shapes and the way the game hides them in context. The
  reticle is hidden outright when a head turn puts the aim point behind the
  view, rather than pinned to a screen edge that would imply a target there.

### Changed

- Settings move to `CameraUnlock.ini`, in the game folder. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, deadzone or axis inversion you changed from its default. Set these in your tracker instead.
  - A hotkey set to Ctrl, Shift or Alt on its own. That key goes down before the key of any chord made with it, so the hotkey is left unbound, and it keeps its Ctrl+Shift chord.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. The Ctrl+Shift+Y, G and H chords were fixed before and can now be changed or removed like any other key. `[Hotkeys] Toggle=35`, `TogglePosition=33` and `ToggleYawMode=34`, decimal key codes, become `ToggleKey`, `CycleTrackingModeKey` and `YawModeKey`.
- A hotkey bound to a plain key no longer fires while Ctrl and Shift are both held, so Ctrl+Shift with that key reaches only a binding that names the chord.
- Settings keep their values under their new names: `[Rotation] LocalSmoothing` and `RemoteSmoothing` move to `[Smoothing]`; the `[Position]` limits are `PositionLimitX`, `PositionLimitY`, `PositionLimitZ` and `PositionLimitZBack`, and `PositionLimitYDown` is the downward limit, which `LimitY` set too (see the next item); `[General] PositionEnabled=false` becomes the tracking mode `RotationEnabled=true` and `[Position] PositionEnabled=false`; `[Camera] FovScale` keeps its name. On and off settings are written `true` and `false`.
- `LimitY` limits lowering your head as well as raising it, since 71f6cda. The dev build kept the downward limit at 0.20 metres whatever `LimitY` said.
- A value in `CameraUnlock.ini` outside a setting's range is not used: it keeps the default, and `HeadTracking.log` names the line. Earlier versions clamped a number outside its range to the nearest bound. `UdpPort` takes 1 to 65535, and a port below 1024 still uses 4242 with a log line; the smoothing values take 0 to 1, the position limits 0 to 10, and `FovScale` 0 to 2.0, where a value above 0 and below 0.5 uses 0.5 with a log line. A value `HeadTracking.ini` held is imported as the earlier versions read it.
- `uninstall.cmd` leaves `CameraUnlock.ini` and `HeadTracking.ini` in place, so your settings survive a reinstall.
- The log now keeps one previous generation. Each launch renames the existing
  `HeadTracking.log` to `HeadTracking.prev.log` before opening a fresh one, so
  a crash report written on the way down survives the relaunch that follows it.
  A rename that fails is reported in the fresh log, so a stale `.prev.log` is
  never mistaken for the last session.
- Changed pitch, roll and lateral lean to move the right way. All three ran
  backwards against a real tracker, and each is now negated once at the engine
  boundary rather than by flipping an `Invert*` default, so the INI toggles
  stay at `false` and remain available for a tracker whose axes disagree.
- Changed build-profile routing to key on the game EXE rather than the renderer
  DLL, and pinned the camera pose's struct offsets alongside the function RVA.
  A profile that identifies a build but has no hook target yet leaves the mod
  dormant rather than hooking at the image base.
- Made head tracking inert outside gameplay. Control stops ticking the camera
  manager the mod hooks on the title screen, in the main menu, while a level
  loads, during the opening cutscene and in the pause menu - each measured on
  the tested build - so no separate state check is needed. The map keeps
  tracking, because in Control it is an overlay drawn over the live world.
- Changed the mod to initialise immediately instead of waiting for a renderer
  DLL to appear. Every hook target lives in the game EXE, and the build-profile
  fingerprint on that EXE is a stricter check that we are in the right process
  than waiting for a DLL was.
- Stopped running the full shutdown on `DLL_PROCESS_DETACH`. Joining threads
  and letting MinHook suspend the process under the loader lock could not
  complete and hung the game on unload; the ASI loader never unloads a plugin,
  so the only detach that happens is process exit.
- Split smoothing into two settings instead of one: `[Rotation]
  LocalSmoothing` (default `0.00`) applies when the tracker runs on this PC,
  `[Rotation] RemoteSmoothing` (default `0.15`) applies when it is a phone or
  other device on the network. Which one is used is decided per connection from
  the packet's source address and is re-evaluated when the source changes, so
  switching between a local OpenTrack instance and a phone takes effect without
  a restart.
- Made the tracker the only place the centre lives. The in-game recenter, its
  `Home` and `Ctrl+Shift+T` bindings and the `[Hotkeys] Recenter` setting are
  gone. A centre inside the mod sat in series with the tracker's own and the
  two drifted apart, because each side recentred at moments the other could
  not see, and with the view off there was no way to tell which side was
  wrong. The mod now applies the pose it is sent as-is; centre it in your
  tracker app while sitting in your normal playing position.

### Removed

- The sensitivity, deadzone and axis inversion settings: `[Rotation] YawSensitivity`, `PitchSensitivity`, `RollSensitivity`, `InvertYaw`, `InvertPitch`, `InvertRoll`, `YawDeadzone`, `PitchDeadzone` and `RollDeadzone`, and `[Position] SensitivityX`, `SensitivityY` and `SensitivityZ`. Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.
- Removed `[Rotation] Smoothing` and `[Position] Smoothing`. Both new values
  cover rotation and position alike, so there is no separate position smoothing
  setting.
- Removed the hidden 0.15 smoothing floor. It silently overrode whatever the
  user set, so a tracker on the same machine now gets the lightest setting by
  default.
- Removed `[General] AimDecoupling` and `[Network] BindAddress`. Both were read
  and then ignored: decoupling is structural here and cannot be switched off,
  and the receiver binds all interfaces and takes only a port.

## [0.0.0] - 2026-05-30

### Added
- Initial scaffold for the Control: Ultimate Edition head tracking mod.
- Ultimate ASI Loader (winmm.dll) install and uninstall scripts.
- OpenTrack UDP receiver, hotkey thread, INI configuration, and logging.
