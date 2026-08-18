#include "pch.h"
#include "core/mod.h"
#include "core/logging.h"
#include "core/path_utils.h"
#include "core/constants.h"

#include <cameraunlock/diagnostics/crash_handler.h>

#include <process.h>

static unsigned __stdcall InitThread(void* /*unused*/) {
    ControlHT::Log::Open(ControlHT::PathUtils::GetModPathW(ControlHT::LOG_FILENAME));
    cameraunlock::diagnostics::InstallCrashHandler();
    ControlHT::Log::Line(
        "Control Head Tracking v%s attached", ControlHT::CONTROLHT_VERSION);

    // Initialise immediately rather than waiting for a game module to appear.
    // Every hook target is in the game EXE, which is fully mapped before this
    // thread runs, and the build-profile fingerprint on that EXE is a stricter
    // "are we in the right process" check than waiting for a renderer DLL ever
    // was - it identifies the exact build, not merely the engine.
    if (!ControlHT::Mod::Instance().Initialize()) {
        ControlHT::Log::Line("ERROR: Mod initialization failed");
        return 1;
    }
    // Stamped so a log makes it obvious WHICH binary produced it. Without this,
    // "the behaviour is identical" and "you tested the previous build" are
    // indistinguishable from the log alone.
    ControlHT::Log::Line("Mod initialized (built %s %s)", __DATE__, __TIME__);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);
            // The handle is closed straight away rather than kept: nothing ever
            // joins this thread (see DLL_PROCESS_DETACH), so holding it would
            // only leak it.
            HANDLE init = reinterpret_cast<HANDLE>(
                _beginthreadex(nullptr, 0, InitThread, nullptr, 0, nullptr));
            if (init) CloseHandle(init);
            break;
        }
        case DLL_PROCESS_DETACH:
            // No orderly teardown here, on either path.
            //
            // Process exit (lpReserved != NULL) is the usual case: every other
            // thread has already been forcibly killed, possibly mid-lock, so
            // joining them is the classic DllMain deadlock and buys nothing -
            // the OS reclaims it all.
            //
            // A dynamic FreeLibrary (lpReserved == NULL) used to run the full
            // Shutdown() here, and that was worse: it joins three threads and
            // lets MinHook suspend every thread in the process, all under the
            // loader lock, which cannot complete because those threads need the
            // same lock to run their own thread-detach callbacks. It hung the
            // game rather than unloading cleanly. Ultimate ASI Loader never
            // unloads a plugin, so the only detach that actually happens is
            // process exit; the deadlock was pure downside.
            //
            // Closing the log is safe because it takes no other thread's lock,
            // and it means a crash report written moments earlier is on disk.
            ControlHT::Log::Close();
            break;
    }
    return TRUE;
}
