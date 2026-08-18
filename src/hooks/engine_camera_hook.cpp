#include "pch.h"
#include "engine_camera_hook.h"
#include "camera_injection.h"
#include "build_profile.h"
#include "scripted_camera.h"
#include "tracking_ramp.h"
#include "reticle_projection.h"
#include "fov_override.h"
#include "core/logging.h"
#include "core/mod.h"

#include <cameraunlock/hooks/hook_manager.h>

#include <cmath>

namespace ControlHT {

using cameraunlock::math::Quat4;
using cameraunlock::math::Vec3;

using CameraUpdateFn = void(__fastcall*)(void* state, float dt);

static CameraUpdateFn g_origCameraUpdate = nullptr;
static uintptr_t g_hookTarget = 0;

static bool g_installed = false;
static std::atomic<bool> g_enabled{false};
static std::atomic<bool> g_loggedFirstInjection{false};

// Offsets of the camera pose within the camera-manager state, taken from the
// matched build profile rather than baked in here: a patch can move a struct
// field without moving the function, and that has to be expressible as a new
// profile.
static uint32_t g_quatOffset = 0;
static uint32_t g_posOffset = 0;

// How far the composed quaternion may drift from unit length before the pose is
// rejected. Wide enough to absorb float error through the composition, narrow
// enough that a scaled or corrupted rotation cannot reach the renderer.
static constexpr float kMinPoseNormSquared = 0.9f;
static constexpr float kMaxPoseNormSquared = 1.1f;

// A pose only goes into the game if it is finite and the quaternion is close to
// unit length. Nothing upstream guarantees that - an INI with
// `YawSensitivity = nan` parses fine and poisons the whole pipeline, and
// Quat4::Normalized() does not catch NaN because its length test is a
// comparison, which NaN fails. Four NaNs in the camera quaternion would give
// the engine a NaN view matrix for the rest of the session.
static bool PoseIsSane(const Quat4& q, float px, float py, float pz) {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) ||
        !std::isfinite(q.z) || !std::isfinite(q.w)) {
        return false;
    }
    if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) return false;
    const float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return n2 > kMinPoseNormSquared && n2 < kMaxPoseNormSquared;
}

static void LogRejectedPoseOnce() {
    static std::atomic<bool> logged{false};
    if (logged.exchange(true)) return;
    Log::Line(
        "ERROR: head tracking produced a non-finite camera pose and was skipped. "
        "Check HeadTracking.ini for a malformed sensitivity, deadzone or limit "
        "value. The camera is being left alone.");
}

struct CameraPose {
    Quat4 rotation;
    float px, py, pz;
};

// Head tracking is held while Control owns the camera and eased back in
// afterwards; tracking_ramp.h has the reasoning. The ramp itself is a pure state
// machine, so the transitions it reports are logged here rather than inside it.
static TrackingRamp g_trackingRamp;

static float TrackingWeightForCamera(float dt) {
    const RampUpdate update = g_trackingRamp.Update(CurrentCameraOwner(), dt);
    switch (update.event) {
        case RampEvent::ScriptedShotStarted:
            Log::Line("Camera taken for a scripted shot - head tracking paused");
            break;
        case RampEvent::CameraStateUnreadable:
            // Never silently: an unreadable camera state suppresses tracking for
            // as long as it lasts, and without this the user sees a successful
            // install and a mod that simply never moves the view.
            Log::Line("WARN: cannot read Control's camera state, so a scripted shot "
                      "cannot be told from gameplay - head tracking is paused until it "
                      "reads again. Please report this with this log.");
            break;
        case RampEvent::HandedBackToPlayer:
            Log::Line("Camera handed back to the player - head tracking resuming");
            break;
        case RampEvent::None:
            break;
    }
    return update.weight;
}

