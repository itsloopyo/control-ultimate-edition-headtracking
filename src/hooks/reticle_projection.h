#pragma once

#include <cameraunlock/math/quat4.h>

namespace ControlHT {

// Works out where the clean aim direction lands on screen once the head has
// rotated the view away from it, and hands that position to the crosshair.
//
// Shots go where the GAME's camera points - the clean, mouse-controlled
// rotation - so the crosshair has to be drawn wherever that direction projects
// into the head-tracked view, or it stops marking where shots land. The
// projection reads Control's live field of view and the window's client area
// every frame, so an FOV override or a resolution change carries through with
// no second setting to keep in step.
//
// Computed on whichever job thread the camera tick landed on; the value is
// applied to Control's own crosshair later, on Coherent's thread.
void UpdateReticle(const cameraunlock::math::Quat4& cleanRotation,
                   const cameraunlock::math::Quat4& trackedRotation);

} // namespace ControlHT
