#pragma once

#include <string>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/data/position_settings.h"

namespace headtracking {

namespace legacy {
struct Config;
enum class ReadStatus;
}  // namespace legacy

// Next to Titanfall2.exe.
constexpr wchar_t kConfigFileName[] = L"CameraUnlock.ini";
// The file every build before the canonical config format read, beside it. Imported once while
// CameraUnlock.ini is absent and never written.
constexpr wchar_t kLegacyConfigFileName[] = L"HeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it. The settings file's first
// line names it.
constexpr char kGameDisplayName[] = "Titanfall 2";

// Source units per metre of head movement. 1 unit is 1 inch, so 39.37 moves the view as far as
// the head moved.
constexpr float kSourceUnitsPerMetre = 39.37f;

struct Config : cameraunlock::HeadTrackingConfig {
    // The field of view to draw at, in the same degrees the game's own slider uses. 0 follows
    // that slider, live. Anything else overrides it, including outside the 70-119 degrees the
    // slider allows: the mod holds cl_fovScale at a value of its own every frame, so the drawn
    // field of view is already ours to pick. See fov_control.h.
    float fov_override_degrees = 0.0f;

    // How much wider than the DRAWN field of view the engine is told to cull. The picture is
    // scaled back down before it is drawn, so this changes nothing you can see. 1.0 is none:
    // the culling frustum is aimed at the head (view_angles_hook.h), so only a positional lean
    // at the edge of the frame can use more.
    float cull_fov_scale = 1.0f;

    // Per-frame [view] diagnostics only. The lifecycle lines (build fingerprint, profile
    // match, hook install, map gate, fault report) are written unconditionally. See
    // debug_log.h.
    bool log_to_file = false;
    // One-shot dump of the three render view structs, for rederiving their field offsets
    // against a live frame.
    bool dump_view_setup = false;
};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw are Writable: the
// mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> ConfigTable();

// HeadTracking.ini as the builds before the canonical format read it (legacy_config/), mapped
// into Config.
cameraunlock::config::LegacyImport<Config> LegacyConfigImport();

// The map from what the frozen reader read into the settings the mod runs on.
cameraunlock::config::ImportResult MapLegacyConfig(legacy::ReadStatus status, const legacy::Config& read,
                                                   Config& out);

// The owner's options for CameraUnlock.ini in `folder`, with HeadTracking.ini beside it as the
// legacy file. `folder` ends in a separator.
cameraunlock::config::ConfigOwnerOptions<Config> ConfigOwnerOptionsFor(const std::wstring& folder,
                                                                       cameraunlock::config::DefaultsFile defaults);

// The folder Titanfall2.exe is in, ending in a separator.
std::wstring ConfigFolder();

// What the position processor is handed. X and Z are inverted, Y is not: the trackers this
// mod is used with send sideways and forward the other way round from the shared core's
// frame, so without it a lean left moves the camera right and leaning in pulls the view
// back. Every earlier build shipped that as [Position] InvertX=true and InvertZ=true. The
// processor applies the inversion BEFORE its asymmetric Z clamp, which keeps the generous
// forward allowance on the physical forward lean (see the Z note in the .cpp).
cameraunlock::PositionSettings PositionSettingsFor(const Config& c);

}  // namespace headtracking
