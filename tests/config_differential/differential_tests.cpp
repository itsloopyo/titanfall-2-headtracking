// The config differential test. Every input is read three ways:
//
//   oracle     d366649's reader (oracle/), the rolling dev pre-release and the only published
//              build, and d366649's startup code
//   import     the frozen reader in src/legacy_config/, and the startup code it runs under
//   migration  the config owner importing the input, as HeadTracking.ini, into a new
//              CameraUnlock.ini beside it, then the canonical reader and table on the result,
//              and the startup code of this build
//
// Comparison 1, oracle against import, finds what a player updating from the dev build sees
// change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Comparison 2, import against migration, is the proof for the migration: no difference but
// the approved ones, each of which the import must record as dropped. A sensitivity, deadzone,
// unit scale or axis inversion the player set away from what the build shipped is dropped
// (pose_shaping); the shipped WorldScale is kSourceUnitsPerMetre and the shipped InvertX=true
// and InvertZ=true are the inversions PositionSettingsFor hands the processor, which
// config_tests holds against the published settings. MoveCrosshair=false is dropped (reticle):
// the crosshair always follows the aim. A hotkey code outside 0x01-0xFE imports as unbound
// (N1). No default moved, so the no-file input has no difference either. The frozen reader
// clamps every number it reads into a range the canonical rows hold and replaces a value that
// is not finite, so no input is deferred and N2 never applies.
//
// Every owner reads and creates one scratch Defaults.ini, which it creates with the built-in
// values, so an input whose values are the built-in ones migrates to `default` rows. The
// distinct migrated files are written beside the executable under migrated\, for
// lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: no file, an empty file, the first-run output of d366649 (extracted once into
// inputs/), hand-written files for the listed differences and for hotkey codes outside the
// range a key name covers, core's corpus over the first-run output, and the first-run output
// with all three hotkeys on each code from 0x01 to 0xFE. The dev build shipped no config file
// and no launcher seed, so its first-run output is the file every player of it holds unless
// they edited it.

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle/oracle_config.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace legacy = headtracking::legacy;
namespace oracle = headtracking::oracle;
namespace cfg = cameraunlock::config;
using cameraunlock::TrackingMode;
using cameraunlock::config::testing::GenerateIniMutations;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;
using cameraunlock::input::KeyModifiers;
using headtracking::Config;

int g_failures = 0;

void Fail(const std::string& input, const std::string& what) {
    if (g_failures < 50) std::printf("FAIL [%s]: %s\n", input.c_str(), what.c_str());
    ++g_failures;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

std::string Narrow(const std::wstring& path) {
    const int size = WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), 'x');
    WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size) - 1);
    return out;
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + Narrow(path));
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + Narrow(path));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Every file in the folder, name and bytes, for "the import changed nothing".
std::map<std::wstring, std::string> Snapshot(const std::wstring& dir) {
    std::map<std::wstring, std::string> files;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot list the test folder");
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files[data.cFileName] = ReadBytes(dir + L"\\" + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return files;
}

void EmptyFolder(const std::wstring& dir) {
    for (const auto& [name, bytes] : Snapshot(dir)) {
        const std::wstring path = dir + L"\\" + name;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(path.c_str())) throw std::runtime_error("cannot empty the test folder");
    }
}

std::wstring MakeFolder(const std::wstring& parent, const wchar_t* name) {
    const std::wstring dir = parent + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("cannot create the test folder");
    }
    EmptyFolder(dir);
    return dir;
}

// ---------------------------------------------------------------------------
// Startup state: what each build does with its Config
// ---------------------------------------------------------------------------

enum class Action { Toggle, CycleMode, YawMode, AdsMode };

const char* ActionName(Action a) {
    switch (a) {
        case Action::Toggle: return "toggle";
        case Action::CycleMode: return "cycle mode";
        case Action::YawMode: return "yaw mode";
        case Action::AdsMode: return "ADS mode";
    }
    throw std::logic_error("action");
}

// One key the poller watches for an action, and the modifiers it fires with: none is
// NavGuarded (not while Ctrl and Shift are both held), Ctrl+Shift is ChordGuarded.
struct Registration {
    Action action;
    int vk;
    unsigned modifiers;

    bool operator<(const Registration& o) const {
        return std::tie(action, vk, modifiers) < std::tie(o.action, o.vk, o.modifiers);
    }
    bool operator==(const Registration& o) const {
        return action == o.action && vk == o.vk && modifiers == o.modifiers;
    }
};

constexpr unsigned kNav = static_cast<unsigned>(KeyModifiers::kNone);
constexpr unsigned kChord = static_cast<unsigned>(KeyModifiers::kCtrl | KeyModifiers::kShift);

std::string Describe(const std::vector<Registration>& regs) {
    std::string out;
    for (const Registration& r : regs) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%s:%s0x%02X", out.empty() ? "" : " ", ActionName(r.action),
                      r.modifiers == kChord ? "Ctrl+Shift+" : "", r.vk);
        out += buf;
    }
    return out;
}

// d366649's HotkeyHandler::Start, with the poller calls recorded. HotkeyPoller skips a
// code of 0, so a key read as 0 registers nothing.
std::vector<Registration> OracleHotkeys(const oracle::Config& cfg) {
    std::vector<Registration> regs;
    const auto nav = [&regs](Action a, int vk) {
        if (vk != 0) regs.push_back({a, vk, kNav});
    };
    nav(Action::Toggle, cfg.toggle_vk);
    nav(Action::YawMode, cfg.yaw_mode_vk);
    nav(Action::CycleMode, cfg.mode_cycle_vk);
    nav(Action::AdsMode, cfg.ads_mode_vk);
    regs.push_back({Action::Toggle, 'Y', kChord});
    regs.push_back({Action::YawMode, 'H', kChord});
    regs.push_back({Action::CycleMode, 'G', kChord});
    regs.push_back({Action::AdsMode, 'U', kChord});
    std::sort(regs.begin(), regs.end());
    return regs;
}

