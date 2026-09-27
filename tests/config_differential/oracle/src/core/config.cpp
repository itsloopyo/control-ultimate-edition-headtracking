#include "pch.h"
#include "config.h"
#include "logging.h"

#include <cameraunlock/config/ini_reader.h>

#include <cmath>

namespace ControlHT {
namespace {

// Validation bands for the INI's numeric keys. Each is the range the value can
// mean something in, not a preference: past them the setting stops describing
// anything the pipeline can do.
constexpr float kMaxSensitivity = 10.0f;
constexpr float kMaxDeadzoneDegrees = 45.0f;
constexpr float kMaxPositionLimitMeters = 2.0f;

// Smoothing is a 0..1 blend, and nothing floors it: a configured 0.0 stays 0.0.
// The value reaches cameraunlock::math::CalculateSmoothingFactor, which runs
// exp() on it, so a NaN there would poison the smoothed pose for the rest of the
// session.
constexpr float kMaxSmoothing = 1.0f;

// Every float the INI carries goes through here. strtod happily parses "nan"
// and "inf", and a NaN then defeats every comparison-based clamp downstream
// (each comparison against it is false), so one bad key silently poisons the
// pose for the rest of the session. The INI is the system boundary, so this is
// where it gets caught - the camera hook's own finite check is the backstop,
// not the diagnostic.
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

// Field of view multiplier, in tan-of-half-angle terms: Control's default 70
// degrees horizontal becomes 82 at 1.25, 93 at 1.5 and 109 at 2.0. The upper
// bound is where the third-person camera's near geometry starts to distort
// badly enough that it stops being a setting and starts being a bug report; the
// lower one is the game's own.
constexpr float kFovScaleMin = 0.5f;
constexpr float kFovScaleMax = 2.0f;

// Zero is the documented "leave Control's own FOV Scale slider alone", so it
// cannot go through ReadFiniteFloat's single band - the usable values and the
// off switch are not contiguous. A negative is neither, and says the user meant
// something the key cannot express, so it is reported rather than read as off.
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

// The hotkey keys are the only INI ints that reach a Win32 API, and the two
// ways they go wrong are both silent. GetAsyncKeyState is defined for virtual
// key codes 1..254, and the poller skips any binding whose code is 0 - which is
// also exactly what IniReader::ReadInt yields for a key that is PRESENT but
// unparseable (`Toggle = End` rather than a number; see the parsing rules on
// IniReader). Either way the hotkey simply never fires, with nothing in the log
// to separate a mistyped config from a broken mod, so the value is checked here
// where the file is read.
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

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
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

bool Config::LoadFromFile(const std::string& path) {
    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("WARN: Config file not found at %s, using defaults", path.c_str());
        return false;
    }

    // Struct member defaults double as the read defaults, so missing keys
    // keep their documented values without a second constant table.
    udpPort = ini.ReadInt("Network", "UdpPort", udpPort);

    enableOnStartup = ini.ReadBool("General", "EnableOnStartup", enableOnStartup);
    positionEnabled = ini.ReadBool("General", "PositionEnabled", positionEnabled);
    worldSpaceYaw = ini.ReadBool("General", "WorldSpaceYaw", worldSpaceYaw);

    fovScale = SanitizeFovScale(ini.ReadFloat("Camera", "FovScale", fovScale));

    yawSensitivity = ReadFiniteFloat(ini, "Rotation", "YawSensitivity", yawSensitivity, 0.0f, kMaxSensitivity);
    pitchSensitivity = ReadFiniteFloat(ini, "Rotation", "PitchSensitivity", pitchSensitivity, 0.0f, kMaxSensitivity);
    rollSensitivity = ReadFiniteFloat(ini, "Rotation", "RollSensitivity", rollSensitivity, 0.0f, kMaxSensitivity);
    invertYaw = ini.ReadBool("Rotation", "InvertYaw", invertYaw);
    invertPitch = ini.ReadBool("Rotation", "InvertPitch", invertPitch);
    invertRoll = ini.ReadBool("Rotation", "InvertRoll", invertRoll);
    // Each key falls back to its own default (local 0.0, remote 0.15), never to
    // a shared one: a bad RemoteSmoothing dropping to the local default would
    // leave a phone's network jitter entirely unsmoothed.
    localSmoothing = ReadFiniteFloat(ini, "Rotation", "LocalSmoothing", localSmoothing,
                                     0.0f, kMaxSmoothing);
    remoteSmoothing = ReadFiniteFloat(ini, "Rotation", "RemoteSmoothing", remoteSmoothing,
                                      0.0f, kMaxSmoothing);

