// The differential test for the conversion from HeadTracking.ini to
// CameraUnlock.ini.
//
//   Oracle     the dev pre-release's reader and startup code, the newest and
//              only published build (oracle/oracle_reader.cpp)
//   Import     the frozen reader in src/legacy_config/, through the startup
//              code of the commit that froze it
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since the dev build that change how the
// file is read or applied. The reader's source is the same at 2696dd1 and at
// the commit that froze it, and every core source it compiles holds the same
// code at both pins. One startup change sits between them: 71f6cda made LimitY
// set the downward lean limit as well as the upward one, where the dev build
// left the downward limit at 0.20. That is the only difference allowed, and
// only on the downward limit.
//
// Inputs: the file the dev build writes on its first run (no release shipped
// or seeded a HeadTracking.ini), no file, an empty file, core's mutation corpus
// over the first-run file, and that file with each hotkey set to every code
// from 0 to 255.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "oracle/oracle_reader.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"

namespace {

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;
namespace legacy = ControlHT::legacy;
namespace testing = cameraunlock::config::testing;

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
}

std::string ReadFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFileBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

// ---- Scratch folders ---------------------------------------------------------
//
// One folder per reading: GetPrivateProfileString, which every reader here sits
// on, is free to cache the file it last read. `game` stands for the folder the
// game exe runs from. Every folder lives under one root for the run, removed
// once at the end.

void RemoveTree(const fs::path& root) {
    if (!fs::exists(root)) return;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    fs::remove_all(root);
}

const fs::path& ScratchRoot() {
    static const fs::path root = [] {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        fs::path r = fs::path(temp) / ("control_ht_diff_" + std::to_string(GetCurrentProcessId()));
        RemoveTree(r);
        return r;
    }();
    return root;
}

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        root_ = ScratchRoot() / std::to_string(s_next++);
        fs::create_directories(root_ / "game");
    }

    fs::path game() const { return root_ / "game"; }
    fs::path legacy() const { return game() / "HeadTracking.ini"; }

    void WriteLegacy(const std::string& bytes) const { WriteFileBytes(legacy(), bytes); }

private:
    fs::path root_;
};

// ---- What a reading does -------------------------------------------------------
//
// A Record names everything the running mod acts on after reading the file:
// `field.*` the settings, `start.*` the state the session starts in, `hotkey.*`
// the bindings that can fire, each as `modifiers:code` (Ctrl 1, Shift 2, as
// cameraunlock::input::KeyModifiers numbers them) in ascending order. Floats are
// their bits.

using Record = std::map<std::string, std::string>;

std::string Bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(bits));
    return text;
}

std::string Flag(bool value) { return value ? "1" : "0"; }

const char* const kActionNames[] = {"Toggle", "CycleTrackingMode", "YawMode"};

// The bindings a set of HotkeyPoller registrations can fire. The poller skips
// code 0, and GetAsyncKeyState reports no code above 0xFF or below 0 down.
void AddHotkeys(Record& r, const std::vector<control_oracle::Registration>& registrations) {
    std::map<int, std::vector<std::pair<unsigned, int>>> byAction;
    for (int action = 0; action < 3; ++action) byAction[action];
    for (const auto& [action, vk, modifiers] : registrations) {
        if (vk < 0x01 || vk > 0xFF) continue;
        byAction[action].push_back({modifiers, vk});
    }
    for (auto& [action, items] : byAction) {
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        std::string text;
        for (const auto& [modifiers, vk] : items) {
            char item[32];
            std::snprintf(item, sizeof(item), "%s%u:0x%02X", text.empty() ? "" : " ", modifiers, static_cast<unsigned>(vk));
            text += item;
        }
        r[std::string("hotkey.") + kActionNames[action]] = text;
    }
}

const char* ModeName(int mode) {
    switch (mode) {
        case 0: return "RotationAndPosition";
        case 1: return "RotationOnly";
        case 2: return "PositionOnly";
        default: return "none";
    }
}

