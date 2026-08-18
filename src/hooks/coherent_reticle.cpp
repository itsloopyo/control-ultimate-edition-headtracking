#include "pch.h"
#include "coherent_reticle.h"
#include "view_slot_table.h"
#include "core/logging.h"
#include "core/safe_read.h"

#include <cameraunlock/hooks/hook_manager.h>
#include <cameraunlock/memory/pe_fingerprint.h>

#include <atomic>
#include <cmath>

namespace ControlHT {
namespace {

// coherentuigt.dll, timestamped 2020-04-09. Pinned like every other address this
// mod depends on: the RVA below is meaningless against a different build of the
// UI library, and Coherent GT is shipped with the game rather than patched with
// it, so a mismatch means something unexpected and we stand down rather than hook
// an address that is no longer ExecuteScript.
// The PUBLIC View::ExecuteScript(const char* script, const char* frameSelector).
// It builds a WTF::String via WTF::String::fromUTF8, calls the internal overload at
// +0xE24E0 (the one carrying the "can't find frame with selector" diagnostic that
// identified this pair), then releases the string. Hooking the public one means
// scripts arrive as plain C strings and our own calls need no string construction.
constexpr uint32_t kExecuteScriptRva = 0xDBD00;

// The build both RVAs were derived against, read off the shipped
// coherentuigt.dll. Enforced, not merely logged: against any other build these
// are two arbitrary addresses, and hooking an arbitrary address with a detour
// that calls it as `void(void*, const char*, const char*)` corrupts the
// arguments of whatever function is really there, on the game's UI thread. A
// mismatch means we stand down and leave Control's own crosshair alone, which
// costs the reticle offset and nothing else.
constexpr cameraunlock::memory::PeFingerprint kCoherentUiGtBuild = {
    0x5E8F6E9A,  // TimeDateStamp
    0x329000,    // SizeOfImage
    0x00323A4A,  // CheckSum
};

using ExecuteScriptFn = void(__fastcall*)(void*, const char*, const char*);
ExecuteScriptFn g_origExecuteScript = nullptr;

// The reticle shim.
//
// `window.__ht(x, y, visible)` takes a normalised device position (+x right, +y
// up, centre 0,0) and moves Control's own crosshair there from wherever the game
// put it, converting to pixels against the page's own viewport. Proven in game:
// driving this element by CSS transform swung the real crosshair a quarter of the
// screen width with nothing else moving.
//
// The whole body is wrapped in try/catch because there is no JS-to-C++ channel
// here: an exception inside the shim would look from outside exactly like "the
// offset is not being applied", and it must never escape into the game's own UI.
//
// The shim CALIBRATES ITSELF rather than trusting any theory about coordinate
// spaces. It nudges the element 100px, measures how far its bounding box actually
// moved, and scales every later offset by the inverse. In play the reticle tracked
// at half speed, and both explanations I reached for - a HUD authored at a different
// resolution from the render target, and a scaled ancestor - predicted the same
// symptom while changing nothing when fixed. Measuring the response covers all of
// them and anything else, including a CSS zoom or a transformed ancestor.
//
// The element is cached and revalidated cheaply (still connected, still laid out),
// falling back to a full scan only when that fails. A scan of the whole HUD DOM on
// every update would run up to sixty times a second, but Control rebuilds its HUD as
// weapons and context change, so the element cannot simply be found once and kept.
// Finding nothing is the normal state whenever the game hides the crosshair and must
// be a silent no-op.
//
// The computed transform is preserved and ours appended: a crosshair centred by a
// stylesheet `transform: translate(-50%, -50%)` would jump by half its own size if
// we replaced it. The original is captured once per element, because reading it
// back after we have written to it would compound our own offset.
constexpr const char* kReticleShim =
    "window.__ht=function(x,y,v){try{var w=window,d=document;if(!d.body)return;var e=w.__hte;"
    "if(!e||!e.length||!e[0].isConnected){e=[];var a=d.querySelectorAll('*');for(var i=0;i<a."
    "length;i++){var t=a[i];if(!/cross.?hair|retic|ammo/i.test((t.id||'')+' '+String(t.classN"
    "ame||'')))continue;var r=t.getBoundingClientRect();if(!r.width||!r.height)continue;var i"
    "nside=false;for(var j=0;j<e.length;j++){if(e[j].contains(t)||t.contains(e[j])){inside=tr"
    "ue;break;}}if(inside)continue;e.push(t);}w.__hte=e;for(var k=0;k<e.length;k++){var g=e[k"
    "];var b=w.getComputedStyle(g).transform;g.__b=(!b||b==='none')?'':b;var q0=g.getBounding"
    "ClientRect().left;g.style.transform=g.__b+' translate(100px,0px)';var q1=g.getBoundingCl"
    "ientRect().left;g.style.transform=g.__b;var m=(q1-q0)/100;g.__g=(m>0.01)?1/m:1;}}if(!e.l"
    "ength)return;for(var n=0;n<e.length;n++){var el=e[n];el.style.transform=el.__b+' transla"
    "te('+(x*w.innerWidth*0.5*el.__g)+'px,'+(-y*w.innerHeight*0.5*el.__g)+'px)';el.style.visi"
    "bility=v?'':'hidden';}}catch(err){}};";

// Hooked purely for its trampoline: `g_origExecuteScript` is how the shim and
// every later offset are pushed into a View. The detour itself has nothing to
// add to the game's own bootstrap scripts and forwards them untouched.
void __fastcall ExecuteScriptDetour(void* self, const char* script, const char* frameSelector) {
    g_origExecuteScript(self, script, frameSelector);
}

// Per-frame, on Coherent's own thread: the View method that calls
// WebCore::FrameView::serviceScriptedAnimations, which is what drives
// requestAnimationFrame. Its signature is just void(this), so hooking it carries
// none of the argument-forwarding risk of a method whose parameters we would have
// to guess.
//
// This is the only safe place to push offsets. The camera tick computes them but
// runs on a job pool - a different thread almost every tick, measured - while
// Coherent's View methods are thread-affine, so pushing from there would eventually
// crash the game rather than fail cleanly.
constexpr uint32_t kServiceAnimationsRva = 0xD9A90;

using ServiceAnimationsFn = void(__fastcall*)(void*);
ServiceAnimationsFn g_origServiceAnimations = nullptr;

std::atomic<float> g_reticleNdcX{0.0f};
std::atomic<float> g_reticleNdcY{0.0f};
std::atomic<bool> g_reticleVisible{true};

// Big enough for the shim definition plus the call appended to it; a script that
// did not fit would be truncated into invalid JS, so it is reported instead.
constexpr size_t kMaxScriptBytes = 4096;

// An offset change smaller than this is below one pixel on any plausible
// display, so it is not worth rebuilding and parsing a script for.
constexpr float kNdcChangeThreshold = 0.0005f;

// The View's page. ExecuteScript dereferences it without checking, so handing it a
// View whose page is null crashes inside CoherentUIGT - which is exactly what
// pushing to a stale, destroyed View did.
constexpr uintptr_t kViewPageOffset = 0xA8;

// Last value pushed to each view. Per view, not global: several views are serviced
// each frame, and a single shared threshold would let only the first one see any
// given update. Slots left behind by destroyed views are reclaimed once they stop
// being serviced - see view_slot_table.h.
ViewSlotTable g_views;

void __fastcall ServiceAnimationsDetour(void* self) {
    g_origServiceAnimations(self);

    // `self` and nothing else. This View is being serviced right now, so it is
    // alive and on the right thread; the most recently CREATED view is not, and
    // pushing to one that had since been destroyed crashed the game.
    const uintptr_t view = reinterpret_cast<uintptr_t>(self);
    if (view == 0 || g_origExecuteScript == nullptr) return;

    // ExecuteScript dereferences the View's page without checking, so handing it a
    // View whose page is null crashes inside CoherentUIGT.
    uintptr_t page = 0;
    if (!SafeRead(view + kViewPageOffset, page) || page == 0) return;

    const DWORD now = GetTickCount();
    ViewPushState* state = g_views.Acquire(view, now);
    if (state == nullptr) return;

    // Rebuilding and parsing a script every frame is the cost of this channel, so an
    // unchanged offset is normally skipped - but never for longer than the heartbeat.
    //
    // Skipping on value alone was why the shim appeared dead: the one push a session
    // landed moments after the view was created, before the HUD document existed, so
    // the shim ran against a null document.body and did nothing. With the offset then
    // sitting at zero, no further push was ever due and the shim never ran again. A
    // heartbeat means the shim always gets another chance once the page is ready.
    constexpr DWORD kHeartbeatMs = 250;
    const float ndcX = g_reticleNdcX.load(std::memory_order_relaxed);
    const float ndcY = g_reticleNdcY.load(std::memory_order_relaxed);
    const bool visible = g_reticleVisible.load(std::memory_order_relaxed);
    const bool unchanged = visible == state->visible &&
                           fabsf(ndcX - state->ndcX) < kNdcChangeThreshold &&
                           fabsf(ndcY - state->ndcY) < kNdcChangeThreshold;
    const bool sendDefinition = now - state->lastDefinition >= kHeartbeatMs;
    if (unchanged && !sendDefinition) return;
    if (sendDefinition) state->lastDefinition = now;

    state->ndcX = ndcX;
    state->ndcY = ndcY;
    state->visible = visible;

    // The definition goes out on the heartbeat and a bare call on every other push.
    //
    // Injecting the shim once at view creation does not work: the view then navigates
    // to the real HUD document, the JS global is replaced, and every later call is a
    // silent ReferenceError. That was proven by pushing a bare `document.body.style
    // .border` and seeing it land while the shim itself never ran. Resending the
    // definition periodically restores it within a heartbeat of any navigation,
    // without paying to parse it every frame.
    char script[kMaxScriptBytes];
    if (sendDefinition) {
        const int written = snprintf(script, sizeof(script), "%s__ht(%.5f,%.5f,%d)",
                                     kReticleShim, ndcX, ndcY, visible ? 1 : 0);
        if (written < 0 || written >= static_cast<int>(sizeof(script))) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                Log::Line("COHERENT: shim script does not fit in the buffer (%d bytes) - it "
                          "would be truncated into invalid JS",
                          written);
            }
            return;
        }
    } else {
        snprintf(script, sizeof(script), "window.__ht&&__ht(%.5f,%.5f,%d)", ndcX, ndcY,
                 visible ? 1 : 0);
    }
    g_origExecuteScript(self, script, nullptr);
}

