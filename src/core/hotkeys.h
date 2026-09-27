#pragma once

#include "config.h"

#include <cameraunlock/input/hotkey_poller.h>

namespace ControlHT {

// The three key lists from CameraUnlock.ini, chords included, all polled on
// one core HotkeyPoller thread (~60Hz).
class Hotkeys {
public:
    bool Start(const Config& cfg);
    void Stop();

private:
    cameraunlock::input::HotkeyPoller m_poller;
    bool m_started = false;
};

} // namespace ControlHT
