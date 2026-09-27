#include "config.h"
#include "logging.h"

#include "legacy_config/legacy_config.h"

#include <cameraunlock/config/value_codecs.h>
#include <cameraunlock/input/key_bindings.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ControlHT::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;
using ::cameraunlock::input::FormatKeyBindings;
using ::cameraunlock::input::KeyModifiers;

constexpr const wchar_t* kIniName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyIniName = L"HeadTracking.ini";

// data/games.json's display_name for control-ultimate-edition.
constexpr const char* kDisplayName = "Control: Ultimate Edition";

// FovScale: 0 leaves the game's own slider in charge; the widest the mod
// writes is 2.0.
constexpr double kMaxFovScale = 2.0;

constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;

// The chords every build before the canonical format bound in code.
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

void Save(const char* rows, const std::function<void(Config&)>& change) {
    const cfg::ConfigSaveResult result = g_owner->Save(change);
    if (result.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("config: %s %s: %s", rows, cfg::ConfigSaveStatusName(result.status), result.reason.c_str());
    }
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
}

// A legacy hotkey code as a key list, with the action's Ctrl+Shift chord, which
// every earlier build bound beside it in code.
std::string HotkeyList(int code, const char* key, int chord, std::vector<cfg::DroppedValue>& dropped) {
    const std::string plain = cfg::LegacyVirtualKeyToBindings(code, "Hotkeys", key, dropped);
    const std::string chorded = FormatKeyBindings({{kChord, chord}});
    return plain.empty() ? chorded : plain + ", " + chorded;
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    // Every earlier build opened HeadTracking.ini by its ANSI path, and the
    // frozen reader does the same. Where it finds no file, the published build
    // ran on its defaults.
    legacy::Config read;
    const bool present = legacy::Load(input.ansi_path, read);

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> pose_shaping;

    // Every sensitivity, inversion and deadzone shipped at identity, and the
    // engine's axis signs are already in camera_injection.h, so nothing folds:
    // the mod applies the pose as the tracker sends it, and a value the player
    // changed is dropped.
    const auto shaping = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, pose_shaping, dropped);
    };
    shaping(read.yawSensitivity, 1.0f, "Rotation", "YawSensitivity");
    shaping(read.pitchSensitivity, 1.0f, "Rotation", "PitchSensitivity");
    shaping(read.rollSensitivity, 1.0f, "Rotation", "RollSensitivity");
    shaping(read.invertYaw, false, "Rotation", "InvertYaw");
    shaping(read.invertPitch, false, "Rotation", "InvertPitch");
    shaping(read.invertRoll, false, "Rotation", "InvertRoll");
    shaping(read.yawDeadzone, 0.0f, "Rotation", "YawDeadzone");
    shaping(read.pitchDeadzone, 0.0f, "Rotation", "PitchDeadzone");
    shaping(read.rollDeadzone, 0.0f, "Rotation", "RollDeadzone");
    shaping(read.positionSensitivityX, 1.0f, "Position", "SensitivityX");
    shaping(read.positionSensitivityY, 1.0f, "Position", "SensitivityY");
    shaping(read.positionSensitivityZ, 1.0f, "Position", "SensitivityZ");

    // The startup code put a port outside 1024-65535 on 4242; the import
    // carries the port the session ran on.
    const int port = (read.udpPort < MIN_UDP_PORT || read.udpPort > MAX_UDP_PORT) ? DEFAULT_UDP_PORT : read.udpPort;

    // The reader keeps every float finite and inside a range the canonical rows
    // hold, and FovScale at 0 or inside 0.5-2.0, so each carries over as it is.
    out.udpPort = port;
    out.enableOnStartup = read.enableOnStartup;
    out.worldSpaceYaw = read.worldSpaceYaw;
    out.fovScale = read.fovScale;
    out.localSmoothing = read.localSmoothing;
    out.remoteSmoothing = read.remoteSmoothing;
    out.limitX = read.limitX;
    out.limitY = read.limitY;
    // The startup code set the downward limit from LimitY.
    out.limitYDown = read.limitY;
    out.limitZ = read.limitZ;
    out.limitZBack = read.limitZBack;

    // [General] PositionEnabled chose the startup mode and nothing else: the
    // cycle reached every mode either way.
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(
        read.positionEnabled ? cameraunlock::TrackingMode::RotationAndPosition
                             : cameraunlock::TrackingMode::RotationOnly);
    out.rotationEnabled = channels.rotation_enabled;
    out.positionEnabled = channels.position_enabled;

    out.toggleKey = HotkeyList(read.toggleKey, "Toggle", kVkY, dropped);
    out.cycleTrackingModeKey = HotkeyList(read.togglePositionKey, "TogglePosition", kVkG, dropped);
    out.yawModeKey = HotkeyList(read.toggleYawModeKey, "ToggleYawMode", kVkH, dropped);

    // A setting still at what the dev build shipped is no player's choice, so
    // it follows Defaults.ini. The frozen struct's defaults are what it
    // shipped: its first-run file wrote the same values.
    const legacy::Config shipped;
    cfg::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, port, shipped.udpPort);
    follows.Setting(Concept::EnableOnStartup, read.enableOnStartup, shipped.enableOnStartup);
    follows.Setting(Concept::WorldSpaceYaw, read.worldSpaceYaw, shipped.worldSpaceYaw);
    follows.TrackingMode(read.positionEnabled, shipped.positionEnabled);
    follows.Setting(Concept::LocalSmoothing, read.localSmoothing, shipped.localSmoothing);
    follows.Setting(Concept::RemoteSmoothing, read.remoteSmoothing, shipped.remoteSmoothing);
    follows.Setting(Concept::PositionLimitX, read.limitX, shipped.limitX);
    follows.Setting(Concept::PositionLimitY, read.limitY, shipped.limitY);
    follows.Setting(Concept::PositionLimitYDown, read.limitY, shipped.limitY);
    follows.Setting(Concept::PositionLimitZ, read.limitZ, shipped.limitZ);
    follows.Setting(Concept::PositionLimitZBack, read.limitZBack, shipped.limitZBack);
    follows.Setting(Concept::ToggleKey, read.toggleKey, shipped.toggleKey);
    follows.Setting(Concept::CycleTrackingModeKey, read.togglePositionKey, shipped.togglePositionKey);
    follows.Setting(Concept::YawModeKey, read.toggleYawModeKey, shipped.toggleYawModeKey);

    return present ? cfg::ImportResult::Imported(std::move(dropped), std::move(pose_shaping), follows.Concepts())
                   : cfg::ImportResult::Absent(std::move(dropped), std::move(pose_shaping), follows.Concepts());
}

}  // namespace

