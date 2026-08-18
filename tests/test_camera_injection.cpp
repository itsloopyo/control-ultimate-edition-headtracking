// Standalone unit tests for the head-tracking injection math in
// src/hooks/camera_injection.h. These functions are pure - they take a camera
// quaternion and position and return new ones - so they run headless on any
// toolchain with no game and no Windows dependency.
//
// The properties tested here are the ones the camera hook relies on:
//   1. A zero head rotation leaves the camera exactly as the game set it, so
//      a disabled or data-starved mod renders identically to vanilla.
//   2. Composition is exactly invertible, which is what lets the hook work on
//      a copy and leave the game's own camera state untouched.
//   3. The result stays a unit quaternion (no scale or skew leaks into the
//      rendered view).
//   4. Horizon-locked yaw turns about the world up axis whatever the camera is
//      doing, and camera-local yaw turns about the camera's own up axis. The
//      two differ exactly where the doctrine says they should: pointing
//      straight down, world yaw spins the view while local yaw sweeps it.
//   5. The positional offset is mapped through the CLEAN camera rotation, and
//      the core's negative-z-is-forward convention is flipped exactly once.
//   6. The engine's field of view is HORIZONTAL, and the reticle projection
//      scales with it - which is what carries a FovScale override through to
//      the crosshair without a second setting to keep in step.

#include "hooks/camera_injection.h"

#include <cmath>
#include <cstdio>

using cameraunlock::math::Quat4;
using cameraunlock::math::Vec3;

static int g_failures = 0;

static void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static bool Near(float a, float b, float eps = 1e-4f) {
    float d = a - b;
    return (d < 0 ? -d : d) <= eps;
}

static void CheckNear(float a, float b, const char* what, float eps = 1e-4f) {
    if (!Near(a, b, eps)) {
        std::printf("FAIL: %s (%.6f vs %.6f)\n", what, a, b);
        ++g_failures;
    }
}

static void CheckVecNear(const Vec3& a, const Vec3& b, const char* what,
                         float eps = 1e-4f) {
    if (!Near(a.x, b.x, eps) || !Near(a.y, b.y, eps) || !Near(a.z, b.z, eps)) {
        std::printf("FAIL: %s ((%.4f %.4f %.4f) vs (%.4f %.4f %.4f))\n", what,
                    a.x, a.y, a.z, b.x, b.y, b.z);
        ++g_failures;
    }
}

