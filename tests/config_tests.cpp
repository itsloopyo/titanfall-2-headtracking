// CameraUnlock.ini against the table: the committed CameraUnlock.ini is the table's fresh render
// byte for byte, the owner creates exactly those bytes, each toggle's save changes the lines of
// its own rows and no other byte, and the axis conversion the position processor is handed is
// the one every earlier build shipped. `--render-config <path>` writes the fresh render to
// <path> instead and runs nothing else (pixi run render-config).
//
// Every owner here reads and creates a scratch Defaults.ini, never the player's own.

#include "config.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/input/key_bindings.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace cfg = cameraunlock::config;
using headtracking::Config;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read a test file");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write a test file");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string FreshRender() {
    return cfg::RenderCanonicalFresh(headtracking::ConfigTable(), cfg::RenderHeader{headtracking::kGameDisplayName});
}

// A fresh folder in %TEMP%, ending in a separator, with Defaults.ini in a folder of its own.
struct Scratch {
    std::wstring folder;
    std::wstring defaults;
};

Scratch MakeScratch(const wchar_t* name) {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring root = std::wstring(temp) + L"titanfall2-config-tests-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(root.c_str(), nullptr);
    const std::wstring dir = root + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr)) throw std::runtime_error("cannot create a scratch folder");
    const std::wstring global = dir + L"\\global";
    if (!CreateDirectoryW(global.c_str(), nullptr)) throw std::runtime_error("cannot create a scratch folder");
    return {dir + L"\\", global + L"\\Defaults.ini"};
}

cfg::ConfigOwnerOptions<Config> Options(const Scratch& s) {
    return headtracking::ConfigOwnerOptionsFor(s.folder, cfg::DefaultsFile::At(s.defaults));
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < bytes.size()) {
        const size_t end = bytes.find("\r\n", start);
        if (end == std::string::npos) {
            lines.push_back(bytes.substr(start));
            break;
        }
        lines.push_back(bytes.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}

// The lines of `after` that differ from `before`, which must have as many.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) return {"a line was added or removed"};
    std::vector<std::string> changed;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

void CommittedFileIsTheFreshRender() {
    std::printf("CameraUnlock.ini, the committed file, is the table's fresh render\n");
    Check(ReadBytes(TITANFALL2_COMMITTED_CONFIG) == FreshRender(), "run pixi run render-config after changing a row");
}

void EveryHotkeyDefaultParses() {
    std::printf("every hotkey list the table defaults to parses, and they share no key\n");
    const Config defaults = headtracking::ConfigTable().defaults();
    Check(defaults.toggle_key_name == "End, Ctrl+Shift+Y", "ToggleKey is the fleet's default");
    Check(defaults.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G", "CycleTrackingModeKey is the fleet's default");
    Check(defaults.yaw_mode_key_name == "PageDown, Ctrl+Shift+H", "YawModeKey is the fleet's default");
    std::vector<cameraunlock::input::KeyBinding> all;
    for (const std::string* list :
         {&defaults.toggle_key_name, &defaults.cycle_tracking_mode_key_name, &defaults.yaw_mode_key_name}) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        Check(parsed.ok(), list->c_str());
        all.insert(all.end(), parsed.bindings.begin(), parsed.bindings.end());
    }
    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = i + 1; j < all.size(); ++j) {
            Check(!(all[i].vk == all[j].vk && all[i].modifiers == all[j].modifiers), "two actions share a default key");
        }
    }
}

void FirstLaunchCreatesTheCommittedFile() {
    std::printf("the first launch with no file creates the committed bytes\n");
    const Scratch s = MakeScratch(L"created");
    cfg::ConfigOwner<Config> owner(Options(s));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Created, "the load is Created");
    Check(ReadBytes(s.folder + headtracking::kConfigFileName) == FreshRender(), "the created file is the fresh render");
    Check(GetFileAttributesW((s.folder + headtracking::kLegacyConfigFileName).c_str()) == INVALID_FILE_ATTRIBUTES,
          "no HeadTracking.ini is written");
}