// HotkeyHandler::Start as the build that carries the frozen reader runs it (cc165ee), with
// the poller calls recorded.
std::vector<Registration> ImportHotkeys(const legacy::Config& cfg) {
    std::vector<Registration> regs;
    const auto nav = [&regs](Action a, int vk) {
        if (vk != 0) regs.push_back({a, vk, kNav});
    };
    nav(Action::Toggle, cfg.toggle_vk);
    nav(Action::YawMode, cfg.yaw_mode_vk);
    nav(Action::CycleMode, cfg.mode_cycle_vk);
    regs.push_back({Action::Toggle, 'Y', kChord});
    regs.push_back({Action::YawMode, 'H', kChord});
    regs.push_back({Action::CycleMode, 'G', kChord});
    std::sort(regs.begin(), regs.end());
    return regs;
}

uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

// Every field the two Configs share, floats bit for bit. The oracle's ADS fields have no
// counterpart; comparison 1 lists them.
template <class A, class B>
std::vector<std::string> SharedFieldDifferences(const A& a, const B& b) {
    std::vector<std::string> out;
    const auto check = [&out](bool same, const char* name) {
        if (!same) out.push_back(name);
    };
#define SAME(f) check(a.f == b.f, #f)
#define SAME_BITS(f) check(Bits(a.f) == Bits(b.f), #f)
    SAME(port);
    SAME(enabled_on_startup);
    SAME_BITS(sens_yaw);
    SAME_BITS(sens_pitch);
    SAME_BITS(sens_roll);
    SAME(invert_yaw);
    SAME(invert_pitch);
    SAME(invert_roll);
    SAME_BITS(local_smoothing);
    SAME_BITS(remote_smoothing);
    SAME_BITS(deadzone_yaw);
    SAME_BITS(deadzone_pitch);
    SAME_BITS(deadzone_roll);
    SAME(pos_enabled);
    SAME_BITS(pos_sens_x);
    SAME_BITS(pos_sens_y);
    SAME_BITS(pos_sens_z);
    SAME(pos_invert_x);
    SAME(pos_invert_y);
    SAME(pos_invert_z);
    SAME_BITS(pos_limit_x);
    SAME_BITS(pos_limit_y);
    SAME_BITS(pos_limit_z);
    SAME_BITS(pos_limit_z_back);
    SAME_BITS(pos_world_scale);
    SAME_BITS(fov_override_degrees);
    SAME_BITS(cull_fov_scale);
    SAME(move_crosshair);
    SAME(toggle_vk);
    SAME(yaw_mode_vk);
    SAME(mode_cycle_vk);
    SAME(world_space_yaw);
    SAME(log_to_file);
    SAME(dump_view_setup);
#undef SAME
#undef SAME_BITS
    return out;
}

// ---------------------------------------------------------------------------
// Comparison 1: d366649 against the frozen reader
// ---------------------------------------------------------------------------

// What a player updating from the dev build sees change, and the commit that made each
// change. The changelog carries the same list.
struct ListedDifference {
    const char* id;
    const char* commit;
    const char* what;
    int seen = 0;
};

ListedDifference kComparisonOneDifferences[] = {
    {"ads-mode", "ea74da9",
     "[View] AdsMode is no longer read: head tracking carries on through the sights in every "
     "case, and the lean eases out while they are up"},
    {"ads-key", "ea74da9",
     "[Hotkeys] AdsMode is no longer read, and neither it nor Ctrl+Shift+U cycles an ADS mode"},
};

ListedDifference& Listed(const char* id) {
    for (ListedDifference& d : kComparisonOneDifferences) {
        if (std::strcmp(d.id, id) == 0) return d;
    }
    throw std::logic_error(id);
}

struct ImportRun {
    legacy::ReadStatus status = legacy::ReadStatus::Read;
    legacy::Config cfg;
};

void CompareOracleWithImport(const std::string& name, const oracle::Config& o, const ImportRun& i) {
    for (const std::string& field : SharedFieldDifferences(o, i.cfg)) {
        Fail(name, "comparison 1: " + field + " differs from d366649 with no listed reason");
    }

    if (o.ads_mode != oracle::kDefaultAdsMode) ++Listed("ads-mode").seen;
    ++Listed("ads-key").seen;

    // d366649's registrations less the ADS cycle give the import's.
    std::vector<Registration> expected;
    for (const Registration& r : OracleHotkeys(o)) {
        if (r.action != Action::AdsMode) expected.push_back(r);
    }
    const std::vector<Registration> actual = ImportHotkeys(i.cfg);
    if (expected != actual) {
        Fail(name, "comparison 1: hotkeys " + Describe(actual) + ", d366649 less the listed differences " +
                       Describe(expected));
    }
}

// ---------------------------------------------------------------------------
// The corpus descriptors: one valid value other than the first-run one, and one value past
// each bound the reader clamps or refuses, for every key LegacyConfigImport().keys lists
// ---------------------------------------------------------------------------

