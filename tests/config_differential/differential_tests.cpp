// The differential test for the conversion from HeadTracking.ini to
// CameraUnlock.ini.
//
//   Oracle     the dev pre-release's reader and startup code, the newest and
//              only published build (oracle/oracle_reader.cpp)
//   Import     the frozen reader in src/legacy_config/, through the startup
//              code of the commit that froze it
//   Migration  the config owner's Load in a folder holding only the input as
//              HeadTracking.ini, which imports it into a new CameraUnlock.ini,
//              then the canonical reader and table on it, through this build's
//              startup code
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
// Comparison 2, import against migration, is the proof for the conversion; see
// the section of that name below for what it allows.
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
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "core/config.h"
#include "legacy_config/legacy_config.h"
#include "oracle/oracle_reader.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"

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
// game exe runs from, and Defaults.ini sits in `global` beside it. Every folder
// lives under one root for the run, removed once at the end.

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
    fs::path canonical() const { return game() / "CameraUnlock.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }

    void WriteLegacy(const std::string& bytes) const { WriteFileBytes(legacy(), bytes); }
    void WriteDefaults(const std::string& bytes) const {
        fs::create_directories(defaults().parent_path());
        WriteFileBytes(defaults(), bytes);
    }

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

// ---- Comparison 2 ------------------------------------------------------------------
//
// The migration: the config owner's Load in a game folder holding only the input
// as HeadTracking.ini, which imports it through config::Import into a new
// CameraUnlock.ini, then the canonical reader and table on that file. It must
// start the mod exactly as the import did, apart from the changes core's
// data/config-format.json approves:
//
//   pose_shaping  a sensitivity, inversion or deadzone the player set away from
//                 the shipped identity is dropped, and the session runs at
//                 identity. Every shipped value is identity, so nothing folds.
//   N3            a hotkey on Ctrl, Shift or Alt alone is unbound; its
//                 Ctrl+Shift chord stays.
//
// The frozen reader holds every float finite and inside a range the canonical
// rows accept, and every hotkey inside 0x01-0xFE, so no input needs N1 or N2
// and none is deferred.
//
// Each input with a file migrates three times: over a Defaults.ini the owner
// creates with the built-in values, from a read-only HeadTracking.ini, and over
// a Defaults.ini that differs from the built-in value on every global row. The
// first two give the settings the import read. A setting still at what the dev
// build shipped is no player's choice (owner rule of 2026-09-26), so it is
// written `default` and the third gives Defaults.ini's value for it; a setting
// the player changed keeps the imported value there too.

using Drop = std::tuple<cfg::DropRule, std::string, std::string>;

// The settings the running mod acts on from a canonical Config, through this
// build's startup code: src/core/mod.cpp ApplyConfigToSession, which leaves the
// processors at identity sensitivity, inversion and deadzone, its port fallback
// and FovScale floor, and src/core/hotkeys.cpp, which puts each list through
// ParseKeyBindings and RegisterKeyBindings.
Record ObserveCanonical(const ControlHT::Config& c) {
    control_oracle::Published g;
    g.udp_port = (c.udpPort < 1024 || c.udpPort > 65535) ? 4242 : c.udpPort;
    g.tracking_enabled = c.enableOnStartup;
    g.world_space_yaw = c.worldSpaceYaw;
    g.tracking_mode = static_cast<int>(ControlHT::config::StartupTrackingMode(c));
    g.fov_scale = (c.fovScale > 0.0f && c.fovScale < 0.5f) ? 0.5f : c.fovScale;
    g.yaw_sens = 1.0f;
    g.pitch_sens = 1.0f;
    g.roll_sens = 1.0f;
    g.local_smoothing = c.localSmoothing;
    g.remote_smoothing = c.remoteSmoothing;
    g.pos_sens_x = 1.0f;
    g.pos_sens_y = 1.0f;
    g.pos_sens_z = 1.0f;
    g.limit_x = c.limitX;
    g.limit_y = c.limitY;
    g.limit_y_down = c.limitYDown;
    g.limit_z = c.limitZ;
    g.limit_z_back = c.limitZBack;
    const std::pair<int, const std::string*> lists[] = {
        {control_oracle::kToggle, &c.toggleKey},
        {control_oracle::kCycleMode, &c.cycleTrackingModeKey},
        {control_oracle::kYawMode, &c.yawModeKey},
    };
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        Check(parsed.ok(), "a migrated key list parses: " + *list);
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            g.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    return ObserveOracle(g);
}

// The drops the approved changes call for, from what the frozen reader read, and
// the settings the session then runs on: comparison 2's whole allowance.
struct Allowed {
    std::vector<Drop> dropped;
    Record observed;
};

bool ModifierKey(int code) { return (code >= 0x10 && code <= 0x12) || (code >= 0xA0 && code <= 0xA5); }

Allowed ApplyApprovedChanges(const legacy::Config& read) {
    Allowed a;
    legacy::Config c = read;
    const auto shaping = [&a](auto& value, auto shipped, const char* section, const char* key) {
        if (value != shipped) a.dropped.push_back({cfg::DropRule::PoseShaping, section, key});
        value = shipped;
    };
    shaping(c.yawSensitivity, 1.0f, "Rotation", "YawSensitivity");
    shaping(c.pitchSensitivity, 1.0f, "Rotation", "PitchSensitivity");
    shaping(c.rollSensitivity, 1.0f, "Rotation", "RollSensitivity");
    shaping(c.invertYaw, false, "Rotation", "InvertYaw");
    shaping(c.invertPitch, false, "Rotation", "InvertPitch");
    shaping(c.invertRoll, false, "Rotation", "InvertRoll");
    shaping(c.yawDeadzone, 0.0f, "Rotation", "YawDeadzone");
    shaping(c.pitchDeadzone, 0.0f, "Rotation", "PitchDeadzone");
    shaping(c.rollDeadzone, 0.0f, "Rotation", "RollDeadzone");
    shaping(c.positionSensitivityX, 1.0f, "Position", "SensitivityX");
    shaping(c.positionSensitivityY, 1.0f, "Position", "SensitivityY");
    shaping(c.positionSensitivityZ, 1.0f, "Position", "SensitivityZ");
    const std::pair<int*, const char*> hotkeys[] = {
        {&c.toggleKey, "Toggle"}, {&c.togglePositionKey, "TogglePosition"}, {&c.toggleYawModeKey, "ToggleYawMode"}};
    for (const auto& [code, key] : hotkeys) {
        if (!ModifierKey(*code)) continue;
        a.dropped.push_back({cfg::DropRule::ModifierKey, "Hotkeys", key});
        *code = 0;
    }

    std::sort(a.dropped.begin(), a.dropped.end());
    a.observed = ObserveImport(c);
    return a;
}

std::vector<std::string> CanonicalDiagnostics(const std::string& bytes, ControlHT::Config& out) {
    std::vector<std::string> found;
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(bytes);
    for (const cfg::CanonicalDiagnostic& d : doc.diagnostics) found.push_back("reader: " + cfg::DescribeCanonicalDiagnostic(d));
    const cfg::ConfigTable<ControlHT::Config> table = ControlHT::config::Table();
    out = table.defaults();
    for (const cfg::CanonicalDiagnostic& d : cfg::ApplyCanonical(doc, table, out).diagnostics) {
        found.push_back("table: " + cfg::DescribeCanonicalDiagnostic(d));
    }
    return found;
}

bool AsciiCrlf(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c > 0x7E) return false;
        if (c == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) return false;
        if (c == '\n' && (i == 0 || bytes[i - 1] != '\r')) return false;
        if (c < 0x20 && c != '\r' && c != '\n') return false;
    }
    return !bytes.empty() && bytes.back() == '\n';
}

