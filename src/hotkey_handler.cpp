#include "hotkey_handler.h"
#include "plugin.h"
#include "debug_log.h"

#include <Windows.h>
#include <functional>
#include <stdexcept>
#include <string>

#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"

namespace headtracking {

namespace {
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

// A key list from CameraUnlock.ini onto the poller. The table only holds lists its
// hotkey codec read, so one that does not parse here is a bug, not a player's typo.
void Register(cameraunlock::input::HotkeyPoller& poller, const std::string& list, const char* key,
              std::function<void()> action) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) {
        throw std::logic_error(std::string("[Hotkeys] ") + key + "=" + list + " does not parse: " + parsed.error);
    }
    cameraunlock::input::RegisterKeyBindings(poller, parsed.bindings, FocusGuarded(std::move(action)));
}
}  // namespace

void HotkeyHandler::Start(Plugin& plugin, const Config& config) {
    // The mode cycle only REQUESTS here; the plugin applies it on the render
    // thread, so what it did is logged there rather than the moment the key went
    // down.
    const auto modeCycle = [&plugin]() { plugin.CycleTrackingMode(); };
    const auto toggle = [&plugin]() {
        plugin.ToggleEnabled();
        HT_LOG("[hotkey] toggle -> %s", plugin.IsEnabled() ? "on" : "off");
    };
    const auto yawMode = [&plugin]() { plugin.ToggleYawMode(); };

    // Each list holds every key that fires its action, the Ctrl+Shift chord
    // included, and a key without modifiers stays silent while Ctrl and Shift
    // are both held, so one press never fires two actions.
    Register(m_poller, config.toggle_key_name, "ToggleKey", toggle);
    Register(m_poller, config.cycle_tracking_mode_key_name, "CycleTrackingModeKey", modeCycle);
    Register(m_poller, config.yaw_mode_key_name, "YawModeKey", yawMode);

    m_poller.Start(16);
    HT_LOG("[hotkey] toggle=[%s] cycle tracking mode=[%s] yaw mode=[%s]", config.toggle_key_name.c_str(),
           config.cycle_tracking_mode_key_name.c_str(), config.yaw_mode_key_name.c_str());
}

}  // namespace headtracking
