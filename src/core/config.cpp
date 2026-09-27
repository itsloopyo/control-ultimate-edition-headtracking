#include "pch.h"
#include "config.h"

#include <cameraunlock/config/ini_reader.h>

namespace ControlHT {

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
