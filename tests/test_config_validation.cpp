// Unit tests for HeadTracking.ini parsing in src/core/config.cpp.
//
// The INI is the mod's file-system boundary: a hand-edited text file whose
// numbers reach the tracking pipeline, the camera pose and a Win32 API. Every
// value here is one a user can actually type, and the property under test is
// that a malformed one lands on a documented default instead of reaching the
// camera - a NaN sensitivity defeats every comparison-based clamp downstream
// (each comparison against it is false), and a hotkey that is not a virtual key
// code simply never fires.
//
// Runs headless: it writes real INI files to the temp directory and reads them
// back through the same IniReader the game uses. No game, no log file (the
// process-wide log is closed, so Log::Line is a no-op).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core/config.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using ControlHT::Config;

static int g_failures = 0;

static void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static void CheckEq(float actual, float expected, const char* what) {
    if (!(std::fabs(actual - expected) <= 1e-6f)) {
        std::printf("FAIL: %s (got %.6f, expected %.6f)\n", what, actual, expected);
        ++g_failures;
    }
}

static void CheckEqInt(int actual, int expected, const char* what) {
    if (actual != expected) {
        std::printf("FAIL: %s (got %d, expected %d)\n", what, actual, expected);
        ++g_failures;
    }
}

// GetPrivateProfileString resolves a bare filename against the Windows
// directory, so the path handed to IniReader has to be absolute.
static std::string WriteTempIni(const char* body) {
    char dir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, dir);
    static int counter = 0;
    char path[MAX_PATH] = {};
    std::snprintf(path, sizeof(path), "%scontrolht-test-%lu-%d.ini", dir,
                  static_cast<unsigned long>(GetCurrentProcessId()), counter++);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << body;
    out.close();
    return path;
}

static Config LoadIni(const char* body) {
    const std::string path = WriteTempIni(body);
    Config cfg;
    const bool opened = cfg.LoadFromFile(path);
    DeleteFileA(path.c_str());
    Check(opened, "the test INI was opened");
    return cfg;
}

// A file that does not exist keeps every documented default rather than
// zeroing anything.
static void TestMissingFileKeepsDefaults() {
    Config cfg;
    const bool loaded = cfg.LoadFromFile("Z:\\no\\such\\HeadTracking.ini");
    Check(!loaded, "a missing config file reports that it was not loaded");
    CheckEq(cfg.yawSensitivity, 1.0f, "missing file keeps the default sensitivity");
    CheckEq(cfg.localSmoothing, 0.0f, "missing file keeps LocalSmoothing at 0");
    CheckEq(cfg.remoteSmoothing, 0.15f, "missing file keeps RemoteSmoothing at 0.15");
    CheckEqInt(cfg.toggleKey, VK_END, "missing file keeps the default toggle key");
}

static void TestEmptyFileKeepsDefaults() {
    Config cfg = LoadIni("");
    CheckEq(cfg.pitchSensitivity, 1.0f, "an empty file keeps the default sensitivity");
    CheckEq(cfg.limitZ, 0.40f, "an empty file keeps the default forward lean limit");
    CheckEqInt(cfg.udpPort, 4242, "an empty file keeps the default UDP port");
    CheckEqInt(cfg.toggleYawModeKey, VK_NEXT, "an empty file keeps the default yaw-mode key");
}

// strtod parses "nan" and "inf" happily, and a NaN then passes every clamp
// downstream because every comparison against it is false.
static void TestNonFiniteFloatsFallBackToDefaults() {
    Config cfg = LoadIni(
        "[Rotation]\n"
        "YawSensitivity=nan\n"
        "PitchSensitivity=inf\n"
        "RollSensitivity=-nan\n"
        "LocalSmoothing=nan\n"
        "[Position]\n"
        "LimitZ=inf\n");
    CheckEq(cfg.yawSensitivity, 1.0f, "a NaN sensitivity falls back to the default");
    CheckEq(cfg.pitchSensitivity, 1.0f, "an infinite sensitivity falls back to the default");
    CheckEq(cfg.rollSensitivity, 1.0f, "a negative NaN sensitivity falls back to the default");
    CheckEq(cfg.localSmoothing, 0.0f, "a NaN smoothing falls back to the default");
    CheckEq(cfg.limitZ, 0.40f, "an infinite position limit falls back to the default");
    Check(std::isfinite(cfg.yawSensitivity) && std::isfinite(cfg.limitZ),
          "nothing non-finite survives the config boundary");
}

static void TestOutOfRangeFloatsAreClamped() {
    Config cfg = LoadIni(
        "[Rotation]\n"
        "YawSensitivity=99\n"
        "PitchSensitivity=-4\n"
        "LocalSmoothing=-1\n"
        "RemoteSmoothing=5\n"
        "YawDeadzone=180\n"
        "[Position]\n"
        "LimitX=50\n"
        "LimitZBack=-3\n");
    CheckEq(cfg.yawSensitivity, 10.0f, "sensitivity clamps to the top of its band");
    CheckEq(cfg.pitchSensitivity, 0.0f, "a negative sensitivity clamps to zero");
    CheckEq(cfg.localSmoothing, 0.0f, "smoothing clamps to zero, and nothing floors it");
    CheckEq(cfg.remoteSmoothing, 1.0f, "smoothing clamps to one");
    CheckEq(cfg.yawDeadzone, 45.0f, "the deadzone clamps to 45 degrees");
    CheckEq(cfg.limitX, 2.0f, "a position limit clamps to 2 metres");
    CheckEq(cfg.limitZBack, 0.0f, "a negative position limit clamps to zero");
}

