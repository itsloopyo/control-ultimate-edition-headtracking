#pragma once

#include "scripted_camera.h"

namespace ControlHT {

// How strongly head tracking applies this frame, given who currently owns the
// camera.
//
// Control hands the camera to scripted shots regularly: the establish that plays
// on every save load starts fully upside down and glides in over about forty
// seconds, and along the way it parks in front of Jesse looking back at her.
// Head tracking on top of any of that is disorienting, and while the camera is
// inverted it is worse than that - horizon-locked yaw turns about WORLD up, so
// turning your head steers the view backwards. So tracking is held at zero for
// as long as the game owns the camera, and eased back in afterwards rather than
// popping the full head offset in on one frame.
//
// A state machine over camera ownership and nothing else: no logging, no game
// memory, no clock of its own. The transitions worth reporting come back as an
// event for the caller to log, which is what keeps this testable headlessly.

enum class RampEvent {
    None,
    ScriptedShotStarted,  // the game took the camera
    CameraStateUnreadable,  // ownership cannot be determined, so tracking is held
    HandedBackToPlayer,
};

struct RampUpdate {
    float weight = 0.0f;
    RampEvent event = RampEvent::None;
};

class TrackingRamp {
public:
    RampUpdate Update(CameraOwner owner, float dt) {
        if (owner != CameraOwner::PlayerDriven) {
            RampUpdate result;
            if (!m_scripted) {
                result.event = owner == CameraOwner::Scripted
                                   ? RampEvent::ScriptedShotStarted
                                   : RampEvent::CameraStateUnreadable;
            }
            m_scripted = true;
            m_weight = 0.0f;
            return result;
        }

        RampUpdate result;
        if (m_scripted) {
            m_scripted = false;
            result.event = RampEvent::HandedBackToPlayer;
        }

        if (m_weight < 1.0f) {
            // An unusable dt (a stall, or a frame the clock could not measure)
            // completes the ramp rather than stretching it by an unknown amount.
            const bool usableDt = dt > 0.0f && dt < kMaxUsableDtSeconds;
            m_weight += usableDt ? dt / kResumeSeconds : 1.0f;
            if (m_weight > 1.0f) m_weight = 1.0f;
        }
        result.weight = m_weight;
        return result;
    }

private:
    static constexpr float kResumeSeconds = 0.35f;
    static constexpr float kMaxUsableDtSeconds = 0.5f;

    // Starts suppressed: the first thing that happens after a load is an
    // establishing shot, and ownership is not readable until the camera has
    // ticked.
    bool m_scripted = true;
    float m_weight = 0.0f;
};

} // namespace ControlHT
