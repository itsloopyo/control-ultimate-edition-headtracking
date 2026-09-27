#pragma once

#include "constants.h"

#include <cameraunlock/config/config_concepts.g.h>
#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/data/position_settings.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/tracking/tracking_mode.h>

#include <string>

namespace ControlHT {

// The settings CameraUnlock.ini holds, at their defaults.
struct Config {
    // UDP / OpenTrack. There is deliberately no bind-address setting: the
    // receiver binds all interfaces and takes only a port.
    int udpPort = DEFAULT_UDP_PORT;

    bool enableOnStartup = true;

    // The tracking mode at startup, the pair the mode hotkey saves.
    bool rotationEnabled = true;
    bool positionEnabled = true;

    // Yaw mode: true = horizon-locked (world up-axis), false = camera-local.
    bool worldSpaceYaw = true;

    // Field of view multiplier. Control has its own FOV Scale slider under
    // Options > Graphics; its settings path clamps that to [0.75, 1.25], and
    // this is the same multiplier written past the clamp. 0 means the mod never
    // touches it and the game's slider stays in charge, which is the default
    // because a head-tracking install has no business quietly overriding a
    // setting the player already chose.
    float fovScale = 0.0f;

    // Smoothing. Chosen per connection from the packet's source address, and
    // both values cover rotation and position alike.
    float localSmoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    // Position (6DOF) - meters
    float limitX = cameraunlock::PositionSettings{}.limit_x;
    float limitY = cameraunlock::PositionSettings{}.limit_y;
    float limitYDown = cameraunlock::PositionSettings{}.limit_y_down;
    float limitZ = cameraunlock::PositionSettings{}.limit_z;
    float limitZBack = cameraunlock::PositionSettings{}.limit_z_back;

    std::string toggleKey =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault;
    std::string cycleTrackingModeKey =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::CycleTrackingModeKey>::kCanonicalDefault;
    std::string yawModeKey =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::YawModeKey>::kCanonicalDefault;
};

}  // namespace ControlHT

// CameraUnlock.ini, beside the game exe, in cameraunlock-core's canonical config
// format. One ConfigOwner reads and writes it; nothing else in the mod touches
// it. HeadTracking.ini, the file every earlier build read, is imported once
// while CameraUnlock.ini is absent and is never written.
namespace ControlHT::config {

cameraunlock::config::ConfigTable<Config> Table();

cameraunlock::config::RenderHeader Header();

// HeadTracking.ini through the frozen reader in src/legacy_config/, mapped into
// Config.
cameraunlock::config::LegacyImport<Config> Import();

// The owner's options for CameraUnlock.ini in `mod_dir`, a full path, with
// HeadTracking.ini beside it as the legacy file and Defaults.ini where
// `defaults` says.
cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& mod_dir,
                                                              cameraunlock::config::DefaultsFile defaults);

// Reads, imports or creates CameraUnlock.ini in `mod_dir`, logs what the owner
// reports, and returns the settings the session runs on. Call once, from the
// init thread, with the log open. `defaults` is DefaultsFile::PerUser() in the
// mod.
Config Load(const std::wstring& mod_dir, cameraunlock::config::DefaultsFile defaults);

// The tracking mode the settings start in. The table never gives both rows
// false.
cameraunlock::TrackingMode StartupTrackingMode(const Config& config);

// Saves the value a hotkey has just applied. The session keeps it whether or
// not the save succeeds; a failed save is logged. Called on the hotkey thread.
void SaveWorldSpaceYaw(bool worldSpaceYaw);
void SaveTrackingMode(cameraunlock::TrackingMode mode);

}  // namespace ControlHT::config
