#include "config.h"

#include <Windows.h>

#include <limits>
#include <system_error>
#include <utility>
#include <vector>

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"
#include "legacy_config/legacy_config.h"

namespace headtracking {

namespace {

namespace cfg = cameraunlock::config;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

// A legacy hotkey code and the Ctrl+Shift chord every earlier build registered beside it, as
// one key list. A code of 0 registered nothing, and a code outside 0x01-0xFE is unbound by
// normalisation N1, which records it.
std::string KeyList(int vk, char chordLetter, const char* key, std::vector<cfg::DroppedValue>& dropped) {
    const std::string code = cfg::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    const std::string chord = cameraunlock::input::FormatKeyBindings(
        {KeyBinding{KeyModifiers::kCtrl | KeyModifiers::kShift, chordLetter}});
    return code.empty() ? chord : code + ", " + chord;
}

// The working directory, ending in a separator. The published build opened a bare
// "HeadTracking.ini" when the exe path could not be read, which is this folder.
std::wstring WorkingDirectory() {
    std::wstring dir(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(dir.size()), dir.data());
        if (length == 0) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "GetCurrentDirectoryW");
        }
        const bool fits = length < dir.size();
        dir.resize(length);
        if (!fits) continue;
        if (dir.back() != L'\\' && dir.back() != L'/') dir.push_back(L'\\');
        return dir;
    }
}

}  // namespace

cfg::ConfigTable<Config> ConfigTable() {
    using C = cfg::schema::Concept;
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {C::UdpPort, C::EnableOnStartup, C::WorldSpaceYaw, C::RotationEnabled, C::LocalSmoothing,
         C::RemoteSmoothing, C::PositionEnabled, C::PositionLimitX, C::PositionLimitY, C::PositionLimitYDown,
         C::PositionLimitZ, C::PositionLimitZBack, C::ToggleKey, C::CycleTrackingModeKey, C::YawModeKey});
    table.Select(C::WorldSpaceYaw).Writable()
        .Select(C::RotationEnabled).Writable()
        .Select(C::PositionEnabled).Writable();
    table.Local("View", "FieldOfView", &Config::fov_override_degrees,
                cfg::FloatCodec(0.0f, std::numeric_limits<float>::max()),
                "Field of view in degrees, on the same scale as the game's own slider.\n"
                "0 follows that slider as you move it. Any other value is drawn instead, held to\n"
                "50 to 130, so it can go outside the 70 to 119 the slider allows.")
        .Local("View", "CullFovScale", &Config::cull_fov_scale, cfg::FloatCodec(1.0f, 1.7f),
               "How much wider than the drawn field of view the game is told to cull, 1.0 to 1.7.\n"
               "1.0 adds nothing: the culled view already turns with your head. Raise it only if\n"
               "a lean shows missing scenery at the edge of the screen; it costs frames.")
        .Local("Debug", "LogToFile", &Config::log_to_file, cfg::BoolCodec(),
               "true: write a line per frame about the view to Titanfall2HeadTracking.log.\n"
               "The game build, the hook install, the map gate and any crash are logged either way.")
        .Local("Debug", "DumpViewSetup", &Config::dump_view_setup, cfg::BoolCodec(),
               "true: write the game's render view data to the log once, for finding its\n"
               "layout again after a game update.");
    return table;
}

cfg::ImportResult MapLegacyConfig(legacy::ReadStatus status, const legacy::Config& read, Config& out) {
    namespace d = legacy::defaults;
    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> shaping;

    // Every sensitivity shipped at 1.0 and every deadzone at 0, identity. The shipped
    // WorldScale is kSourceUnitsPerMetre, and the shipped InvertX=true and InvertZ=true are the
    // inversions PositionSettingsFor hands the processor. A value the player changed is dropped.
    const auto shape = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, shaping, dropped);
    };
    shape(read.sens_yaw, d::kSensitivity, "Sensitivity", "Yaw");
    shape(read.sens_pitch, d::kSensitivity, "Sensitivity", "Pitch");
    shape(read.sens_roll, d::kSensitivity, "Sensitivity", "Roll");
    shape(read.invert_yaw, d::kInvert, "Sensitivity", "InvertYaw");
    shape(read.invert_pitch, d::kInvert, "Sensitivity", "InvertPitch");
    shape(read.invert_roll, d::kInvert, "Sensitivity", "InvertRoll");
    shape(read.deadzone_yaw, d::kDeadzone, "Deadzone", "Yaw");
    shape(read.deadzone_pitch, d::kDeadzone, "Deadzone", "Pitch");
    shape(read.deadzone_roll, d::kDeadzone, "Deadzone", "Roll");
    shape(read.pos_world_scale, d::kWorldScale, "Position", "WorldScale");
    shape(read.pos_sens_x, d::kPositionSensitivity, "Position", "SensX");
    shape(read.pos_sens_y, d::kPositionSensitivity, "Position", "SensY");
    shape(read.pos_sens_z, d::kPositionSensitivity, "Position", "SensZ");
    shape(read.pos_invert_x, d::kPositionInvertX, "Position", "InvertX");
    shape(read.pos_invert_y, d::kPositionInvertY, "Position", "InvertY");
    shape(read.pos_invert_z, d::kPositionInvertZ, "Position", "InvertZ");

    out.udp_port = read.port;
    out.enable_on_startup = read.enabled_on_startup;
    out.world_space_yaw = read.world_space_yaw;

    // The frozen reader holds both to [0, 0.99] and every limit to [0, 2], and replaces a value
    // that is not finite with the default, so nothing here is out of a canonical range.
    out.local_smoothing = read.local_smoothing;
    out.position.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position.remote_smoothing = read.remote_smoothing;

    // [Position] Enabled chose only the mode the session started in: the cycle key reached every
    // mode either way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        read.pos_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                         : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    // LimitY bounded both directions, so it becomes both explicit values.
    out.position.limit_x = read.pos_limit_x;
    out.position.limit_y = read.pos_limit_y;
    out.position.limit_y_down = read.pos_limit_y;
    out.position.limit_z = read.pos_limit_z;
    out.position.limit_z_back = read.pos_limit_z_back;

    out.fov_override_degrees = read.fov_override_degrees;
    out.cull_fov_scale = read.cull_fov_scale;
    out.log_to_file = read.log_to_file;
    out.dump_view_setup = read.dump_view_setup;

    // The crosshair always follows the aim now.
    if (!read.move_crosshair) dropped.push_back({cfg::DropRule::Reticle, "View", "MoveCrosshair", "false"});

    out.toggle_key_name = KeyList(read.toggle_vk, 'Y', "Toggle", dropped);
    out.cycle_tracking_mode_key_name = KeyList(read.mode_cycle_vk, 'G', "ModeCycle", dropped);
    out.yaw_mode_key_name = KeyList(read.yaw_mode_vk, 'H', "YawMode", dropped);

    return status == legacy::ReadStatus::NotOpened
               ? cfg::ImportResult::Absent(std::move(dropped), std::move(shaping))
               : cfg::ImportResult::Imported(std::move(dropped), std::move(shaping));
}