// A file's bytes, last write time and attributes, which no load may change.
struct FileState {
    std::string bytes;
    unsigned long long written = 0;
    DWORD attributes = 0;
    bool operator==(const FileState& other) const {
        return bytes == other.bytes && written == other.written && attributes == other.attributes;
    }
};

std::optional<FileState> StateOf(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("cannot read the attributes of " + path.string());
    }
    FileState state;
    state.bytes = ReadFileBytes(path);
    state.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

std::set<std::string> Names(const fs::path& folder) {
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(folder)) names.insert(entry.path().filename().string());
    return names;
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    for (const std::string& line : log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// A Defaults.ini holding a value other than the built-in one on every global row
// the table binds, so a migration that wrote `default` where the imported value
// is not what `default` gives would read back differently over it.
const char* const kSkewedDefaults =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5252\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.5\r\n\r\n"
    "[Position]\r\nPositionEnabled=true\r\nPositionLimitX=0.5\r\nPositionLimitY=0.5\r\nPositionLimitYDown=0.5\r\n"
    "PositionLimitZ=0.5\r\nPositionLimitZBack=0.5\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n";

// A global row of the table and whether the player left it at what the dev
// build shipped, with what kSkewedDefaults gives it. The frozen struct's
// defaults are what the dev build shipped, and its first-run file writes the
// same values. The two tracking mode rows are one unit, read from
// [General] PositionEnabled alone, and the downward limit is LimitY's.
struct FollowRow {
    const char* key;
    bool untouched;
    std::function<void(ControlHT::Config&)> skew;
};

std::vector<FollowRow> FollowRows(const legacy::Config& read) {
    const legacy::Config shipped;
    const bool mode = read.positionEnabled == shipped.positionEnabled;
    const int port = (read.udpPort < 1024 || read.udpPort > 65535) ? 4242 : read.udpPort;
    using C = ControlHT::Config;
    return {
        {"UdpPort", port == shipped.udpPort, [](C& c) { c.udpPort = 5252; }},
        {"EnableOnStartup", read.enableOnStartup == shipped.enableOnStartup, [](C& c) { c.enableOnStartup = false; }},
        {"WorldSpaceYaw", read.worldSpaceYaw == shipped.worldSpaceYaw, [](C& c) { c.worldSpaceYaw = false; }},
        {"RotationEnabled", mode, [](C& c) { c.rotationEnabled = false; }},
        {"PositionEnabled", mode, [](C& c) { c.positionEnabled = true; }},
        {"LocalSmoothing", read.localSmoothing == shipped.localSmoothing, [](C& c) { c.localSmoothing = 0.5f; }},
        {"RemoteSmoothing", read.remoteSmoothing == shipped.remoteSmoothing, [](C& c) { c.remoteSmoothing = 0.5f; }},
        {"PositionLimitX", read.limitX == shipped.limitX, [](C& c) { c.limitX = 0.5f; }},
        {"PositionLimitY", read.limitY == shipped.limitY, [](C& c) { c.limitY = 0.5f; }},
        {"PositionLimitYDown", read.limitY == shipped.limitY, [](C& c) { c.limitYDown = 0.5f; }},
        {"PositionLimitZ", read.limitZ == shipped.limitZ, [](C& c) { c.limitZ = 0.5f; }},
        {"PositionLimitZBack", read.limitZBack == shipped.limitZBack, [](C& c) { c.limitZBack = 0.5f; }},
        {"ToggleKey", read.toggleKey == shipped.toggleKey, [](C& c) { c.toggleKey = "F8"; }},
        {"CycleTrackingModeKey", read.togglePositionKey == shipped.togglePositionKey,
         [](C& c) { c.cycleTrackingModeKey = "F9"; }},
        {"YawModeKey", read.toggleYawModeKey == shipped.toggleYawModeKey, [](C& c) { c.yawModeKey = "F10"; }},
    };
}

// The value text of `key` in a canonical file, whose keys this table never
// repeats across sections; nullopt where the file has no such line.
std::optional<std::string> RowValue(const std::string& bytes, const std::string& key) {
    const std::string start = "\r\n" + key + "=";
    const std::size_t at = bytes.find(start);
    if (at == std::string::npos) return std::nullopt;
    const std::size_t from = at + start.size();
    return bytes.substr(from, bytes.find("\r\n", from) - from);
}

// The folder beside this executable the migrated files are written to, for
// lint-migrated.mjs, which CTest runs after this test.
fs::path MigratedFolder() {
    std::vector<wchar_t> exe(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length == 0) throw std::runtime_error("cannot find this executable's path");
        if (length < exe.size()) return fs::path(std::wstring(exe.data(), length)).parent_path() / "migrated";
        exe.resize(exe.size() * 2);
    }
}

