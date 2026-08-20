# Changelog

All notable changes to this project will be documented in this file. Format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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
