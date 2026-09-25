#pragma once

#include <cstdint>

// The HeadTracking.ini reader as the last build before the canonical config format
// (cc165ee) ran it, frozen so an old file converts exactly as that build read it. Never
// edited: a change here changes what a player's old file means.
//
// The defaults and limits are literals rather than core's constants, which is what those
// constants held at cameraunlock-core 10789ea, so a later core default cannot move what an
// old file without the key means.

namespace headtracking::legacy {

namespace defaults {

constexpr uint16_t kPort = 4242;
constexpr bool kEnableOnStartup = true;

constexpr float kSensitivity = 1.0f;
constexpr bool kInvert = false;

constexpr float kLocalSmoothing = static_cast<float>(0.0);
constexpr float kRemoteSmoothing = static_cast<float>(0.15);

constexpr float kDeadzone = 0.0f;

constexpr bool kPositionEnabled = true;
constexpr float kPositionSensitivity = 1.0f;
constexpr bool kPositionInvertX = true;
constexpr bool kPositionInvertY = false;
constexpr bool kPositionInvertZ = true;
constexpr float kLimitX = 0.30f;
constexpr float kLimitY = 0.20f;
constexpr float kLimitZ = 0.40f;
constexpr float kLimitZBack = 0.10f;
constexpr float kWorldScale = 39.37f;

constexpr float kFieldOfView = 0.0f;
constexpr float kCullFovScale = 1.0f;

constexpr int kToggleVk = 0x23;
constexpr int kYawModeVk = 0x22;
constexpr int kModeCycleVk = 0x21;

constexpr bool kWorldSpaceYaw = true;
constexpr bool kMoveCrosshair = true;
constexpr bool kLogToFile = false;
constexpr bool kDumpViewSetup = false;

}  // namespace defaults

namespace limits {

constexpr int kMinPort = 1;
constexpr int kMaxPort = 65535;

constexpr float kMinSensitivity = 0.01f;
constexpr float kMaxSensitivity = 10.0f;

constexpr float kMaxSmoothing = 0.99f;

constexpr float kMinPositionSensitivity = 0.0f;
constexpr float kMaxPositionSensitivity = 10.0f;

constexpr float kMinPositionLimit = 0.0f;
constexpr float kMaxPositionLimit = 2.0f;

constexpr float kMinWorldScale = 0.0f;
constexpr float kMaxWorldScale = 1000.0f;

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

    float local_smoothing = defaults::kLocalSmoothing;
    float remote_smoothing = defaults::kRemoteSmoothing;

    float deadzone_yaw = defaults::kDeadzone;
    float deadzone_pitch = defaults::kDeadzone;
    float deadzone_roll = defaults::kDeadzone;

    bool  pos_enabled    = defaults::kPositionEnabled;
    float pos_sens_x     = defaults::kPositionSensitivity;
    float pos_sens_y     = defaults::kPositionSensitivity;
    float pos_sens_z     = defaults::kPositionSensitivity;
    bool  pos_invert_x   = defaults::kPositionInvertX;
    bool  pos_invert_y   = defaults::kPositionInvertY;
    bool  pos_invert_z   = defaults::kPositionInvertZ;
    float pos_limit_x      = defaults::kLimitX;
    float pos_limit_y      = defaults::kLimitY;
    float pos_limit_z      = defaults::kLimitZ;
    float pos_limit_z_back = defaults::kLimitZBack;
    float pos_world_scale  = defaults::kWorldScale;

    float fov_override_degrees = defaults::kFieldOfView;
    float cull_fov_scale = defaults::kCullFovScale;
    bool move_crosshair = defaults::kMoveCrosshair;

    int toggle_vk     = defaults::kToggleVk;
    int yaw_mode_vk   = defaults::kYawModeVk;
    int mode_cycle_vk = defaults::kModeCycleVk;

    bool world_space_yaw = defaults::kWorldSpaceYaw;

    bool log_to_file = defaults::kLogToFile;
    bool dump_view_setup = defaults::kDumpViewSetup;
};

enum class ReadStatus {
    // The file was read into the Config.
    Read,
    // IniReader could not open the path, which it does only when the path does not name a
    // file. The build logged it and ran on the defaults, which the Config holds.
    NotOpened,
};

// Reads the file at the ANSI path through GetPrivateProfileStringA, as the build did.
// Writes nothing. Logs through cameraunlock::logging as the build did.
ReadStatus Read(const char* iniPath, Config& cfg);

}  // namespace headtracking::legacy