std::vector<MutationKey> CorpusKeys() {
    const auto boolean = [](const char* s, const char* k, const char* alt) {
        return MutationKey{s, k, alt, {}, false, {}};
    };
    const auto sens = [](const char* s, const char* k) { return MutationKey{s, k, "0.5", {"0.001", "11"}, false, {}}; };
    const auto posSens = [](const char* k) { return MutationKey{"Position", k, "0.5", {"-1", "11"}, false, {}}; };
    const auto deadzone = [](const char* k) { return MutationKey{"Deadzone", k, "0.5", {"-1"}, false, {}}; };
    const auto limit = [](const char* k) { return MutationKey{"Position", k, "0.25", {"-1", "2.5"}, false, {}}; };
    const auto smooth = [](const char* k) { return MutationKey{"Smoothing", k, "0.3", {"-0.5", "1.5"}, false, {}}; };
    const auto retired = [](const char* s, const char* k) { return MutationKey{s, k, "0.3", {}, false, {}}; };
    const auto hotkey = [](const char* k, const char* alt) {
        return MutationKey{"Hotkeys", k, alt, {"0xFF", "0x100", "-1"}, true, {}};
    };
    return {
        MutationKey{"Network", "Port", "5000", {"0", "65536"}, false, {}},
        boolean("Network", "EnableOnStartup", "0"),
        sens("Sensitivity", "Yaw"),
        sens("Sensitivity", "Pitch"),
        sens("Sensitivity", "Roll"),
        boolean("Sensitivity", "InvertYaw", "1"),
        boolean("Sensitivity", "InvertPitch", "1"),
        boolean("Sensitivity", "InvertRoll", "1"),
        smooth("LocalSmoothing"),
        smooth("RemoteSmoothing"),
        retired("Smoothing", "Amount"),
        retired("Position", "Smoothing"),
        deadzone("Yaw"),
        deadzone("Pitch"),
        deadzone("Roll"),
        boolean("Position", "Enabled", "0"),
        MutationKey{"Position", "WorldScale", "20", {"-1", "1001"}, false, {}},
        posSens("SensX"),
        posSens("SensY"),
        posSens("SensZ"),
        boolean("Position", "InvertX", "0"),
        boolean("Position", "InvertY", "1"),
        boolean("Position", "InvertZ", "0"),
        limit("LimitX"),
        limit("LimitY"),
        limit("LimitZ"),
        limit("LimitZBack"),
        hotkey("Toggle", "0x70"),
        hotkey("YawMode", "0x71"),
        hotkey("ModeCycle", "0x72"),
        boolean("View", "WorldSpaceYaw", "0"),
        MutationKey{"View", "FieldOfView", "90", {"-5"}, false, {}},
        MutationKey{"View", "CullFovScale", "1.3", {"0.5", "2"}, false, {}},
        boolean("View", "MoveCrosshair", "0"),
        boolean("Debug", "LogToFile", "1"),
        boolean("Debug", "DumpViewSetup", "1"),
    };
}

// ---------------------------------------------------------------------------
// Comparison 2: the frozen reader against the migration
// ---------------------------------------------------------------------------

// What the mod starts with: every setting Plugin::Initialize and HotkeyHandler::Start take from
// the Config, floats as their bits.
struct Startup {
    int port = 0;
    bool enabled = false;
    TrackingMode mode = TrackingMode::RotationAndPosition;
    bool world_yaw = false;
    uint32_t local_smoothing = 0;
    uint32_t remote_smoothing = 0;
    // The rotation processor's sensitivity and deadzone.
    uint32_t sens_yaw = 0, sens_pitch = 0, sens_roll = 0;
    bool invert_yaw = false, invert_pitch = false, invert_roll = false;
    uint32_t deadzone_yaw = 0, deadzone_pitch = 0, deadzone_roll = 0;
    // What the position processor is handed, less the smoothing the session overwrites.
    uint32_t pos_sens_x = 0, pos_sens_y = 0, pos_sens_z = 0;
    bool pos_invert_x = false, pos_invert_y = false, pos_invert_z = false;
    uint32_t limit_x = 0, limit_y = 0, limit_y_down = 0, limit_z = 0, limit_z_back = 0;
    // Source units per metre the lean is multiplied by.
    uint32_t world_scale = 0;
    uint32_t fov_override = 0;
    uint32_t cull_fov_scale = 0;
    bool log_to_file = false;
    bool dump_view_setup = false;
    // Whether the crosshair and hit indicator hooks are installed.
    bool crosshair = false;
    std::vector<Registration> hotkeys;
};

void SetRotation(Startup& s, const cameraunlock::SensitivitySettings& r, const cameraunlock::DeadzoneSettings& d) {
    s.sens_yaw = Bits(r.yaw);
    s.sens_pitch = Bits(r.pitch);
    s.sens_roll = Bits(r.roll);
    s.invert_yaw = r.invert_yaw;
    s.invert_pitch = r.invert_pitch;
    s.invert_roll = r.invert_roll;
    s.deadzone_yaw = Bits(d.yaw);
    s.deadzone_pitch = Bits(d.pitch);
    s.deadzone_roll = Bits(d.roll);
}

void SetPosition(Startup& s, const cameraunlock::PositionSettings& p) {
    s.pos_sens_x = Bits(p.sensitivity_x);
    s.pos_sens_y = Bits(p.sensitivity_y);
    s.pos_sens_z = Bits(p.sensitivity_z);
    s.pos_invert_x = p.invert_x;
    s.pos_invert_y = p.invert_y;
    s.pos_invert_z = p.invert_z;
    s.limit_x = Bits(p.limit_x);
    s.limit_y = Bits(p.limit_y);
    s.limit_y_down = Bits(p.limit_y_down);
    s.limit_z = Bits(p.limit_z);
    s.limit_z_back = Bits(p.limit_z_back);
}

const cfg::DroppedValue* FindDrop(const std::vector<cfg::DroppedValue>& dropped, cfg::DropRule rule,
                                  const char* section, const char* key) {
    for (const cfg::DroppedValue& d : dropped) {
        if (d.rule == rule && d.section == section && d.key == key) return &d;
    }
    return nullptr;
}

// A hotkey code outside 0x01-0xFE imports as unbound (N1), and only then is it dropped. Code 0
// never fired on the published build's poller and imports as unbound, unrecorded.
bool KeyKept(const std::string& name, int vk, const char* key, const std::vector<cfg::DroppedValue>& dropped) {
    const bool outOfRange = vk != 0 && (vk < 0x01 || vk > 0xFE);
    if (outOfRange != (FindDrop(dropped, cfg::DropRule::KeyCodeOutOfRange, "Hotkeys", key) != nullptr)) {
        Fail(name, std::string("[Hotkeys] ") + key + " dropped as out of range does not match its code");
    }
    return vk != 0 && !outOfRange;
}

