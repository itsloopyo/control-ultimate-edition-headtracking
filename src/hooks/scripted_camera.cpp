#include "pch.h"
#include "scripted_camera.h"
#include "core/logging.h"
#include "core/safe_read.h"

#include <atomic>
#include <cstring>

namespace ControlHT {
namespace {

// Control's player camera update reads a global `PlayerCameraComponentState`
// pointer, writes the final camera pose into it, and then calls the camera tick
// this mod hooks with that object's +0x4D0. So the global and our hook are two
// ends of the same call and the object can simply be read.
uintptr_t g_globalAddress = 0;
std::atomic<uintptr_t> g_playerCameraState{0};

// Named so the global can be validated: if the pointers at these three offsets
// resolve to these three classes, the address really is a PlayerCameraComponentState.
// Revalidated on this interval as well as on any change of pointer value, so a
// freed object replaced at the same address is caught rather than trusted
// forever.
constexpr DWORD kRevalidateMs = 2000;

struct CameraModeSlot {
    uintptr_t offset;
    const char* className;
};

constexpr CameraModeSlot kModeSlots[] = {
    {0x4D0, ".?AVCameraManComponentState@@"},
    {0x4D8, ".?AVTailCameraComponentState@@"},
    {0x4E0, ".?AVFPSCameraComponentState@@"},
};

// The live field of view, in radians, on the same object. An output flow pin the
// player camera update writes each frame - `pin = scaleFOV(baseFov,
// lerp(FOVMultiplier, 1, scriptedBlend))` - a handful of instructions before it
// calls the camera tick this mod hooks. Observed tracking aim zoom: 70 degrees
// at rest, 43.3 and 56.5 while aiming. +0x140 is the base value and does not
// follow the zoom, so it is the wrong one for a projection.
constexpr uintptr_t kLiveFovOffset = 0x88;

// A field of view outside this band is not one the engine could be rendering,
// so it means the read landed on something that is not the pin.
constexpr float kMinPlausibleFovRadians = 0.05f;
constexpr float kMaxPlausibleFovRadians = 3.0f;

uint32_t g_activeCameraCountOffset = 0;
bool g_gaveUp = false;

struct ModuleRange {
    uintptr_t base = 0;
    uintptr_t end = 0;
};

ModuleRange ModuleContaining(uintptr_t address) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(address), &mod) ||
        !mod) {
        return {};
    }
    const auto base = reinterpret_cast<uintptr_t>(mod);
    IMAGE_DOS_HEADER dos = {};
    if (!SafeRead(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return {};
    IMAGE_NT_HEADERS64 nt = {};
    if (!SafeRead(base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE) return {};
    return {base, base + nt.OptionalHeader.SizeOfImage};
}

// MSVC x64 puts a pointer to the RTTICompleteObjectLocator immediately before
// vtable slot 0. Every image-relative field is bounds-checked against the module
// the vtable lives in before it is followed, and the class name is COPIED OUT
// rather than returned as a pointer: an earlier version returned a pointer into
// game memory that the caller then passed to strcmp, which walks until it finds a
// zero and would leave the module entirely for a type descriptor sitting near the
// end of the last section.
constexpr size_t kMaxClassName = 128;

// RTTICompleteObjectLocator: a signature dword at +0x00 (1 for x64 images) and
// the type descriptor's image-relative address at +0x0C.
constexpr uintptr_t kColSignatureOffset = 0x00;
constexpr uint32_t kColSignatureX64 = 1;
constexpr uintptr_t kColTypeDescriptorRvaOffset = 0x0C;
constexpr uintptr_t kColSize = 0x10;

bool ClassNameOfObject(uintptr_t object, char (&nameOut)[kMaxClassName]) {
    nameOut[0] = '\0';

    uintptr_t vtable = 0;
    if (!SafeRead(object, vtable) || vtable == 0 || (vtable & 7) != 0) return false;
    const ModuleRange mod = ModuleContaining(vtable);
    if (mod.base == 0) return false;

    uintptr_t col = 0;
    if (!SafeRead(vtable - sizeof(uintptr_t), col)) return false;
    if (col < mod.base || col + kColSize > mod.end) return false;

    uint32_t signature = 0;
    if (!SafeRead(col + kColSignatureOffset, signature) || signature != kColSignatureX64) {
        return false;
    }

    uint32_t typeDescriptorRva = 0;
    if (!SafeRead(col + kColTypeDescriptorRvaOffset, typeDescriptorRva)) return false;
    const uintptr_t typeDescriptor = mod.base + typeDescriptorRva;

    // TypeDescriptor is { void* vfptr; void* spare; char name[]; }. The name must
    // fit inside the module with room for the whole buffer, so the copy below
    // cannot run past the last mapped page.
    const uintptr_t name = typeDescriptor + 2 * sizeof(uintptr_t);
    if (typeDescriptor < mod.base || name + kMaxClassName > mod.end) return false;

    for (size_t i = 0; i < kMaxClassName - 1; i++) {
        char c = 0;
        if (!SafeRead(name + i, c)) return false;
        nameOut[i] = c;
        if (c == '\0') return i >= 3 && nameOut[0] == '.' && nameOut[1] == '?' &&
                               nameOut[2] == 'A';
    }
    return false;
}

// The global is believed only once all three of its camera-mode pointers name
// the classes they should. A raw pointer read would accept a wrong address
// silently; this cannot. Re-verified whenever the pointer changes, which is how
// a level change rebuilding the camera is picked up.
bool ResolvePlayerCameraState(uintptr_t& state) {
    const uintptr_t cached = g_playerCameraState.load(std::memory_order_relaxed);
    uintptr_t current = 0;
    if (!SafeRead(g_globalAddress, current) || current == 0) {
        g_playerCameraState.store(0, std::memory_order_relaxed);
        return false;
    }
    static DWORD lastValidated = 0;
    const DWORD now = GetTickCount();
    if (current == cached && now - lastValidated < kRevalidateMs) {
        state = cached;
        return true;
    }

    for (const CameraModeSlot& slot : kModeSlots) {
        uintptr_t component = 0;
        char name[kMaxClassName];
        if (!SafeRead(current + slot.offset, component) || component == 0 ||
            !ClassNameOfObject(component, name) || strcmp(name, slot.className) != 0) {
            // Drop the cache rather than leaving it standing. The revalidation
            // clock is stamped only on success, so a pointer that just failed
            // its class check cannot be handed out as valid for the rest of the
            // interval - which is exactly the window in which it is least
            // likely to be a PlayerCameraComponentState.
            g_playerCameraState.store(0, std::memory_order_relaxed);
            return false;
        }
    }
    lastValidated = now;

    const bool changed = g_playerCameraState.exchange(current, std::memory_order_relaxed) !=
                         current;
    state = current;
    // Logged against the last address REPORTED, not the last one cached, so a
    // camera that flickers in and out of validation cannot flood the log with
    // the same address once per frame.
    static uintptr_t lastLogged = 0;
    if (!changed || current == lastLogged) return true;
    lastLogged = current;
    Log::Line("Player camera resolved at %p (scripted, follow and first-person cameras all "
              "verified by class)",
              reinterpret_cast<void*>(current));
    return true;
}

}  // namespace

bool InstallScriptedCameraDetection(uint32_t playerCameraGlobalRva,
                                   uint32_t activeCameraCountOffset) {
    if (playerCameraGlobalRva == 0 || activeCameraCountOffset == 0) {
        Log::Line("ERROR: this build profile does not pin the player camera global, so a "
                  "scripted shot cannot be told from gameplay - head tracking will stay on "
                  "through cutscenes.");
        g_gaveUp = true;
        return false;
    }
    // GetModuleHandleA(nullptr) cannot fail for the running image, so there is no
    // fallback here to degrade into.
    g_globalAddress = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) +
                      playerCameraGlobalRva;
    g_activeCameraCountOffset = activeCameraCountOffset;
    return true;
}

CameraOwner CurrentCameraOwner() {
    if (g_gaveUp) return CameraOwner::PlayerDriven;

    uintptr_t state = 0;
    if (!ResolvePlayerCameraState(state)) return CameraOwner::Unknown;

    uint32_t activeCameras = 0;
    if (!SafeRead(state + g_activeCameraCountOffset, activeCameras)) return CameraOwner::Unknown;
    return activeCameras != 0 ? CameraOwner::Scripted : CameraOwner::PlayerDriven;
}

bool CurrentCameraFovRadians(float& fovRadians) {
    uintptr_t state = 0;
    if (!ResolvePlayerCameraState(state)) return false;
    float fov = 0.0f;
    if (!SafeRead(state + kLiveFovOffset, fov)) return false;
    if (!(fov > kMinPlausibleFovRadians) || !(fov < kMaxPlausibleFovRadians)) return false;
    fovRadians = fov;
    return true;
}

} // namespace ControlHT