    WarnRetiredSmoothingKey(ini, "Rotation", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");

    yawDeadzone = ReadFiniteFloat(ini, "Rotation", "YawDeadzone", yawDeadzone, 0.0f, kMaxDeadzoneDegrees);
    pitchDeadzone = ReadFiniteFloat(ini, "Rotation", "PitchDeadzone", pitchDeadzone, 0.0f, kMaxDeadzoneDegrees);
    rollDeadzone = ReadFiniteFloat(ini, "Rotation", "RollDeadzone", rollDeadzone, 0.0f, kMaxDeadzoneDegrees);

    positionSensitivityX = ReadFiniteFloat(ini, "Position", "SensitivityX", positionSensitivityX, 0.0f, kMaxSensitivity);
    positionSensitivityY = ReadFiniteFloat(ini, "Position", "SensitivityY", positionSensitivityY, 0.0f, kMaxSensitivity);
    positionSensitivityZ = ReadFiniteFloat(ini, "Position", "SensitivityZ", positionSensitivityZ, 0.0f, kMaxSensitivity);
    limitX = ReadFiniteFloat(ini, "Position", "LimitX", limitX, 0.0f, kMaxPositionLimitMeters);
    limitY = ReadFiniteFloat(ini, "Position", "LimitY", limitY, 0.0f, kMaxPositionLimitMeters);
    limitZ = ReadFiniteFloat(ini, "Position", "LimitZ", limitZ, 0.0f, kMaxPositionLimitMeters);
    limitZBack = ReadFiniteFloat(ini, "Position", "LimitZBack", limitZBack, 0.0f, kMaxPositionLimitMeters);

    toggleKey = ReadVirtualKey(ini, "Toggle", toggleKey);
    togglePositionKey = ReadVirtualKey(ini, "TogglePosition", togglePositionKey);
    toggleYawModeKey = ReadVirtualKey(ini, "ToggleYawMode", toggleYawModeKey);

    return true;
}

bool Config::SaveDefaultIfMissing(const std::string& path) const {
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return false;

    cameraunlock::IniWriter w;
    if (!w.Open(path)) return false;

    w.WriteComment("Control: Ultimate Edition Head Tracking - configuration");
    w.WriteComment("Hotkey values are Win32 Virtual Key codes (decimal).");
    w.WriteBlankLine();

    w.WriteSection("Network");
    w.WriteInt("UdpPort", udpPort);
    w.WriteBlankLine();

    w.WriteSection("General");
    w.WriteBool("EnableOnStartup", enableOnStartup);
    w.WriteBool("PositionEnabled", positionEnabled);
    w.WriteComment("Yaw mode: true = horizon-locked yaw (default), false = camera-local.");
    w.WriteBool("WorldSpaceYaw", worldSpaceYaw);
    w.WriteBlankLine();

    w.WriteSection("Camera");
    w.WriteComment("Field of view. Control has its own FOV Scale slider under Options >");
    w.WriteComment("Graphics, but the game clamps that slider to 0.75-1.25. This is the");
    w.WriteComment("same multiplier, written past the clamp and re-applied every frame so");
    w.WriteComment("changing a graphics option cannot quietly undo it.");
    w.WriteComment("0 leaves Control's own slider in charge. Otherwise 0.5-2.0.");
    w.WriteComment("It scales the tangent of the half-angle, so from Control's default 70");
    w.WriteComment("degrees horizontal: 1.25 gives 82, 1.5 gives 93, 2.0 gives 109.");
    w.WriteComment("Aim zoom still works, and cutscenes keep their authored framing - the");
    w.WriteComment("game fades the multiplier out whenever it owns the camera.");
    w.WriteDouble("FovScale", fovScale);
    w.WriteBlankLine();

    w.WriteSection("Rotation");
    w.WriteDouble("YawSensitivity", yawSensitivity);
    w.WriteDouble("PitchSensitivity", pitchSensitivity);
    w.WriteDouble("RollSensitivity", rollSensitivity);
    w.WriteBool("InvertYaw", invertYaw);
    w.WriteBool("InvertPitch", invertPitch);
    w.WriteBool("InvertRoll", invertRoll);
    w.WriteComment("Smoothing covers rotation and position alike. Which of the two is used");
    w.WriteComment("is picked per connection from the packet's source address: LOOPBACK only");
    w.WriteComment("counts as local, so a tracker on this PC sending to this machine's LAN");
    w.WriteComment("address is treated as remote. 0 responsive, 1 heavy. Nothing floors");
    w.WriteComment("either value; 0 is the lightest setting, a 20ms time constant.");
    w.WriteDouble("LocalSmoothing", localSmoothing);
    w.WriteDouble("RemoteSmoothing", remoteSmoothing);
    w.WriteDouble("YawDeadzone", yawDeadzone);
    w.WriteDouble("PitchDeadzone", pitchDeadzone);
    w.WriteDouble("RollDeadzone", rollDeadzone);
    w.WriteBlankLine();

    w.WriteSection("Position");
    w.WriteDouble("SensitivityX", positionSensitivityX);
    w.WriteDouble("SensitivityY", positionSensitivityY);
    w.WriteDouble("SensitivityZ", positionSensitivityZ);
    w.WriteDouble("LimitX", limitX);
    w.WriteDouble("LimitY", limitY);
    w.WriteDouble("LimitZ", limitZ);
    w.WriteDouble("LimitZBack", limitZBack);
    w.WriteBlankLine();

    w.WriteSection("Hotkeys");
    w.WriteComment("Defaults: End=Toggle, PgUp=TogglePosition, PgDn=ToggleYawMode.");
    w.WriteInt("Toggle", toggleKey);
    w.WriteInt("TogglePosition", togglePositionKey);
    w.WriteInt("ToggleYawMode", toggleYawModeKey);

    w.Close();
    return true;
}

} // namespace ControlHT