// Whether another attempt could change the answer. Only a UI library that has
// not been mapped yet is worth waiting for; a build we cannot hook, or a hook
// that would not take, stays that way for the session.
enum class InstallOutcome {
    NotLoadedYet,
    StandDown,
    Installed,
};

InstallOutcome TryInstall() {
    HMODULE ui = GetModuleHandleA("coherentuigt.dll");
    if (!ui) return InstallOutcome::NotLoadedYet;

    cameraunlock::memory::PeFingerprint fingerprint{};
    if (!cameraunlock::memory::ReadPeFingerprint(ui, fingerprint)) {
        Log::Line("COHERENT: could not fingerprint coherentuigt.dll, so its build cannot be "
                  "identified. Reticle compensation is unavailable; head tracking still "
                  "works and shots still land where the game aims.");
        return InstallOutcome::StandDown;
    }
    Log::Line("COHERENT: coherentuigt.dll at %p (TimeDateStamp=0x%08X SizeOfImage=0x%X "
              "CheckSum=0x%08X)",
              reinterpret_cast<void*>(ui), fingerprint.TimeDateStamp, fingerprint.SizeOfImage,
              fingerprint.CheckSum);

    if (!fingerprint.Matches(kCoherentUiGtBuild)) {
        Log::Line("COHERENT: this is not the build of coherentuigt.dll the UI addresses were "
                  "derived against (expected TimeDateStamp=0x%08X SizeOfImage=0x%X "
                  "CheckSum=0x%08X). NOT hooking it - the addresses would land on whatever "
                  "happens to be there. Reticle compensation is unavailable this session; "
                  "head tracking still works and shots still land where the game aims. "
                  "Please report this log so the UI offsets can be rederived.",
                  kCoherentUiGtBuild.TimeDateStamp, kCoherentUiGtBuild.SizeOfImage,
                  kCoherentUiGtBuild.CheckSum);
        return InstallOutcome::StandDown;
    }

    void* target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ui) + kExecuteScriptRva);
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    HookStatus st = HookManager::Instance().CreateHook(
        target, reinterpret_cast<void*>(&ExecuteScriptDetour),
        reinterpret_cast<void**>(&g_origExecuteScript));
    if (st != HookStatus::Ok) {
        Log::Line("COHERENT: hooking View::ExecuteScript at +0x%X failed: %s",
                  kExecuteScriptRva, cameraunlock::hooks::HookStatusToString(st));
        return InstallOutcome::StandDown;
    }
    void* animations =
        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ui) + kServiceAnimationsRva);
    if (HookManager::Instance().CreateHook(animations,
                                           reinterpret_cast<void*>(&ServiceAnimationsDetour),
                                           reinterpret_cast<void**>(&g_origServiceAnimations)) !=
        HookStatus::Ok) {
        Log::Line("ERROR: could not hook Coherent's per-frame animation service, so the "
                  "reticle cannot be driven. Head tracking still works and shots still land "
                  "where the game aims.");
        return InstallOutcome::StandDown;
    }
    if (HookManager::Instance().EnableAllHooks() != HookStatus::Ok) {
        Log::Line("COHERENT: enabling the UI hooks failed");
        return InstallOutcome::StandDown;
    }
    Log::Line("COHERENT: hooked ExecuteScript (+0x%X) and the per-frame animation service "
              "(+0x%X)",
              kExecuteScriptRva, kServiceAnimationsRva);
    return InstallOutcome::Installed;
}