void TogglesSaveTheirRowsOnly() {
    std::printf("each toggle's save writes its own rows and no other byte\n");
    const Scratch s = MakeScratch(L"saves");
    const std::wstring path = s.folder + headtracking::kConfigFileName;
    {
        cfg::ConfigOwner<Config> owner(Options(s));
        owner.Load();
    }
    const std::string fresh = ReadBytes(path);

    cfg::ConfigOwner<Config> owner(Options(s));
    owner.Load();
    const cfg::ConfigSaveResult yaw = owner.Save([](Config& c) { c.world_space_yaw = false; });
    Check(yaw.status == cfg::ConfigSaveStatus::Saved, "the yaw save is Saved");
    Check(!yaw.log.empty(), "the log says WorldSpaceYaw no longer follows Defaults.ini");
    const std::string afterYaw = ReadBytes(path);
    const std::vector<std::string> yawLines = ChangedLines(fresh, afterYaw);
    Check(yawLines.size() == 1 && yawLines[0] == "WorldSpaceYaw=false", "only WorldSpaceYaw=default became false");

    const cfg::ConfigSaveResult mode = owner.Save([](Config& c) {
        c.rotation_enabled = false;
        c.position_enabled = true;
    });
    Check(mode.status == cfg::ConfigSaveStatus::Saved, "the mode save is Saved");
    const std::vector<std::string> modeLines = ChangedLines(afterYaw, ReadBytes(path));
    Check(modeLines.size() == 2 && modeLines[0] == "RotationEnabled=false" && modeLines[1] == "PositionEnabled=true",
          "a mode change writes both rows of the pair and nothing else");

    bool threw = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::exception&) {
        threw = true;
    }
    Check(threw, "EnableOnStartup is not Writable: End never persists");

    cfg::ConfigOwner<Config> again(Options(s));
    const cfg::ConfigLoadResult<Config> reread = again.Load();
    Check(reread.status == cfg::ConfigLoadStatus::Canonical, "the saved file reads back as canonical");
    Check(!reread.config.world_space_yaw, "the yaw choice survives a restart");
    Check(!reread.config.rotation_enabled && reread.config.position_enabled, "the mode survives a restart");
}

// The settings every earlier build handed the position processor with the file it shipped:
// ApplyPositionConfig at cc165ee on [Position] SensX/Y/Z=1, InvertX=true, InvertY=false,
// InvertZ=true, LimitY copied into both vertical limits and LimitZ and LimitZBack swapped.
cameraunlock::PositionSettings Published(float x, float y, float z, float zBack) {
    cameraunlock::PositionSettings ps;
    ps.sensitivity_x = 1.0f;
    ps.sensitivity_y = 1.0f;
    ps.sensitivity_z = 1.0f;
    ps.invert_x = true;
    ps.invert_y = false;
    ps.invert_z = true;
    ps.limit_x = x;
    ps.limit_y = y;
    ps.limit_y_down = y;
    ps.limit_z = zBack;
    ps.limit_z_back = z;
    return ps;
}

bool SameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

// Smoothing is left out: the session overwrites both of the processor's smoothing values with
// its own, which Plugin::Initialize sets from the smoothing rows after this.
bool SameAxisConversion(const cameraunlock::PositionSettings& a, const cameraunlock::PositionSettings& b) {
    return SameBits(a.sensitivity_x, b.sensitivity_x) && SameBits(a.sensitivity_y, b.sensitivity_y) &&
           SameBits(a.sensitivity_z, b.sensitivity_z) && SameBits(a.limit_x, b.limit_x) &&
           SameBits(a.limit_y, b.limit_y) && SameBits(a.limit_y_down, b.limit_y_down) &&
           SameBits(a.limit_z, b.limit_z) && SameBits(a.limit_z_back, b.limit_z_back) && a.invert_x == b.invert_x &&
           a.invert_y == b.invert_y && a.invert_z == b.invert_z;
}

void AxisConversionIsThePublishedOne() {
    std::printf("the position processor gets the axis conversion every earlier build shipped\n");
    const Config defaults = headtracking::ConfigTable().defaults();
    Check(SameAxisConversion(headtracking::PositionSettingsFor(defaults), Published(0.30f, 0.20f, 0.40f, 0.10f)),
          "at the defaults");
    Check(defaults.position.limit_z > defaults.position.limit_z_back, "more room to lean in than to pull back");

    Config leaned = defaults;
    leaned.position.limit_x = 0.5f;
    leaned.position.limit_y = 0.25f;
    leaned.position.limit_y_down = 0.25f;
    leaned.position.limit_z = 0.6f;
    leaned.position.limit_z_back = 0.05f;
    Check(SameAxisConversion(headtracking::PositionSettingsFor(leaned), Published(0.5f, 0.25f, 0.6f, 0.05f)),
          "with every limit moved");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
            const std::string path = argv[2];
            WriteBytes(std::wstring(path.begin(), path.end()), FreshRender());
            std::printf("wrote %s\n", argv[2]);
            return 0;
        }

        std::printf("Titanfall2HeadTracking config tests\n===================================\n");
        CommittedFileIsTheFreshRender();
        EveryHotkeyDefaultParses();
        FirstLaunchCreatesTheCommittedFile();
        TogglesSaveTheirRowsOnly();
        AxisConversionIsThePublishedOne();
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("All tests passed!\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
