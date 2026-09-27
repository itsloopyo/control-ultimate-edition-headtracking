#include "pch.h"
#include "hotkeys.h"
#include "mod.h"
#include "logging.h"

#include <cameraunlock/input/key_binding_registration.h>
#include <cameraunlock/input/key_bindings.h>

#include <functional>

namespace ControlHT {

namespace {

// Puts one key list from CameraUnlock.ini on the poller. The table only accepts
// a list that parses, so a failure here is a bug, not a player's typo.
void Register(cameraunlock::input::HotkeyPoller& poller, const char* name, const std::string& list,
              std::function<void()> action) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error(std::string("[Hotkeys] ") + name + " does not parse: " + list);
    cameraunlock::input::RegisterKeyBindings(poller, parsed.bindings, std::move(action));
    Log::Line("Hotkey %s: %s", name, list.empty() ? "(unbound)" : list.c_str());
}

}  // namespace

bool Hotkeys::Start(const Config& cfg) {
    if (m_started) return true;

    Register(m_poller, "ToggleKey", cfg.toggleKey, [] { Mod::Instance().Toggle(); });
    Register(m_poller, "CycleTrackingModeKey", cfg.cycleTrackingModeKey,
             [] { Mod::Instance().CycleTrackingMode(); });
    Register(m_poller, "YawModeKey", cfg.yawModeKey, [] { Mod::Instance().ToggleYawMode(); });

    if (!m_poller.Start()) {
        Log::Line("ERROR: Hotkey poller failed to start");
        return false;
    }

    m_started = true;
    return true;
}

void Hotkeys::Stop() {
    if (!m_started) return;
    m_poller.Stop();
    m_started = false;
}

} // namespace ControlHT