// Plugin::Initialize and HotkeyHandler::Start as commit A ran them on the frozen reader's
// Config (ApplyRotationConfig, ApplyPositionConfig with LimitY copied into both vertical
// limits and the Z bounds swapped, the world scale, [Position] Enabled choosing between the
// first two modes, the crosshair hooks behind MoveCrosshair), with the approved drops the
// import recorded applied: a dropped pose-shaping value runs as it shipped, a dropped
// MoveCrosshair=false as the crosshair following the aim, and a code N1 unbinds registers
// nothing.
Startup FromImport(const std::string& name, const legacy::Config& c, const std::vector<cfg::DroppedValue>& dropped) {
    namespace d = legacy::defaults;
    const auto shaped = [&dropped](const char* section, const char* key) {
        return FindDrop(dropped, cfg::DropRule::PoseShaping, section, key) != nullptr;
    };

    Startup s;
    s.port = c.port;
    s.enabled = c.enabled_on_startup;
    s.mode = c.pos_enabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly;
    s.world_yaw = c.world_space_yaw;
    s.local_smoothing = Bits(c.local_smoothing);
    s.remote_smoothing = Bits(c.remote_smoothing);

    cameraunlock::SensitivitySettings r;
    r.yaw = shaped("Sensitivity", "Yaw") ? d::kSensitivity : c.sens_yaw;
    r.pitch = shaped("Sensitivity", "Pitch") ? d::kSensitivity : c.sens_pitch;
    r.roll = shaped("Sensitivity", "Roll") ? d::kSensitivity : c.sens_roll;
    r.invert_yaw = shaped("Sensitivity", "InvertYaw") ? d::kInvert : c.invert_yaw;
    r.invert_pitch = shaped("Sensitivity", "InvertPitch") ? d::kInvert : c.invert_pitch;
    r.invert_roll = shaped("Sensitivity", "InvertRoll") ? d::kInvert : c.invert_roll;
    cameraunlock::DeadzoneSettings z;
    z.yaw = shaped("Deadzone", "Yaw") ? d::kDeadzone : c.deadzone_yaw;
    z.pitch = shaped("Deadzone", "Pitch") ? d::kDeadzone : c.deadzone_pitch;
    z.roll = shaped("Deadzone", "Roll") ? d::kDeadzone : c.deadzone_roll;
    SetRotation(s, r, z);

    cameraunlock::PositionSettings p;
    p.sensitivity_x = shaped("Position", "SensX") ? d::kPositionSensitivity : c.pos_sens_x;
    p.sensitivity_y = shaped("Position", "SensY") ? d::kPositionSensitivity : c.pos_sens_y;
    p.sensitivity_z = shaped("Position", "SensZ") ? d::kPositionSensitivity : c.pos_sens_z;
    p.invert_x = shaped("Position", "InvertX") ? d::kPositionInvertX : c.pos_invert_x;
    p.invert_y = shaped("Position", "InvertY") ? d::kPositionInvertY : c.pos_invert_y;
    p.invert_z = shaped("Position", "InvertZ") ? d::kPositionInvertZ : c.pos_invert_z;
    p.limit_x = c.pos_limit_x;
    p.limit_y = c.pos_limit_y;
    p.limit_y_down = c.pos_limit_y;
    p.limit_z = c.pos_limit_z_back;
    p.limit_z_back = c.pos_limit_z;
    SetPosition(s, p);
    s.world_scale = Bits(shaped("Position", "WorldScale") ? d::kWorldScale : c.pos_world_scale);

    s.fov_override = Bits(c.fov_override_degrees);
    s.cull_fov_scale = Bits(c.cull_fov_scale);
    s.log_to_file = c.log_to_file;
    s.dump_view_setup = c.dump_view_setup;
    s.crosshair = c.move_crosshair || FindDrop(dropped, cfg::DropRule::Reticle, "View", "MoveCrosshair") != nullptr;

    const bool toggleKept = KeyKept(name, c.toggle_vk, "Toggle", dropped);
    const bool cycleKept = KeyKept(name, c.mode_cycle_vk, "ModeCycle", dropped);
    const bool yawKept = KeyKept(name, c.yaw_mode_vk, "YawMode", dropped);
    for (const Registration& reg : ImportHotkeys(c)) {
        const bool kept = reg.modifiers == kChord || (reg.action == Action::Toggle && toggleKept) ||
                          (reg.action == Action::CycleMode && cycleKept) || (reg.action == Action::YawMode && yawKept);
        if (kept) s.hotkeys.push_back(reg);
    }
    return s;
}

// This build: Plugin::Initialize, which never sets the rotation processor's sensitivity or
// deadzone, and the lists HotkeyHandler::Start registers.
Startup FromMigration(const Config& c) {
    Startup s;
    s.port = c.udp_port;
    s.enabled = c.enable_on_startup;
    s.mode = cameraunlock::DecodeTrackingMode(c.rotation_enabled, c.position_enabled).value();
    s.world_yaw = c.world_space_yaw;
    s.local_smoothing = Bits(c.local_smoothing);
    s.remote_smoothing = Bits(c.remote_smoothing);
    SetRotation(s, cameraunlock::SensitivitySettings{}, cameraunlock::DeadzoneSettings::None());
    SetPosition(s, headtracking::PositionSettingsFor(c));
    s.world_scale = Bits(headtracking::kSourceUnitsPerMetre);
    s.fov_override = Bits(c.fov_override_degrees);
    s.cull_fov_scale = Bits(c.cull_fov_scale);
    s.log_to_file = c.log_to_file;
    s.dump_view_setup = c.dump_view_setup;
    s.crosshair = true;
    const std::pair<Action, const std::string*> lists[] = {
        {Action::Toggle, &c.toggle_key_name},
        {Action::CycleMode, &c.cycle_tracking_mode_key_name},
        {Action::YawMode, &c.yaw_mode_key_name},
    };
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        if (!parsed.ok()) throw std::logic_error("a migrated hotkey list does not parse: " + *list);
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            s.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    std::sort(s.hotkeys.begin(), s.hotkeys.end());
    return s;
}