// Each smoothing key falls back to ITS OWN default. A bad RemoteSmoothing
// dropping to the local default would leave a phone's network jitter entirely
// unsmoothed.
static void TestSmoothingKeysFallBackIndependently() {
    Config cfg = LoadIni(
        "[Rotation]\n"
        "LocalSmoothing=0.4\n"
        "RemoteSmoothing=nan\n");
    CheckEq(cfg.localSmoothing, 0.4f, "a good LocalSmoothing is kept");
    CheckEq(cfg.remoteSmoothing, 0.15f, "a bad RemoteSmoothing falls back to 0.15, not to 0");
}

// Zero means "leave Control's own FOV Scale slider alone", so the off switch
// and the usable band are not contiguous and cannot share one clamp.
static void TestFovScaleOffSwitchIsSeparateFromItsBand() {
    CheckEq(LoadIni("[Camera]\nFovScale=0\n").fovScale, 0.0f, "FovScale=0 stays off");
    CheckEq(LoadIni("[Camera]\nFovScale=1.5\n").fovScale, 1.5f, "a value in band is kept");
    CheckEq(LoadIni("[Camera]\nFovScale=9\n").fovScale, 2.0f, "an over-wide FovScale clamps to 2");
    CheckEq(LoadIni("[Camera]\nFovScale=0.1\n").fovScale, 0.5f, "a too-narrow FovScale clamps to 0.5");
    CheckEq(LoadIni("[Camera]\nFovScale=-2\n").fovScale, 0.0f,
            "a negative FovScale is not a field of view, so the game keeps its own");
    CheckEq(LoadIni("[Camera]\nFovScale=nan\n").fovScale, 0.0f,
            "a non-finite FovScale leaves the game's own setting in charge");
}

// The regression. GetAsyncKeyState is defined for virtual key codes 1..254 and
// the poller skips code 0 - which is also what IniReader::ReadInt returns for a
// key that is present but unparseable. Unvalidated, each of these silently
// disabled a hotkey with nothing in the log to say why.
static void TestHotkeysMustBeVirtualKeyCodes() {
    Config cfg = LoadIni(
        "[Hotkeys]\n"
        "Toggle=999\n"
        "TogglePosition=End\n"
        "ToggleYawMode=-5\n");
    CheckEqInt(cfg.toggleKey, VK_END, "an out-of-range hotkey falls back to the default");
    CheckEqInt(cfg.togglePositionKey, VK_PRIOR,
               "a key NAME is not a VK code, so it falls back to the default");
    CheckEqInt(cfg.toggleYawModeKey, VK_NEXT, "a negative hotkey falls back to the default");

    CheckEqInt(LoadIni("[Hotkeys]\nToggle=0\n").toggleKey, VK_END,
               "a zero hotkey falls back to the default");
}

static void TestValidHotkeysAreKeptExactly() {
    Config cfg = LoadIni(
        "[Hotkeys]\n"
        "Toggle=113\n"     // F2
        "TogglePosition=1\n"    // VK_LBUTTON, the bottom of the valid range
        "ToggleYawMode=254\n"); // the top of it
    CheckEqInt(cfg.toggleKey, 113, "a valid VK code is passed through untouched");
    CheckEqInt(cfg.togglePositionKey, 1, "the bottom of the valid range is accepted");
    CheckEqInt(cfg.toggleYawModeKey, 254, "the top of the valid range is accepted");
}

// The port is range-checked where the receiver is started (out-of-range values
// would otherwise be truncated by the uint16_t cast), so the parse itself only
// has to carry the value through intact.
static void TestUdpPortIsReadVerbatim() {
    CheckEqInt(LoadIni("[Network]\nUdpPort=5000\n").udpPort, 5000, "a valid port is read");
    CheckEqInt(LoadIni("[Network]\nUdpPort=70000\n").udpPort, 70000,
               "an out-of-range port is carried through for the receiver to reject");
}

static void TestBoolsAndYawModeRoundTrip() {
    Config cfg = LoadIni(
        "[General]\n"
        "EnableOnStartup=false\n"
        "PositionEnabled=false\n"
        "WorldSpaceYaw=false\n"
        "[Rotation]\n"
        "InvertPitch=true\n");
    Check(!cfg.enableOnStartup, "EnableOnStartup=false is honoured");
    Check(!cfg.positionEnabled, "PositionEnabled=false is honoured");
    Check(!cfg.worldSpaceYaw, "WorldSpaceYaw=false is honoured");
    Check(cfg.invertPitch, "InvertPitch=true is honoured");
    Check(!cfg.invertYaw && !cfg.invertRoll, "the other inversions keep their defaults");
}

int main() {
    TestMissingFileKeepsDefaults();
    TestEmptyFileKeepsDefaults();
    TestNonFiniteFloatsFallBackToDefaults();
    TestOutOfRangeFloatsAreClamped();
    TestSmoothingKeysFallBackIndependently();
    TestFovScaleOffSwitchIsSeparateFromItsBand();
    TestHotkeysMustBeVirtualKeyCodes();
    TestValidHotkeysAreKeptExactly();
    TestUdpPortIsReadVerbatim();
    TestBoolsAndYawModeRoundTrip();

    if (g_failures == 0) {
        std::printf("All config_validation tests passed.\n");
        return 0;
    }
    std::printf("%d config_validation test(s) FAILED.\n", g_failures);
    return 1;
}
