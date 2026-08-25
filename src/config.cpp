#include "config.h"

#include <Windows.h>
#include <cmath>
#include <filesystem>

#include "cameraunlock/config/ini_reader.h"
#include "debug_log.h"

namespace headtracking {

namespace {

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    HT_LOG(
        "Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

// The one shape every numeric key is read through: a non-finite value falls
// back to the default, anything else is clamped into range. See the `limits`
// block in config.h for why nothing may reach the render view unbounded.
float Bounded(float value, float fallback, float lo, float hi) {
    if (!std::isfinite(value)) return fallback;
    return value < lo ? lo : (value > hi ? hi : value);
}

}  // namespace

// The INI lives next to the game EXE, and every reader that touches it is an
// ANSI one - IniReader is built on GetPrivateProfileStringA / GetFileAttributesA
// - so the path has to be handed over as ANSI-code-page bytes whatever this
// function does internally.
//
// Which makes the encoding a real question rather than a formality. Taking the
// EXE path from GetModuleFileNameW and narrowing it through
// std::filesystem::path::string() does NOT dodge it: that conversion is the
// active code page too (and UTF-8 if the game's CRT has set a UTF-8 locale,
// which does not match the A-family functions either). A Steam library under a
// folder the code page cannot spell - Cyrillic on an English install, say -
// therefore ends up with '?' where those characters were, the file is looked for
// somewhere that does not exist, and every setting the user wrote is discarded
// in favour of the compiled-in defaults.
//
// It cannot be made to work from here; the reader would have to be wide. What it
// must not do is fail quietly, so the lossy conversion is detected and named.
// The '?'-substituted path is returned anyway, so the existing "could not open"
// line still points at what was tried.
std::string Config::IniPath() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "HeadTracking.ini";
    const std::filesystem::path exe(std::wstring(buf, len));
    const std::wstring wide = (exe.parent_path() / L"HeadTracking.ini").wstring();

    // Best-fit mapping has to be OFF, or the detection below never fires: it
    // substitutes a similar ASCII letter and does NOT report a default character,
    // so the path quietly becomes a different, non-existent one. It never helps -
    // the folder on disk is spelled the way it is spelled.
    //
    // Both that flag and lpUsedDefaultChar are rejected outright for CP_UTF8,
    // which the ANSI code page genuinely is on a system with the UTF-8 option
    // turned on - and there the conversion is lossless, so neither is wanted.
    const bool utf8 = GetACP() == CP_UTF8;
    const DWORD flags = utf8 ? 0u : WC_NO_BEST_FIT_CHARS;
    BOOL lossy = FALSE;
    BOOL* const lossyOut = utf8 ? nullptr : &lossy;

