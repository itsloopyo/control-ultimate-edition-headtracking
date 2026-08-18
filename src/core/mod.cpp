#include "pch.h"
#include "mod.h"
#include "logging.h"
#include "path_utils.h"
#include "constants.h"

#include "hooks/engine_camera_hook.h"
#include "hooks/coherent_reticle.h"
#include "hooks/fov_override.h"

#include <cameraunlock/hooks/hook_manager.h>

namespace ControlHT {

Mod& Mod::Instance() {
    static Mod inst;
    return inst;
}

// Maps the loaded config onto the tracking pipeline, so there is one place to
// look when a rotation, position or smoothing setting does not appear to be
// taking effect. The keys that configure something outside the pipeline - the
// UDP port, the FOV override - are applied where that thing is started.
void Mod::ApplyConfigToSession() {
    cameraunlock::TrackingProcessor& processor = m_session.GetProcessor();

    cameraunlock::SensitivitySettings sens;
    sens.yaw = m_config.yawSensitivity;
    sens.pitch = m_config.pitchSensitivity;
    sens.roll = m_config.rollSensitivity;
    sens.invert_yaw = m_config.invertYaw;
    sens.invert_pitch = m_config.invertPitch;
    sens.invert_roll = m_config.invertRoll;
    processor.SetSensitivity(sens);

    cameraunlock::DeadzoneSettings dz;
    dz.yaw = m_config.yawDeadzone;
    dz.pitch = m_config.pitchDeadzone;
    dz.roll = m_config.rollDeadzone;
    processor.SetDeadzone(dz);

    cameraunlock::PositionSettings pos;
    pos.sensitivity_x = m_config.positionSensitivityX;
    pos.sensitivity_y = m_config.positionSensitivityY;
    pos.sensitivity_z = m_config.positionSensitivityZ;
    pos.limit_x = m_config.limitX;
    pos.limit_y = m_config.limitY;
    pos.limit_z = m_config.limitZ;
    pos.limit_z_back = m_config.limitZBack;
    m_session.GetPositionProcessor().SetSettings(pos);

    // One pair of values for rotation and position alike, applied after the
    // position settings so a settings rebuild cannot drop them. The session
    // picks between the two per connection from the receiver's source-address
    // check, so nothing here decides which one is in effect.
    m_session.SetLocalSmoothing(m_config.localSmoothing);
    m_session.SetRemoteSmoothing(m_config.remoteSmoothing);

    if (!m_config.positionEnabled) {
        m_session.SetMode(cameraunlock::TrackingMode::RotationOnly);
    }

    m_worldLockedYaw.store(m_config.worldSpaceYaw);
}

void Mod::StartUdpReceiver() {
    m_udpReceiver.SetLog([](const std::string& msg) {
        Log::Line("UDP: %s", msg.c_str());
    });

    // Validate the configured port at the config boundary. A value outside
    // the valid UDP range would be silently truncated by the uint16_t cast
    // (e.g. 70000 -> 4464), binding a different port than the user asked for
    // and than we log. Fall back to the default and say so, loudly.
    if (m_config.udpPort < MIN_UDP_PORT || m_config.udpPort > MAX_UDP_PORT) {
        Log::Line(
            "WARN: Configured UdpPort %d is out of range (%d-%d); falling back to %d",
            m_config.udpPort, MIN_UDP_PORT, MAX_UDP_PORT, DEFAULT_UDP_PORT);
        m_config.udpPort = DEFAULT_UDP_PORT;
    }
    const uint16_t udpPort = static_cast<uint16_t>(m_config.udpPort);

    // A false return means the port was busy; the receiver keeps a
    // background retry thread alive and binds once it frees up. Don't
    // bail - the hooks must stay installed so tracking resumes on its own.
    if (m_udpReceiver.Start(udpPort)) {
        Log::Line("UDP receiver listening on port %d", static_cast<int>(udpPort));
    }
}

bool Mod::Initialize() {
    if (m_initialized.load()) return true;

    LoadConfig();
    ApplyConfigToSession();
    StartUdpReceiver();

    if (!InitializeHooks()) {
        Log::Line("ERROR: Hook initialization failed");
        return false;
    }

    m_enabled.store(m_config.enableOnStartup);
    SetCameraHookEnabled(m_enabled.load());
    m_initialized.store(true);
    return true;
}

// Deliberately not gated on m_initialized: a failed Initialize() has already
// started the receiver threads, MinHook and possibly the hotkey poller, and
// those are exactly the ones that must still be torn down. Every step below is
// safe to run twice.
void Mod::Shutdown() {
    m_udpReceiver.Stop();
    ShutdownHooks();
    m_initialized.store(false);
}

bool Mod::LoadConfig() {
    std::string path = PathUtils::GetModDirectory() + "\\" + CONFIG_FILENAME;
    m_config.SaveDefaultIfMissing(path);
    return m_config.LoadFromFile(path);
}

bool Mod::InitializeHooks() {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    using cameraunlock::hooks::HookStatusToString;

    HookStatus status = HookManager::Instance().Initialize();
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: MinHook initialization failed: %s", HookStatusToString(status));
        return false;
    }

