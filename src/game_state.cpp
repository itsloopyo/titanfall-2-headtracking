#include "game_state.h"

#include <Windows.h>

#include <cctype>
#include <cstdint>
#include <cstring>

#include "build_profile.h"
#include "debug_log.h"
#include "engine_interface.h"

namespace headtracking {

namespace {

// A Titanfall 2 map name is well under this. The bound is what stops a build
// whose offsets have moved from turning a missing terminator into a runaway
// read.
constexpr size_t kMapNameCapacity = 64;

// Titanfall 2 names every campaign map sp_* and everything multiplayer -
// playlists, Frontier Defense, the lobby - mp_*. These two prefixes ARE the
// gate; nothing else distinguishes a match from the campaign.
constexpr char kCampaignPrefix[] = "sp_";
constexpr char kMultiplayerPrefix[] = "mp_";
constexpr size_t kPrefixLength = 3;

char g_mapName[kMapNameCapacity + 1] = {};

// Once anything says multiplayer, it says so for the rest of the process.
//
// Every signal here is a per-frame test, and a per-frame test has a safety
// margin of exactly one frame: a stale read, a torn string, an offset that
// moved, and the gate is open again. Latching makes the failure mode "tracking
// stopped, relaunch the game" instead of "tracking was live in someone else's
// match", and those two costs are not remotely comparable.
bool g_latchedMultiplayer = false;

// Northstar can finish loading AFTER our bootstrap thread has run its check, so
// refusing to hook at startup (plugin.cpp) is a first line, not a guarantee: the
// two proxy DLLs load in an order nothing here controls. Re-asked while the game
// runs and folded into the same latch, because the reason Northstar is
// disqualifying does not expire - its servers host campaign maps as multiplayer
// arenas, so the sp_/mp_ prefix test below cannot tell a match from a campaign.
//
// Rate-limited because GetModuleHandle takes the loader lock and this is called
// from the render thread once per frame. 300 frames is a few seconds, which is
// far inside the time it takes to reach a server.
constexpr int kNorthstarRecheckFrames = 300;

void LatchNorthstarIfPresent() {
    if (g_latchedMultiplayer) return;
    static int s_untilRecheck = 0;
    if (s_untilRecheck-- > 0) return;
    s_untilRecheck = kNorthstarRecheckFrames;

    if (!NorthstarPresent()) return;
    g_latchedMultiplayer = true;
    HT_LOG("[state] Northstar loaded after startup - head tracking suppressed for the rest of "
           "this session. Northstar is a multiplayer client and its servers can host campaign "
           "maps, so a map-name check cannot tell a campaign apart from a match.");
}

// When the level name last changed. The name is set as soon as the client
// processes server info, which is before the level is playable: the first
// rendered frames of a load are a loading screen and a camera that has not been
// placed yet, and the mod is supposed to be quiet outside gameplay. Tracking
// during a load is cosmetic rather than dangerous, but it is also the window in
// which the camera is least likely to be what we think it is.
uint64_t g_mapChangedTick = 0;
constexpr uint64_t kWarmupMs = 1500;

using GetLevelNameFn = const char*(*)(void* self);

// Asks the engine for the level name it is actually running, rather than
// reading client.dll's cached, lowercased copy of the same string. The cache is
// written by game code we do not own and do not invalidate, so its freshness
// across a campaign -> menu -> multiplayer transition is an assumption. This is
// the source that cache is built from, so there is nothing to go stale.
//
// Returns nullptr if the interface is not up yet, which reads as "no level".
const char* QueryLevelName() {
    if (!HasActiveProfile()) return nullptr;
    const auto& off = ActiveProfile().offsets;

    void* iface = EngineInterface(off.engine_client_ptr);
    auto fn = InterfaceMethod<GetLevelNameFn>(iface, off.get_level_name_slot);
    if (!fn) return nullptr;
    return fn(iface);
}

// "maps/sp_training.bsp" -> "sp_training". The engine hands back a path; the
// prefix test needs the bare name.
void CopyBaseName(const char* src) {
    g_mapName[0] = '\0';
    if (!src) return;

    // The engine's own path, so it is well under this. The bound is what stops
    // a missing terminator on some future build from being a runaway read.
    constexpr ptrdiff_t kMaxPathScan = 512;
    const char* start = src;
    for (const char* p = src; *p && (p - src) < kMaxPathScan; ++p) {
        if (*p == '/' || *p == '\\') start = p + 1;
    }

    size_t n = 0;
    while (n < kMapNameCapacity && start[n] != '\0' && start[n] != '.') {
        g_mapName[n] = static_cast<char>(std::tolower(static_cast<unsigned char>(start[n])));
        ++n;
    }
    g_mapName[n] = '\0';
}

// The host's pause flag. Set while the campaign pause menu is up, cleared on
// resume; found by diffing the module's writable pages across four pause /
// unpause transitions (see build_profile.cpp).
//
// Reading the HOST's flag rather than a client-side one is sound precisely
// because of the gate above it: this is only ever consulted on an sp_* map,
// where the client is the listen server, so the host state is this session's
// state. It is never reached in multiplayer, where it would be someone else's.
//
// Anything other than a clean 0 says paused. A value that is neither 0 nor 1 is
// not the field we think it is, and suppressing tracking is the cheap failure.
bool HostPaused() {
    if (!HasActiveProfile()) return true;
    const uintptr_t base = EngineBase();
    uint32_t value = 1;
    __try {
        value = *reinterpret_cast<const volatile uint32_t*>(
            base + ActiveProfile().offsets.engine_paused_flag);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 1;
    }
    if (value > 1) {
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            HT_LOG("[state] pause flag reads %u, which is neither 0 nor 1 - treating the "
                   "game as paused. Please report this log with your game version.", value);
        }
        return true;
    }
    return value != 0;
}

}  // namespace

