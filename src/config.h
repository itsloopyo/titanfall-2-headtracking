#pragma once

#include <cstdint>
#include <string>

#include "ads.h"

namespace headtracking {

// Every default value, once.
//
// They used to be written out three times over - as member initialisers below,
// as the literals WriteDefault puts in a fresh INI, and again as the fallbacks
// LoadOrCreateDefault passes to each Read call. Three copies of a number that
// has to agree with itself: let one drift and the INI a user is handed
// documents a default the loader does not actually use, which is invisible
// until someone deletes the key and gets different behaviour from someone who
// never had it.
namespace defaults {

constexpr uint16_t kPort = 4242;
constexpr bool kEnableOnStartup = true;

constexpr float kSensitivity = 1.0f;
constexpr bool kInvert = false;

constexpr float kLocalSmoothing = 0.0f;
constexpr float kRemoteSmoothing = 0.15f;

constexpr float kDeadzone = 0.0f;

constexpr bool kPositionEnabled = true;
constexpr float kPositionSensitivity = 1.0f;
// X and Z default to INVERTED, Y does not. The trackers this mod is used with
// send sideways and forward the other way round from the shared core's frame, so
// without this a lean left moves the camera right and leaning in pulls the view
// back. This is the sanctioned place for a tracker-convention flip: the
// processor applies it before the asymmetric Z clamp, so the generous forward
// allowance stays on the physical forward lean. A tracker that already matches
// the core's frame sets both back to false.
constexpr bool kPositionInvertX = true;
constexpr bool kPositionInvertY = false;
constexpr bool kPositionInvertZ = true;
constexpr float kLimitX = 0.30f;
constexpr float kLimitY = 0.20f;
constexpr float kLimitZ = 0.40f;
constexpr float kLimitZBack = 0.10f;
// Source units per metre. 1 unit = 1 inch, so 39.37 is 1:1 with real-world
// head movement.
constexpr float kWorldScale = 39.37f;

// 0 = follow the game's own FOV slider, live.
constexpr float kFieldOfView = 0.0f;
// No widening. The culling frustum is AIMED at the head rather than stretched to
// cover it (view_angles_hook.h), so there is nothing left for a wider cone to
// buy - and widening is a bad trade on a wide monitor anyway: the frustum grows
// in TANGENT, and a 32:9 view is already so far up that curve that trebling it
// adds about 22 degrees of turn while pushing the projection past 165 degrees,
// where the frame flickers. Raise it only if a positional lean finds the edge of
// the frame; every 0.1 above 1.0 is geometry submitted and not drawn.
constexpr float kCullFovScale = 1.0f;

constexpr int kToggleVk = 0x23;      // VK_END
constexpr int kYawModeVk = 0x22;     // VK_NEXT (Page Down)
constexpr int kModeCycleVk = 0x21;   // VK_PRIOR (Page Up)
constexpr int kAdsModeVk = 0x2D;     // VK_INSERT

// What head tracking does while the sights are up. `paused` is the mode that
// cannot be wrong, so it is what a player who never touches this gets - see
// ads_mode.h.
constexpr AdsMode kAdsMode = kDefaultAdsMode;

constexpr bool kWorldSpaceYaw = true;
constexpr bool kMoveCrosshair = true;
constexpr bool kLogToFile = false;
constexpr bool kDumpViewSetup = false;

}  // namespace defaults

// What the INI is allowed to ask for.
//
// This is not defensive decoration. The INI is the only thing between a user
// and the render view origin, and nothing downstream damps it: an out-of-range
// value goes through the world scale, into the view matrix's -dot(axis, origin)
// terms, into the view-projection and to the GPU. At 1e35 the four-term sums in
// the matrix multiply overflow to infinity; a sensitivity large enough to
// overflow makes sin/cos NaN, and NaN in the basis is a frame the driver has to
// decide what to do with.
//
// WorldScale is the one a real user gets wrong. It is documented in Source
// units per metre, so anyone who reads it as millimetres types 39370 and flings
// the camera a hundred metres out of the map on every lean, with nothing
// anywhere reporting a problem.
namespace limits {

constexpr int kMinPort = 1;
constexpr int kMaxPort = 65535;

constexpr float kMinSensitivity = 0.01f;
constexpr float kMaxSensitivity = 10.0f;

// Smoothing of exactly 1.0 is a time constant of 100 seconds, which reads as
// the view having stopped responding.
constexpr float kMaxSmoothing = 0.99f;

constexpr float kMinPositionSensitivity = 0.0f;
constexpr float kMaxPositionSensitivity = 10.0f;

// Metres of head movement. 2 m is already far past what anyone's head does and
// well past what the game's own head-bob envelope expects.
constexpr float kMinPositionLimit = 0.0f;
constexpr float kMaxPositionLimit = 2.0f;

constexpr float kMinWorldScale = 0.0f;
constexpr float kMaxWorldScale = 1000.0f;

// The engine clamps cl_fovScale at 1.7 on every READ whatever is written into
// it, so asking for more than that cannot buy any.
constexpr float kMinCullFovScale = 1.0f;
constexpr float kMaxCullFovScale = 1.7f;

}  // namespace limits

struct Config {
    uint16_t port = defaults::kPort;
    bool enabled_on_startup = defaults::kEnableOnStartup;

