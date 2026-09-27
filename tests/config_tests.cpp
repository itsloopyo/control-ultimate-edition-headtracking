// CameraUnlock.ini in the canonical config format.
//
// The committed CameraUnlock.ini is the table's fresh render, which is also what
// the owner creates beside the game exe at first launch: `default` on every
// global row, so each follows Defaults.ini, and the mod's own FovScale at 0. A
// toggle's save changes the lines of its rows and no other byte. An older
// HeadTracking.ini is imported once into a new CameraUnlock.ini through the
// frozen import and is never written; tests/config_differential/ holds that to
// the published build over the whole corpus, and the cases here are the ones
// worth reading as examples.
//
// `test_config --render-config <path>` writes the fresh render to <path> and
// exits, which is how `pixi run render-config` rewrites the committed file after
// a change to a row, a comment or a default.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

#include "core/config.h"

namespace {

namespace cfg = ::cameraunlock::config;
namespace fs = std::filesystem;
using cameraunlock::TrackingMode;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s\n", what.c_str());
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

std::string Rendered() {
    return cfg::RenderCanonicalFresh(ControlHT::config::Table(), ControlHT::config::Header());
}

std::string CommittedFile() { return ReadFileBytes(fs::path(CONTROL_SOURCE_DIR) / "CameraUnlock.ini"); }

// A folder of its own per case, removed afterwards: `game` stands for the folder
// holding the game exe, and Defaults.ini sits in `global` beside it.
class Scratch {
public:
    explicit Scratch(const char* tag) {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        root_ = fs::path(temp) /
                ("control_ht_config_" + std::string(tag) + "_" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(game());
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    // A scanner can still hold a file the test just wrote, and a destructor must
    // not throw, so a folder left behind is reported and the run carries on.
    ~Scratch() {
        std::error_code error;
        fs::remove_all(root_, error);
        if (error) std::printf("  scratch folder left behind: %s: %s\n", root_.string().c_str(), error.message().c_str());
    }

    fs::path game() const { return root_ / "game"; }
    fs::path ini() const { return game() / "CameraUnlock.ini"; }
    fs::path legacy() const { return game() / "HeadTracking.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }

    ControlHT::Config Load() const {
        return ControlHT::config::Load(game().wstring(), cfg::DefaultsFile::At(defaults().wstring()));
    }

    std::set<std::string> Names() const {
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(game())) names.insert(entry.path().filename().string());
        return names;
    }

private:
    fs::path root_;
};

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t end; (end = bytes.find("\r\n", start)) != std::string::npos; start = end + 2) {
        lines.push_back(bytes.substr(start, end - start));
    }
    return lines;
}

// The lines that differ between two files of the same line count, or "count" when
// the counts differ.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before), b = Lines(after);
    if (a.size() != b.size()) return {"count"};
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

bool Holds(const std::string& bytes, const std::string& line) {
    return bytes.find("\r\n" + line + "\r\n") != std::string::npos;
}

void TheCommittedFileIsTheFreshRender() {
    Check(Rendered() == CommittedFile(), "CameraUnlock.ini is the table's fresh render; run pixi run render-config");
}

// Every global row holds `default`; FovScale is this mod's own.
void TheCommittedFileFollowsDefaultsIni() {
    const std::string committed = CommittedFile();
    for (const char* line :
         {"UdpPort=default", "EnableOnStartup=default", "WorldSpaceYaw=default", "RotationEnabled=default",
          "LocalSmoothing=default", "RemoteSmoothing=default", "PositionEnabled=default", "PositionLimitX=default",
          "PositionLimitY=default", "PositionLimitYDown=default", "PositionLimitZ=default",
          "PositionLimitZBack=default", "ToggleKey=default", "CycleTrackingModeKey=default", "YawModeKey=default",
          "FovScale=0.0"}) {
        Check(Holds(committed, line), std::string("the committed file holds ") + line);
    }
    Check(committed.find("Sensitivity") == std::string::npos && committed.find("Invert") == std::string::npos &&
              committed.find("Deadzone") == std::string::npos,
          "the committed file has no sensitivity, inversion or deadzone");
}

void FirstLaunchCreatesTheCommittedFile() {
    Scratch s("created");
    const ControlHT::Config loaded = s.Load();
    Check(ReadFileBytes(s.ini()) == CommittedFile(), "the first launch writes the committed file byte for byte");
    Check(s.Names() == std::set<std::string>{"CameraUnlock.ini"},
          "the first launch creates CameraUnlock.ini and nothing else beside the exe");
    Check(fs::exists(s.defaults()), "the first launch creates Defaults.ini where none exists");
    Check(loaded.toggleKey == "End, Ctrl+Shift+Y", "ToggleKey starts at End, Ctrl+Shift+Y");
    Check(loaded.cycleTrackingModeKey == "PageUp, Ctrl+Shift+G", "CycleTrackingModeKey starts at PageUp, Ctrl+Shift+G");
    Check(loaded.yawModeKey == "PageDown, Ctrl+Shift+H", "YawModeKey starts at PageDown, Ctrl+Shift+H");
    Check(loaded.enableOnStartup && loaded.worldSpaceYaw, "tracking starts on, in world-space yaw");
    Check(ControlHT::config::StartupTrackingMode(loaded) == TrackingMode::RotationAndPosition,
          "tracking starts in rotation and position");
    Check(loaded.fovScale == 0.0f, "FovScale starts at 0, leaving Control's own slider in charge");
    Check(loaded.limitYDown == 0.20f, "the downward lean limit starts at 0.20");
}

// A value in Defaults.ini reaches every row holding `default`, and never FovScale.
void ADefaultRowFollowsDefaultsIni() {
    Scratch s("follows");
    s.Load();
    WriteFileBytes(s.defaults(), "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n[General]\r\nWorldSpaceYaw=false\r\n\r\n"
                                 "[Hotkeys]\r\nToggleKey=F8\r\n");
    const ControlHT::Config c = s.Load();
    Check(c.toggleKey == "F8", "ToggleKey follows Defaults.ini");
    Check(!c.worldSpaceYaw, "WorldSpaceYaw follows Defaults.ini");
    Check(c.fovScale == 0.0f, "FovScale stays the game's own");
}

void TheYawToggleSavesItsLineAndNothingElse() {
    Scratch s("save_yaw");
    s.Load();
    const std::string before = ReadFileBytes(s.ini());
    const std::string defaults = ReadFileBytes(s.defaults());
    ControlHT::config::SaveWorldSpaceYaw(false);
    Check(ChangedLines(before, ReadFileBytes(s.ini())) == std::vector<std::string>{"WorldSpaceYaw=false"},
          "a yaw save writes WorldSpaceYaw over default, and nothing else");
    Check(ReadFileBytes(s.defaults()) == defaults, "a save leaves Defaults.ini as it was");
    Check(!s.Load().worldSpaceYaw, "the saved yaw mode comes back at the next launch");
}

// The mode is one setting in two rows, so a save writes both.
void TheModeCycleSavesThePair() {
    Scratch s("save_mode");
    s.Load();
    const std::string before = ReadFileBytes(s.ini());

    ControlHT::config::SaveTrackingMode(TrackingMode::RotationOnly);
    Check(ChangedLines(before, ReadFileBytes(s.ini())) ==
              (std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=false"}),
          "rotation only writes the pair over default");

    ControlHT::config::SaveTrackingMode(TrackingMode::PositionOnly);
    Check(ChangedLines(before, ReadFileBytes(s.ini())) ==
              (std::vector<std::string>{"RotationEnabled=false", "PositionEnabled=true"}),
          "position only writes the pair");
    Check(ControlHT::config::StartupTrackingMode(s.Load()) == TrackingMode::PositionOnly,
          "the saved mode comes back at the next launch");

    ControlHT::config::SaveTrackingMode(TrackingMode::RotationAndPosition);
    Check(ChangedLines(before, ReadFileBytes(s.ini())) ==
              (std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=true"}),
          "back to full, the pair holds values");
}

// The legacy file is imported into a new CameraUnlock.ini and left as it was.
void TheLegacyFileIsImportedAndLeftAsItWas() {
    Scratch s("import");
    const std::string legacy =
        "[General]\r\nPositionEnabled=false\r\nWorldSpaceYaw=false\r\n; my note\r\n[Camera]\r\nFovScale=1.5\r\n"
        "[Rotation]\r\nYawSensitivity=1.5\r\nRemoteSmoothing=0.40\r\n[Position]\r\nLimitY=0.35\r\n"
        "[Hotkeys]\r\nToggle=113\r\nToggleYawMode=16\r\n";
    WriteFileBytes(s.legacy(), legacy);
    const ControlHT::Config c = s.Load();
    Check(!c.worldSpaceYaw, "WorldSpaceYaw=false is carried");
    Check(c.remoteSmoothing == 0.4f, "RemoteSmoothing is carried");
    Check(c.fovScale == 1.5f, "FovScale is carried");
    Check(ControlHT::config::StartupTrackingMode(c) == TrackingMode::RotationOnly,
          "PositionEnabled=false starts in rotation only");
    Check(c.limitY == 0.35f && c.limitYDown == 0.35f, "LimitY bounds both vertical directions, as it did");
    Check(c.toggleKey == "F2, Ctrl+Shift+Y", "the toggle key is carried beside its chord");
    Check(c.yawModeKey == "Ctrl+Shift+H", "a yaw key on Shift alone is unbound, and its chord kept");
    Check(ReadFileBytes(s.legacy()) == legacy, "HeadTracking.ini keeps its bytes");
    Check((s.Names() == std::set<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}),
          "the import creates CameraUnlock.ini and nothing else");
    const std::string migrated = ReadFileBytes(s.ini());
    for (const char* line :
         {"WorldSpaceYaw=false", "RemoteSmoothing=0.4", "RotationEnabled=true", "PositionEnabled=false",
          "PositionLimitY=0.35", "PositionLimitYDown=0.35", "FovScale=1.5", "ToggleKey=F2, Ctrl+Shift+Y",
          "YawModeKey=Ctrl+Shift+H", "LocalSmoothing=default", "CycleTrackingModeKey=default"}) {
        Check(Holds(migrated, line), std::string("the migrated file holds ") + line);
    }
    Check(migrated.find("Sensitivity") == std::string::npos, "a changed sensitivity is not carried");

    // Once CameraUnlock.ini exists, HeadTracking.ini is not read again.
    WriteFileBytes(s.legacy(), "[General]\r\nWorldSpaceYaw=true\r\n");
    Check(!s.Load().worldSpaceYaw, "the next launch reads CameraUnlock.ini, not HeadTracking.ini");
    Check(ReadFileBytes(s.ini()) == migrated, "the next launch writes nothing");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        WriteFileBytes(argv[2], Rendered());
        return 0;
    }

    TheCommittedFileIsTheFreshRender();
    TheCommittedFileFollowsDefaultsIni();
    FirstLaunchCreatesTheCommittedFile();
    ADefaultRowFollowsDefaultsIni();
    TheYawToggleSavesItsLineAndNothingElse();
    TheModeCycleSavesThePair();
    TheLegacyFileIsImportedAndLeftAsItWas();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
