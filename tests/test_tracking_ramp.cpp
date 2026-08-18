// Characterization tests for the scripted-shot suppression ramp in
// src/hooks/tracking_ramp.h. The ramp decides how strongly head tracking applies
// on a given frame from who owns the camera, and nothing else - no game memory,
// no logging, no clock of its own - so it runs headless.
//
// The behaviour pinned here is what the camera hook depends on:
//   1. Tracking starts suppressed, because the first thing after a load is an
//      establishing shot and ownership is not readable until the camera ticks.
//   2. While the game owns the camera the weight is exactly zero, so the pose
//      the renderer gets is the one the game authored.
//   3. Handing the camera back eases tracking in over the resume window rather
//      than popping the full head offset in on one frame.
//   4. Each transition is reported exactly once, so the log records a scripted
//      shot starting rather than repeating it every frame.
//   5. A dt the frame clock could not measure completes the ramp instead of
//      stretching it by an unknown amount.

#include "hooks/tracking_ramp.h"

#include <cstdio>

using ControlHT::CameraOwner;
using ControlHT::RampEvent;
using ControlHT::RampUpdate;
using ControlHT::TrackingRamp;

static int g_failures = 0;

static void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static void CheckNear(float a, float b, const char* what, float eps = 1e-4f) {
    const float d = a - b;
    if ((d < 0 ? -d : d) > eps) {
        std::printf("FAIL: %s (%.6f vs %.6f)\n", what, a, b);
        ++g_failures;
    }
}

// One frame at 60Hz, comfortably inside the resume window.
static constexpr float kFrameDt = 1.0f / 60.0f;

static void TestStartsSuppressed() {
    TrackingRamp ramp;
    // Before the camera has ticked, ownership reads Unknown and nothing moves.
    const RampUpdate first = ramp.Update(CameraOwner::Unknown, kFrameDt);
    CheckNear(first.weight, 0.0f, "an unreadable camera applies no head tracking");
    Check(first.event == RampEvent::None,
          "starting out suppressed is not reported as a transition");
}

static void TestScriptedShotHoldsAtZeroAndReportsOnce() {
    TrackingRamp ramp;
    // Get to the player-driven state first so the shot is a real transition.
    ramp.Update(CameraOwner::PlayerDriven, 1.0f);

    const RampUpdate taken = ramp.Update(CameraOwner::Scripted, kFrameDt);
    CheckNear(taken.weight, 0.0f, "a scripted shot applies no head tracking");
    Check(taken.event == RampEvent::ScriptedShotStarted, "the shot starting is reported");

    for (int frame = 0; frame < 10; frame++) {
        const RampUpdate held = ramp.Update(CameraOwner::Scripted, kFrameDt);
        CheckNear(held.weight, 0.0f, "tracking stays at zero for the whole shot");
        Check(held.event == RampEvent::None, "the shot is reported once, not every frame");
    }
}

static void TestUnreadableStateIsReportedSeparately() {
    TrackingRamp ramp;
    ramp.Update(CameraOwner::PlayerDriven, 1.0f);

    const RampUpdate lost = ramp.Update(CameraOwner::Unknown, kFrameDt);
    CheckNear(lost.weight, 0.0f, "an unreadable camera suppresses tracking");
    Check(lost.event == RampEvent::CameraStateUnreadable,
          "an unreadable camera is reported as itself, not as a scripted shot");
}

static void TestResumeEasesInAndSaturates() {
    TrackingRamp ramp;
    ramp.Update(CameraOwner::Scripted, kFrameDt);

    const RampUpdate handback = ramp.Update(CameraOwner::PlayerDriven, kFrameDt);
    Check(handback.event == RampEvent::HandedBackToPlayer, "the hand-back is reported");
    Check(handback.weight > 0.0f && handback.weight < 1.0f,
          "tracking eases in rather than popping to full on the first frame");

    float previous = handback.weight;
    for (int frame = 0; frame < 20; frame++) {
        const RampUpdate step = ramp.Update(CameraOwner::PlayerDriven, kFrameDt);
        Check(step.weight >= previous, "the ramp only ever climbs");
        Check(step.event == RampEvent::None, "the hand-back is reported once");
        previous = step.weight;
    }
    CheckNear(previous, 1.0f, "the ramp reaches full weight and stops there");
}

static void TestUnusableDtCompletesTheRamp() {
    TrackingRamp ramp;
    // A stall long enough that the frame clock cannot be trusted.
    const RampUpdate stalled = ramp.Update(CameraOwner::PlayerDriven, 5.0f);
    CheckNear(stalled.weight, 1.0f, "an unusable dt completes the ramp in one frame");

    TrackingRamp zeroDt;
    CheckNear(zeroDt.Update(CameraOwner::PlayerDriven, 0.0f).weight, 1.0f,
              "a zero dt completes the ramp rather than stalling it at zero");
}

static void TestSecondShotRestartsTheRamp() {
    TrackingRamp ramp;
    ramp.Update(CameraOwner::PlayerDriven, 1.0f);
    CheckNear(ramp.Update(CameraOwner::PlayerDriven, kFrameDt).weight, 1.0f,
              "tracking is at full weight before the second shot");

    ramp.Update(CameraOwner::Scripted, kFrameDt);
    const RampUpdate resumed = ramp.Update(CameraOwner::PlayerDriven, kFrameDt);
    Check(resumed.weight < 1.0f, "the ramp restarts after a second scripted shot");
}

int main() {
    TestStartsSuppressed();
    TestScriptedShotHoldsAtZeroAndReportsOnce();
    TestUnreadableStateIsReportedSeparately();
    TestResumeEasesInAndSaturates();
    TestUnusableDtCompletesTheRamp();
    TestSecondShotRestartsTheRamp();

    if (g_failures == 0) {
        std::printf("All tracking_ramp tests passed.\n");
        return 0;
    }
    std::printf("%d tracking_ramp test(s) FAILED.\n", g_failures);
    return 1;
}