SessionKind CurrentSession() {
    LatchNorthstarIfPresent();

    // A faulting read here would take the whole game down from inside the
    // render detour. The engine interface is a pointer chase into another
    // module, so it is guarded rather than trusted.
    const char* raw = nullptr;
    __try {
        raw = QueryLevelName();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        raw = nullptr;
    }
    __try {
        CopyBaseName(raw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_mapName[0] = '\0';
    }

    static char s_lastLogged[kMapNameCapacity + 1] = { '\1' };
    if (std::strcmp(s_lastLogged, g_mapName) != 0) {
        std::memcpy(s_lastLogged, g_mapName, sizeof(g_mapName));
        g_mapChangedTick = GetTickCount64();
        HT_LOG("[state] map '%s'", g_mapName);
    }

    if (std::strncmp(g_mapName, kMultiplayerPrefix, kPrefixLength) == 0) {
        g_latchedMultiplayer = true;
    }
    if (g_latchedMultiplayer) return SessionKind::Multiplayer;

    if (std::strncmp(g_mapName, kCampaignPrefix, kPrefixLength) == 0) {
        if (GetTickCount64() - g_mapChangedTick < kWarmupMs) return SessionKind::Loading;
        return HostPaused() ? SessionKind::Paused : SessionKind::Campaign;
    }
    // Anything else - no level, a menu, or a naming scheme this build does not
    // use - is not positively a campaign, and a gate whose job is to stay out of
    // multiplayer has to fail closed.
    return SessionKind::NoLevel;
}

const char* CurrentLevelName() { return g_mapName; }

bool NorthstarPresent() {
    for (const char* dll : { "Northstar.dll", "NorthstarLauncher.exe", "r2ds.dll" }) {
        if (GetModuleHandleA(dll)) return true;
    }
    char exe[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameA(nullptr, exe, sizeof(exe));
    if (len > 0 && len < sizeof(exe)) {
        for (DWORD i = 0; i < len; ++i) {
            exe[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(exe[i])));
        }
        if (std::strstr(exe, "northstar")) return true;
    }
    return false;
}

}  // namespace headtracking
