#pragma once

#include "config.h"
#include "hotkeys.h"

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/time/frame_clock.h>
#include <cameraunlock/tracking/head_tracking_session.h>

namespace ControlHT {

enum class YawMode { CameraLocal, WorldLocked };

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    void SetEnabled(bool enabled);
    void Toggle();

    void Recenter();
    void CycleTrackingMode();
    void ToggleYawMode();

    YawMode GetYawMode() const {
        return m_worldLockedYaw.load() ? YawMode::WorldLocked : YawMode::CameraLocal;
    }

    // Per-frame, called by the camera hook: runs the tracking pipeline
    // (receiver -> interpolator -> processor) for this frame and returns the
    // processed YPR in degrees. False = no valid data (hold last pose /
    // skip injection).
    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);

    // 6DOF positional offset (meters) computed by the same frame's pipeline
    // run; call after GetProcessedRotation. False = position disabled or no
    // data.
    bool GetPositionOffset(float& x, float& y, float& z);

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();
    void ApplyConfigToSession();
    void StartUdpReceiver();
    bool InitializeHooks();
    void ShutdownHooks();

    // The session itself re-reads the receiver's connection locality each
    // update and points both processors at LocalSmoothing or RemoteSmoothing.
    // This only reports the switch, so a bug report can say which of the two
    // values was actually in effect.
    void LogConnectionLocality();

    // Frame dt is clamped to this ceiling so a stall (alt-tab, load hitch)
    // cannot feed a huge dt into the smoothing/extrapolation math.
    static constexpr float kMaxFrameDtSeconds = 0.25f;

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;
    // Without IsRemoteConnection() on the receiver the session silently falls
    // back to LocalSmoothing forever, with nothing at the call site to show it.
    static_assert(Session::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() to select Local/RemoteSmoothing");
    Session m_session{m_udpReceiver};
    cameraunlock::time::FrameClock m_frameClock{kMaxFrameDtSeconds};

    bool m_remoteConnection = false;
    bool m_remoteConnectionKnown = false;

    Hotkeys m_hotkeys;

    std::atomic<bool> m_worldLockedYaw{true};

    bool m_cameraHookInstalled = false;
};

} // namespace ControlHT