cfg::ConfigTable<Config> Table() {
    cfg::ConfigTable<Config> table;
    table.Concept<Concept::UdpPort>(&Config::udpPort)
        .Concept<Concept::EnableOnStartup>(&Config::enableOnStartup)
        .Concept<Concept::WorldSpaceYaw>(&Config::worldSpaceYaw)
        .Writable()
        .Concept<Concept::RotationEnabled>(&Config::rotationEnabled)
        .Writable()
        .Concept<Concept::LocalSmoothing>(&Config::localSmoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remoteSmoothing)
        .Concept<Concept::PositionEnabled>(&Config::positionEnabled)
        .Writable()
        .Concept<Concept::PositionLimitX>(&Config::limitX)
        .Concept<Concept::PositionLimitY>(&Config::limitY)
        .Concept<Concept::PositionLimitYDown>(&Config::limitYDown)
        .Concept<Concept::PositionLimitZ>(&Config::limitZ)
        .Concept<Concept::PositionLimitZBack>(&Config::limitZBack)
        .Concept<Concept::ToggleKey>(&Config::toggleKey)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycleTrackingModeKey)
        .Concept<Concept::YawModeKey>(&Config::yawModeKey)
        .Local("Camera", "FovScale", &Config::fovScale, cfg::FloatCodec(),
               "Field of view. Control has its own FOV Scale slider under Options > Graphics,\n"
               "but the game clamps that slider to 0.75-1.25. This is the same multiplier,\n"
               "written past the clamp and applied again every frame. 0 leaves Control's own\n"
               "slider in charge. Otherwise 0.5 to 2.0: from Control's default 70 degrees\n"
               "horizontal, 1.25 gives 82, 1.5 gives 93 and 2.0 gives 109. Cutscenes keep their\n"
               "own framing.")
        .Range(0.0, kMaxFovScale);
    return table;
}

cfg::RenderHeader Header() {
    cfg::RenderHeader header;
    header.display_name = kDisplayName;
    return header;
}

cfg::LegacyImport<Config> Import() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    for (const legacy::Key& key : legacy::ReadKeys()) import.keys.push_back({key.section, key.key});
    return import;
}

cfg::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& mod_dir, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = mod_dir + L"\\" + kIniName;
    options.table = Table();
    options.import = Import();
    options.legacy_path = mod_dir + L"\\" + kLegacyIniName;
    options.header = Header();
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::wstring& mod_dir, cfg::DefaultsFile defaults) {
    g_owner = std::make_unique<cfg::ConfigOwner<Config>>(OwnerOptions(mod_dir, std::move(defaults)));
    const cfg::ConfigLoadResult<Config> result = g_owner->Load();
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
    if (!result.reason.empty()) Log::Line("config: %s", result.reason.c_str());
    Log::Line("config: %s", cfg::ConfigLoadStatusName(result.status));
    return result.config;
}

cameraunlock::TrackingMode StartupTrackingMode(const Config& config) {
    const auto mode = cameraunlock::DecodeTrackingMode(config.rotationEnabled, config.positionEnabled);
    if (!mode) throw std::logic_error("RotationEnabled and PositionEnabled are both false, which the table never gives");
    return *mode;
}

void SaveWorldSpaceYaw(bool worldSpaceYaw) {
    Save("[General] WorldSpaceYaw", [worldSpaceYaw](Config& c) { c.worldSpaceYaw = worldSpaceYaw; });
}

void SaveTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    Save("[General] RotationEnabled and [Position] PositionEnabled", [channels](Config& c) {
        c.rotationEnabled = channels.rotation_enabled;
        c.positionEnabled = channels.position_enabled;
    });
}

}  // namespace ControlHT::config
