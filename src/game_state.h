#pragma once

namespace headtracking {

enum class SessionKind {
    NoLevel,       // main menu, between levels
    Loading,       // an sp_* map whose name has only just appeared
    Campaign,      // an sp_* map, being played
    Paused,        // an sp_* map with the pause menu up
    Multiplayer,   // an mp_* map: playlists, Frontier Defense, the lobby
};

// Classifies the level the client currently has loaded, by calling
// IVEngineClient::GetLevelName() through the interface the client itself uses
// (see build_profile.cpp). Asking the interface rather than reading client.dll's
// own cached, lowercased copy of the same string removes a staleness question:
// that cache is written by game code we neither own nor invalidate. Asking the
// CLIENT rather than the local server matters too - on a multiplayer server the
// client is not hosting, so the server-side map name is stale or empty while the
// client's is always the map being rendered.
//
// Titanfall 2 names every campaign map sp_* and everything multiplayer -
// playlists, Frontier Defense, the lobby - mp_*.
//
// A campaign map is reported as Paused whenever the host's pause flag is set.
// Only Campaign is tracked on, so everything that pauses the game - the pause
// menu, and anything else that trips the same flag - suppresses tracking.
//
// Cheap enough to call per frame: a bounded string read, a 3-byte compare and
// one dword read.
SessionKind CurrentSession();

// The raw map name, "" when none is loaded. Diagnostics only.
const char* CurrentLevelName();

// True when the Northstar community multiplayer client is in the process.
//
// Northstar runs on the RETAIL client.dll - same bytes, same PE fingerprint -
// so the build profile matches and this mod loads under it exactly as under
// vanilla. Its servers are also not restricted to mp_* maps: community servers
// host campaign maps as multiplayer arenas, which walks straight through a
// map-name prefix test. There is no way to tell those apart from a real
// campaign, so the only honest answer is to not engage at all.
//
// Checked before the hook is installed, because refusing to install is strictly
// stronger than refusing to apply: under Northstar the mod then never patches
// client.dll, never walks the engine interface and never reads a render view.
// The game is bit-for-bit vanilla, which is the same posture the mod takes on a
// build it does not recognise.
//
// Also re-asked while the game runs, rate-limited, from CurrentSession(). The
// install-time check happens as the process starts, where our proxy DLL and
// Northstar's are racing, so a Northstar that loads a moment later would answer
// "no" to it and walk straight through. The re-check cannot un-install the hook,
// but it latches the session to Multiplayer, which is what stops anything being
// applied.
bool NorthstarPresent();

}  // namespace headtracking