    float sens_yaw = defaults::kSensitivity;
    float sens_pitch = defaults::kSensitivity;
    float sens_roll = defaults::kSensitivity;
    bool invert_yaw = defaults::kInvert;
    bool invert_pitch = defaults::kInvert;
    bool invert_roll = defaults::kInvert;

    // Smoothing is picked per connection from the packet source address: a
    // tracker on this machine gets local_smoothing, a remote device on the
    // network gets remote_smoothing. Both cover rotation and position.
    float local_smoothing = defaults::kLocalSmoothing;
    float remote_smoothing = defaults::kRemoteSmoothing;

    float deadzone_yaw = defaults::kDeadzone;
    float deadzone_pitch = defaults::kDeadzone;
    float deadzone_roll = defaults::kDeadzone;

    // Positional (6DOF) tracking. Head displacement is applied to the render
    // view origin only - the game's clean camera origin is untouched, so aim /
    // raycasts / physics are unaffected. See camera_hook.cpp.
    bool  pos_enabled    = defaults::kPositionEnabled;
    float pos_sens_x     = defaults::kPositionSensitivity;
    float pos_sens_y     = defaults::kPositionSensitivity;
    float pos_sens_z     = defaults::kPositionSensitivity;
    bool  pos_invert_x   = defaults::kPositionInvertX;
    bool  pos_invert_y   = defaults::kPositionInvertY;
    bool  pos_invert_z   = defaults::kPositionInvertZ;
    // Head-movement envelope in metres (clamped before world scaling). Z is
    // asymmetric: pos_limit_z = forward lean (generous), z_back = backward.
    float pos_limit_x      = defaults::kLimitX;
    float pos_limit_y      = defaults::kLimitY;
    float pos_limit_z      = defaults::kLimitZ;
    float pos_limit_z_back = defaults::kLimitZBack;
    // Source world units per metre of head movement. Primary lean tuning knob.
    float pos_world_scale  = defaults::kWorldScale;

    // The field of view to draw at, in the same degrees the game's own slider
    // uses. 0 follows that slider, live. Anything else overrides it, including
    // outside the 70-119 degrees the slider allows: the mod holds cl_fovScale at
    // a value of its own every frame to stop the engine culling what the head
    // turns to look at, so the drawn field of view is already ours to pick.
    // See fov_control.h.
    float fov_override_degrees = defaults::kFieldOfView;

    // How much wider than the DRAWN field of view the engine is told to cull.
    // The picture is scaled back down before it is drawn, so this changes nothing
    // you can see - it only stops the engine discarding the scenery your head can
    // turn to look at. 1.0 disables it and brings the culling back.
    float cull_fov_scale = defaults::kCullFovScale;

    // Move the game's own crosshair onto the gun (crosshair_hook.h). Off leaves
    // it pinned to the centre of the screen, where it marks the aim only while
    // your head is centred.
    bool move_crosshair = defaults::kMoveCrosshair;

    int toggle_vk     = defaults::kToggleVk;
    int yaw_mode_vk   = defaults::kYawModeVk;
    int mode_cycle_vk = defaults::kModeCycleVk;  // 6DOF -> rotation -> position
    int ads_mode_vk   = defaults::kAdsModeVk;    // paused -> marker -> tracked

    // What happens while the sights are up. Cycled in game as well as set here,
    // and the cycle writes the new value back to the INI - it is the player's
    // choice and it survives a restart.
    AdsMode ads_mode = defaults::kAdsMode;

    // true  = horizon-locked yaw (yaw around world up axis, default)
    // false = camera-local yaw (yaw composed with pitch/roll)
    bool world_space_yaw = defaults::kWorldSpaceYaw;

    // Per-frame [view] diagnostics only. The lifecycle lines (build fingerprint,
    // profile match, hook install, map gate, fault report) are written
    // unconditionally - they are what a bug report needs and they happen before
    // this is read. See debug_log.h.
    bool log_to_file = defaults::kLogToFile;
    // One-shot dump of the three render view structs, for rederiving their field
    // offsets against a live frame. Diagnostic only.
    bool dump_view_setup = defaults::kDumpViewSetup;

    static std::string IniPath();  // <game folder>\HeadTracking.ini
    static Config LoadOrCreateDefault();
    static void WriteDefault(const std::string& path);

    // Writes just the ADS mode back, leaving every other key and every comment
    // in the file alone. The in-game cycle is the main way this setting gets
    // set, so discarding the choice at the end of the session would be a bug.
    static void SaveAdsMode(AdsMode mode);
};

}  // namespace headtracking
