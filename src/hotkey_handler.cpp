#include "hotkey_handler.h"
#include "plugin.h"
#include "debug_log.h"

#include <Windows.h>
#include <functional>

#include "cameraunlock/input/chord_hotkeys.h"

namespace headtracking {

namespace {
// Ctrl+Shift chord letters per the shared T/Y/U/G/H/J cluster convention:
// Y = toggle tracking, G = mode cycle, H = yaw mode, U = ADS mode.
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;
constexpr int kVkU = 0x55;

// The poller reads global key state, so without this every hotkey also fires
// while the player is alt-tabbed. Both key sets collide with everyday shortcuts
// elsewhere - End/PageUp/PageDown are editor navigation, Ctrl+Shift+Y reopens a
// closed browser tab - and silently toggling tracking from another app reads as
// the mod being flaky.
bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

template <typename F>
std::function<void()> FocusGuarded(F action) {
    return [action]() { if (GameHasFocus()) action(); };
}
}  // namespace

void HotkeyHandler::Start(Plugin& plugin, int toggle_vk, int yaw_mode_vk,
                          int mode_cycle_vk, int ads_mode_vk) {
    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    // The mode cycle only REQUESTS here; the plugin applies it on the render
    // thread, so what it did is logged there rather than the moment the key went
    // down.
    const auto modeCycle = [&plugin]() { plugin.CycleTrackingMode(); };
    const auto toggle = [&plugin]() {
        plugin.ToggleEnabled();
        HT_LOG("[hotkey] toggle -> %s", plugin.IsEnabled() ? "on" : "off");
    };
    const auto yawMode = [&plugin]() { plugin.ToggleYawMode(); };
    const auto adsMode = [&plugin]() { plugin.CycleAdsMode(); };

    m_poller.SetToggleKey(toggle_vk, FocusGuarded(NavGuarded(toggle)));
    m_poller.AddHotkey(yaw_mode_vk, FocusGuarded(NavGuarded(yawMode)));
    m_poller.AddHotkey(mode_cycle_vk, FocusGuarded(NavGuarded(modeCycle)));
    m_poller.AddHotkey(ads_mode_vk, FocusGuarded(NavGuarded(adsMode)));

    m_poller.AddHotkey(kVkY, FocusGuarded(ChordGuarded(toggle)));
    m_poller.AddHotkey(kVkH, FocusGuarded(ChordGuarded(yawMode)));
    m_poller.AddHotkey(kVkG, FocusGuarded(ChordGuarded(modeCycle)));
    m_poller.AddHotkey(kVkU, FocusGuarded(ChordGuarded(adsMode)));

    m_poller.Start(16);
}

}  // namespace headtracking
