// Frozen. See legacy_config.h.

#include "legacy_config.h"

#include "core/logging.h"

#include <cameraunlock/config/ini_reader.h>

#include <cmath>
#include <string>
#include <vector>

namespace ControlHT::legacy {
namespace {

// Validation bands for the INI's numeric keys. Each is the range the value can
// mean something in, not a preference: past them the setting stops describing
// anything the pipeline can do.
constexpr float kMaxSensitivity = 10.0f;
constexpr float kMaxDeadzoneDegrees = 45.0f;
constexpr float kMaxPositionLimitMeters = 2.0f;

// Smoothing is a 0..1 blend, and nothing floors it: a configured 0.0 stays 0.0.
constexpr float kMaxSmoothing = 1.0f;

// Every float the INI carries goes through here. strtod happily parses "nan"
// and "inf", so a value that is not finite takes the fallback, and a finite one
// outside the band is clamped to it, with a log line either way.
float ReadFiniteFloat(const cameraunlock::IniReader& reader, const char* section,
                      const char* key, float fallback, float lo, float hi) {
    const float value = reader.ReadFloat(section, key, fallback);
    if (!std::isfinite(value)) {
        Log::Line("WARN: [%s] %s is not a finite number, using %.2f", section, key,
                  fallback);
        return fallback;
    }
    if (value < lo || value > hi) {
        const float clamped = (value < lo) ? lo : hi;
        Log::Line("WARN: [%s] %s=%g is outside [%g, %g], clamped to %g", section, key,
                  static_cast<double>(value), static_cast<double>(lo),
                  static_cast<double>(hi), static_cast<double>(clamped));
        return clamped;
    }
    return value;
}

constexpr float kFovScaleMin = 0.5f;
constexpr float kFovScaleMax = 2.0f;

// Zero is the documented "leave Control's own FOV Scale slider alone", so the
// usable values and the off switch are not contiguous. A negative is reported
// rather than read as off.
float SanitizeFovScale(float value) {
    if (!std::isfinite(value)) {
        Log::Line("WARN: [Camera] FovScale is not a finite number, leaving Control's own "
                  "FOV Scale setting in charge");
        return 0.0f;
    }
    if (value == 0.0f) return 0.0f;
    if (value < 0.0f) {
        Log::Line("WARN: [Camera] FovScale=%g is negative, which is not a field of view. "
                  "Leaving Control's own FOV Scale setting in charge; use 0 to say that "
                  "deliberately, or %g-%g to override it.",
                  static_cast<double>(value), static_cast<double>(kFovScaleMin),
                  static_cast<double>(kFovScaleMax));
        return 0.0f;
    }
    if (value < kFovScaleMin || value > kFovScaleMax) {
        const float clamped = (value < kFovScaleMin) ? kFovScaleMin : kFovScaleMax;
        Log::Line("WARN: [Camera] FovScale=%g is outside [%g, %g], clamped to %g",
                  static_cast<double>(value), static_cast<double>(kFovScaleMin),
                  static_cast<double>(kFovScaleMax), static_cast<double>(clamped));
        return clamped;
    }
    return value;
}

// A hotkey outside 1..254, or a present value that does not parse (ReadInt
// gives 0 for it), takes the fallback with a log line.
int ReadVirtualKey(const cameraunlock::IniReader& reader, const char* key, int fallback) {
    constexpr int kMinVirtualKey = 0x01;
    constexpr int kMaxVirtualKey = 0xFE;
    int value = 0;
    if (reader.ReadIntInRange("Hotkeys", key, value, kMinVirtualKey, kMaxVirtualKey, fallback)) {
        return value;
    }
    Log::Line("WARN: [Hotkeys] %s=%d is not a virtual key code, so that hotkey would never "
              "fire; using %d. Values are decimal Win32 VK codes in %d-%d - a key name is not "
              "accepted here.",
              key, value, fallback, kMinVirtualKey, kMaxVirtualKey);
    return fallback;
}

// Warned once per process. The old single Smoothing value is not migrated.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "WARN: Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

}  // namespace

bool Load(const std::string& path, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("WARN: Config file not found at %s, using defaults", path.c_str());
        return false;
    }

    out.udpPort = ini.ReadInt("Network", "UdpPort", out.udpPort);

    out.enableOnStartup = ini.ReadBool("General", "EnableOnStartup", out.enableOnStartup);
    out.positionEnabled = ini.ReadBool("General", "PositionEnabled", out.positionEnabled);
    out.worldSpaceYaw = ini.ReadBool("General", "WorldSpaceYaw", out.worldSpaceYaw);