std::vector<std::string> StartupDifferences(const Startup& a, const Startup& b) {
    std::vector<std::string> out;
#define SAME(f) \
    if (a.f != b.f) out.push_back(#f)
    SAME(port);
    SAME(enabled);
    SAME(mode);
    SAME(world_yaw);
    SAME(local_smoothing);
    SAME(remote_smoothing);
    SAME(sens_yaw);
    SAME(sens_pitch);
    SAME(sens_roll);
    SAME(invert_yaw);
    SAME(invert_pitch);
    SAME(invert_roll);
    SAME(deadzone_yaw);
    SAME(deadzone_pitch);
    SAME(deadzone_roll);
    SAME(pos_sens_x);
    SAME(pos_sens_y);
    SAME(pos_sens_z);
    SAME(pos_invert_x);
    SAME(pos_invert_y);
    SAME(pos_invert_z);
    SAME(limit_x);
    SAME(limit_y);
    SAME(limit_y_down);
    SAME(limit_z);
    SAME(limit_z_back);
    SAME(world_scale);
    SAME(fov_override);
    SAME(cull_fov_scale);
    SAME(log_to_file);
    SAME(dump_view_setup);
    SAME(crosshair);
#undef SAME
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys " + Describe(a.hotkeys) + " against " + Describe(b.hotkeys));
    return out;
}

// Every pose-shaping value the frozen reader read is listed in its place, folded where it holds
// what the build shipped and dropped as PoseShaping where it does not. Returns how many were
// dropped.
int CheckPoseShaping(const std::string& name, const legacy::Config& c, const cfg::ImportResult& result) {
    namespace d = legacy::defaults;
    struct Read {
        const char* section;
        const char* key;
        bool shipped;
    };
    const Read reads[] = {
        {"Sensitivity", "Yaw", c.sens_yaw == d::kSensitivity},
        {"Sensitivity", "Pitch", c.sens_pitch == d::kSensitivity},
        {"Sensitivity", "Roll", c.sens_roll == d::kSensitivity},
        {"Sensitivity", "InvertYaw", c.invert_yaw == d::kInvert},
        {"Sensitivity", "InvertPitch", c.invert_pitch == d::kInvert},
        {"Sensitivity", "InvertRoll", c.invert_roll == d::kInvert},
        {"Deadzone", "Yaw", c.deadzone_yaw == d::kDeadzone},
        {"Deadzone", "Pitch", c.deadzone_pitch == d::kDeadzone},
        {"Deadzone", "Roll", c.deadzone_roll == d::kDeadzone},
        {"Position", "WorldScale", c.pos_world_scale == d::kWorldScale},
        {"Position", "SensX", c.pos_sens_x == d::kPositionSensitivity},
        {"Position", "SensY", c.pos_sens_y == d::kPositionSensitivity},
        {"Position", "SensZ", c.pos_sens_z == d::kPositionSensitivity},
        {"Position", "InvertX", c.pos_invert_x == d::kPositionInvertX},
        {"Position", "InvertY", c.pos_invert_y == d::kPositionInvertY},
        {"Position", "InvertZ", c.pos_invert_z == d::kPositionInvertZ},
    };
    if (result.pose_shaping.size() != std::size(reads)) {
        Fail(name, "the import lists " + std::to_string(result.pose_shaping.size()) + " pose-shaping values, not 16");
        return 0;
    }
    int dropped = 0;
    for (size_t k = 0; k < std::size(reads); ++k) {
        const cfg::PoseShapingValue& v = result.pose_shaping[k];
        const std::string label = std::string("[") + reads[k].section + "] " + reads[k].key;
        if (v.section != reads[k].section || v.key != reads[k].key) Fail(name, label + " is not listed in its place");
        if (v.folded != reads[k].shipped) Fail(name, label + " is " + (v.folded ? "folded" : "dropped") + " wrongly");
        const bool listed = FindDrop(result.dropped, cfg::DropRule::PoseShaping, reads[k].section, reads[k].key) != nullptr;
        if (listed == reads[k].shipped) {
            Fail(name, label + (listed ? " is dropped at its shipped value" : " is changed and not dropped"));
        }
        if (!reads[k].shipped) ++dropped;
    }
    return dropped;
}

// Every drop the import recorded is by one of the approved rules this map applies, and
// MoveCrosshair is dropped exactly when it was off.
void CheckDropRules(const std::string& name, const legacy::Config& c, const cfg::ImportResult& result) {
    for (const cfg::DroppedValue& d : result.dropped) {
        const bool approved = d.rule == cfg::DropRule::PoseShaping || d.rule == cfg::DropRule::KeyCodeOutOfRange ||
                              d.rule == cfg::DropRule::Reticle;
        if (!approved) Fail(name, "the import drops [" + d.section + "] " + d.key + " by a rule this map never applies");
    }
    const bool reticleDropped = FindDrop(result.dropped, cfg::DropRule::Reticle, "View", "MoveCrosshair") != nullptr;
    if (reticleDropped == c.move_crosshair) Fail(name, "MoveCrosshair is not dropped exactly when it was false");
}

struct MigrationTally {
    std::string committed;
    std::wstring defaults;
    std::set<std::string> migrated;
    int created = 0;
    int converted = 0;
    int with_pose_shaping_dropped = 0;
    int with_reticle_dropped = 0;
    int with_n1 = 0;
};

cfg::ConfigOwnerOptions<Config> Options(const std::wstring& dir, const MigrationTally& tally) {
    return headtracking::ConfigOwnerOptionsFor(dir + L"\\", cfg::DefaultsFile::At(tally.defaults));
}

FILETIME WriteTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        throw std::runtime_error("cannot read a test file's attributes");
    }
    return data.ftLastWriteTime;
}

