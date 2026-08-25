#pragma once

#include "cameraunlock/input/hotkey_poller.h"

namespace headtracking {

class Plugin;

class HotkeyHandler {
public:
    // Starts the poller thread. There is no matching Stop: the plugin that owns
    // this is deliberately leaked (see plugin.cpp), so the poller runs until the
    // process exits.
    void Start(Plugin& plugin, int toggle_vk, int yaw_mode_vk, int mode_cycle_vk,
               int ads_mode_vk);

private:
    cameraunlock::input::HotkeyPoller m_poller;
};

}  // namespace headtracking
