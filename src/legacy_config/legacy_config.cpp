#include "legacy_config.h"

#include <cmath>
#include <string>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/logging/file_log.h"

namespace headtracking::legacy {

namespace {

void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    cameraunlock::logging::Line(
        "Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

float Bounded(float value, float fallback, float lo, float hi) {
    if (!std::isfinite(value)) return fallback;
    return value < lo ? lo : (value > hi ? hi : value);
}

}  // namespace

ReadStatus Read(const char* iniPath, Config& c) {
    const std::string path = iniPath;

    cameraunlock::IniReader r;
    if (!r.Open(path)) {
        cameraunlock::logging::Line("[config] could not open %s, using defaults", path.c_str());
        return ReadStatus::NotOpened;
    }

    const int port = r.ReadInt("Network", "Port", defaults::kPort);
    c.port = (port < limits::kMinPort || port > limits::kMaxPort)
                 ? defaults::kPort
                 : static_cast<uint16_t>(port);
    c.enabled_on_startup = r.ReadBool("Network", "EnableOnStartup", defaults::kEnableOnStartup);

    auto sensitivity = [&r](const char* key) {
        return Bounded(r.ReadFloat("Sensitivity", key, defaults::kSensitivity),
                       defaults::kSensitivity, limits::kMinSensitivity, limits::kMaxSensitivity);
    };
    c.sens_yaw   = sensitivity("Yaw");
    c.sens_pitch = sensitivity("Pitch");
    c.sens_roll  = sensitivity("Roll");
    c.invert_yaw   = r.ReadBool("Sensitivity", "InvertYaw",   defaults::kInvert);
    c.invert_pitch = r.ReadBool("Sensitivity", "InvertPitch", defaults::kInvert);
    c.invert_roll  = r.ReadBool("Sensitivity", "InvertRoll",  defaults::kInvert);

    auto smoothing = [](float v, float fallback) {
        if (!std::isfinite(v) || v < 0.0f) return fallback;
        return v > limits::kMaxSmoothing ? limits::kMaxSmoothing : v;
    };
    c.local_smoothing = smoothing(
        r.ReadFloat("Smoothing", "LocalSmoothing", defaults::kLocalSmoothing),
        defaults::kLocalSmoothing);
    c.remote_smoothing = smoothing(
        r.ReadFloat("Smoothing", "RemoteSmoothing", defaults::kRemoteSmoothing),
        defaults::kRemoteSmoothing);
    WarnRetiredSmoothingKey(r, "Smoothing", "Amount");
    WarnRetiredSmoothingKey(r, "Position", "Smoothing");

    auto deadzone = [&r](const char* key) {
        const float v = r.ReadFloat("Deadzone", key, defaults::kDeadzone);
        return (std::isfinite(v) && v > 0.0f) ? v : defaults::kDeadzone;
    };
    c.deadzone_yaw   = deadzone("Yaw");
    c.deadzone_pitch = deadzone("Pitch");
    c.deadzone_roll  = deadzone("Roll");

    c.pos_enabled = r.ReadBool("Position", "Enabled", defaults::kPositionEnabled);
    c.pos_world_scale = Bounded(r.ReadFloat("Position", "WorldScale", defaults::kWorldScale),
                                defaults::kWorldScale,
                                limits::kMinWorldScale, limits::kMaxWorldScale);

    auto posSensitivity = [&r](const char* key) {
        return Bounded(r.ReadFloat("Position", key, defaults::kPositionSensitivity),
                       defaults::kPositionSensitivity,
                       limits::kMinPositionSensitivity, limits::kMaxPositionSensitivity);
    };
    c.pos_sens_x = posSensitivity("SensX");
    c.pos_sens_y = posSensitivity("SensY");
    c.pos_sens_z = posSensitivity("SensZ");
    c.pos_invert_x = r.ReadBool("Position", "InvertX", defaults::kPositionInvertX);
    c.pos_invert_y = r.ReadBool("Position", "InvertY", defaults::kPositionInvertY);
    c.pos_invert_z = r.ReadBool("Position", "InvertZ", defaults::kPositionInvertZ);

    auto positionLimit = [&r](const char* key, float fallback) {
        return Bounded(r.ReadFloat("Position", key, fallback), fallback,
                       limits::kMinPositionLimit, limits::kMaxPositionLimit);
    };
    c.pos_limit_x      = positionLimit("LimitX",     defaults::kLimitX);
    c.pos_limit_y      = positionLimit("LimitY",     defaults::kLimitY);
    c.pos_limit_z      = positionLimit("LimitZ",     defaults::kLimitZ);
    c.pos_limit_z_back = positionLimit("LimitZBack", defaults::kLimitZBack);

    c.toggle_vk     = r.ReadHex("Hotkeys", "Toggle",    defaults::kToggleVk);
    c.yaw_mode_vk   = r.ReadHex("Hotkeys", "YawMode",   defaults::kYawModeVk);
    c.mode_cycle_vk = r.ReadHex("Hotkeys", "ModeCycle", defaults::kModeCycleVk);

    c.world_space_yaw = r.ReadBool("View", "WorldSpaceYaw", defaults::kWorldSpaceYaw);
    const float fov = r.ReadFloat("View", "FieldOfView", defaults::kFieldOfView);
    c.fov_override_degrees = (std::isfinite(fov) && fov > 0.0f) ? fov : defaults::kFieldOfView;
    c.cull_fov_scale = Bounded(r.ReadFloat("View", "CullFovScale", defaults::kCullFovScale),
                               defaults::kCullFovScale,
                               limits::kMinCullFovScale, limits::kMaxCullFovScale);

    c.move_crosshair = r.ReadBool("View", "MoveCrosshair", defaults::kMoveCrosshair);

    c.log_to_file = r.ReadBool("Debug", "LogToFile", defaults::kLogToFile);
    c.dump_view_setup = r.ReadBool("Debug", "DumpViewSetup", defaults::kDumpViewSetup);

    return ReadStatus::Read;
}

}  // namespace headtracking::legacy
