#pragma once

#include "cameraunlock/input/hotkey_poller.h"

namespace headtracking {

class Plugin;
struct Config;

class HotkeyHandler {
public:
    // Registers the key lists CameraUnlock.ini holds for each action, the
    // Ctrl+Shift chords among them, and starts the poller thread. There is no
    // matching Stop: the plugin that owns this is deliberately leaked (see
    // plugin.cpp), so the poller runs until the process exits.
    void Start(Plugin& plugin, const Config& config);

private:
    cameraunlock::input::HotkeyPoller m_poller;
};

}  // namespace headtracking
