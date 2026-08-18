#pragma once

namespace ControlHT {

// Control's field of view: where it comes from, and how to widen it past the
// limit the game's own settings put on it.
//
// Northlight carries the live camera FOV as a HORIZONTAL angle in radians.
// `m::PerspectiveView` stores that same horizontal angle: `getVerticalFov()`
// derives its result as `2*atan(tan(fov/2) / aspect)`, and the projection it
// builds is `m00 = 1/(tan(fov/2)*zoom)`, `m11 = aspect/(tan(fov/2)*zoom)`. The
// player camera publishes it on a flow pin of `PlayerCameraComponentState`,
// which is where `CurrentCameraFovRadians` reads it and the reticle projection
// consumes it.
//
// The value on that pin is
//
//     fov = m::PerspectiveView::scaleFOV(baseFov, lerp(FOVMultiplier, 1, blend))
//
// with `scaleFOV(f, s) = 2*atan(tan(f/2) * s)`, `FOVMultiplier` the exported
// global `rend::RenderOptions::FOVMultiplier`, and `blend` how far the game has
// taken the camera for a scripted shot - so the multiplier fades out of
// cutscenes and the authored framing survives untouched.
//
// Control does expose this to the player: Options > Graphics > FOV Scale drives
// exactly that global. What it does not expose is any value outside
// [0.75, 1.25], because the settings-apply path clamps it there. Writing the
// global ourselves is what gets past the clamp.
//
// Two consequences worth stating, because both are load-bearing:
//
// The reticle needs no separate wiring. The multiplier is applied BEFORE the
// pin is published, so the FOV the reticle projects through is already the
// widened one, on the same frame the widened view is rendered.
//
// The global is resolved by its exported name, not an RVA, so it needs no entry
// in the build profile and survives a game patch that moves everything else.

// Records the configured multiplier. `scale <= 0` leaves Control's own FOV Scale
// setting in charge and nothing is ever written.
void ConfigureFovOverride(float scale);

// Re-asserts the configured multiplier, once per camera tick. Not a one-shot
// write: the game's settings-apply path rewrites the global - clamped back into
// [0.75, 1.25] - whenever any graphics option changes, which would otherwise
// silently undo the override mid-session.
void ApplyFovOverride();

} // namespace ControlHT