// Composes the head pose onto the game's camera pose. Returns false when the
// result is not usable, in which case the caller leaves the game's pose alone.
static bool ApplyHeadTracking(CameraPose& pose, float dt) {
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    // Run the pipeline even when the result is about to be discarded, so the
    // interpolator and smoothing stay fed and tracking does not lurch when a
    // scripted move ends.
    if (!Mod::Instance().GetProcessedRotation(yaw, pitch, roll)) return false;
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(roll)) {
        LogRejectedPoseOnce();
        return false;
    }

    const Quat4 clean = pose.rotation;

    const float weight = TrackingWeightForCamera(dt);
    if (weight <= 0.0f) return false;
    yaw *= weight;
    pitch *= weight;
    roll *= weight;
    const bool worldYaw = Mod::Instance().GetYawMode() == YawMode::WorldLocked;
    const Quat4 tracked = ComposeForMode(clean, yaw, pitch, roll, worldYaw).Normalized();

    float px = pose.px, py = pose.py, pz = pose.pz;
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    if (Mod::Instance().GetPositionOffset(ox, oy, oz)) {
        if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz)) {
            LogRejectedPoseOnce();
            return false;
        }
        // Mapped through the CLEAN rotation, not the head-rotated one, so
        // leaning follows the body rather than the gaze. Weighted with the
        // rotation so the ease-in after a scripted move covers leaning too.
        const Vec3 offset = PositionOffsetWorld(clean, ox, oy, oz);
        px += offset.x * weight;
        py += offset.y * weight;
        pz += offset.z * weight;
    }

    if (!PoseIsSane(tracked, px, py, pz)) {
        LogRejectedPoseOnce();
        return false;
    }

    UpdateReticle(clean, tracked);

    pose.rotation = tracked;
    pose.px = px;
    pose.py = py;
    pose.pz = pz;

    if (!g_loggedFirstInjection.exchange(true)) {
        Log::Line("Head tracking is driving the camera (yaw=%.1f pitch=%.1f roll=%.1f)",
                  yaw, pitch, roll);
    }
    return true;
}

// Puts the camera-manager's pose back exactly as it was found, on every exit
// path including an exception thrown out of the game's own tick. Without this,
// one throw leaves the pose rotated, the next frame rotates that, and the view
// spins away with no way back.
class ScopedPoseRestore {
public:
    ScopedPoseRestore(float* quat, float* pos) : m_quat(quat), m_pos(pos) {
        for (int i = 0; i < 4; i++) m_savedQuat[i] = quat[i];
        for (int i = 0; i < 3; i++) m_savedPos[i] = pos[i];
    }
    ~ScopedPoseRestore() {
        for (int i = 0; i < 4; i++) m_quat[i] = m_savedQuat[i];
        for (int i = 0; i < 3; i++) m_pos[i] = m_savedPos[i];
    }
    ScopedPoseRestore(const ScopedPoseRestore&) = delete;
    ScopedPoseRestore& operator=(const ScopedPoseRestore&) = delete;

    // The pose as the game left it, which is the clean rotation everything
    // outside the render path reads.
    CameraPose SavedPose() const {
        return CameraPose{
            Quat4(m_savedQuat[0], m_savedQuat[1], m_savedQuat[2], m_savedQuat[3]),
            m_savedPos[0], m_savedPos[1], m_savedPos[2]};
    }

private:
    float* m_quat;
    float* m_pos;
    float m_savedQuat[4];
    float m_savedPos[3];
};

// Northlight is job-threaded and nothing here proves the camera tick is neither
// re-entered nor called on two threads at once. Either would be ruinous: a second
// call would snapshot the first call's ALREADY-ROTATED pose as the clean one and
// restore THAT, so the game's own camera state would come out rotated and every
// later frame would rotate it again until the view spun away - and aim, raycasts
// and movement would all read the rotated camera, which is the whole thing this
// hook exists to avoid.
//
// One atomic thread id covers both cases. Claiming it fails when any thread
// already holds it, including this one, so a nested call and a concurrent call
// both fall through to the original untouched.
static std::atomic<DWORD> g_activeThread{0};

class ScopedCameraTickClaim {
public:
    ScopedCameraTickClaim() {
        DWORD unowned = 0;
        m_claimed = g_activeThread.compare_exchange_strong(unowned, GetCurrentThreadId(),
                                                           std::memory_order_acquire,
                                                           std::memory_order_relaxed);
    }
    // Released on every exit path, including an exception thrown out of the
    // game's own tick. A bare assignment would latch the guard on and leave head
    // tracking silently dead for the rest of the session.
    ~ScopedCameraTickClaim() {
        if (m_claimed) g_activeThread.store(0, std::memory_order_release);
    }
    ScopedCameraTickClaim(const ScopedCameraTickClaim&) = delete;
    ScopedCameraTickClaim& operator=(const ScopedCameraTickClaim&) = delete;

    bool Claimed() const { return m_claimed; }

private:
    bool m_claimed;
};

