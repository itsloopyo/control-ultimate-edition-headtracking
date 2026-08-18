#include "pch.h"
#include "build_profile.h"
#include "core/logging.h"

namespace ControlHT {

using cameraunlock::memory::PeFingerprint;
using cameraunlock::memory::FingerprintMismatch;

// ---- profile registry (append-only; newest first) -------------------------

// Steam build, EXEs dated 2025-03-24, fingerprinted 2026-08-17.
static const BuildProfile kSteamDx12_20250324 = {
    "steam-win64-dx12-20250324",
    "Control_DX12.exe",
    {
        0x67E06BF2,  // TimeDateStamp
        0x13EA000,   // SizeOfImage
        0x01392285,  // CheckSum
    },
    0x24C310,  // cameraUpdateRva
    0x40,      // cameraQuatOffset
    0x50,      // cameraPosOffset
    0x129A668,  // playerCameraGlobalRva
    0x538,      // activeCameraCountOffset
};

// Same build, the DX11 executable. Control ships both and the launcher picks
// one, so each needs its own profile. The camera tick landed at the same RVA in
// both binaries - confirmed by decompiling that address in the DX11 EXE and
// checking it reads the same +0x40 / +0x50 pose and calls the same
// safeMatrixLookAt, not by assuming the layouts match.
static const BuildProfile kSteamDx11_20250324 = {
    "steam-win64-dx11-20250324",
    "Control_DX11.exe",
    {
        0x67E06E49,  // TimeDateStamp
        0x13EA000,   // SizeOfImage
        0x01393A08,  // CheckSum
    },
    0x24C310,  // cameraUpdateRva
    0x40,      // cameraQuatOffset
    0x50,      // cameraPosOffset
    0x129A5E8,  // playerCameraGlobalRva (NOT the DX12 value - derived separately)
    0x538,      // activeCameraCountOffset
};

static const BuildProfile* kKnownProfiles[] = {
    &kSteamDx12_20250324,
    &kSteamDx11_20250324,
};
// The running EXE, not a named module: Control ships Control_DX11.exe and
// Control_DX12.exe from the same source, so the profile has to be chosen by
// which one is actually executing.
static bool FingerprintRunningExe(PeFingerprint& out, char* nameOut, size_t nameSize) {
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) return false;
    char path[MAX_PATH] = {};
    if (GetModuleFileNameA(exe, path, MAX_PATH) == 0) {
        Log::Line("ERROR: could not read the game's path, so its build cannot be "
                  "identified. Head tracking is dormant.");
        return false;
    }
    const char* leaf = strrchr(path, '\\');
    strncpy_s(nameOut, nameSize, leaf ? leaf + 1 : path, _TRUNCATE);
    return cameraunlock::memory::ReadPeFingerprint(exe, out);
}

bool IsProfileComplete(const BuildProfile& profile) {
    // Every value the detour DEREFERENCES, not just the hook target. A profile
    // with the tick RVA derived but the pose offsets still TBD is a normal
    // intermediate state, and treating it as complete made the detour write seven
    // floats at state + 0, straight over the object's vtable pointer, so the next
    // virtual call on it crashed. That is the exact outcome the dormancy contract
    // exists to prevent.
    return profile.cameraUpdateRva != 0 && profile.cameraQuatOffset != 0 &&
           profile.cameraPosOffset != 0 && profile.playerCameraGlobalRva != 0 &&
           profile.activeCameraCountOffset != 0;
}

const BuildProfile* MatchRunningBuild() {
    PeFingerprint running{};
    char name[MAX_PATH] = {};
    if (!FingerprintRunningExe(running, name, sizeof(name))) return nullptr;

    for (const BuildProfile* p : kKnownProfiles) {
        if (_stricmp(p->exeName, name) != 0) continue;
        if (!running.Matches(p->fingerprint)) continue;
        if (!IsProfileComplete(*p)) {
            Log::Line(
                "WARN: %s is build %s, which this mod recognises but has not "
                "been worked out for yet. Head tracking is dormant.",
                name, p->name);
            return nullptr;
        }
        return p;
    }
    return nullptr;
}

static void LogKnownProfiles() {
    for (const BuildProfile* p : kKnownProfiles) {
        Log::Line("  known profile: %s (TimeDateStamp=0x%08X SizeOfImage=0x%X CheckSum=0x%08X)",
                  p->name, p->fingerprint.TimeDateStamp, p->fingerprint.SizeOfImage,
                  p->fingerprint.CheckSum);
    }
}

// The build the unmatched-fingerprint message is worded against: the profile for
// this EXE if there is one, else the diagnostic primary. The registry is
// newest-first, so the FIRST profile for this EXE is the diagnostic primary.
// Taking the last match would compare against the oldest known build and word
// the newer/older message backwards, which is precisely when that message
// matters.
static const BuildProfile* DiagnosticReferenceFor(const char* exeName) {
    for (const BuildProfile* p : kKnownProfiles) {
        if (_stricmp(p->exeName, exeName) == 0) return p;
    }
    return kKnownProfiles[0];
}

void LogUnmatchedBuildDiagnostic() {
    PeFingerprint running{};
    char name[MAX_PATH] = {};
    if (!FingerprintRunningExe(running, name, sizeof(name))) {
        Log::Line("WARN: could not fingerprint the running EXE - mod dormant.");
        return;
    }

    Log::Line("Running %s fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%X CheckSum=0x%08X",
              name, running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

    LogKnownProfiles();
    const BuildProfile* reference = DiagnosticReferenceFor(name);

    // A recognised-but-incomplete profile reaches here with a fingerprint that
    // MATCHES its reference. ClassifyMismatch has no equal case, so it fell through
    // to Differs and told the user their EXE was "tampered or repacked", quoting
    // identical numbers either side of "vs".
    if (running.Matches(reference->fingerprint)) {
        Log::Line(
            "WARN: %s is build %s, which this mod recognises but has not been worked out "
            "for yet. Head tracking is dormant; this is not a problem with your game files.",
            name, reference->name);
        return;
    }

    switch (cameraunlock::memory::ClassifyMismatch(running, reference->fingerprint)) {
        case FingerprintMismatch::Newer:
            Log::Line(
                "WARN: %s is NEWER than any build this mod knows about "
                "(TimeDateStamp 0x%08X > 0x%08X). Check the releases page for an "
                "updated mod. Head tracking is dormant.",
                name, running.TimeDateStamp, reference->fingerprint.TimeDateStamp);
            break;
        case FingerprintMismatch::Older:
            Log::Line(
                "WARN: %s is OLDER than this mod's newest known build "
                "(TimeDateStamp 0x%08X < 0x%08X). Let the store finish updating. "
                "Head tracking is dormant.",
                name, running.TimeDateStamp, reference->fingerprint.TimeDateStamp);
            break;
        case FingerprintMismatch::Differs:
            Log::Line(
                "WARN: %s has the expected TimeDateStamp but a different "
                "size/checksum (0x%08X/0x%08X vs 0x%08X/0x%08X) - tampered or "
                "repacked binary. Head tracking is dormant.",
                name, running.SizeOfImage, running.CheckSum,
                reference->fingerprint.SizeOfImage, reference->fingerprint.CheckSum);
            break;
    }
}

} // namespace ControlHT
