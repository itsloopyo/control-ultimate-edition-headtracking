#pragma once

#include <cameraunlock/memory/pe_fingerprint.h>

#include <cstdint>

namespace ControlHT {

// Append-only build-profile registry (AGENTS.md "Maintain compatibility
// across new patches"). A patch shifts RVAs; users update on their own
// schedule. Each profile pins the camera hook targets to one shipped build,
// identified by the PE fingerprint of the game EXE that hosts them. At load we
// fingerprint the running EXE and route to the matching profile; no match
// leaves the mod dormant.
//
// NEVER edit an existing profile's RVAs in place and NEVER delete old
// profiles - that strands users who have not taken the patch. Add a new
// profile to the TOP of kKnownProfiles (the diagnostic primary) when a
// patch breaks the current build.

struct BuildProfile {
    const char* name;      // "steam-win64-dx12-YYYYMMDD"
    const char* exeName;   // Control_DX12.exe / Control_DX11.exe
    cameraunlock::memory::PeFingerprint fingerprint;

    // The camera-manager tick, inside the game EXE. It reads the manager's own
    // pose, adds camera shake, and builds the final look-at the renderer uses.
    // Head tracking is injected by rotating that pose for the duration of this
    // call only. Zero means "this build is identified but not yet worked out",
    // which keeps the mod dormant - see IsProfileComplete().
    uint32_t cameraUpdateRva;

    // Where the pose lives inside the camera-manager state: a unit quaternion
    // (x, y, z, w) followed by a world position (x, y, z). Pinned per build
    // alongside the RVA, because a patch can move a struct field without moving
    // the function that reads it.
    uint32_t cameraQuatOffset;
    uint32_t cameraPosOffset;

    // Global holding the `PlayerCameraComponentState` pointer. The player camera
    // update reads it, writes the final pose into it, and then calls the camera
    // tick above with its +0x4D0 - which is the very state our hook receives, so
    // the two are tied together. Reading it is how the mod tells a scripted shot
    // from gameplay. Derived per binary: the two executables do NOT share this
    // address, even though they share the tick's RVA.
    uint32_t playerCameraGlobalRva;

    // Offset into `PlayerCameraComponentState` of the COUNT of camera entities
    // the player camera is currently blending toward. FUN_140516BD0 loops
    // `while (i != *(uint*)(this + 0x538))`, resolving each entry's entity handle
    // and fetching its `coregame::CameraComponentState`, so a non-zero count means
    // a camera other than the player's own owns the view. Verified at the same
    // offset in both executables by decompiling each.
    uint32_t activeCameraCountOffset;
};

// A profile whose hook target is not yet worked out identifies the build but
// cannot drive it. Matching one must leave the mod dormant rather than hook at
// the image base - which matters because `pixi run check-fingerprint` prints
// exactly that shape for a newly patched build, so an incomplete profile is a
// normal intermediate state rather than a hypothetical one.
bool IsProfileComplete(const BuildProfile& profile);

// Finds the profile whose exeName is the running EXE and whose fingerprint
// matches. Returns nullptr if no known build matches (mod stays dormant).
const BuildProfile* MatchRunningBuild();

// Logs, for an unmatched build, whether the running EXE is newer or older than
// the diagnostic-primary profile (top of the registry), so the user knows
// whether to update the mod or let the store finish updating.
void LogUnmatchedBuildDiagnostic();

} // namespace ControlHT
