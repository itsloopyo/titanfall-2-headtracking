#include "plugin.h"

#include <cmath>

#include "angle_units.h"
#include "build_profile.h"
#include "camera_hook.h"
#include "fov_control.h"
#include "view_angles_hook.h"
#include "game_state.h"
#include "hotkey_handler.h"
#include "ads_state.h"
#include "cockpit_hook.h"
#include "crosshair_hook.h"
#include "debug_log.h"
#include "hit_indicator.h"
#include "world_marker_hook.h"

#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace headtracking {

// Deliberately leaked, never destroyed. A function-local static would register
// ~Plugin in the CRT's onexit table, and MSVC runs that table from
// dllmain_crt_process_detach AFTER our DllMain has returned - so the careful
// "don't tear down on process exit" guard in dllmain.cpp would be undone by the
// destructor doing the teardown anyway, under the loader lock, joining threads
// ExitProcess has already killed.
Plugin& GetPlugin() {
    static Plugin* instance = new Plugin();
    return *instance;
}

Plugin::Plugin() = default;
Plugin::~Plugin() = default;

void Plugin::LoadConfig() {
    namespace cfg = cameraunlock::config;
    m_configOwner.emplace(ConfigOwnerOptionsFor(ConfigFolder(), cfg::DefaultsFile::PerUser()));
    const cfg::ConfigLoadResult<Config> loaded = m_configOwner->Load();
    for (const std::string& line : loaded.log) HT_LOG("[config] %s", line.c_str());
    HT_LOG("[config] %s", cfg::ConfigLoadStatusName(loaded.status));
    // Every status hands back the settings to run on.
    m_config = loaded.config;
}

void Plugin::SaveConfig(const std::function<void(Config&)>& change) {
    namespace cfg = cameraunlock::config;
    const cfg::ConfigSaveResult saved = m_configOwner->Save(change);
    for (const std::string& line : saved.log) HT_LOG("[config] %s", line.c_str());
    if (saved.status != cfg::ConfigSaveStatus::Saved) {
        HT_LOG("[config] save %s: %s", cfg::ConfigSaveStatusName(saved.status), saved.reason.c_str());
    }
}

void Plugin::Initialize() {
    LoadConfig();
    SetVerboseLogging(m_config.log_to_file);
    m_enabled.store(m_config.enable_on_startup);
    m_worldSpaceYaw.store(m_config.world_space_yaw);
    // The table reads a pair that names no mode as its defaults, so every loaded pair decodes.
    const cameraunlock::TrackingMode mode =
        cameraunlock::DecodeTrackingMode(m_config.rotation_enabled, m_config.position_enabled).value();
    m_session.SetMode(mode);
    m_desiredMode.store(mode);

    // The rotation processor keeps its identity sensitivity and no deadzone: the tracker
    // shapes the pose. The position processor gets the axis conversion (PositionSettingsFor).
    // Our trackers report head position directly, so the core's synthetic pivot-forward term
    // (which cancels a webcam pivot) only injects phantom rotation-coupled movement. Disable it.
    m_session.GetPositionProcessor().SetSettings(PositionSettingsFor(m_config));
    m_session.GetPositionProcessor().SetTrackerPivotForward(0.0f);
    // Both smoothing parameters cover rotation and position; the session picks
    // between them per connection from the receiver's source-address check, so
    // a switch from a local OpenTrack instance to a phone on WiFi mid-session
    // needs no restart.
    m_session.SetLocalSmoothing(m_config.local_smoothing);
    m_session.SetRemoteSmoothing(m_config.remote_smoothing);

    m_receiver.SetLog([](const std::string& msg) {
        HT_LOG("[receiver] %s", msg.c_str());
    });
    const auto port = static_cast<uint16_t>(m_config.udp_port);
    if (m_receiver.Start(port)) {
        HT_LOG("[plugin] listening on UDP %u", port);
    } else {
        HT_LOG("[plugin] UDP port %u busy, receiver will retry in background", port);
    }

    // Nothing may touch game memory until a build profile has been selected -
    // and nothing may hook at all under Northstar, whose servers can host
    // campaign maps as multiplayer arenas (see game_state.h).
    //
    // Northstar is asked about AFTER SelectProfile, not before: this runs from
    // DLL_PROCESS_ATTACH, where Northstar's proxy DLL and ours are still racing
    // each other, and asking first answered "no" for a Northstar that simply had
    // not loaded yet. SelectProfile waits for client.dll and engine.dll, so by
    // the time it returns the game has loaded its own modules and Northstar is
    // either in the process or it is not. Refusing to install is stronger than
    // refusing to apply, so this is worth waiting for - and CurrentSession()
    // keeps asking while the game runs, for the case that still slips past.
    if (!SelectProfile()) {
        HT_LOG("[plugin] unrecognised game build - mod is dormant, game runs vanilla");
    } else if (NorthstarPresent()) {
        HT_LOG("[plugin] Northstar detected - not hooking, the game runs vanilla. Northstar is "
               "a multiplayer client and its servers can host campaign maps, so a map-name "
               "check cannot tell a campaign apart from a match.");
    } else {
        m_cameraHook = std::make_unique<CameraHook>();
        if (!m_cameraHook->Install()) {
            HT_LOG("[plugin] camera hook not installed - mod is dormant (view unmodified)");
        }
        if (!InstallViewAnglesHook()) {
            HT_LOG("[plugin] the head rotation has nowhere to go - the render-phase hook is what "
                   "puts it into the angles the frame is built from. The mod will lean but not "
                   "turn. Please report this log.");
        }
        // Titans only. Without it the cockpit stays put in the world while the
        // view turns inside it, so head tracking in a Titan looks around the
        // inside of the cockpit instead of through it (cockpit_hook.h). A
        // failure here costs Titan sections and nothing else, so it does not
        // gate the rest.
        InstallCockpitHook();
        // The HUD's world-anchored marks - the BT-7274 marker, objective
        // waypoints - are placed by the game's own world-to-screen, which
        // projects with the clean camera and so pins them to the glass while the
        // world turns under them (world_marker_hook.h). A failure here costs
        // those marks and nothing else.
        InstallWorldMarkerHook();
        GetFovControl().Initialize(m_config.fov_override_degrees, m_config.cull_fov_scale);
        InstallAdsStateHook();
        InstallCrosshairHook();
        // The hit indicator is the game's, not ours, and it belongs on the same
        // mark as the crosshair.
        InstallHitIndicatorHook();
    }

    m_hotkeys = std::make_unique<HotkeyHandler>();
    m_hotkeys->Start(*this, m_config);
    HT_LOG("[plugin] initialized");
}