cfg::ConfigOwnerOptions<ControlHT::Config> OwnerOptions(const Scratch& s) {
    return ControlHT::config::OwnerOptions(s.game().wstring(), cfg::DefaultsFile::At(s.defaults().wstring()));
}

// Runs the owner's Load in `s`, whose game folder holds the input as
// HeadTracking.ini or nothing, checks what a load must do beyond comparison 2,
// and returns the settings the session runs on. A file it creates by migrating
// goes into `migrated_files`.
ControlHT::Config Migrate(const Input& input, const Scratch& s, const std::string& label,
                          std::set<std::string>& migrated_files) {
    const std::optional<FileState> legacy_before = StateOf(s.legacy());
    const std::optional<FileState> defaults_before = StateOf(s.defaults());

    const cfg::ConfigLoadResult<ControlHT::Config> loaded = cfg::ConfigOwner<ControlHT::Config>(OwnerOptions(s)).Load();
    const cfg::ConfigLoadStatus want = input.present ? cfg::ConfigLoadStatus::Migrated : cfg::ConfigLoadStatus::Created;
    if (loaded.status != want) {
        std::printf("  %s: %s, %s\n", label.c_str(), cfg::ConfigLoadStatusName(loaded.status), loaded.reason.c_str());
    }
    Check(loaded.status == want, label + ": the load is " + cfg::ConfigLoadStatusName(want));
    Check(StateOf(s.legacy()) == legacy_before, label + ": a load leaves HeadTracking.ini's bytes, write time and attributes");
    Check(!defaults_before || StateOf(s.defaults()) == defaults_before, label + ": a load leaves Defaults.ini as it was");
    if (loaded.status != want) return loaded.config;

    Check(Names(s.game()) == (input.present ? std::set<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}
                                            : std::set<std::string>{"CameraUnlock.ini"}),
          label + ": the game folder holds HeadTracking.ini and CameraUnlock.ini and nothing else");

    const std::string migrated = ReadFileBytes(s.canonical());
    Check(cfg::HasCanonicalStamp(migrated), label + ": CameraUnlock.ini carries the stamp");
    Check(AsciiCrlf(migrated), label + ": CameraUnlock.ini is ASCII with CRLF line ends");
    ControlHT::Config reread;
    const std::vector<std::string> diagnostics = CanonicalDiagnostics(migrated, reread);
    for (const std::string& d : diagnostics) std::printf("  %s: CameraUnlock.ini, %s\n", label.c_str(), d.c_str());
    Check(diagnostics.empty(), label + ": CameraUnlock.ini reads with no diagnostic");
    if (input.present) migrated_files.insert(migrated);

    // The next start reads CameraUnlock.ini, imports nothing and writes nothing.
    const std::optional<FileState> created = StateOf(s.canonical());
    const cfg::ConfigLoadResult<ControlHT::Config> again = cfg::ConfigOwner<ControlHT::Config>(OwnerOptions(s)).Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical, label + ": the next start reads CameraUnlock.ini");
    Check(Differences(ObserveCanonical(again.config), ObserveCanonical(loaded.config)).empty(),
          label + ": the next start runs on the same settings");
    Check(StateOf(s.canonical()) == created && StateOf(s.legacy()) == legacy_before,
          label + ": the next start changes neither file");
    Check(!input.present || LogSays(again.log, "is left as it was and is not read"),
          label + ": the next start logs that HeadTracking.ini is not read");
    return loaded.config;
}