cfg::LegacyImport<Config> LegacyConfigImport() {
    cfg::LegacyImport<Config> import;
    import.run = [](const cfg::LegacyInput& input, Config& out) {
        legacy::Config read;
        const legacy::ReadStatus status = legacy::Read(input.ansi_path.c_str(), read);
        return MapLegacyConfig(status, read, out);
    };
    // Every key the frozen reader reads, the two retired smoothing keys it only warns about
    // included.
    import.keys = {
        {"Network", "Port"},          {"Network", "EnableOnStartup"},
        {"Sensitivity", "Yaw"},       {"Sensitivity", "Pitch"},       {"Sensitivity", "Roll"},
        {"Sensitivity", "InvertYaw"}, {"Sensitivity", "InvertPitch"}, {"Sensitivity", "InvertRoll"},
        {"Smoothing", "LocalSmoothing"}, {"Smoothing", "RemoteSmoothing"},
        {"Smoothing", "Amount"},      {"Position", "Smoothing"},
        {"Deadzone", "Yaw"},          {"Deadzone", "Pitch"},          {"Deadzone", "Roll"},
        {"Position", "Enabled"},      {"Position", "WorldScale"},
        {"Position", "SensX"},        {"Position", "SensY"},          {"Position", "SensZ"},
        {"Position", "InvertX"},      {"Position", "InvertY"},        {"Position", "InvertZ"},
        {"Position", "LimitX"},       {"Position", "LimitY"},         {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Hotkeys", "Toggle"},        {"Hotkeys", "YawMode"},         {"Hotkeys", "ModeCycle"},
        {"View", "WorldSpaceYaw"},    {"View", "FieldOfView"},        {"View", "CullFovScale"},
        {"View", "MoveCrosshair"},
        {"Debug", "LogToFile"},       {"Debug", "DumpViewSetup"},
    };
    return import;
}

cfg::ConfigOwnerOptions<Config> ConfigOwnerOptionsFor(const std::wstring& folder, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = folder + kConfigFileName;
    options.legacy_path = folder + kLegacyConfigFileName;
    options.table = ConfigTable();
    options.import = LegacyConfigImport();
    options.header.display_name = kGameDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

// Next to the game EXE. A zero length is failure and a length of MAX_PATH is truncation, and
// neither names a real folder, so both take the working directory, where the published build's
// bare "HeadTracking.ini" pointed.
std::wstring ConfigFolder() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return WorkingDirectory();
    std::wstring dir(buf, len);
    dir.resize(dir.find_last_of(L"\\/") + 1);
    return dir;
}

cameraunlock::PositionSettings PositionSettingsFor(const Config& c) {
    cameraunlock::PositionSettings ps = c.position;
    ps.invert_x = true;
    ps.invert_y = false;
    ps.invert_z = true;
    // The two cores disagree about which sign of z is "forward". C++ clamps z to
    // [-limit_z, +limit_z_back]; the C# original it is a port of clamps to
    // [-LimitZBack, +LimitZ]. Transposed, not mirrored - with the same settings one puts the
    // generous allowance on negative z and the other on positive.
    //
    // After the inversion above, a forward lean arrives as POSITIVE z, so the generous
    // allowance has to land on the positive bound, which means handing the C++ processor its
    // two bounds the other way round. Verified in game: a 40 cm forward lean reaches the full
    // 0.40 m (15.75 units), a 40 cm backward lean stops at 0.10 m (3.94).
    ps.limit_z      = c.position.limit_z_back;  // negative bound: leaning back
    ps.limit_z_back = c.position.limit_z;       // positive bound: leaning in
    return ps;
}

}  // namespace headtracking