    out.fovScale = SanitizeFovScale(ini.ReadFloat("Camera", "FovScale", out.fovScale));

    out.yawSensitivity = ReadFiniteFloat(ini, "Rotation", "YawSensitivity", out.yawSensitivity, 0.0f, kMaxSensitivity);
    out.pitchSensitivity = ReadFiniteFloat(ini, "Rotation", "PitchSensitivity", out.pitchSensitivity, 0.0f, kMaxSensitivity);
    out.rollSensitivity = ReadFiniteFloat(ini, "Rotation", "RollSensitivity", out.rollSensitivity, 0.0f, kMaxSensitivity);
    out.invertYaw = ini.ReadBool("Rotation", "InvertYaw", out.invertYaw);
    out.invertPitch = ini.ReadBool("Rotation", "InvertPitch", out.invertPitch);
    out.invertRoll = ini.ReadBool("Rotation", "InvertRoll", out.invertRoll);
    // Each key falls back to its own default (local 0.0, remote 0.15).
    out.localSmoothing = ReadFiniteFloat(ini, "Rotation", "LocalSmoothing", out.localSmoothing,
                                         0.0f, kMaxSmoothing);
    out.remoteSmoothing = ReadFiniteFloat(ini, "Rotation", "RemoteSmoothing", out.remoteSmoothing,
                                          0.0f, kMaxSmoothing);

    WarnRetiredSmoothingKey(ini, "Rotation", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");

    out.yawDeadzone = ReadFiniteFloat(ini, "Rotation", "YawDeadzone", out.yawDeadzone, 0.0f, kMaxDeadzoneDegrees);
    out.pitchDeadzone = ReadFiniteFloat(ini, "Rotation", "PitchDeadzone", out.pitchDeadzone, 0.0f, kMaxDeadzoneDegrees);
    out.rollDeadzone = ReadFiniteFloat(ini, "Rotation", "RollDeadzone", out.rollDeadzone, 0.0f, kMaxDeadzoneDegrees);

    out.positionSensitivityX = ReadFiniteFloat(ini, "Position", "SensitivityX", out.positionSensitivityX, 0.0f, kMaxSensitivity);
    out.positionSensitivityY = ReadFiniteFloat(ini, "Position", "SensitivityY", out.positionSensitivityY, 0.0f, kMaxSensitivity);
    out.positionSensitivityZ = ReadFiniteFloat(ini, "Position", "SensitivityZ", out.positionSensitivityZ, 0.0f, kMaxSensitivity);
    out.limitX = ReadFiniteFloat(ini, "Position", "LimitX", out.limitX, 0.0f, kMaxPositionLimitMeters);
    out.limitY = ReadFiniteFloat(ini, "Position", "LimitY", out.limitY, 0.0f, kMaxPositionLimitMeters);
    out.limitZ = ReadFiniteFloat(ini, "Position", "LimitZ", out.limitZ, 0.0f, kMaxPositionLimitMeters);
    out.limitZBack = ReadFiniteFloat(ini, "Position", "LimitZBack", out.limitZBack, 0.0f, kMaxPositionLimitMeters);

    out.toggleKey = ReadVirtualKey(ini, "Toggle", out.toggleKey);
    out.togglePositionKey = ReadVirtualKey(ini, "TogglePosition", out.togglePositionKey);
    out.toggleYawModeKey = ReadVirtualKey(ini, "ToggleYawMode", out.toggleYawModeKey);

    return true;
}

std::vector<Key> ReadKeys() {
    return {
        {"Network", "UdpPort"},
        {"General", "EnableOnStartup"},
        {"General", "PositionEnabled"},
        {"General", "WorldSpaceYaw"},
        {"Camera", "FovScale"},
        {"Rotation", "YawSensitivity"},
        {"Rotation", "PitchSensitivity"},
        {"Rotation", "RollSensitivity"},
        {"Rotation", "InvertYaw"},
        {"Rotation", "InvertPitch"},
        {"Rotation", "InvertRoll"},
        {"Rotation", "LocalSmoothing"},
        {"Rotation", "RemoteSmoothing"},
        {"Rotation", "YawDeadzone"},
        {"Rotation", "PitchDeadzone"},
        {"Rotation", "RollDeadzone"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Hotkeys", "Toggle"},
        {"Hotkeys", "TogglePosition"},
        {"Hotkeys", "ToggleYawMode"},
    };
}

}  // namespace ControlHT::legacy