bool SameTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

// Each input is read in folders of its own. With one file per reader rewritten for every input,
// fallout-4-headtracking's run of this test failed on 3, 17 and 0 inputs, each time a reader
// getting values another input held: both readers go through GetPrivateProfileString, and a
// path it has not seen before has not failed that way since.
struct Folders {
    std::wstring root;
    std::wstring oracle;
    std::wstring import;
    std::wstring migration;
    std::wstring read_only;
};

Folders NextFolders(const std::wstring& root) {
    static int n = 0;
    const std::wstring dir = MakeFolder(root, std::to_wstring(n++).c_str());
    return {dir, MakeFolder(dir, L"oracle"), MakeFolder(dir, L"import"), MakeFolder(dir, L"migration"),
            MakeFolder(dir, L"read-only")};
}

void RemoveFolders(const Folders& f) {
    for (const std::wstring& dir : {f.oracle, f.import, f.migration, f.read_only, f.root}) {
        EmptyFolder(dir);
        if (!RemoveDirectoryW(dir.c_str())) throw std::runtime_error("cannot remove a test folder");
    }
}

const wchar_t kIniName[] = L"HeadTracking.ini";

// The owner's load on the legacy file `legacyBytes` in `dir`, read-only when asked, with
// everything the migration must leave as it was checked afterwards. Holds the migrated bytes,
// or nothing when no file was created.
struct Migration {
    cfg::ConfigLoadResult<Config> loaded;
    std::optional<std::string> bytes;
};

Migration Migrate(const std::string& name, const std::wstring& dir, const std::optional<std::string>& legacyBytes,
                  bool readOnly, const MigrationTally& tally) {
    const std::wstring legacyPath = dir + L"\\" + kIniName;
    const std::wstring path = dir + L"\\" + headtracking::kConfigFileName;
    FILETIME before{};
    if (legacyBytes) {
        WriteBytes(legacyPath, *legacyBytes);
        if (readOnly) SetFileAttributesW(legacyPath.c_str(), FILE_ATTRIBUTE_READONLY);
        before = WriteTime(legacyPath);
    }

    Migration m{};
    {
        cfg::ConfigOwner<Config> owner(Options(dir, tally));
        m.loaded = owner.Load();
    }

    std::map<std::wstring, std::string> expected;
    if (legacyBytes) {
        expected[kIniName] = *legacyBytes;
        if (!SameTime(WriteTime(legacyPath), before)) Fail(name, "the legacy file's write time changed");
        const DWORD attributes = GetFileAttributesW(legacyPath.c_str());
        if (((attributes & FILE_ATTRIBUTE_READONLY) != 0) != readOnly) {
            Fail(name, "the legacy file's read-only attribute changed");
        }
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        m.bytes = ReadBytes(path);
        expected[headtracking::kConfigFileName] = *m.bytes;
    }
    if (Snapshot(dir) != expected) Fail(name, "the folder holds files other than the legacy file and CameraUnlock.ini");
    return m;
}

// The migrated file loaded again over the same Defaults.ini: it reads as canonical with
// nothing to report, gives the same start, imports nothing and changes neither file.
void CheckSecondLoad(const std::string& name, const std::wstring& dir, const Migration& first,
                     const std::optional<std::string>& legacyBytes, const MigrationTally& tally) {
    const auto before = Snapshot(dir);
    cfg::ConfigOwner<Config> owner(Options(dir, tally));
    const cfg::ConfigLoadResult<Config> again = owner.Load();
    if (again.status != cfg::ConfigLoadStatus::Canonical) Fail(name, "the second load is not Canonical");
    if (!again.diagnostics.empty()) Fail(name, "the migrated file draws diagnostics");
    if (!StartupDifferences(FromMigration(first.loaded.config), FromMigration(again.config)).empty()) {
        Fail(name, "the second load starts differently from the first");
    }
    if (Snapshot(dir) != before) Fail(name, "the second load changed a file");
    const bool saysLegacyUnread = std::any_of(again.log.begin(), again.log.end(), [](const std::string& line) {
        return line.find("is left as it was and is not read") != std::string::npos;
    });
    if (legacyBytes.has_value() != saysLegacyUnread) {
        Fail(name, "the second load's log does not say whether the legacy file was left unread");
    }
}

void MigrateInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes,
                  const ImportRun& i, const cfg::ImportResult* result, MigrationTally& tally) {
    using cfg::ConfigLoadStatus;
    const Migration m = Migrate(name, f.migration, bytes, false, tally);

    if (!bytes) {
        ++tally.created;
        if (m.loaded.status != ConfigLoadStatus::Created) Fail(name, "no file is not Created");
        if (m.bytes != tally.committed) Fail(name, "the created file is not CameraUnlock.ini as committed");
        for (const std::string& d : StartupDifferences(FromImport(name, i.cfg, {}), FromMigration(m.loaded.config))) {
            Fail(name, "comparison 2: " + d);
        }
        CheckSecondLoad(name, f.migration, m, bytes, tally);
        return;
    }

    if (CheckPoseShaping(name, i.cfg, *result) > 0) ++tally.with_pose_shaping_dropped;
    CheckDropRules(name, i.cfg, *result);
    if (!i.cfg.move_crosshair) ++tally.with_reticle_dropped;
    if (std::any_of(result->dropped.begin(), result->dropped.end(),
                    [](const cfg::DroppedValue& d) { return d.rule == cfg::DropRule::KeyCodeOutOfRange; })) {
        ++tally.with_n1;
    }

    for (const std::string& d :
         StartupDifferences(FromImport(name, i.cfg, result->dropped), FromMigration(m.loaded.config))) {
        Fail(name, "comparison 2: " + d);
    }

    ++tally.converted;
    if (m.loaded.status != ConfigLoadStatus::Migrated) {
        Fail(name, std::string("the migration is ") + cfg::ConfigLoadStatusName(m.loaded.status) + ": " + m.loaded.reason);
        return;
    }
    tally.migrated.insert(*m.bytes);

    const Migration ro = Migrate(name, f.read_only, bytes, true, tally);
    if (ro.loaded.status != ConfigLoadStatus::Migrated || ro.bytes != m.bytes) {
        Fail(name, "a read-only legacy file does not import as a writable one does");
    }

    CheckSecondLoad(name, f.migration, m, bytes, tally);
}