void ImportAgainstMigration(const std::vector<Input>& inputs) {
    const std::string committed = ReadFileBytes(fs::path(CONTROL_SOURCE_DIR) / "CameraUnlock.ini");
    const cfg::ConfigTable<ControlHT::Config> table = ControlHT::config::Table();
    std::set<std::string> migrated_files;
    int compared = 0;
    int dropping = 0;
    int modifier = 0;
    std::map<std::string, std::pair<int, int>> follow_seen;  // untouched, changed
    for (const Input& input : inputs) {
        const std::string& name = input.name;

        // The import, run as the owner runs it but on a read-only copy: it reads
        // what the frozen reader reads, drops what the approved changes drop, and
        // writes nothing.
        cfg::ImportResult imported;
        legacy::Config read;
        {
            Scratch ro;
            if (input.present) {
                ro.WriteLegacy(input.bytes);
                SetFileAttributesW(ro.legacy().c_str(), FILE_ATTRIBUTE_READONLY);
            }
            const std::optional<FileState> before = StateOf(ro.legacy());
            ControlHT::Config unused = table.defaults();
            imported = ControlHT::config::Import().run({ro.legacy().wstring(), ro.legacy().string(), false}, unused);
            const std::set<std::string> left = input.present ? std::set<std::string>{"HeadTracking.ini"}
                                                             : std::set<std::string>{};
            Check(StateOf(ro.legacy()) == before && Names(ro.game()) == left,
                  name + ": the import leaves a read-only folder as it was");
            legacy::Load(ro.legacy().string(), read);
        }
        Check(imported.status == (input.present ? cfg::ImportStatus::Imported : cfg::ImportStatus::Absent),
              name + ": the import reads every input, as the published build did");

        const Allowed allowed = ApplyApprovedChanges(read);
        const std::vector<FollowRow> follow = FollowRows(read);
        for (const FollowRow& row : follow) {
            auto& seen = follow_seen[row.key];
            ++(row.untouched ? seen.first : seen.second);
        }
        if (!allowed.dropped.empty()) ++dropping;
        for (const Drop& d : allowed.dropped) {
            if (std::get<0>(d) == cfg::DropRule::ModifierKey) ++modifier;
        }
        std::vector<Drop> dropped;
        for (const cfg::DroppedValue& d : imported.dropped) dropped.push_back({d.rule, d.section, d.key});
        std::sort(dropped.begin(), dropped.end());
        Check(dropped == allowed.dropped, name + ": the import drops exactly what the approved changes drop");
        Check(imported.pose_shaping.size() == 12, name + ": the import records all twelve pose-shaping settings");
        for (const cfg::PoseShapingValue& p : imported.pose_shaping) {
            const bool changed = std::find(allowed.dropped.begin(), allowed.dropped.end(),
                                           Drop{cfg::DropRule::PoseShaping, p.section, p.key}) != allowed.dropped.end();
            Check(p.folded != changed, name + ": [" + p.section + "] " + p.key + " folds exactly when it is the shipped value");
        }

        // Over a Defaults.ini the owner creates with the built-in values.
        ControlHT::Config migrated_over_built_in;
        {
            Scratch s;
            if (input.present) s.WriteLegacy(input.bytes);
            const ControlHT::Config migrated = Migrate(input, s, name, migrated_files);
            migrated_over_built_in = migrated;
            const std::vector<std::string> diff = Differences(allowed.observed, ObserveCanonical(migrated));
            for (const std::string& d : diff) std::printf("  comparison 2, %s: %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": comparison 2, the migration runs as the import read, less the approved changes");

            // Over the built-in values the table's own defaults stand for Defaults.ini.
            ControlHT::Config reread;
            CanonicalDiagnostics(ReadFileBytes(s.canonical()), reread);
            Check(Differences(ObserveCanonical(reread), ObserveCanonical(migrated)).empty(),
                  name + ": CameraUnlock.ini reads back as the settings the session runs on");

            // A row the player left at what the dev build shipped is written
            // default. One they changed holds its value: every shipped value is
            // the schema's, so a changed one is never what default gives here.
            const std::string written = ReadFileBytes(s.canonical());
            for (const FollowRow& row : follow) {
                const std::optional<std::string> value = RowValue(written, row.key);
                Check(value.has_value(), name + ": CameraUnlock.ini has a " + row.key + " row");
                if (!value) continue;
                Check((*value == "default") == row.untouched,
                      name + ": " + row.key + "=" + *value +
                          (row.untouched ? " follows Defaults.ini, as the player never changed it"
                                         : " is the player's own value"));
            }

            // Fresh equals upgrade: the published build's first-run file, and no
            // file at all, both end as the committed file.
            if (name == kFirstRunName || name == "no file") {
                Check(written == committed, name + ": gives the committed file byte for byte");
            }
        }

        if (!input.present) {
            ++compared;
            continue;
        }

        // From a read-only HeadTracking.ini, which keeps its attribute.
        {
            Scratch ro;
            ro.WriteLegacy(input.bytes);
            SetFileAttributesW(ro.legacy().c_str(), FILE_ATTRIBUTE_READONLY);
            const ControlHT::Config c = Migrate(input, ro, name + " (read-only)", migrated_files);
            Check(Differences(allowed.observed, ObserveCanonical(c)).empty(),
                  name + ": a read-only HeadTracking.ini imports as a writable one does");
            Check((GetFileAttributesW(ro.legacy().c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
                  name + ": HeadTracking.ini keeps its read-only attribute");
        }

        // Over a Defaults.ini that differs everywhere.
        {
            Scratch skewed;
            skewed.WriteLegacy(input.bytes);
            skewed.WriteDefaults(kSkewedDefaults);
            const ControlHT::Config c = Migrate(input, skewed, name + " (skewed Defaults.ini)", migrated_files);
            ControlHT::Config expected = migrated_over_built_in;
            for (const FollowRow& row : follow) {
                if (row.untouched) row.skew(expected);
            }
            const std::vector<std::string> diff = Differences(ObserveCanonical(expected), ObserveCanonical(c));
            for (const std::string& d : diff) std::printf("  comparison 2, %s (skewed Defaults.ini): %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": comparison 2 over a Defaults.ini that differs everywhere, where each "
                                "setting the player never changed takes Defaults.ini's value");
        }
        ++compared;
    }
    std::printf("comparison 2: %d inputs, %d with a value the approved changes drop, %d modifier hotkeys\n",
                compared, dropping, modifier);
    Check(dropping > 0 && modifier > 0, "the inputs reach the pose-shaping and modifier-key drops");
    for (const auto& [key, seen] : follow_seen) {
        Check(seen.first > 0 && seen.second > 0, "the inputs leave " + key + " at what the dev build shipped and change it");
    }

    // Core's canonical config lint runs over these next (lint-migrated.mjs).
    const fs::path lint = MigratedFolder();
    fs::remove_all(lint);
    fs::create_directories(lint);
    std::size_t n = 0;
    for (const std::string& file : migrated_files) {
        WriteFileBytes(lint / (std::to_string(n++) + ".ini"), file);
    }
    std::printf("%zu distinct migrated files written to %s\n", migrated_files.size(), lint.string().c_str());
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        const std::vector<Input> inputs = Inputs();
        Compare(inputs);
        ImportAgainstMigration(inputs);
        RemoveTree(ScratchRoot());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
