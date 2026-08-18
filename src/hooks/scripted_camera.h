#pragma once

#include <cstdint>

namespace ControlHT {

// Tells head tracking when Control has taken the camera for a scripted shot.
//
// Control's player camera keeps a list of camera entities it is blending toward.
// Gameplay leaves that list empty; a scripted shot, a cutscene or any other
// moment the game takes the view puts an entry in it. The count of that list is
// the whole signal - categorical, needs no motion, and so it catches a cutscene
// whose camera does not move, which every behavioural rule tried before it could
// not.
bool InstallScriptedCameraDetection(uint32_t playerCameraGlobalRva,
                                    uint32_t activeCameraCountOffset);

enum class CameraOwner {
    Unknown,       // the player camera component has not been located yet
    Scripted,      // the game is driving the camera
    PlayerDriven,  // the player is
};

CameraOwner CurrentCameraOwner();

// Control's live HORIZONTAL field of view in radians. The player camera update
// writes it each frame, immediately before the camera tick this mod hooks:
//
//     fov = m::PerspectiveView::scaleFOV(baseFov, lerp(FOVMultiplier, 1, blend))
//
// so it already carries the game's FOV Scale setting and anything fov_override.h
// has written over it, and it drops out of view during a scripted shot on its
// own. It follows aim zoom too: 70 degrees at rest, down to 43 while aiming.
// False if it cannot be read.
bool CurrentCameraFovRadians(float& fovRadians);

} // namespace ControlHT