void RunInput(const std::wstring& root, const std::string& name, const std::optional<std::string>& bytes,
              MigrationTally& tally) {
    const Folders f = NextFolders(root);
    oracle::Config o;
    {
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o = oracle::Config::LoadOrCreateDefault(Narrow(path));
    }

    ImportRun i;
    std::optional<cfg::ImportResult> result;
    {
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (bytes) {
            Config mapped = headtracking::ConfigTable().defaults();
            result = headtracking::LegacyConfigImport().run(cfg::LegacyInput{path, Narrow(path), false}, mapped);
        }
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (bytes && i.status != legacy::ReadStatus::Read) Fail(name, "the import did not read a file that is there");
        if (!bytes && i.status != legacy::ReadStatus::NotOpened) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
    MigrateInput(f, name, bytes, i, result ? &*result : nullptr, tally);
    RemoveFolders(f);
}

// A legacy file another program holds open with no sharing. The published build found the
// file, so it wrote nothing, and GetPrivateProfileString read nothing from it, so it ran on its
// defaults. The owner defers the import on its own defaults, which start the same, creates
// nothing and saves nothing that session.
void TestUnopenableFile(const std::wstring& root, const std::string& firstRun, const MigrationTally& tally) {
    const std::string name = "a legacy file another program holds open with no sharing";
    const Folders f = NextFolders(root);
    oracle::Config o;
    ImportRun i;
    std::optional<cfg::ConfigLoadResult<Config>> loaded;
    for (const std::wstring& dir : {f.oracle, f.import, f.migration}) {
        const std::wstring path = dir + L"\\" + kIniName;
        WriteBytes(path, firstRun);
        HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot hold the test file open");
        if (dir == f.oracle) {
            o = oracle::Config::LoadOrCreateDefault(Narrow(path));
        } else if (dir == f.import) {
            i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        } else {
            cfg::ConfigOwner<Config> owner(Options(dir, tally));
            loaded.emplace(owner.Load());
            if (owner.Save([](Config& c) { c.world_space_yaw = false; }).status != cfg::ConfigSaveStatus::NotSaved) {
                Fail(name, "a deferred session saved");
            }
        }
        CloseHandle(held);
        if (ReadBytes(path) != firstRun) Fail(name, "a build rewrote a file it could not open");
    }
    CompareOracleWithImport(name, o, i);
    if (loaded->status != cfg::ConfigLoadStatus::Deferred) {
        Fail(name, std::string("the owner's load is ") + cfg::ConfigLoadStatusName(loaded->status) + ", not Deferred");
    }
    for (const std::string& d : StartupDifferences(FromImport(name, i.cfg, {}), FromMigration(loaded->config))) {
        Fail(name, "comparison 2: " + d);
    }
    if (Snapshot(f.migration) != std::map<std::wstring, std::string>{{kIniName, firstRun}}) {
        Fail(name, "a deferred import created a file or changed the legacy one");
    }
    RemoveFolders(f);
}

// Registration compares the two builds by key and modifiers, which holds only while a binding
// with no modifiers fires as the old build's NavGuarded did (not while Ctrl and Shift are both
// held) and a Ctrl+Shift binding as its ChordGuarded did (while both are held). Alt changes
// neither.
void TestRegistrationModel() {
    using cameraunlock::input::detail::BindingFires;
    for (unsigned held = 0; held < 8; ++held) {
        const auto mods = static_cast<KeyModifiers>(held);
        const bool chordHeld = cameraunlock::input::HasModifiers(mods, KeyModifiers::kCtrl | KeyModifiers::kShift);
        if (BindingFires(KeyModifiers::kNone, mods) != !chordHeld) {
            Fail("registration", "a key with no modifiers does not fire as NavGuarded did, held " + std::to_string(held));
        }
        if (BindingFires(KeyModifiers::kCtrl | KeyModifiers::kShift, mods) != chordHeld) {
            Fail("registration", "a Ctrl+Shift key does not fire as ChordGuarded did, held " + std::to_string(held));
        }
    }
}

std::string ReadInput(const std::string& file) {
    return ReadBytes(Widen(std::string(TITANFALL2_DIFFERENTIAL_INPUTS) + "/" + file));
}

std::string Replaced(std::string base, const std::string& from, const std::string& to) {
    const size_t at = base.find(from);
    if (at == std::string::npos) throw std::logic_error("no " + from + " in the base file");
    return base.replace(at, from.size(), to);
}

// Every hotkey code the frozen reader can hand the poller in range, 0x01 to 0xFE, on all three
// hotkeys at once. The corpus tries one alternate code per hotkey; this is where the codes the
// key table has no name for, or names only as a modifier, are covered.
std::vector<std::pair<std::string, std::string>> EveryHotkeyCode(const std::string& firstRun) {
    std::vector<std::pair<std::string, std::string>> inputs;
    for (int vk = 0x01; vk <= 0xFE; ++vk) {
        char code[8];
        std::snprintf(code, sizeof(code), "0x%02X", static_cast<unsigned>(vk));
        std::string bytes = Replaced(firstRun, "Toggle=0x23", std::string("Toggle=") + code);
        bytes = Replaced(bytes, "YawMode=0x22", std::string("YawMode=") + code);
        bytes = Replaced(bytes, "ModeCycle=0x21", std::string("ModeCycle=") + code);
        inputs.emplace_back(std::string("every hotkey ") + code, bytes);
    }
    return inputs;
}

void TestFrozenDefaults() {
    const oracle::Config o;
    const legacy::Config l;
    for (const std::string& field : SharedFieldDifferences(o, l)) {
        Fail("defaults", field + ": the frozen default differs from d366649's");
    }
}

}  // namespace

