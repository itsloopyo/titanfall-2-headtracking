// The config differential test. Every input is read two ways:
//
//   oracle  d366649's reader (oracle/), the rolling dev pre-release and the only published
//           build, and d366649's startup code
//   import  the frozen reader in src/legacy_config/, and the startup code it runs under
//
// Comparison 1, oracle against import, finds what a player updating from the dev build sees
// change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Inputs: no file, an empty file, the first-run output of d366649 (extracted once into
// inputs/), hand-written files for the listed differences and for hotkey codes outside the
// range a key name covers, and core's corpus over the first-run output. The dev build shipped
// no config file and no launcher seed.

#include "legacy_config/legacy_config.h"
#include "oracle/oracle_config.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace legacy = headtracking::legacy;
namespace oracle = headtracking::oracle;
using cameraunlock::config::LegacyKey;
using cameraunlock::config::testing::GenerateIniMutations;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;
using cameraunlock::input::KeyModifiers;

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
// The keys the frozen reader reads, and the corpus descriptors
// ---------------------------------------------------------------------------

std::vector<LegacyKey> FrozenReaderKeys() {
    return {
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
}

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
    const auto hotkey = [](const char* k, const char* alt) { return MutationKey{"Hotkeys", k, alt, {}, true, {}}; };
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
// The run
// ---------------------------------------------------------------------------

struct Folders {
    std::wstring oracle;
    std::wstring import;
};

const wchar_t kIniName[] = L"HeadTracking.ini";

void RunInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes) {
    oracle::Config o;
    {
        EmptyFolder(f.oracle);
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o = oracle::Config::LoadOrCreateDefault(Narrow(path));
    }

    ImportRun i;
    {
        EmptyFolder(f.import);
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (bytes && i.status != legacy::ReadStatus::Read) Fail(name, "the import did not read a file that is there");
        if (!bytes && i.status != legacy::ReadStatus::NotOpened) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
}

std::string ReadInput(const std::string& file) {
    return ReadBytes(Widen(std::string(TITANFALL2_DIFFERENTIAL_INPUTS) + "/" + file));
}

std::string Replaced(std::string base, const std::string& from, const std::string& to) {
    const size_t at = base.find(from);
    if (at == std::string::npos) throw std::logic_error("no " + from + " in the base file");
    return base.replace(at, from.size(), to);
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
        const Folders folders{MakeFolder(root, L"oracle"), MakeFolder(root, L"import")};

        TestFrozenDefaults();

        const std::string firstRun = ReadInput("first-run-d366649.ini");
        {
            EmptyFolder(folders.oracle);
            const std::wstring path = folders.oracle + L"\\" + kIniName;
            oracle::Config::LoadOrCreateDefault(Narrow(path));
            if (ReadBytes(path) != firstRun) {
                Fail("first run", "the oracle's first-run output is not inputs/first-run-d366649.ini");
            }
        }

        std::vector<std::pair<std::string, std::optional<std::string>>> inputs = {
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

        for (const auto& [name, bytes] : inputs) RunInput(folders, name, bytes);

        const std::vector<IniMutation> corpus = GenerateIniMutations(firstRun, FrozenReaderKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(folders, "corpus: " + m.name, m.bytes);

        std::printf("%zu inputs, %zu of them from the corpus\n", inputs.size() + corpus.size(), corpus.size());
        std::printf("comparison 1, d366649 against the frozen reader:\n");
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
        }

        for (const std::wstring& dir : {folders.oracle, folders.import}) {
            EmptyFolder(dir);
            RemoveDirectoryW(dir.c_str());
        }
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