// coherentuigt.dll is loaded during engine start-up, after the mod initialises, so
// this waits for it rather than giving up.
constexpr int kInstallAttempts = 120;
constexpr DWORD kInstallRetryMs = 1000;

DWORD WINAPI InstallThread(LPVOID) {
    for (int attempt = 0; attempt < kInstallAttempts; attempt++) {
        // Anything other than "not mapped yet" is settled for this session:
        // retrying a build we will not hook, or a hook that would not take,
        // only repeats the same diagnostic 120 times.
        if (TryInstall() != InstallOutcome::NotLoadedYet) return 0;
        Sleep(kInstallRetryMs);
    }
    Log::Line("COHERENT: coherentuigt.dll never appeared, so the UI layer cannot be "
              "reached. Reticle compensation is unavailable.");
    return 0;
}

}  // namespace

bool InstallCoherentReticle() {
    HANDLE thread = CreateThread(nullptr, 0, &InstallThread, nullptr, 0, nullptr);
    if (!thread) return false;
    CloseHandle(thread);
    return true;
}

void SetReticleOffset(bool visible, float ndcX, float ndcY) {
    g_reticleNdcX.store(ndcX, std::memory_order_relaxed);
    g_reticleNdcY.store(ndcY, std::memory_order_relaxed);
    g_reticleVisible.store(visible, std::memory_order_relaxed);
}

} // namespace ControlHT