int main() {
    try {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const std::wstring root = std::wstring(temp) + L"titanfall2-config-differential-" +
                                  std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(root.c_str(), nullptr);

        MigrationTally tally;
        tally.committed = ReadBytes(Widen(TITANFALL2_COMMITTED_CONFIG));
        tally.defaults = MakeFolder(root, L"global") + L"\\Defaults.ini";

        TestFrozenDefaults();
        TestRegistrationModel();

        const std::string firstRun = ReadInput("first-run-d366649.ini");
        {
            const Folders f = NextFolders(root);
            const std::wstring path = f.oracle + L"\\" + kIniName;
            oracle::Config::LoadOrCreateDefault(Narrow(path));
            if (ReadBytes(path) != firstRun) {
                Fail("first run", "the oracle's first-run output is not inputs/first-run-d366649.ini");
            }
            RemoveFolders(f);
        }

        const std::vector<std::pair<std::string, std::optional<std::string>>> inputs = {
            {"no file", std::nullopt},
            {"empty file", std::string()},
            {"first run, dev d366649", firstRun},
            {"AdsMode=tracked", Replaced(firstRun, "AdsMode=paused", "AdsMode=tracked")},
            // SaveAdsMode wrote this with WritePrivateProfileStringA when there was no file.
            {"AdsMode saved with no file", std::string("[View]\r\nAdsMode=marker\r\n")},
            {"Toggle=0x100", Replaced(firstRun, "Toggle=0x23", "Toggle=0x100")},
            {"Toggle=-1", Replaced(firstRun, "Toggle=0x23", "Toggle=-1")},
            {"Toggle=0", Replaced(firstRun, "Toggle=0x23", "Toggle=0")},
            {"Toggle=0xFF", Replaced(firstRun, "Toggle=0x23", "Toggle=0xFF")},
            {"YawMode on the Toggle key", Replaced(firstRun, "YawMode=0x22", "YawMode=0x23")},
            {"ModeCycle on Y", Replaced(firstRun, "ModeCycle=0x21", "ModeCycle=0x59")},
        };
        for (const auto& [name, bytes] : inputs) RunInput(root, name, bytes, tally);
        TestUnopenableFile(root, firstRun, tally);

        // Fresh equals upgrade: the file the published build wrote at first launch, which is the
        // file every player of it holds unless they edited it, converts to the committed file, as
        // no file is created as it. So do the files that differ from it only in keys this build
        // no longer reads.
        for (const char* name : {"first run, dev d366649", "AdsMode=tracked", "AdsMode saved with no file"}) {
            const auto input =
                std::find_if(inputs.begin(), inputs.end(), [name](const auto& in) { return in.first == name; });
            const Folders f = NextFolders(root);
            const Migration m = Migrate(name, f.migration, input->second, false, tally);
            if (m.bytes != tally.committed) {
                Fail("fresh equals upgrade", std::string(name) + " does not convert to the committed file");
            }
            RemoveFolders(f);
        }

        const std::vector<IniMutation> corpus =
            GenerateIniMutations(firstRun, headtracking::LegacyConfigImport().keys, CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(root, "corpus: " + m.name, m.bytes, tally);

        const auto codes = EveryHotkeyCode(firstRun);
        for (const auto& [name, bytes] : codes) RunInput(root, name, bytes, tally);

        std::printf("%zu inputs, %zu of them from the corpus and %zu with every hotkey on one code\n",
                    inputs.size() + corpus.size() + codes.size(), corpus.size(), codes.size());
        std::printf("comparison 1, d366649 against the frozen reader:\n");
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
        }
        std::printf("comparison 2, the frozen reader against the migration: %d created, %d converted, "
                    "%zu distinct files\n",
                    tally.created, tally.converted, tally.migrated.size());
        std::printf("  %d with a changed sensitivity, deadzone, scale or inversion dropped (pose_shaping)\n",
                    tally.with_pose_shaping_dropped);
        std::printf("  %d with MoveCrosshair=false dropped (reticle)\n", tally.with_reticle_dropped);
        std::printf("  %d with a hotkey code outside 0x01-0xFE unbound (N1)\n", tally.with_n1);
        if (tally.with_pose_shaping_dropped == 0) Fail("pose shaping", "no input drops a changed value");
        if (tally.with_reticle_dropped == 0) Fail("reticle", "no input drops MoveCrosshair=false");
        if (tally.with_n1 == 0) Fail("N1", "no input unbinds an out-of-range code");
        if (tally.migrated.count(tally.committed) == 0) Fail("first run", "no input migrated to the committed file");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring lintDir(exe);
        lintDir = lintDir.substr(0, lintDir.find_last_of(L'\\'));
        lintDir = MakeFolder(lintDir, L"migrated");
        int n = 0;
        for (const std::string& file : tally.migrated) {
            WriteBytes(lintDir + L"\\" + std::to_wstring(n++) + L".ini", file);
        }

        const std::wstring global = root + L"\\global";
        EmptyFolder(global);
        RemoveDirectoryW(global.c_str());
        RemoveDirectoryW(root.c_str());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("config differential: all passed\n");
        return 0;
    }
    std::printf("config differential: %d failure(s)\n", g_failures);
    return 1;
}