    // Before the camera hook goes live, because the detour is what re-applies
    // it: the override has to be configured by the time the first tick lands.
    ConfigureFovOverride(m_config.fovScale);

    m_cameraHookInstalled = InstallEngineCameraHook();
    if (!m_cameraHookInstalled) {
        Log::Line(
            "Head tracking is INACTIVE this session - the camera hook did not "
            "attach (see the line above). Hotkeys and the UDP receiver still run, "
            "but nothing will move the view.");
    }

    InstallCoherentReticle();

    if (!m_hotkeys.Start(m_config)) return false;

    status = HookManager::Instance().EnableAllHooks();
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: Enabling hooks failed: %s", HookStatusToString(status));
        return false;
    }
    return true;
}

void Mod::ShutdownHooks() {
    m_hotkeys.Stop();
    if (m_cameraHookInstalled) { RemoveEngineCameraHook(); m_cameraHookInstalled = false; }
    cameraunlock::hooks::HookManager::Instance().Shutdown();
}

void Mod::SetEnabled(bool enabled) {
    bool prev = m_enabled.exchange(enabled);
    if (prev != enabled) {
        SetCameraHookEnabled(enabled);
        Log::Line("Head tracking %s", enabled ? "ENABLED" : "DISABLED");
    }
}

void Mod::Toggle() { SetEnabled(!m_enabled.load()); }

void Mod::Recenter() {
    m_session.Recenter();
    Log::Line("Recentered");
}

void Mod::CycleTrackingMode() {
    cameraunlock::TrackingMode next = m_session.CycleMode();
    static const char* kNames[] = {"rotation+position", "rotation only", "position only"};
    Log::Line("Tracking mode: %s", kNames[static_cast<int>(next)]);
}

void Mod::ToggleYawMode() {
    bool next = !m_worldLockedYaw.load();
    m_worldLockedYaw.store(next);
    Log::Line("Yaw mode: %s", next ? "world-locked" : "camera-local");
}

// The session re-reads the receiver's source-address check every update, so a
// player who switches from a local OpenTrack instance to a phone on WiFi
// mid-session gets the other smoothing parameter without restarting the game.
// This only records the switch.
void Mod::LogConnectionLocality() {
    const bool isRemote = m_session.IsRemoteConnection();
    if (m_remoteConnectionKnown && isRemote == m_remoteConnection) return;

    m_remoteConnection = isRemote;
    m_remoteConnectionKnown = true;
    Log::Line("Tracker source is %s - smoothing=%.2f",
              isRemote ? "a remote device" : "on this machine",
              cameraunlock::math::GetEffectiveSmoothing(
                  m_config.localSmoothing, m_config.remoteSmoothing, isRemote));
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    if (!m_enabled.load() || !m_initialized.load()) return false;
    if (!m_session.Update(m_frameClock.Tick())) return false;
    LogConnectionLocality();
    return m_session.GetRotation(yaw, pitch, roll);
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    if (!m_enabled.load() || !m_initialized.load()) return false;
    return m_session.GetPositionOffset(x, y, z);
}

} // namespace ControlHT
