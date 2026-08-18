#include "pch.h"
#include "hotkeys.h"
#include "mod.h"
#include "logging.h"

#include <cameraunlock/input/chord_hotkeys.h>

namespace ControlHT {

bool Hotkeys::Start(const Config& cfg) {
    if (m_started) return true;

    using cameraunlock::input::ChordGuarded;

    // Primary nav-cluster bindings
    m_poller.AddHotkey(cfg.recenterKey, [] { Mod::Instance().Recenter(); });
    m_poller.AddHotkey(cfg.toggleKey, [] { Mod::Instance().Toggle(); });
    m_poller.AddHotkey(cfg.togglePositionKey, [] { Mod::Instance().CycleTrackingMode(); });
    m_poller.AddHotkey(cfg.toggleYawModeKey, [] { Mod::Instance().ToggleYawMode(); });

    // Chord aliases: Ctrl+Shift+T/Y/G/H
    m_poller.AddHotkey('T', ChordGuarded([] { Mod::Instance().Recenter(); }));
    m_poller.AddHotkey('Y', ChordGuarded([] { Mod::Instance().Toggle(); }));
    m_poller.AddHotkey('G', ChordGuarded([] { Mod::Instance().CycleTrackingMode(); }));
    m_poller.AddHotkey('H', ChordGuarded([] { Mod::Instance().ToggleYawMode(); }));

    if (!m_poller.Start()) {
        Log::Line("ERROR: Hotkey poller failed to start");
        return false;
    }

    Log::Line("Hotkeys ready: recenter=0x%02X toggle=0x%02X position=0x%02X yawmode=0x%02X "
              "+ Ctrl+Shift+T/Y/G/H chords",
              cfg.recenterKey, cfg.toggleKey, cfg.togglePositionKey, cfg.toggleYawModeKey);

    m_started = true;
    return true;
}

void Hotkeys::Stop() {
    if (!m_started) return;
    m_poller.Stop();
    m_started = false;
}

} // namespace ControlHT
