#pragma once

#include <string>
#include <tuple>
#include <vector>

// The oracle: what the dev pre-release (commit 2696dd1), the newest and only
// published build, ran on after reading HeadTracking.ini. oracle_reader.cpp
// compiles its reader and transcribes the startup code that consumed it.
namespace control_oracle {

enum Action { kToggle = 0, kCycleMode = 1, kYawMode = 2 };

// One HotkeyPoller registration: the action, the code, and 3 where the
// callback is ChordGuarded (fires only while Ctrl and Shift are both held), 0
// where it is a plain key.
using Registration = std::tuple<int, int, unsigned>;

struct Published {
    int udp_port = 0;
    bool tracking_enabled = false;
    bool world_space_yaw = false;
    // The mode ApplyConfigToSession handed the session: 0 rotation and
    // position, 1 rotation only (cameraunlock::TrackingMode's numbers).
    int tracking_mode = 0;
    float fov_scale = 0;
    float yaw_sens = 0, pitch_sens = 0, roll_sens = 0;
    bool invert_yaw = false, invert_pitch = false, invert_roll = false;
    float yaw_deadzone = 0, pitch_deadzone = 0, roll_deadzone = 0;
    float local_smoothing = 0, remote_smoothing = 0;
    float pos_sens_x = 0, pos_sens_y = 0, pos_sens_z = 0;
    float limit_x = 0, limit_y = 0, limit_y_down = 0, limit_z = 0, limit_z_back = 0;
    std::vector<Registration> hotkeys;
};

// `path` is HeadTracking.ini's full path. Like the dev build, this writes the
// default file there first when none exists.
Published Read(const std::string& path);

}  // namespace control_oracle