Record ObserveOracle(const control_oracle::Published& g) {
    Record r;
    r["field.udp_port"] = std::to_string(g.udp_port);
    r["field.fov_scale"] = Bits(g.fov_scale);
    r["field.rot.yaw_sensitivity"] = Bits(g.yaw_sens);
    r["field.rot.pitch_sensitivity"] = Bits(g.pitch_sens);
    r["field.rot.roll_sensitivity"] = Bits(g.roll_sens);
    r["field.rot.invert_yaw"] = Flag(g.invert_yaw);
    r["field.rot.invert_pitch"] = Flag(g.invert_pitch);
    r["field.rot.invert_roll"] = Flag(g.invert_roll);
    r["field.rot.yaw_deadzone"] = Bits(g.yaw_deadzone);
    r["field.rot.pitch_deadzone"] = Bits(g.pitch_deadzone);
    r["field.rot.roll_deadzone"] = Bits(g.roll_deadzone);
    r["field.local_smoothing"] = Bits(g.local_smoothing);
    r["field.remote_smoothing"] = Bits(g.remote_smoothing);
    r["field.pos.sensitivity_x"] = Bits(g.pos_sens_x);
    r["field.pos.sensitivity_y"] = Bits(g.pos_sens_y);
    r["field.pos.sensitivity_z"] = Bits(g.pos_sens_z);
    r["field.pos.limit_x"] = Bits(g.limit_x);
    r["field.pos.limit_y"] = Bits(g.limit_y);
    r["field.pos.limit_y_down"] = Bits(g.limit_y_down);
    r["field.pos.limit_z"] = Bits(g.limit_z);
    r["field.pos.limit_z_back"] = Bits(g.limit_z_back);
    r["start.enabled"] = Flag(g.tracking_enabled);
    r["start.mode"] = ModeName(g.tracking_mode);
    r["start.world_space_yaw"] = Flag(g.world_space_yaw);
    AddHotkeys(r, g.hotkeys);
    return r;
}

// The frozen reader's settings through the startup code of the commit that
// froze it: src/core/mod.cpp's ApplyConfigToSession, which since 71f6cda sets
// the downward limit from LimitY, its port fallback, and src/core/hotkeys.cpp,
// unchanged since the dev build.
Record ObserveImport(const legacy::Config& c) {
    control_oracle::Published g;
    g.udp_port = (c.udpPort < 1024 || c.udpPort > 65535) ? 4242 : c.udpPort;
    g.tracking_enabled = c.enableOnStartup;
    g.world_space_yaw = c.worldSpaceYaw;
    g.tracking_mode = c.positionEnabled ? 0 : 1;
    g.fov_scale = c.fovScale;
    g.yaw_sens = c.yawSensitivity;
    g.pitch_sens = c.pitchSensitivity;
    g.roll_sens = c.rollSensitivity;
    g.invert_yaw = c.invertYaw;
    g.invert_pitch = c.invertPitch;
    g.invert_roll = c.invertRoll;
    g.yaw_deadzone = c.yawDeadzone;
    g.pitch_deadzone = c.pitchDeadzone;
    g.roll_deadzone = c.rollDeadzone;
    g.local_smoothing = c.localSmoothing;
    g.remote_smoothing = c.remoteSmoothing;
    g.pos_sens_x = c.positionSensitivityX;
    g.pos_sens_y = c.positionSensitivityY;
    g.pos_sens_z = c.positionSensitivityZ;
    g.limit_x = c.limitX;
    g.limit_y = c.limitY;
    g.limit_y_down = c.limitY;
    g.limit_z = c.limitZ;
    g.limit_z_back = c.limitZBack;
    g.hotkeys = {{control_oracle::kToggle, c.toggleKey, 0},
                 {control_oracle::kCycleMode, c.togglePositionKey, 0},
                 {control_oracle::kYawMode, c.toggleYawModeKey, 0},
                 {control_oracle::kToggle, 0x59, 3},
                 {control_oracle::kCycleMode, 0x47, 3},
                 {control_oracle::kYawMode, 0x48, 3}};
    return ObserveOracle(g);
}

std::vector<std::string> Differences(const Record& a, const Record& b) {
    std::vector<std::string> out;
    for (const auto& [name, value] : a) {
        const auto it = b.find(name);
        if (it == b.end()) {
            out.push_back(name + " only on the left");
        } else if (it->second != value) {
            out.push_back(name + ": " + value + " / " + it->second);
        }
    }
    for (const auto& [name, value] : b) {
        if (a.find(name) == a.end()) out.push_back(name + " only on the right");
    }
    return out;
}

// ---- Inputs --------------------------------------------------------------------

fs::path DataPath(const char* name) {
    return fs::path(CONTROL_SOURCE_DIR) / "tests" / "config_differential" / "data" / name;
}

// What the dev build's SaveDefaultIfMissing wrote on the first run: the file
// every player who ran a published build holds, edited or not.
std::string FirstRun() { return ReadFileBytes(DataPath("first-run-dev-2696dd1.ini")); }

const char* const kFirstRunName = "dev build first-run file";