// Called from the hotkey thread. The next mode is computed from the one the
// render thread last applied, so two presses before one frame are one step; it
// is stored as the desired mode for Update() to apply on the render thread (see
// plugin.h), and saved here, off the render thread.
void Plugin::CycleTrackingMode() {
    const auto next = static_cast<cameraunlock::TrackingMode>((static_cast<int>(m_session.GetMode()) + 1) % 3);
    m_desiredMode.store(next);
    m_modeApplyRequested.store(true, std::memory_order_release);

    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(next);
    SaveConfig([channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

// Yaw mode is a plain atomic the render thread reads once per frame, so the
// hotkey thread can flip it directly.
void Plugin::ToggleYawMode() {
    const bool next = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(next);
    HT_LOG("[plugin] yaw mode -> %s", next ? "world-space" : "camera-local");
    SaveConfig([next](Config& c) { c.world_space_yaw = next; });
}

void Plugin::ConsumeHotkeyRequests() {
    if (m_modeApplyRequested.exchange(false, std::memory_order_acquire)) {
        m_session.SetMode(m_desiredMode.load());
        HT_LOG("[plugin] tracking mode -> %s", TrackingModeName());
    }
}

const char* Plugin::TrackingModeName() const {
    switch (m_session.GetMode()) {
        case cameraunlock::TrackingMode::RotationAndPosition: return "6DOF (rotation + position)";
        case cameraunlock::TrackingMode::RotationOnly:        return "rotation only";
        case cameraunlock::TrackingMode::PositionOnly:        return "position only";
    }
    return "?";
}

void Plugin::Invalidate() {
    m_cachedValid.store(false, std::memory_order_release);
    m_cachedPosValid.store(false, std::memory_order_release);
}

// Tracking loss must not snap the view back to centre. The receiver already
// holds the last pose for its connection timeout; past that, the held pose
// eases out over a few hundred milliseconds. A Wi-Fi burst on a phone tracker
// is otherwise a jump to centre and a jump back, which reads far worse than the
// frozen view it replaces.
bool Plugin::HoldThroughLoss(float dt) {
    if (!m_cachedValid.load(std::memory_order_acquire)) return false;

    if (!m_holding) {
        m_holding = true;
        m_holdYaw   = m_cachedYaw.load(std::memory_order_acquire);
        m_holdPitch = m_cachedPitch.load(std::memory_order_acquire);
        m_holdRoll  = m_cachedRoll.load(std::memory_order_acquire);
        m_holdPosX  = m_cachedPosX.load(std::memory_order_acquire);
        m_holdPosY  = m_cachedPosY.load(std::memory_order_acquire);
        m_holdPosZ  = m_cachedPosZ.load(std::memory_order_acquire);
    }
    m_lossSeconds += dt;

    const float scale = std::exp(-kLossFadeSpeed * m_lossSeconds);
    if (scale < 0.01f) {
        Invalidate();
        // The view is now at centre while the player's head is wherever it is.
        // Publishing the full pose on the first packet back would snap it there
        // in one frame, and with LocalSmoothing at its 0.0 default there is
        // nothing downstream to soften that - so the fade-out would have bought
        // a worse jump than the one it was added to prevent. Blending back in
        // over the same time constant is the other half of the same easing.
        m_resumeSeconds = 0.0f;
        return false;
    }
    m_cachedYaw.store(m_holdYaw     * scale, std::memory_order_release);
    m_cachedPitch.store(m_holdPitch * scale, std::memory_order_release);
    m_cachedRoll.store(m_holdRoll   * scale, std::memory_order_release);
    m_cachedPosX.store(m_holdPosX   * scale, std::memory_order_release);
    m_cachedPosY.store(m_holdPosY   * scale, std::memory_order_release);
    m_cachedPosZ.store(m_holdPosZ   * scale, std::memory_order_release);
    return true;
}

bool Plugin::Update() {
    // Ticked unconditionally so the fade below advances on real time whichever
    // branch the frame takes.
    const float dt = m_frameClock.Tick();

    // Applied here, on the render thread, rather than where the key was pressed.
    ConsumeHotkeyRequests();

    if (!m_enabled.load()) {
        Invalidate();
        m_holding = false;
        m_lossSeconds = 0.0f;
        m_resumeSeconds = kResumeBlendComplete;
        return false;
    }

    static bool s_loggedConnected = false;
    if (!m_receiver.IsReceiving()) {
        if (s_loggedConnected) {
            HT_LOG("[plugin] tracking source disconnected (no packets within timeout)");
            s_loggedConnected = false;
        }
        return HoldThroughLoss(dt);
    }
    if (!s_loggedConnected) {
        HT_LOG("[plugin] tracking source connected (remote=%d)",
               m_receiver.IsRemoteConnection() ? 1 : 0);
        s_loggedConnected = true;
    }
    m_holding = false;
    m_lossSeconds = 0.0f;

    if (!m_session.Update(dt)) {
        Invalidate();
        return false;
    }

    m_resumeSeconds += dt;
    const float blend = (m_resumeSeconds >= kResumeBlendComplete)
                            ? 1.0f
                            : 1.0f - std::exp(-kLossFadeSpeed * m_resumeSeconds);

    float yaw_deg = 0.0f, pitch_deg = 0.0f, roll_deg = 0.0f;
    m_session.GetRotation(yaw_deg, pitch_deg, roll_deg);
    m_cachedYaw.store(yaw_deg     * kDegToRad * blend, std::memory_order_release);
    m_cachedPitch.store(pitch_deg * kDegToRad * blend, std::memory_order_release);
    m_cachedRoll.store(roll_deg   * kDegToRad * blend, std::memory_order_release);
    m_cachedValid.store(true, std::memory_order_release);

    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    if (m_session.GetPositionOffset(ox, oy, oz)) {
        m_cachedPosX.store(ox * kSourceUnitsPerMetre * blend, std::memory_order_release);
        m_cachedPosY.store(oy * kSourceUnitsPerMetre * blend, std::memory_order_release);
        m_cachedPosZ.store(oz * kSourceUnitsPerMetre * blend, std::memory_order_release);
        m_cachedPosValid.store(true, std::memory_order_release);
    } else {
        m_cachedPosValid.store(false, std::memory_order_release);
    }
    return true;
}

bool Plugin::GetRotationRadians(float& yaw, float& pitch, float& roll) const {
    if (!m_cachedValid.load(std::memory_order_acquire)) return false;
    yaw   = m_cachedYaw.load(std::memory_order_acquire);
    pitch = m_cachedPitch.load(std::memory_order_acquire);
    roll  = m_cachedRoll.load(std::memory_order_acquire);
    return true;
}

bool Plugin::GetPositionOffset(float& x, float& y, float& z) const {
    if (!m_cachedPosValid.load(std::memory_order_acquire)) return false;
    x = m_cachedPosX.load(std::memory_order_acquire);
    y = m_cachedPosY.load(std::memory_order_acquire);
    z = m_cachedPosZ.load(std::memory_order_acquire);
    return true;
}

}  // namespace headtracking