// Quaternions q and -q are the same rotation, so compare by what they do to a
// basis rather than component by component.
static void CheckSameRotation(const Quat4& a, const Quat4& b, const char* what) {
    const Vec3 axes[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
    for (const Vec3& v : axes) {
        CheckVecNear(a.Rotate(v), b.Rotate(v), what);
    }
}

// A camera that is neither identity nor axis-aligned, so a bug that only holds
// for trivial orientations cannot pass.
static Quat4 SampleCamera() {
    return Quat4::FromYawPitchRoll(37.0f, -21.0f, 11.0f).Normalized();
}

static void TestZeroRotationIsNoOp() {
    const Quat4 cam = SampleCamera();
    CheckSameRotation(ControlHT::ComposeForMode(cam, 0, 0, 0, false), cam,
                      "camera-local compose with zero angles is a no-op");
    CheckSameRotation(ControlHT::ComposeForMode(cam, 0, 0, 0, true), cam,
                      "world-yaw compose with zero angles is a no-op");
}

static void TestCameraLocalIsInvertible() {
    const Quat4 cam = SampleCamera();
    const Quat4 tracked = ControlHT::ComposeCameraLocal(cam, 17.0f, -9.0f, 4.0f);
    // Undoing the head rotation on the right restores the game's camera
    // exactly. This is the guarantee that makes injecting on a copy safe.
    const Quat4 restored = tracked * ControlHT::HeadRotation(17.0f, -9.0f, 4.0f).Inverse();
    CheckSameRotation(restored, cam, "camera-local compose is invertible");
}

static void TestStaysUnitLength() {
    const Quat4 cam = SampleCamera();
    for (int worldYaw = 0; worldYaw <= 1; worldYaw++) {
        const Quat4 q = ControlHT::ComposeForMode(cam, 25.0f, -35.0f, 15.0f, worldYaw != 0);
        const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        CheckNear(n, 1.0f, "composed rotation stays unit length");
    }
}

static void TestWorldYawTurnsAboutWorldUp() {
    // A camera looking along +Z, level. Yawing 90 degrees about world up must
    // leave the up axis pointing at world up.
    const Quat4 cam = Quat4::Identity();
    const Quat4 q = ControlHT::ComposeWorldYaw(cam, 90.0f, 0.0f, 0.0f);
    CheckVecNear(q.Rotate(Vec3(0, 1, 0)), Vec3(0, 1, 0),
                 "world yaw keeps the up axis at world up");
    const Vec3 fwd = q.Rotate(Vec3(0, 0, 1));
    CheckNear(fwd.y, 0.0f, "world yaw keeps forward level");
    Check(std::fabs(fwd.x) > 0.5f, "world yaw swings forward into X");
}

static void TestWorldYawVersusLocalYawAtThePole() {
    // Pitched onto the vertical. This is the case the doctrine calls out: head
    // yaw about world up becomes a spin about the view axis, so the forward
    // direction must NOT move. Camera-local yaw, by contrast, sweeps it.
    //
    // This is the GAME camera's orientation, built straight from the quaternion
    // helper, so it is not affected by the head-pose sign conventions applied in
    // HeadRotation: FromYawPitchRoll(0, -90, 0) points forward at +Y, straight
    // up. The property under test holds at either pole.
    const Quat4 down = Quat4::FromYawPitchRoll(0.0f, -90.0f, 0.0f).Normalized();
    const Vec3 fwdBefore = down.Rotate(Vec3(0, 0, 1));

    const Quat4 world = ControlHT::ComposeWorldYaw(down, 40.0f, 0.0f, 0.0f);
    CheckVecNear(world.Rotate(Vec3(0, 0, 1)), fwdBefore,
                 "world yaw looking down does not move the view direction");

    const Quat4 local = ControlHT::ComposeCameraLocal(down, 40.0f, 0.0f, 0.0f);
    const Vec3 localFwd = local.Rotate(Vec3(0, 0, 1));
    Check(!Near(localFwd.x, fwdBefore.x, 1e-2f) ||
          !Near(localFwd.y, fwdBefore.y, 1e-2f) ||
          !Near(localFwd.z, fwdBefore.z, 1e-2f),
          "camera-local yaw looking down DOES move the view direction");
}

static void TestPositionOffsetUsesCleanRotationAndFlipsZ() {
    // Camera yawed 90 degrees about world up: its local +X now points along
    // world -Z, local +Y is unchanged, local +Z points along world +X.
    const Quat4 cam = Quat4::FromYawPitchRoll(90.0f, 0.0f, 0.0f).Normalized();

    // x is negated at the engine boundary, so a positive processor x comes out
    // along the camera's LEFT. This is the assertion that fails if that
    // negation is ever dropped.
    CheckVecNear(ControlHT::PositionOffsetWorld(cam, 1.0f, 0.0f, 0.0f),
                 cam.Rotate(Vec3(-1, 0, 0)),
                 "positive processor x leans along the camera's left axis");

    // The processor emits NEGATIVE z for a forward lean, and the boundary
    // flips it exactly once, so -0.4 must land on the camera's +Z axis.
    CheckVecNear(ControlHT::PositionOffsetWorld(cam, 0.0f, 0.0f, -0.4f),
                 cam.Rotate(Vec3(0, 0, 0.4f)),
                 "a forward lean (negative processor z) maps to camera +Z");

    // A zero offset must be exactly zero, so position tracking that is off or
    // centred cannot nudge the camera.
    CheckVecNear(ControlHT::PositionOffsetWorld(cam, 0.0f, 0.0f, 0.0f),
                 Vec3(0, 0, 0), "zero offset moves the camera not at all");
}

// The signs every axis renders with. These are the values that were checked on
// screen, so they are the tests that fail if a sign is ever flipped by a
// refactor - the invertibility and no-op tests use HeadRotation as their own
// oracle and cancel any error inside it.
static void TestAbsoluteAxisDirections() {
    const Quat4 cam = Quat4::Identity();

    // +yaw looks right: forward swings from +Z toward +X.
    const Vec3 yawFwd = ControlHT::ComposeCameraLocal(cam, 90.0f, 0.0f, 0.0f)
                            .Rotate(Vec3(0, 0, 1));
    CheckVecNear(yawFwd, Vec3(1, 0, 0), "+yaw 90 turns forward to +X (looks right)");

    // +pitch looks UP: forward rises to +Y. Pitch is negated at the engine
    // boundary in HeadRotation, so this is the assertion that fails if that
    // negation is ever dropped.
    const Vec3 pitchFwd = ControlHT::ComposeCameraLocal(cam, 0.0f, 30.0f, 0.0f)
                              .Rotate(Vec3(0, 0, 1));
    CheckVecNear(pitchFwd, Vec3(0.0f, 0.5f, 0.86603f),
                 "+pitch 30 raises forward to +Y (looks up)");

    // +roll tilts the up axis toward -X, the mirror of the pre-inversion sign.
    const Vec3 rollUp = ControlHT::ComposeCameraLocal(cam, 0.0f, 0.0f, 20.0f)
                            .Rotate(Vec3(0, 1, 0));
    CheckVecNear(rollUp, Vec3(-0.34202f, 0.93969f, 0.0f),
                 "+roll 20 tilts up toward -X");
}

// For a level, unrolled camera the two yaw modes must agree exactly: world up
// and the camera's own up are the same axis, so there is nothing to decompose.
// This one assertion pins the world-yaw composition - it fails if pitch or roll
// is applied about a world axis instead of the camera's, which no other test
// here can see.
static void TestYawModesAgreeOnALevelCamera() {
    const float kCameraYaws[] = {0.0f, 55.0f, -130.0f};
    for (float camYaw : kCameraYaws) {
        const Quat4 cam = Quat4::FromYawPitchRoll(camYaw, 0.0f, 0.0f).Normalized();
        CheckSameRotation(ControlHT::ComposeWorldYaw(cam, 25.0f, -15.0f, 10.0f),
                          ControlHT::ComposeCameraLocal(cam, 25.0f, -15.0f, 10.0f),
                          "yaw modes agree on a level camera");
    }

    // And must NOT agree once the camera is pitched, or world yaw is a no-op
    // dressed up as a mode.
    const Quat4 pitched = Quat4::FromYawPitchRoll(55.0f, -30.0f, 0.0f).Normalized();
    const Vec3 worldFwd = ControlHT::ComposeWorldYaw(pitched, 25.0f, -15.0f, 10.0f)
                              .Rotate(Vec3(0, 0, 1));
    const Vec3 localFwd = ControlHT::ComposeCameraLocal(pitched, 25.0f, -15.0f, 10.0f)
                              .Rotate(Vec3(0, 0, 1));
    Check(!Near(worldFwd.x, localFwd.x, 1e-2f) ||
          !Near(worldFwd.y, localFwd.y, 1e-2f) ||
          !Near(worldFwd.z, localFwd.z, 1e-2f),
          "yaw modes differ once the camera is pitched");
}

// A lean must not follow the camera's pitch. With the camera pitched down, a
// forward lean has to travel along the ground, not into it.
static void TestPositionOffsetIsHorizonLocked() {
    const Quat4 pitchedDown = Quat4::FromYawPitchRoll(0.0f, 60.0f, 0.0f).Normalized();

    // Full forward lean: processor z is negative for forward.
    const Vec3 fwdLean = ControlHT::PositionOffsetWorld(pitchedDown, 0.0f, 0.0f, -0.40f);
    CheckNear(fwdLean.y, 0.0f, "a forward lean does not move the camera vertically");
    CheckNear(fwdLean.z, 0.40f, "a forward lean travels the full distance along the ground");

    // Upward lean stays vertical.
    const Vec3 upLean = ControlHT::PositionOffsetWorld(pitchedDown, 0.0f, 0.20f, 0.0f);
    CheckVecNear(upLean, Vec3(0.0f, 0.20f, 0.0f), "an upward lean stays vertical");

    // Straight down the camera has no heading left to flatten; the fallback
    // must still produce a horizontal, finite lean rather than zero or NaN.
    const Quat4 straightDown = Quat4::FromYawPitchRoll(0.0f, 90.0f, 0.0f).Normalized();
    const Vec3 poleLean = ControlHT::PositionOffsetWorld(straightDown, 0.0f, 0.0f, -0.40f);
    CheckNear(poleLean.y, 0.0f, "leaning forward while looking straight down stays level");
    CheckNear(std::sqrt(poleLean.x * poleLean.x + poleLean.z * poleLean.z), 0.40f,
              "leaning forward while looking straight down keeps its length");
}

// The reticle must mark where shots land. These are the litmus tests AGENTS.md
// specifies, and they exist because a projection that agrees at small single-axis
// angles can still drift badly on combined poses.
static void TestAimProjection() {
    const float tanH = std::tan(35.0f * 3.14159265f / 180.0f);
    const float tanV = std::tan(20.0f * 3.14159265f / 180.0f);
    const Quat4 level = Quat4::FromYawPitchRoll(0.0f, 0.0f, 0.0f).Normalized();
    float x = 0.0f, y = 0.0f;

    // Head centred: the aim point is dead centre.
    Check(ControlHT::ComputeAimNdc(level, level, tanH, tanV, x, y), "centred aim projects");
    CheckNear(x, 0.0f, "centred aim is horizontally centred");
    CheckNear(y, 0.0f, "centred aim is vertically centred");

    // Turning the head right moves the view right, so the aim point the player is
    // still shooting at appears to the LEFT of centre.
    Quat4 tracked = ControlHT::ComposeForMode(level, 20.0f, 0.0f, 0.0f, false).Normalized();
    Check(ControlHT::ComputeAimNdc(level, tracked, tanH, tanV, x, y), "yawed aim projects");
    Check(x < -0.01f, "a rightward head turn puts the aim point left of centre");
    CheckNear(y, 0.0f, "a pure yaw does not move the aim point vertically");

    // Pure roll: the aim direction is unchanged, so the reticle stays centred.
    tracked = ControlHT::ComposeForMode(level, 0.0f, 0.0f, 30.0f, false).Normalized();
    Check(ControlHT::ComputeAimNdc(level, tracked, tanH, tanV, x, y), "rolled aim projects");
    CheckNear(x, 0.0f, "pure roll leaves the aim point horizontally centred");
    CheckNear(y, 0.0f, "pure roll leaves the aim point vertically centred");

    // Pure pitch moves it purely vertically.
    tracked = ControlHT::ComposeForMode(level, 0.0f, 15.0f, 0.0f, false).Normalized();
    Check(ControlHT::ComputeAimNdc(level, tracked, tanH, tanV, x, y), "pitched aim projects");
    CheckNear(x, 0.0f, "a pure pitch does not move the aim point horizontally");
    Check(std::fabs(y) > 0.01f, "a pure pitch moves the aim point vertically");

    // Looking far enough away that the aim is behind the view has no honest
    // screen position, and must be reported rather than clamped to an edge.
    tracked = ControlHT::ComposeForMode(level, 120.0f, 0.0f, 0.0f, false).Normalized();
    Check(!ControlHT::ComputeAimNdc(level, tracked, tanH, tanV, x, y),
          "an aim point behind the tracked view is rejected");
}

static void TestFovTangents() {
    constexpr float kDeg = 3.14159265f / 180.0f;
    float tanH = 0.0f, tanV = 0.0f;

    // Control's default: 70 degrees HORIZONTAL. The vertical angle is the
    // derived one, exactly as m::PerspectiveView::getVerticalFov() derives it.
    Check(ControlHT::FovTangents(70.0f * kDeg, 16.0f / 9.0f, tanH, tanV),
          "a sane fov and aspect produce tangents");
    CheckNear(tanH, std::tan(35.0f * kDeg), "the stored angle spans the width");
    CheckNear(2.0f * std::atan(tanV) / kDeg, 43.0f,
              "70 degrees horizontal is 43 vertical at 16:9", 0.05f);

    // A taller viewport keeps the horizontal angle and narrows the vertical one,
    // which is what makes reading the stored angle as vertical wrong by exactly
    // the aspect ratio rather than by something that shows up at 1:1.
    float tanH43 = 0.0f, tanV43 = 0.0f;
    Check(ControlHT::FovTangents(70.0f * kDeg, 4.0f / 3.0f, tanH43, tanV43),
          "4:3 produces tangents");
    CheckNear(tanH43, tanH, "the horizontal angle does not depend on the aspect");
    Check(tanV43 > tanV, "a taller viewport has a taller frustum");

    Check(!ControlHT::FovTangents(0.0f, 16.0f / 9.0f, tanH, tanV), "a zero fov is rejected");
    Check(!ControlHT::FovTangents(3.2f, 16.0f / 9.0f, tanH, tanV),
          "a fov of half a turn or more is rejected");
    Check(!ControlHT::FovTangents(70.0f * kDeg, 0.0f, tanH, tanV), "a zero aspect is rejected");
}

// A wider field of view spreads the same world angle over fewer pixels, so the
// aim point sits closer to the centre. This is what makes the FovScale override
// reach the reticle: the projection reads the live fov every frame, so widening
// the view shortens the reticle's travel with no second setting to keep in sync.
static void TestAimProjectionTracksFov() {
    constexpr float kDeg = 3.14159265f / 180.0f;
    const Quat4 level = Quat4::FromYawPitchRoll(0.0f, 0.0f, 0.0f).Normalized();
    const Quat4 tracked = ControlHT::ComposeForMode(level, 20.0f, 0.0f, 0.0f, false).Normalized();

    float narrowH = 0.0f, narrowV = 0.0f, wideH = 0.0f, wideV = 0.0f;
    Check(ControlHT::FovTangents(70.0f * kDeg, 16.0f / 9.0f, narrowH, narrowV), "70 deg fov");
    // 70 degrees taken through scaleFOV at 1.5, the way the engine widens it.
    const float widened = 2.0f * std::atan(std::tan(35.0f * kDeg) * 1.5f);
    Check(ControlHT::FovTangents(widened, 16.0f / 9.0f, wideH, wideV), "widened fov");

    float narrowX = 0.0f, narrowY = 0.0f, wideX = 0.0f, wideY = 0.0f;
    Check(ControlHT::ComputeAimNdc(level, tracked, narrowH, narrowV, narrowX, narrowY),
          "narrow projection");
    Check(ControlHT::ComputeAimNdc(level, tracked, wideH, wideV, wideX, wideY),
          "wide projection");
    Check(std::fabs(wideX) < std::fabs(narrowX),
          "widening the field of view moves the aim point closer to the centre");

    // And the offset is exactly the world angle measured against the frustum
    // half-width, which is the relation the engine's projection encodes.
    CheckNear(narrowX, -std::tan(20.0f * kDeg) / narrowH,
              "the offset is the aim tangent over the frustum half-width");
    CheckNear(wideX, -std::tan(20.0f * kDeg) / wideH,
              "and the same relation holds at the widened field of view");
}

int main() {
    TestZeroRotationIsNoOp();
    TestCameraLocalIsInvertible();
    TestStaysUnitLength();
    TestWorldYawTurnsAboutWorldUp();
    TestAimProjection();
    TestFovTangents();
    TestAimProjectionTracksFov();
    TestWorldYawVersusLocalYawAtThePole();
    TestPositionOffsetUsesCleanRotationAndFlipsZ();
    TestAbsoluteAxisDirections();
    TestYawModesAgreeOnALevelCamera();
    TestPositionOffsetIsHorizonLocked();

    if (g_failures == 0) {
        std::printf("All camera_injection tests passed.\n");
        return 0;
    }
    std::printf("%d camera_injection test(s) FAILED.\n", g_failures);
    return 1;
}
