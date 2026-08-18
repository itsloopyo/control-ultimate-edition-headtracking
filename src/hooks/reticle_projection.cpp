#include "pch.h"
#include "reticle_projection.h"
#include "camera_injection.h"
#include "coherent_reticle.h"
#include "scripted_camera.h"
#include "core/logging.h"

#include <atomic>
#include <cmath>

namespace ControlHT {
namespace {

using cameraunlock::math::Quat4;

// The render surface's aspect ratio, for turning a normalised aim position into
// a screen position. Taken from the game window's client area rather than a
// swapchain, and refreshed on this interval so a resolution change is picked up.
constexpr DWORD kRenderSizeRefreshMs = 2000;

bool RenderAspect(float& aspect) {
    static DWORD lastQuery = 0;
    static float cached = 0.0f;

    const DWORD now = GetTickCount();
    if (now - lastQuery > kRenderSizeRefreshMs || cached <= 0.0f) {
        lastQuery = now;
        struct Finder {
            DWORD pid;
            HWND hwnd;
        } finder{GetCurrentProcessId(), nullptr};
        EnumWindows(
            [](HWND hwnd, LPARAM param) -> BOOL {
                auto* f = reinterpret_cast<Finder*>(param);
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid != f->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) {
                    return TRUE;
                }
                f->hwnd = hwnd;
                return FALSE;
            },
            reinterpret_cast<LPARAM>(&finder));
        RECT rect{};
        if (finder.hwnd && GetClientRect(finder.hwnd, &rect)) {
            const float width = static_cast<float>(rect.right - rect.left);
            const float height = static_cast<float>(rect.bottom - rect.top);
            cached = (width > 0.0f && height > 0.0f) ? width / height : 0.0f;
        }
    }
    aspect = cached;
    return cached > 0.0f;
}

// The field of view the reticle is actually being projected through, logged the
// first time it reads. Reported because it is the one number that makes a
// FovScale setting checkable from the log alone - both that the override reached
// the camera and what it came out as.
void LogLiveFovOnce(float fovRadians, float tanHalfV) {
    static std::atomic<bool> logged{false};
    if (logged.exchange(true)) return;
    Log::Line("Camera field of view: %.1f degrees horizontal, %.1f vertical",
              fovRadians * kRadToDeg, 2.0f * std::atan(tanHalfV) * kRadToDeg);
}

}  // namespace

void UpdateReticle(const Quat4& cleanRotation, const Quat4& trackedRotation) {
    float fov = 0.0f, aspect = 0.0f, tanHalfH = 0.0f, tanHalfV = 0.0f;
    if (!CurrentCameraFovRadians(fov) || !RenderAspect(aspect) ||
        !FovTangents(fov, aspect, tanHalfH, tanHalfV)) {
        SetReticleOffset(true, 0.0f, 0.0f);
        return;
    }
    LogLiveFovOnce(fov, tanHalfV);

    float ndcX = 0.0f, ndcY = 0.0f;
    if (!ComputeAimNdc(cleanRotation, trackedRotation, tanHalfH, tanHalfV, ndcX, ndcY)) {
        SetReticleOffset(false, 0.0f, 0.0f);
        return;
    }
    // Screen mapping confirmed in play: a constant +0.5 put the crosshair on the
    // right at roughly three quarters across, so positive is right and the scale is
    // honest. Sign is physically correct too - the aim point moves opposite the head.
    SetReticleOffset(true, ndcX, ndcY);
}

} // namespace ControlHT