// Gameplay detection comes free with this hook target.
//
// Northlight only ticks the camera manager while the player is in control. On
// the Steam build this was developed against, the title screen, the main menu,
// level loading, the opening cutscene and the pause menu each produced zero
// calls here, so head tracking cannot move the view in any of them. That is a
// measurement of those states on that build, not a guarantee about every state
// in the game: anywhere the tick does keep running, tracking stays live. The
// known example is the map, which in Control is a holographic overlay drawn
// over the live scene, and where tracking staying live is what you want.
static void __fastcall CameraUpdateDetour(void* state, float dt) {
    // Ahead of the enable check, and outside the claim: the field of view is a
    // separate setting from head tracking, so toggling tracking off must not
    // quietly hand the player back Control's own FOV.
    ApplyFovOverride();

    ScopedCameraTickClaim claim;
    if (!g_enabled.load(std::memory_order_relaxed) || state == nullptr || !claim.Claimed()) {
        g_origCameraUpdate(state, dt);
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(state);
    float* quat = reinterpret_cast<float*>(base + g_quatOffset);
    float* pos = reinterpret_cast<float*>(base + g_posOffset);

    {
        // Rotate the pose, let the tick build its camera from it, then restore.
        // The rotated value exists only for the duration of this call, so the
        // camera state the game itself reads afterwards - for aim, for
        // raycasts, for which direction walking takes you - is the one it set.
        ScopedPoseRestore restore(quat, pos);

        CameraPose pose = restore.SavedPose();
        if (ApplyHeadTracking(pose, dt)) {
            quat[0] = pose.rotation.x;
            quat[1] = pose.rotation.y;
            quat[2] = pose.rotation.z;
            quat[3] = pose.rotation.w;
            pos[0] = pose.px;
            pos[1] = pose.py;
            pos[2] = pose.pz;
        }

        g_origCameraUpdate(state, dt);
    }
}

bool InstallEngineCameraHook() {
    if (g_installed) return true;

    const BuildProfile* profile = MatchRunningBuild();
    if (!profile) {
        LogUnmatchedBuildDiagnostic();
        return false;
    }

    HMODULE exe = GetModuleHandleA(nullptr);
    const uintptr_t base = reinterpret_cast<uintptr_t>(exe);
    Log::Line("Build profile %s matched (%s @ %p)", profile->name, profile->exeName,
              reinterpret_cast<void*>(base));

    g_quatOffset = profile->cameraQuatOffset;
    g_posOffset = profile->cameraPosOffset;
    g_hookTarget = base + profile->cameraUpdateRva;

    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    using cameraunlock::hooks::HookStatusToString;

    HookStatus st = HookManager::Instance().CreateHook(
        reinterpret_cast<void*>(g_hookTarget),
        reinterpret_cast<void*>(&CameraUpdateDetour),
        reinterpret_cast<void**>(&g_origCameraUpdate));
    if (st != HookStatus::Ok) {
        Log::Line("ERROR: camera-update hook at +0x%X failed: %s - head tracking "
                  "will NOT work this session.",
                  profile->cameraUpdateRva, HookStatusToString(st));
        g_hookTarget = 0;
        return false;
    }

    Log::Line("Camera hook installed (update +0x%X, pose +0x%X/+0x%X)",
              profile->cameraUpdateRva, g_quatOffset, g_posOffset);

    if (!InstallScriptedCameraDetection(profile->playerCameraGlobalRva,
                                       profile->activeCameraCountOffset)) {
        Log::Line("WARN: scripted-shot detection is unavailable, so head tracking will stay "
                  "on during cutscenes.");
    }
    g_installed = true;
    return true;
}

void RemoveEngineCameraHook() {
    if (!g_installed) return;
    g_enabled.store(false);
    // Actually take the detour out, rather than only flipping a flag and
    // leaving the trampoline live until HookManager::Shutdown() frees it under
    // a thread that may still be inside it.
    cameraunlock::hooks::HookManager::Instance().DisableHook(
        reinterpret_cast<void*>(g_hookTarget));
    cameraunlock::hooks::HookManager::Instance().RemoveHook(
        reinterpret_cast<void*>(g_hookTarget));
    // g_origCameraUpdate is deliberately left pointing at the trampoline.
    // MinHook's thread freeze relocates instruction pointers inside the target
    // and the trampoline, not inside this detour, so a thread parked in the
    // middle of CameraUpdateDetour resumes and calls through it. Nulling it
    // turns quitting mid-tick into a crash on exit.
    g_hookTarget = 0;
    g_installed = false;
}

void SetCameraHookEnabled(bool enabled) {
    g_enabled.store(enabled);
}

} // namespace ControlHT