// Every key the frozen reader reads, and how the corpus varies each one. The
// reader clamps every float it reads into its band and replaces one that is not
// finite, or does not parse whole, with the key's fallback; it puts FovScale
// below 0 or not finite on 0, and a hotkey outside 1-254 on its default. The
// startup code puts a port outside 1024-65535 on 4242.
std::vector<testing::MutationKey> CorpusKeys() {
    return {
        {"Network", "UdpPort", "5771", {"80", "70000"}},
        {"General", "EnableOnStartup", "0", {}},
        {"General", "PositionEnabled", "0", {}},
        {"General", "WorldSpaceYaw", "0", {}},
        {"Camera", "FovScale", "1.5", {"-1", "0.2", "3"}},
        {"Rotation", "YawSensitivity", "0.5", {"-1", "15"}},
        {"Rotation", "PitchSensitivity", "0.5", {"-1", "15"}},
        {"Rotation", "RollSensitivity", "0.5", {"-1", "15"}},
        {"Rotation", "InvertYaw", "1", {}},
        {"Rotation", "InvertPitch", "1", {}},
        {"Rotation", "InvertRoll", "1", {}},
        {"Rotation", "LocalSmoothing", "0.3", {"-0.5", "1.5"}},
        {"Rotation", "RemoteSmoothing", "0.6", {"-0.5", "1.5"}},
        {"Rotation", "YawDeadzone", "2", {"-1", "50"}},
        {"Rotation", "PitchDeadzone", "2", {"-1", "50"}},
        {"Rotation", "RollDeadzone", "2", {"-1", "50"}},
        {"Position", "SensitivityX", "0.5", {"-1", "15"}},
        {"Position", "SensitivityY", "0.5", {"-1", "15"}},
        {"Position", "SensitivityZ", "0.5", {"-1", "15"}},
        {"Position", "LimitX", "0.5", {"-0.3", "3"}},
        {"Position", "LimitY", "0.35", {"-0.3", "3"}},
        {"Position", "LimitZ", "0.6", {"-0.3", "3"}},
        {"Position", "LimitZBack", "0.15", {"-0.3", "3"}},
        {"Hotkeys", "Toggle", "113", {"0", "300"}, true},
        {"Hotkeys", "TogglePosition", "114", {"0", "300"}, true},
        {"Hotkeys", "ToggleYawMode", "115", {"0", "300"}, true},
    };
}

std::vector<cfg::LegacyKey> CorpusReads() {
    std::vector<cfg::LegacyKey> keys;
    for (const legacy::Key& key : legacy::ReadKeys()) keys.push_back({key.section, key.key});
    return keys;
}

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::logic_error("'" + from + "' is not in the text");
    return text.replace(at, from.size(), to);
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {kFirstRunName, true, FirstRun()},
        {"no file", false, {}},
        {"empty file", true, {}},
    };
    for (testing::IniMutation& m : testing::GenerateIniMutations(FirstRun(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    const std::pair<const char*, const char*> hotkeys[] = {
        {"Toggle", "Toggle=35"}, {"TogglePosition", "TogglePosition=33"}, {"ToggleYawMode", "ToggleYawMode=34"}};
    for (const auto& [key, line] : hotkeys) {
        for (int code = 0; code <= 0xFF; ++code) {
            const std::string text = std::string(key) + "=" + std::to_string(code);
            inputs.push_back({text, true, Replace(FirstRun(), line, text)});
        }
    }
    return inputs;
}

// ---- Comparison 1 ----------------------------------------------------------------

void Compare(const std::vector<Input>& inputs) {
    int compared = 0;
    int limit_y_down = 0;
    for (const Input& input : inputs) {
        const std::string& name = input.name;
        Scratch s;
        if (input.present) s.WriteLegacy(input.bytes);
        const Record oracle = ObserveOracle(control_oracle::Read(s.legacy().string()));

        // The dev build writes its default file where none exists, and nothing
        // else.
        if (!input.present) {
            Check(ReadFileBytes(s.legacy()) == FirstRun(), name + ": the dev build's first run writes the committed first-run file");
        } else {
            Check(ReadFileBytes(s.legacy()) == input.bytes, name + ": the dev build leaves an existing file as it was");
        }

        // The frozen reader, on its own copy of the input, so it finds no
        // file where the player had none.
        Scratch t;
        if (input.present) t.WriteLegacy(input.bytes);
        legacy::Config read;
        const bool present = legacy::Load(t.legacy().string(), read);
        Check(present == input.present, name + ": the frozen reader finds the file exactly when it is there");
        Check(input.present || !fs::exists(t.legacy()), name + ": the frozen reader writes no file");
        const Record imported = ObserveImport(read);

        // 71f6cda: the downward lean limit follows LimitY.
        std::vector<std::string> diff = Differences(oracle, imported);
        const std::string mirrored = "field.pos.limit_y_down: " + Bits(0.20f) + " / " + Bits(read.limitY);
        if (read.limitY != 0.20f) {
            Check(std::find(diff.begin(), diff.end(), mirrored) != diff.end(),
                  name + ": comparison 1, the downward limit follows LimitY since 71f6cda");
            diff.erase(std::remove(diff.begin(), diff.end(), mirrored), diff.end());
            ++limit_y_down;
        }
        for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", name.c_str(), d.c_str());
        Check(diff.empty(), name + ": comparison 1, the oracle and the import agree apart from 71f6cda");
        ++compared;
    }
    std::printf("comparison 1: %d inputs, %d of them with LimitY away from 0.20\n", compared, limit_y_down);
    Check(limit_y_down > 0, "the inputs reach 71f6cda's difference");
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        const std::vector<Input> inputs = Inputs();
        Compare(inputs);
        RemoveTree(ScratchRoot());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
