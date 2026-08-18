#pragma once

#include <cameraunlock/math/quat4.h>
#include <cameraunlock/math/vec3.h>

#include <cmath>

namespace ControlHT {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

// Head-tracking injection into Control's camera pose.
//
// Northlight publishes the finished camera to its flow graph as 32 bytes: a
// unit quaternion (x, y, z, w) followed by a world position (x, y, z, pad).
// Both were recovered at runtime - the quaternion by its unit length, the
// offsets by watching which floats change as the camera moves - and confirmed
// against coregame's camera-manager tick, which reads exactly those fields to
// build the view matrix.
//
// Everything here is a pure function of that pose so it is unit-testable with
// no game running.

// Control is Y-up: a horizontal look direction has a near-zero Y component,
// which is what the captured camera directions show.
inline cameraunlock::math::Vec3 WorldUp() {
    return cameraunlock::math::Vec3(0.0f, 1.0f, 0.0f);
}

// Quaternion for a rotation of angleDeg about a world-space axis.
inline cameraunlock::math::Quat4 AxisAngle(const cameraunlock::math::Vec3& axis,
                                           float angleDeg) {
    float half = angleDeg * kDegToRad * 0.5f;
    float s = std::sin(half);
    cameraunlock::math::Vec3 a = axis.Normalized();
    return cameraunlock::math::Quat4(a.x * s, a.y * s, a.z * s, std::cos(half));
}

// Head rotation in the camera's own frame.
//
// Pitch and roll are both negated relative to the processor's output, at this
// boundary rather than through the INI's `InvertPitch` / `InvertRoll`. That is
// the distinction the doctrine draws: `Invert*` exists for a tracker whose axis
// runs the other way and is the user's to set, while a mismatch between the
// core's convention and the ENGINE's is fixed once, here, so the shipped
// defaults stay `false` and the toggles remain available to anyone whose
// tracker disagrees.
//
// Neither sign can be inherited from the rest of the catalogue. The Unity mods
// spell their composition `Euler(pitch, yaw, -roll)`, but they multiply it onto
// a VIEW matrix - the inverse of the camera rotation, conjugated by Unity's
// z-flip - and that conjugation flips roll a second time. Applying the same
// expression straight onto an engine camera quaternion, which is what happens
// here, does not reproduce their behaviour. These are the signs that look right
// on screen in Control.
inline cameraunlock::math::Quat4 HeadRotation(float yawDeg, float pitchDeg,
                                              float rollDeg) {
    return cameraunlock::math::Quat4::FromYawPitchRoll(yawDeg, -pitchDeg, rollDeg);
}

// Camera-local yaw: the whole head rotation is composed in the camera's frame,
// so yawing while looking down spins the view about the view axis.
inline cameraunlock::math::Quat4 ComposeCameraLocal(
    const cameraunlock::math::Quat4& cam, float yawDeg, float pitchDeg, float rollDeg) {
    return cam * HeadRotation(yawDeg, pitchDeg, rollDeg);
}

// Horizon-locked yaw: yaw turns about the WORLD up axis, then pitch and roll
// are applied in the camera's own frame. Keeps "up" constant regardless of
// where the game camera is pointing, which is what stops the view tilting at
// extreme angles. Mirrors ViewMatrixModifier.ApplyHeadRotationDecomposed.
inline cameraunlock::math::Quat4 ComposeWorldYaw(
    const cameraunlock::math::Quat4& cam, float yawDeg, float pitchDeg, float rollDeg) {
    using cameraunlock::math::Quat4;
    Quat4 yawed = AxisAngle(WorldUp(), yawDeg) * cam;
    return yawed * HeadRotation(0.0f, pitchDeg, rollDeg);
}

// Half-angle tangents of the rendered frustum, from the field of view Control
// publishes and the render surface's aspect ratio.
//
// The engine's FOV is HORIZONTAL. `m::PerspectiveView::getVerticalFov()` is
// `2*atan(tan(fov/2) / aspect)`, and the projection it builds is
// `m00 = 1/(tan(fov/2)*zoom)`, `m11 = aspect/(tan(fov/2)*zoom)` - so the stored
// angle spans the width and the height is the derived one. Reading it as
// vertical instead leaves every offset short by exactly the aspect ratio, which
// on 16:9 is a reticle that trails the target by 44% of the distance it should
// have moved and so looks dragged along with the head.
//
// False for a field of view or an aspect that cannot describe a viewport.
inline bool FovTangents(float fovRadians, float aspect, float& tanHalfH, float& tanHalfV) {
    if (!(fovRadians > 0.0f) || !(fovRadians < kPi)) return false;
    if (!(aspect > 0.0f)) return false;
    tanHalfH = std::tan(fovRadians * 0.5f);
    tanHalfV = tanHalfH / aspect;
    return true;
}

// Where the clean aim direction lands on screen, once the head has rotated the
// view away from it.
//
// Shots go where the GAME's camera points, which is the clean, mouse-controlled
// rotation - confirmed in play. The head only moves what is rendered. So the
// reticle has to be drawn wherever that clean direction projects into the
// head-tracked view, or it stops marking where shots land.
//
// This is derived from the composition the camera hook actually applies rather
// than from per-axis angle formulas: the relative rotation trackedView^-1 *
// cleanView is rotated onto the camera's forward axis and perspective-divided.
// Doing it with quaternions keeps combined yaw+pitch+roll exact and, more
// importantly, is automatically right for BOTH yaw modes - a per-axis tangent
// formula agrees at small single-axis angles and then drifts on combined poses,
// which is the trap AGENTS.md documents.
//
// Returns false when the aim direction is behind the tracked view (an extreme
// head turn), where there is no honest screen position for it.
//
// NDC is +x right, +y up, both in [-1, 1] before clamping.
inline bool ComputeAimNdc(const cameraunlock::math::Quat4& clean,
                          const cameraunlock::math::Quat4& tracked,
                          float tanHalfFovH, float tanHalfFovV, float& ndcX, float& ndcY) {
    using cameraunlock::math::Quat4;
    using cameraunlock::math::Vec3;
    if (!(tanHalfFovH > 0.0f) || !(tanHalfFovV > 0.0f)) return false;

    // Control's camera looks along its local +Z, with +Y up.
    const Quat4 relative = tracked.Inverse() * clean;
    const Vec3 aim = relative.Rotate(Vec3(0.0f, 0.0f, 1.0f));
    if (aim.z <= 0.01f) return false;

    ndcX = (aim.x / aim.z) / tanHalfFovH;
    ndcY = (aim.y / aim.z) / tanHalfFovV;

    // Pin to the viewport edge rather than letting it accelerate away: as the aim
    // approaches 90 degrees off-axis the divide runs to infinity.
    if (ndcX > 1.0f) ndcX = 1.0f;
    if (ndcX < -1.0f) ndcX = -1.0f;
    if (ndcY > 1.0f) ndcY = 1.0f;
    if (ndcY < -1.0f) ndcY = -1.0f;
    return true;
}

inline cameraunlock::math::Quat4 ComposeForMode(
    const cameraunlock::math::Quat4& cam, float yawDeg, float pitchDeg,
    float rollDeg, bool worldSpaceYaw) {
    return worldSpaceYaw ? ComposeWorldYaw(cam, yawDeg, pitchDeg, rollDeg)
                         : ComposeCameraLocal(cam, yawDeg, pitchDeg, rollDeg);
}

// Positional offset, expressed in the player's frame and mapped to world space
// against a HORIZON-LOCKED basis built from the CLEAN camera rotation.
//
// Two separate decisions, both load-bearing:
//
// Clean rather than head-rotated, so leaning is independent of where the head
// is pointing: lean left always moves left relative to the body, not relative
// to the gaze.
//
// Horizon-locked rather than the camera's full rotation, because Control's
// camera pitches a long way. Mapping through the raw rotation would tilt the
// lean axes with the camera: with the camera pitched 60 degrees down, a full
// forward lean would drive it 35 cm into the floor while moving only 20 cm
// forward, and an upward lean would push it forwards. The basis here is the
// camera's heading flattened onto the horizon plus world up, so leaning
// forward always means forward along the ground and leaning up always means up.
//
// The processor's convention is x = right, y = up, z = NEGATIVE forward (see
// AGENTS.md "Position Tracking"). Both x and z are negated once here, at the
// boundary where the core's convention meets the engine's, which is the only
// correct place for either flip. Doing z with `invert_z` would be an outright
// bug - inversion is applied BEFORE the clamp, so it would move the generous
// 0.40 m forward limit onto the backward lean and leave 0.10 m for leaning in.
// x has a symmetric limit so the same mistake would not show up in the travel,
// but it belongs here for the same reason: it is an engine-convention flip, not
// a property of the user's tracker.
inline cameraunlock::math::Vec3 PositionOffsetWorld(
    const cameraunlock::math::Quat4& cleanCam, float x, float y, float z) {
    using cameraunlock::math::Vec3;

    const Vec3 up = WorldUp();
    const Vec3 forward = cleanCam.Rotate(Vec3(0.0f, 0.0f, 1.0f));

    // Flatten the heading onto the horizon. Looking straight up or down leaves
    // nothing to flatten, so fall back to the camera's own up axis, which is
    // horizontal in exactly that case and carries the same heading.
    Vec3 flat = forward - up * Vec3::Dot(forward, up);
    if (flat.SqrMagnitude() < 1e-6f) {
        const Vec3 camUp = cleanCam.Rotate(Vec3(0.0f, 1.0f, 0.0f));
        flat = camUp - up * Vec3::Dot(camUp, up);
        if (flat.SqrMagnitude() < 1e-6f) return Vec3::Zero();
    }
    const Vec3 flatForward = flat.Normalized();
    const Vec3 right = Vec3::Cross(up, flatForward).Normalized();

    return right * (-x) + up * y + flatForward * (-z);
}

} // namespace ControlHT
