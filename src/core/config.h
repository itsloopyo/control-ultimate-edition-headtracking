#pragma once

#include "constants.h"

#include <string>

namespace ControlHT {

struct Config {
    // UDP / OpenTrack. There is deliberately no bind-address setting: the
    // receiver binds all interfaces and takes only a port, so a BindAddress key
    // could only ever be read and ignored.
    int udpPort = DEFAULT_UDP_PORT;

    // Master switches. There is deliberately no aim-decoupling setting either -
    // decoupling is structural here (the camera hook rotates the pose only for
    // the duration of the game's own camera tick), so it cannot be turned off
    // and a toggle for it would be a lie.
    bool enableOnStartup = true;
    bool positionEnabled = true;

    // Yaw mode: true = horizon-locked (world up-axis), false = camera-local.
    // The runtime flag (Mod::ToggleYawMode) seeds from this at startup.
    bool worldSpaceYaw = true;

    // Field of view multiplier. Control has its own FOV Scale slider under
    // Options > Graphics; its settings path clamps that to [0.75, 1.25], and
    // this is the same multiplier written past the clamp. 0 means the mod never
    // touches it and the game's slider stays in charge, which is the default
    // because a head-tracking install has no business quietly overriding a
    // setting the player already chose.
    float fovScale = 0.0f;

    // Rotation
    float yawSensitivity = 1.0f;
    float pitchSensitivity = 1.0f;
    float rollSensitivity = 1.0f;
    bool invertYaw = false;
    bool invertPitch = false;
    bool invertRoll = false;

    // Smoothing. Chosen per connection from the packet's source address, and
    // both values cover rotation and position alike. A tracker running on this
    // machine is already steady, so localSmoothing is 0.0 and nothing floors
    // it; a phone on WiFi jitters over the network, which is what
    // remoteSmoothing is for.
    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.15f;

    // Deadzone (degrees)
    float yawDeadzone = 0.0f;
    float pitchDeadzone = 0.0f;
    float rollDeadzone = 0.0f;

    // Position (6DOF) - meters
    float positionSensitivityX = 1.0f;
    float positionSensitivityY = 1.0f;
    float positionSensitivityZ = 1.0f;
    float limitX = 0.30f;
    float limitY = 0.20f;
    float limitZ = 0.40f;
    float limitZBack = 0.10f;

    // Hotkeys (Virtual Key codes). Nav cluster defaults per doctrine.
    int recenterKey = VK_HOME;       // Home
    int toggleKey = VK_END;          // End
    int togglePositionKey = VK_PRIOR; // PgUp
    int toggleYawModeKey = VK_NEXT;   // PgDn

    bool LoadFromFile(const std::string& path);
    bool SaveDefaultIfMissing(const std::string& path) const;
};

} // namespace ControlHT