    const int needed = WideCharToMultiByte(CP_ACP, flags, wide.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return "HeadTracking.ini";
    std::string narrow(static_cast<size_t>(needed) - 1, ' ');
    if (WideCharToMultiByte(CP_ACP, flags, wide.c_str(), -1, narrow.data(), needed,
                            nullptr, lossyOut) <= 0) {
        return "HeadTracking.ini";
    }
    if (lossy) {
        HT_LOG("[config] the game folder contains characters this system's ANSI code page "
               "cannot represent, and HeadTracking.ini is read through the ANSI file API - so "
               "it cannot be read or written there and the built-in defaults are used. Move the "
               "game to a folder whose name is plain ASCII, or switch Windows to UTF-8 "
               "(Region -> Administrative -> Beta: use Unicode UTF-8 worldwide). Tried: %s",
               narrow.c_str());
    }
    return narrow;
}

void Config::WriteDefault(const std::string& path) {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) {
        HT_LOG("[config] failed to write default ini at %s", path.c_str());
        return;
    }
    w.WriteComment(" Titanfall 2 head tracking - default config");
    w.WriteBlankLine();
    w.WriteSection("Network");
    w.WriteInt("Port", defaults::kPort);
    w.WriteBool("EnableOnStartup", defaults::kEnableOnStartup);
    w.WriteBlankLine();
    w.WriteSection("Sensitivity");
    w.WriteDouble("Yaw", defaults::kSensitivity);
    w.WriteDouble("Pitch", defaults::kSensitivity);
    w.WriteDouble("Roll", defaults::kSensitivity);
    w.WriteBool("InvertYaw", defaults::kInvert);
    w.WriteBool("InvertPitch", defaults::kInvert);
    w.WriteBool("InvertRoll", defaults::kInvert);
    w.WriteBlankLine();
    w.WriteSection("Smoothing");
    w.WriteComment(" Smoothing applied when the tracker runs on this machine (loopback).");
    w.WriteComment(" 0 = no smoothing, 1 = heavy. Covers rotation and position.");
    w.WriteDouble("LocalSmoothing", defaults::kLocalSmoothing);
    w.WriteComment(" Smoothing applied when the tracker is a remote device on the network.");
    w.WriteComment(" 0 = no smoothing, 1 = heavy. Covers rotation and position.");
    w.WriteDouble("RemoteSmoothing", defaults::kRemoteSmoothing);
    w.WriteBlankLine();
    w.WriteSection("Deadzone");
    w.WriteDouble("Yaw", defaults::kDeadzone);
    w.WriteDouble("Pitch", defaults::kDeadzone);
    w.WriteDouble("Roll", defaults::kDeadzone);
    w.WriteBlankLine();
    w.WriteSection("Position");
    w.WriteComment(" 6DOF head position, applied to the render view origin only");
    w.WriteBool("Enabled", defaults::kPositionEnabled);
    w.WriteComment(" WorldScale = Source units per metre of head movement (1 unit = 1 inch; 39.37 = 1:1)");
    w.WriteDouble("WorldScale", defaults::kWorldScale);
    w.WriteDouble("SensX", defaults::kPositionSensitivity);
    w.WriteDouble("SensY", defaults::kPositionSensitivity);
    w.WriteDouble("SensZ", defaults::kPositionSensitivity);
    w.WriteComment(" X and Z are inverted by default: the trackers this mod is used with send");
    w.WriteComment(" sideways and forward the other way round from the mod's own frame. Set them");
    w.WriteComment(" to 0 if leaning moves the camera the wrong way for yours.");
    w.WriteBool("InvertX", defaults::kPositionInvertX);
    w.WriteBool("InvertY", defaults::kPositionInvertY);
    w.WriteBool("InvertZ", defaults::kPositionInvertZ);
    w.WriteComment(" Movement envelope in metres before world scaling");
    w.WriteDouble("LimitX", defaults::kLimitX);
    w.WriteDouble("LimitY", defaults::kLimitY);
    w.WriteDouble("LimitZ", defaults::kLimitZ);
    w.WriteDouble("LimitZBack", defaults::kLimitZBack);
    w.WriteBlankLine();
    w.WriteSection("Hotkeys");
    w.WriteHex("Toggle", defaults::kToggleVk);
    w.WriteHex("YawMode", defaults::kYawModeVk);
    w.WriteComment(" Page Up: cycle 6DOF -> rotation-only -> position-only");
    w.WriteHex("ModeCycle", defaults::kModeCycleVk);
    w.WriteComment(" Insert: cycle what head tracking does while the sights are up");
    w.WriteHex("AdsMode", defaults::kAdsModeVk);
    w.WriteBlankLine();
    w.WriteSection("View");
    w.WriteComment(" true = horizon-locked yaw (default), false = camera-local yaw");
    w.WriteBool("WorldSpaceYaw", defaults::kWorldSpaceYaw);
    w.WriteComment(" Field of view in degrees, the same numbers the game's own slider uses.");
    w.WriteComment(" 0 follows that slider live. Anything else overrides it and can go outside");
    w.WriteComment(" the 70-119 the slider allows - 50 to 130 is accepted.");
    w.WriteDouble("FieldOfView", defaults::kFieldOfView);
    w.WriteComment(" How much wider than the drawn FOV the engine is told to cull. The picture is");
    w.WriteComment(" scaled back down either way; this only stops it discarding scenery your");
    w.WriteComment(" head can turn to look at. 1.0 disables it. The default asks for the widest");
    w.WriteComment(" cone the mod will cull to - about 40 degrees of head turn each side at the");
    w.WriteComment(" game's own 70 degree FOV. Lower it if the wider cone costs you frames.");
    w.WriteDouble("CullFovScale", defaults::kCullFovScale);
    w.WriteComment(" Move the game's own crosshair to where the gun is pointing. false leaves it");
    w.WriteComment(" pinned to the centre of the screen, where it marks the aim only while your");
    w.WriteComment(" head is centred.");
    w.WriteBool("MoveCrosshair", defaults::kMoveCrosshair);
    w.WriteComment(" What head tracking does while the sights are up. Cycled in game with Insert");
    w.WriteComment(" or Ctrl+Shift+U, which writes the new value back here.");
    w.WriteComment("   paused  = tracking stands down until you lower the weapon (default)");
    w.WriteComment("   marker  = tracking stays live and a white cross marks where rounds land");
    w.WriteComment("   tracked = tracking stays live with nothing drawn");
    w.WriteString("AdsMode", AdsModeValue(defaults::kAdsMode));
    w.WriteBlankLine();
    w.WriteSection("Debug");
    w.WriteComment(" Per-frame [view] diagnostics. The lifecycle lines - game build, profile");
    w.WriteComment(" match, hook install, map gate, crash report - are always written.");
    w.WriteBool("LogToFile", defaults::kLogToFile);
    w.WriteComment(" One-shot render view field dump, for rederiving offsets on a new build");
    w.WriteBool("DumpViewSetup", defaults::kDumpViewSetup);
}

// GetPrivateProfileString's writer half, which is the only one that can change
// one key of an existing file: IniWriter truncates, so writing this back through
// it would throw away every other setting and every comment. A file that is not
// there yet is created with just this section in it, and the next launch fills
// the rest in.
void Config::SaveAdsMode(AdsMode mode) {
    const std::string path = IniPath();
    if (!WritePrivateProfileStringA("View", "AdsMode", AdsModeValue(mode), path.c_str())) {
        HT_LOG("[config] could not save AdsMode to %s (error %lu) - the setting applies for this "
               "session but will not survive a restart", path.c_str(), GetLastError());
    }
}

Config Config::LoadOrCreateDefault() {
    const std::string path = IniPath();
    if (!std::filesystem::exists(path)) {
        WriteDefault(path);
    }

    cameraunlock::IniReader r;
    Config c;
    if (!r.Open(path)) {
        HT_LOG("[config] could not open %s, using defaults", path.c_str());
        return c;
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

    // Negative smoothing falls back rather than clamping to zero: it is a
    // typo, not a request for the lightest setting, and silently reading it as
    // one hides the typo.
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

    // A negative deadzone is meaningless rather than merely out of range, so it
    // reads as "no deadzone" - the same answer the key's absence gives.
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
    c.ads_mode_vk   = r.ReadHex("Hotkeys", "AdsMode",   defaults::kAdsModeVk);

    c.world_space_yaw = r.ReadBool("View", "WorldSpaceYaw", defaults::kWorldSpaceYaw);
    // 0 means "follow the game's own setting" and is the only value below the
    // accepted range that means anything, so it is passed through rather than
    // clamped up. FovControl clamps the rest.
    const float fov = r.ReadFloat("View", "FieldOfView", defaults::kFieldOfView);
    c.fov_override_degrees = (std::isfinite(fov) && fov > 0.0f) ? fov : defaults::kFieldOfView;
    c.cull_fov_scale = Bounded(r.ReadFloat("View", "CullFovScale", defaults::kCullFovScale),
                               defaults::kCullFovScale,
                               limits::kMinCullFovScale, limits::kMaxCullFovScale);

    c.move_crosshair = r.ReadBool("View", "MoveCrosshair", defaults::kMoveCrosshair);
    // An absent key gives the default, and so does anything that is not one of
    // the three values - a typo, or a mode renamed since an older release wrote
    // this file. Falling back is the migration path; falling through to whichever
    // branch happens to be last would hand the player head tracking through their
    // sights that they never asked for.
    c.ads_mode = ParseAdsMode(
        r.ReadString("View", "AdsMode", AdsModeValue(defaults::kAdsMode)).c_str());

    c.log_to_file = r.ReadBool("Debug", "LogToFile", defaults::kLogToFile);
    c.dump_view_setup = r.ReadBool("Debug", "DumpViewSetup", defaults::kDumpViewSetup);

    return c;
}

}  // namespace headtracking
