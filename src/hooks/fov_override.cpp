#include "pch.h"
#include "fov_override.h"
#include "core/logging.h"

#include <atomic>

namespace ControlHT {
namespace {

// `public: static float rend::RenderOptions::FOVMultiplier`, exported by name
// from the renderer. Control ships a Windows 10 and a Windows 7 renderer built
// from the same source and loads exactly one of them.
constexpr const char* kFovMultiplierExport = "?FOVMultiplier@RenderOptions@rend@@2MA";
constexpr const char* kRendererModules[] = {"renderer_rmdwin10_f.dll",
                                            "renderer_rmdwin7_f.dll"};

std::atomic<float> g_scale{0.0f};
std::atomic<float*> g_multiplier{nullptr};
std::atomic<bool> g_resolveFailed{false};

// Resolved on the first camera tick rather than at init. The renderer is a
// static data import of the game EXE, so it is mapped long before the camera
// ticks and one attempt is enough - but the ASI runs its init on its own thread
// during process start-up, where nothing says the import walk has finished.
float* ResolveFovMultiplier() {
    for (const char* name : kRendererModules) {
        HMODULE renderer = GetModuleHandleA(name);
        if (!renderer) continue;
        FARPROC address = GetProcAddress(renderer, kFovMultiplierExport);
        if (address) return reinterpret_cast<float*>(address);
        Log::Line("ERROR: %s does not export %s, so FovScale cannot be applied. The view "
                  "keeps Control's own field of view.",
                  name, kFovMultiplierExport);
        return nullptr;
    }
    Log::Line("ERROR: neither renderer module is loaded, so FovScale cannot be applied. "
              "The view keeps Control's own field of view.");
    return nullptr;
}

}  // namespace

void ConfigureFovOverride(float scale) {
    g_scale.store(scale > 0.0f ? scale : 0.0f, std::memory_order_relaxed);
}

void ApplyFovOverride() {
    const float scale = g_scale.load(std::memory_order_relaxed);
    if (!(scale > 0.0f)) return;
    if (g_resolveFailed.load(std::memory_order_relaxed)) return;

    float* multiplier = g_multiplier.load(std::memory_order_relaxed);
    if (!multiplier) {
        multiplier = ResolveFovMultiplier();
        if (!multiplier) {
            g_resolveFailed.store(true, std::memory_order_relaxed);
            return;
        }
        g_multiplier.store(multiplier, std::memory_order_relaxed);
        Log::Line("FOV scale %.2f applied (Control's own Options > Graphics > FOV Scale "
                  "only reaches 0.75-1.25; this replaces whatever it is set to).",
                  static_cast<double>(scale));
    }

    // Compared before writing so the usual frame touches nothing. The camera
    // tick can land on a different job thread each frame but never two at once
    // on the same state, and a 4-byte aligned store cannot tear, so a settings
    // apply racing this can only lose or win a frame - never leave a torn value.
    if (*multiplier != scale) *multiplier = scale;
}

} // namespace ControlHT
