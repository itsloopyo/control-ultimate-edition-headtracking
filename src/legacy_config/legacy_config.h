#pragma once

#include <string>
#include <vector>

// The pre-canonical HeadTracking.ini reader, frozen. It reads a file the way
// the last build before the canonical config format did, so a player's old
// file is carried over as that build read it. Never edit anything in this
// folder: CMakeLists.txt pins every file here by hash.
//
// Frozen from src/core/config.cpp and src/core/config.h at 6efc755
// (Config::LoadFromFile and the helpers it calls), with three changes: it fills
// this frozen copy of that commit's settings and their defaults instead of the
// mod's Config, it writes nothing (SaveDefaultIfMissing stays with the mod),
// and it lives in namespace ControlHT::legacy. The defaults are written as the
// literals the code held then (cameraunlock-core's PositionSettings limits and
// smoothing defaults, VK_END, VK_PRIOR and VK_NEXT), so a later change to core
// cannot move what an old file means. IniReader is core's, which core keeps
// frozen.
namespace ControlHT::legacy {

struct Config {
    // [Network]. Read as it stands; the mod's startup puts a port outside
    // 1024-65535 on 4242.
    int udpPort = 4242;

    // [General]
    bool enableOnStartup = true;
    bool positionEnabled = true;
    bool worldSpaceYaw = true;

    // [Camera]. 0, or 0.5 to 2.0.
    float fovScale = 0.0f;

    // [Rotation]
    float yawSensitivity = 1.0f;
    float pitchSensitivity = 1.0f;
    float rollSensitivity = 1.0f;
    bool invertYaw = false;
    bool invertPitch = false;
    bool invertRoll = false;
    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.15f;
    float yawDeadzone = 0.0f;
    float pitchDeadzone = 0.0f;
    float rollDeadzone = 0.0f;

    // [Position]
    float positionSensitivityX = 1.0f;
    float positionSensitivityY = 1.0f;
    float positionSensitivityZ = 1.0f;
    float limitX = 0.30f;
    float limitY = 0.20f;
    float limitZ = 0.40f;
    float limitZBack = 0.10f;

    // [Hotkeys]. Virtual-key codes, each held to 0x01-0xFE.
    int toggleKey = 0x23;          // VK_END
    int togglePositionKey = 0x21;  // VK_PRIOR (Page Up)
    int toggleYawModeKey = 0x22;   // VK_NEXT (Page Down)
};

// Reads `path` into `out`; keys the file lacks keep their defaults. Returns
// whether the file was there to read (IniReader::Open).
bool Load(const std::string& path, Config& out);

struct Key {
    const char* section;
    const char* key;
};

// Every key Load takes a value from. The retired [Rotation] Smoothing and
// [Position] Smoothing are read only to warn that they are ignored, so they
// are not listed.
std::vector<Key> ReadKeys();

}  // namespace ControlHT::legacy
