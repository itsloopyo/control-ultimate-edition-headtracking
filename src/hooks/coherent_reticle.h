#pragma once

#include <cstdint>

namespace ControlHT {

// Moves Control's own crosshair so it keeps marking where shots land.
//
// Head tracking moves what you see, not where you aim, so at any appreciable head
// angle a crosshair fixed at screen centre stops marking the shot. Control's HUD is
// Coherent GT - an HTML/CSS/JS layer - and its crosshair has no position field on
// the C++ side (CrosshairData binds only size, hidden, type and mode), so it is
// centred by the page's own CSS. What can move it is the UI layer itself.
//
// `Coherent::UIGT::View::ExecuteScript` at coherentuigt.dll + 0xDBD00 takes a plain
// C string. Hooking it captures live View pointers, because Control runs bootstrap
// scripts on every View as it is created, and it is also how a shim is injected.
//
// Offsets are pushed on Coherent's own thread, from its per-frame animation service
// at + 0xD9A90. That is not a stylistic choice: the camera tick that computes them
// runs on a job pool, a different thread almost every tick, and View methods are
// thread-affine, so pushing from there would eventually crash the game.
//
// The game's crosshair keeps its own art, weapon changes and context-sensitive
// hiding, which is the whole reason for moving it rather than drawing our own.

bool InstallCoherentReticle();

// Where the reticle should sit, in normalised device coordinates (+x right, +y up,
// centre is 0,0), pushed from the camera tick. Normalised rather than pixels because
// the HUD page has its own coordinate space, which need not match the render
// resolution. Applied to Control's own crosshair on Coherent's thread. `visible`
// is false when the aim point is behind the head-tracked view, where the crosshair
// is hidden rather than pinned to an edge that would imply a target there.
void SetReticleOffset(bool visible, float ndcX, float ndcY);

} // namespace ControlHT
