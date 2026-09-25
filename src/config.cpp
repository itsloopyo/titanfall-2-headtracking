#include "config.h"

#include <Windows.h>
#include <filesystem>

#include "cameraunlock/config/ini_reader.h"
#include "debug_log.h"
#include "legacy_config/legacy_config.h"

namespace headtracking {

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
    w.WriteBlankLine();
    w.WriteSection("Debug");
    w.WriteComment(" Per-frame [view] diagnostics. The lifecycle lines - game build, profile");
    w.WriteComment(" match, hook install, map gate, crash report - are always written.");
    w.WriteBool("LogToFile", defaults::kLogToFile);
    w.WriteComment(" One-shot render view field dump, for rederiving offsets on a new build");
    w.WriteBool("DumpViewSetup", defaults::kDumpViewSetup);
}

Config Config::LoadOrCreateDefault() {
    const std::string path = IniPath();
    if (!std::filesystem::exists(path)) {
        WriteDefault(path);
    }

    legacy::Config read;
    legacy::Read(path.c_str(), read);

    Config c;
    c.port = read.port;
    c.enabled_on_startup = read.enabled_on_startup;
    c.sens_yaw = read.sens_yaw;
    c.sens_pitch = read.sens_pitch;
    c.sens_roll = read.sens_roll;
    c.invert_yaw = read.invert_yaw;
    c.invert_pitch = read.invert_pitch;
    c.invert_roll = read.invert_roll;
    c.local_smoothing = read.local_smoothing;
    c.remote_smoothing = read.remote_smoothing;
    c.deadzone_yaw = read.deadzone_yaw;
    c.deadzone_pitch = read.deadzone_pitch;
    c.deadzone_roll = read.deadzone_roll;
    c.pos_enabled = read.pos_enabled;
    c.pos_sens_x = read.pos_sens_x;
    c.pos_sens_y = read.pos_sens_y;
    c.pos_sens_z = read.pos_sens_z;
    c.pos_invert_x = read.pos_invert_x;
    c.pos_invert_y = read.pos_invert_y;
    c.pos_invert_z = read.pos_invert_z;
    c.pos_limit_x = read.pos_limit_x;
    c.pos_limit_y = read.pos_limit_y;
    c.pos_limit_z = read.pos_limit_z;
    c.pos_limit_z_back = read.pos_limit_z_back;
    c.pos_world_scale = read.pos_world_scale;
    c.fov_override_degrees = read.fov_override_degrees;
    c.cull_fov_scale = read.cull_fov_scale;
    c.move_crosshair = read.move_crosshair;
    c.toggle_vk = read.toggle_vk;
    c.yaw_mode_vk = read.yaw_mode_vk;
    c.mode_cycle_vk = read.mode_cycle_vk;
    c.world_space_yaw = read.world_space_yaw;
    c.log_to_file = read.log_to_file;
    c.dump_view_setup = read.dump_view_setup;
    return c;
}

}  // namespace headtracking
