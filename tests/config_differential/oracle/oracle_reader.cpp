// The dev pre-release's reader and startup code (commit 2696dd1, published
// 2026-08-20 as 0.0.0-nightly.20260820.2696dd1). No v* release was published.
//
// src/core/config.cpp, config.h, logging.h, constants.h and src/pch.h beside
// this file are byte copies of 2696dd1's, compiled here as they shipped, with
// their namespace renamed by the macro below so they can sit in one program
// beside this build's ControlHT. The cameraunlock-core sources the reader
// compiles hold the same code at 2696dd1's pin (3465659) and at this repo's
// (CMakeLists.txt pins them by hash). What is transcribed is the startup code
// that consumed the settings, which cannot be compiled into a test because it
// hooks the game:
//
//   src/core/mod.cpp      lines 24-64    ApplyConfigToSession
//                         lines 75-80    StartUdpReceiver's port fallback
//                         line 103       the startup enable
//                         lines 119-123  LoadConfig
//                         line 138       ConfigureFovOverride
//   src/core/hotkeys.cpp  lines 16-23    the hotkey registrations, as data
//   cameraunlock-core 3465659 data/position_settings.h: limit_y_down, which
//                         ApplyConfigToSession left at its default 0.20

#define ControlHT ControlHT_dev
#include "src/core/config.cpp"
#undef ControlHT

#include "oracle_reader.h"

namespace control_oracle {

namespace {

constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;

constexpr unsigned kPlain = 0;
constexpr unsigned kChord = 3;

constexpr float kDefaultLimitYDown = 0.20f;

}  // namespace

Published Read(const std::string& path) {
    ControlHT_dev::Config m_config;
    m_config.SaveDefaultIfMissing(path);
    m_config.LoadFromFile(path);

    Published p;
    if (m_config.udpPort < ControlHT_dev::MIN_UDP_PORT || m_config.udpPort > ControlHT_dev::MAX_UDP_PORT) {
        m_config.udpPort = ControlHT_dev::DEFAULT_UDP_PORT;
    }
    p.udp_port = m_config.udpPort;
    p.tracking_enabled = m_config.enableOnStartup;
    p.world_space_yaw = m_config.worldSpaceYaw;
    p.tracking_mode = m_config.positionEnabled ? 0 : 1;
    p.fov_scale = m_config.fovScale;

    p.yaw_sens = m_config.yawSensitivity;
    p.pitch_sens = m_config.pitchSensitivity;
    p.roll_sens = m_config.rollSensitivity;
    p.invert_yaw = m_config.invertYaw;
    p.invert_pitch = m_config.invertPitch;
    p.invert_roll = m_config.invertRoll;
    p.yaw_deadzone = m_config.yawDeadzone;
    p.pitch_deadzone = m_config.pitchDeadzone;
    p.roll_deadzone = m_config.rollDeadzone;
    p.local_smoothing = m_config.localSmoothing;
    p.remote_smoothing = m_config.remoteSmoothing;

    p.pos_sens_x = m_config.positionSensitivityX;
    p.pos_sens_y = m_config.positionSensitivityY;
    p.pos_sens_z = m_config.positionSensitivityZ;
    p.limit_x = m_config.limitX;
    p.limit_y = m_config.limitY;
    p.limit_y_down = kDefaultLimitYDown;
    p.limit_z = m_config.limitZ;
    p.limit_z_back = m_config.limitZBack;

    p.hotkeys.push_back({kToggle, m_config.toggleKey, kPlain});
    p.hotkeys.push_back({kCycleMode, m_config.togglePositionKey, kPlain});
    p.hotkeys.push_back({kYawMode, m_config.toggleYawModeKey, kPlain});
    p.hotkeys.push_back({kToggle, kVkY, kChord});
    p.hotkeys.push_back({kCycleMode, kVkG, kChord});
    p.hotkeys.push_back({kYawMode, kVkH, kChord});
    return p;
}

}  // namespace control_oracle
