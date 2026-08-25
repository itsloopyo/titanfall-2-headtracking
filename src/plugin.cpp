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
#include "crosshair_hook.h"
#include "debug_log.h"
#include "hit_indicator.h"

namespace headtracking {

namespace {

void ApplyRotationConfig(cameraunlock::TrackingProcessor& processor, const Config& c) {
    cameraunlock::SensitivitySettings s;
    s.yaw = c.sens_yaw;
    s.pitch = c.sens_pitch;
    s.roll = c.sens_roll;
    s.invert_yaw = c.invert_yaw;
    s.invert_pitch = c.invert_pitch;
    s.invert_roll = c.invert_roll;
    processor.SetSensitivity(s);

    cameraunlock::DeadzoneSettings d;
    d.yaw = c.deadzone_yaw;
    d.pitch = c.deadzone_pitch;
    d.roll = c.deadzone_roll;
    processor.SetDeadzone(d);
}

void ApplyPositionConfig(cameraunlock::PositionProcessor& processor, const Config& c) {
    cameraunlock::PositionSettings ps;
    ps.sensitivity_x = c.pos_sens_x;
    ps.sensitivity_y = c.pos_sens_y;
    ps.sensitivity_z = c.pos_sens_z;
    // The user's Invert* preferences go to the processor, which applies them
    // BEFORE its clamp - and that is the correct side, given the bound swap
    // below. `InvertZ` exists for a tracker whose z runs the other way: with it
    // set, the user's forward lean arrives as raw negative z, the processor
    // flips it to positive, and the positive bound is the generous one. Both
    // settings therefore put 0.40 m on the physical forward lean.
    //
    // Inverting AFTER the clamp instead (folding the sign into the world scale)
    // is what recreates the trap the doctrine warns about: the raw sign is what
    // the asymmetric bounds see, so `InvertZ=true` clamped the user's forward
    // lean at 0.10 m and their backward lean at 0.40 m. The direction still
    // looked right, only the travel was wrong, which is exactly why that shape
    // survives testing.
    ps.invert_x = c.pos_invert_x;
    ps.invert_y = c.pos_invert_y;
    ps.invert_z = c.pos_invert_z;
    ps.limit_x = c.pos_limit_x;
    ps.limit_y = c.pos_limit_y;
    // The two cores disagree about which sign of z is "forward". C++ clamps
    // z to [-limit_z, +limit_z_back]; the C# original it is a port of clamps to
    // [-LimitZBack, +LimitZ]. Transposed, not mirrored - with the same settings
    // one puts the generous allowance on negative z and the other on positive.
    //
    // Our tracker frame leans forward on POSITIVE z, so the generous allowance
    // has to land on the positive bound, which means handing the C++ processor
    // its two bounds the other way round. Deliberately not `invert_z`: that
    // flips BEFORE the clamp, which would fix the direction and quietly leave
    // the forward lean with the 0.10 m backward allowance.
    //
    // If the C++ core is ever reconciled with the C# original, this swap has to
    // go with it. Verified in game: a 40 cm forward lean reaches the full
    // 0.40 m (15.75 units), a 40 cm backward lean stops at 0.10 m (3.94).
    ps.limit_z      = c.pos_limit_z_back;  // negative bound: leaning back
    ps.limit_z_back = c.pos_limit_z;       // positive bound: leaning in
    processor.SetSettings(ps);
    // Our trackers report head position directly, so the core's synthetic
    // pivot-forward term (which cancels a webcam pivot) only injects phantom
    // rotation-coupled movement. Disable it.
    processor.SetTrackerPivotForward(0.0f);
}
}  // namespace

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

void Plugin::Initialize() {
    m_config = Config::LoadOrCreateDefault();
    SetVerboseLogging(m_config.log_to_file);
    m_worldScale = m_config.pos_world_scale;
    m_enabled.store(m_config.enabled_on_startup);
    m_worldSpaceYaw.store(m_config.world_space_yaw);
    // The player's choice, and never reset by start-up logic: whatever the file
    // says is what they set, from this key or from the in-game cycle.
    m_adsMode.store(m_config.ads_mode);
    HT_LOG("[ads] %s", AdsModeToast(m_config.ads_mode));
    m_session.SetMode(m_config.pos_enabled
                          ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);

    ApplyRotationConfig(m_session.GetProcessor(), m_config);
    ApplyPositionConfig(m_session.GetPositionProcessor(), m_config);
    // Both smoothing parameters cover rotation and position; the session picks
    // between them per connection from the receiver's source-address check, so
    // a switch from a local OpenTrack instance to a phone on WiFi mid-session
    // needs no restart.
    m_session.SetLocalSmoothing(m_config.local_smoothing);
    m_session.SetRemoteSmoothing(m_config.remote_smoothing);

    m_receiver.SetLog([](const std::string& msg) {
        HT_LOG("[receiver] %s", msg.c_str());
    });
    if (m_receiver.Start(m_config.port)) {
        HT_LOG("[plugin] listening on UDP %u", m_config.port);
    } else {
        HT_LOG("[plugin] UDP port %u busy, receiver will retry in background", m_config.port);
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
        GetFovControl().Initialize(m_config.fov_override_degrees, m_config.cull_fov_scale);
        InstallAdsStateHook();
        if (m_config.move_crosshair) {
            InstallCrosshairHook();
            // The hit indicator is the game's, not ours, and it belongs on the same
            // mark as the crosshair - so it follows the same setting.
            InstallHitIndicatorHook();
        }
    }

    m_hotkeys = std::make_unique<HotkeyHandler>();
    m_hotkeys->Start(*this, m_config.toggle_vk, m_config.yaw_mode_vk,
                     m_config.mode_cycle_vk, m_config.ads_mode_vk);
    HT_LOG("[plugin] initialized");
}

// Called from the hotkey thread; only raises a flag. The work happens in
// Update() on the render thread. See plugin.h.
void Plugin::CycleTrackingMode() {
    m_modeCycleRequested.store(true, std::memory_order_release);
}

// Advances the cycle, writes the choice back to the INI so it survives a
// restart, and says which mode it landed on. Safe to run straight off the hotkey
// thread, like the yaw mode and unlike the tracking-mode cycle: it reaches
// nothing the render thread is mid-way through, and the store is what the next
// frame's verdict is recomputed from.
void Plugin::CycleAdsMode() {
    const AdsMode next = NextAdsMode(m_adsMode.load(std::memory_order_acquire));
    m_adsMode.store(next, std::memory_order_release);
    Config::SaveAdsMode(next);
    HT_LOG("[ads] %s", AdsModeToast(next));
}

// Yaw mode is a plain atomic the render thread reads once per frame, so the
// hotkey thread can flip it directly.
void Plugin::ToggleYawMode() {
    const bool next = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(next);
    HT_LOG("[plugin] yaw mode -> %s", next ? "world-space" : "camera-local");
}

void Plugin::ConsumeHotkeyRequests() {
    if (m_modeCycleRequested.exchange(false, std::memory_order_acquire)) {
        m_session.CycleMode();
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
        m_cachedPosX.store(ox * m_worldScale * blend, std::memory_order_release);
        m_cachedPosY.store(oy * m_worldScale * blend, std::memory_order_release);
        m_cachedPosZ.store(oz * m_worldScale * blend, std::memory_order_release);
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
