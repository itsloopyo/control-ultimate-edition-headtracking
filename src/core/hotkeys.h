#pragma once

#include "config.h"

#include <cameraunlock/input/hotkey_poller.h>

namespace ControlHT {

// Nav-cluster hotkeys, their Ctrl+Shift chord aliases (AGENTS.md "Chord
// Alternatives"), and the F9/F10 camera-discovery diagnostics, all polled
// on one core HotkeyPoller thread (~60Hz).
class Hotkeys {
public:
    bool Start(const Config& cfg);
    void Stop();

private:
    cameraunlock::input::HotkeyPoller m_poller;
    bool m_started = false;
};

} // namespace ControlHT
