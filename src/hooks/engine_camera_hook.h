#pragma once

namespace ControlHT {

// Northlight camera hook.
//
// The hook target is Control's camera-manager tick, which reads its own pose - a
// unit quaternion followed by a world position - adds shake, and builds the final
// look-at the renderer uses.
//
// The detour SANDWICHES that call: it rotates the pose by the head rotation,
// calls the original, then restores the pose byte for byte. The rotated value
// therefore exists only for the duration of the tick, so the renderer draws the
// head-tracked view while the camera state the game reads afterwards - for aim,
// for raycasts, for which direction walking takes you - is the one the game
// itself set. That is what decouples look from aim, and it is why the restore is
// RAII rather than a write at the end of the function.
//
// The tick's RVA and the pose's offsets within its state are pinned per build in
// build_profile.cpp; a build the mod does not recognise, or recognises without
// having worked out every one of those values, leaves the hook uninstalled and
// the game vanilla.

bool InstallEngineCameraHook();
void RemoveEngineCameraHook();
void SetCameraHookEnabled(bool enabled);

} // namespace ControlHT
