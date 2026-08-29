#include "build_profile.h"

#include <Windows.h>

#include "debug_log.h"

namespace headtracking {

namespace {

// Steam Win64, game v2.0.11.0 (build.txt Titanfall2_v2_0_11_0).
// client.dll, image base 0x180000000.
//
// CViewRender::RenderView is vftable slot 9, rva 0x3723b0, reached from slot 7
// (Render). Respawn's render view struct is matrix-based, not the QAngle-and-
// origin CViewSetup of stock Source: by the time RenderView runs, the view
// matrix (0x40), projection (0x80) and view-projection (0xc0) are already
// built, alongside the origin (0x00) and the 3x4 camera basis (0x10, rows
// forward / right / up).
//
// CViewRender::Render preps three of these structs and hands RenderView the
// third (0x12ecc0). Measured, not guessed: the one RenderView receives drives
// the VIEWMODEL, the sibling at 0xa13c0 holds the identical player camera and
// drives the WORLD, and the one at 0xb55c0 is the 3D skybox - same basis,
// its own origin. All three need the head rotation or the frame comes apart.
//
// Only origin, basis, view and view-projection are written. The main struct
// also carries origin copies at 0x1a0 and 0x1d4, but those offsets are NOT
// origins in the siblings: 0x1d0 there is a 64-bit heap pointer, and writing a
// float over its high half crashed the game inside engine.dll within a frame.
//
// 0x1ac is read, never written: it is the frame's zoom factor, the player's base
// FOV tangent over this frame's, so it sits at exactly 1.0 with the sights down
// and rises as they come up. That is what the ADS suppression runs on.
//
// 0xc3d940 holds the IVEngineClient the client talks to, and GetLevelName sits
// at vtable + 0x6f0 - the slot client.dll+0x3a0eb0 calls to build its own
// cached map name. Asking the interface directly is what the campaign-only gate
// runs on: the cache is written by game code we neither own nor invalidate, so
// its freshness across a campaign -> menu -> multiplayer transition would be an
// assumption rather than a fact.
//
// 0x158eb0 fills one crosshair RUI instance's arguments. It is called ONLY from
// the crosshair submitter at 0x15ef90, which CViewRender::RenderView calls at
// 0x3734bb - so a detour on it sees every crosshair element the frame draws and
// nothing else. The submitter allocates one transform per frame and hangs it off
// every instance it makes at +0x38; the instance carries the frame's render size
// at +0x08, and the transform is two records of {origin(2f), (0,1), xAxis(w,0,0,0),
// yAxis(0,h,0,0)}. Writing the FIRST record's origin moves the crosshair by that
// many pixels; the second record has the same shape and no visible effect.
// Measured in game, one lever per ten seconds against a fixed scene.
//
// 0x3092e0 is the client's RUI instance allocator - RuiCreate, reached from the
// script binding at 0x30b480 - taking (asset, topology id, draw group, sort key)
// and returning a handle. It fills one entry of the table at 0x1e7a5c0: entries
// of 0x20 bytes indexed by the handle's low 11 bits, holding the whole handle at
// +0x00 (so a stale one can be told apart by its generation counter), the
// topology id at +0x04 and the instance at +0x08. The instance's transform
// (+0x38 again) is the topology's own block, shared by every element drawn in
// it, which is why the hit indicator is given a private copy rather than having
// its topology moved. The asset's name is the first field of the asset, which is
// what the RUI system's own "UI %s doesn't expose argument %s" error prints.
//
// The RUI args are not the way in, despite `crosshair_movement_x` / `_y` sitting
// in the arg name table at 0x94c660: asked for by name through the game's own
// accessor (0xc3da00) on a live crosshair instance, both come back null. This
// crosshair does not carry them.
//
// player+0x1698 is the sights-up flag. Found from the RUI argument table: arg 21
// is `player_zoomFrac`, its handler in the jump table at 0x3d3ec8 calls
// 0x2c8440, and that function branches on this byte - non-zero and the fraction
// ramps toward 1 over the player's zoom-in time (+0x201c), zero and it decays to
// 0 from the value at +0x169c stamped at +0x16a0. It is a statement about the
// SIGHTS, not about magnification, which is what makes it right for a weapon
// like the P2011 that holds zoom=1.000 all the way through an aim.
//
// 0x22ac694 is the crosshair-state global. Found from the Crosshair_SetState
// script binding: the registration at 0x37e4d4 names it, the implementation it
// installs is the 80-byte 0x379ba0, and the only thing that function does with
// its argument is bounds-check it against CROSSHAIR_STATE_COUNT and store it
// there. Confirmed live - held at 1 from outside the process, the four crosshair
// dashes go and the hit markers stay.
//
// 0x348ae0 is a thin wrapper over IEngineTrace: it builds a Ray_t from
// (start, end, mins, maxs) through 0xabec0 and calls slot 4 (+0x20) of the
// `EngineTraceClient004` interface held at 0xc3d9c0. The trace_t offsets are
// read off the game's own consumer at 0x14be60, which takes the hit position
// from +0x10 when the fraction at +0x30 is below 1. Vectors are 16-byte
// aligned in Respawn's trace_t, so this is not stock Source's 0x0c / 0x2c.
//
// 0x359250 builds a render view from a setup struct: BuildRenderView(setup,
// out). It is where the frame's camera is born - it hands the setup's origin
// (setup+0xe4) and QAngle (setup+0xf0) to the matrix builder at 0x636420, which
// writes the view matrix at out+0x40, and then fills the projection, the
// view-projection and the field-of-view tangents from the same setup. The world
// view is a wholesale copy of the result (0x36b570), so rotating the setup's
// angles here rotates everything the frame is culled and drawn with.
//
// Found with a hardware write watchpoint on the main view's matrix, because
// static analysis had already pointed at the wrong function twice: the profiling
// label "OnRenderStart->CViewRender::SetUpView" names a time SPAN, and the call
// it brackets (slot 31 of the object at client.dll+0xb1d380) lands in 0x24db60,
// which is CInput::CAM_Think - it writes cam_idealpitch / cam_idealyaw /
// cam_idealdist at its tail. Hooking that on the strength of the label finds the
// third-person camera and nothing the campaign draws.
//
// engine.dll+0x7b6664 holds the player's view angles and is NOT the way in,
// though it reads correctly: held from outside the process at 7258 writes over
// ten seconds, the view never moved and the field was back to its own value the
// moment the loop stopped. It is a per-frame mirror, written from the source
// rather than read as one - fine for a harness to read the aim from, useless to
// write.
//
// 0x4e4000 is C_Titan_Cockpit::CalcView(this, Vector* origin, QAngle* angles) -
// the transform the Titan cockpit is drawn with. It scales the player's pitch by
// `cockpit_pitch_up_frac` / `cockpit_pitch_down_frac` (whose convar objects it
// reads at +0x58, the same value offset the FOV control uses), subtracts the
// cockpit drift computed at 0x2c58b0 from the four `cockpitDrift_*` convars, and
// ends on Source's usual ApplyShake(origin, angles, 1.0) through the view-effects
// interface at 0x363ff0.
//
// Its ONE caller, 0x141d90, resolves an entity handle and __RTDynamicCasts it
// from C_BaseEntity to C_Titan_Cockpit before calling, so the detour runs when
// there is a cockpit to place and never otherwise - which is the whole Titan
// gate, with no flag to read and none to get wrong. That caller is reached from
// SetUpView (0x35aef0) at +0x247, and SetUpView builds the frame's camera at
// +0x9fc, so the cockpit is placed BEFORE the view the head rotation goes into.
// That ordering is why the cockpit hook is the one that decides the frame's pose
// (camera_hook.h, OpenFrame).
//
// 0x19a9f0 is the client's world-to-screen: it asks the CViewRender for the
// frame's world-to-screen matrix (vtable slot 14, which returns the pointer at
// CViewRender+0x12ef90), projects through it with the row-major helper at
// 0x366010, and scales the result into pixels. Found from the other end - the
// `screen_indicator_ellipse_width` / `_height` / `_back_range` / `_pitch_limit` /
// `_pitch_scale` convars, whose one common consumer (0x199330) is the placement
// that clamps an off-screen marker onto an ellipse. Its sibling 0x199830 and the
// `GetEntScreenSpaceBounds` script native above them (registered at 0x13ed70,
// reached through 0x15c050 and 0x14d370) all bottom out in 0x19a9f0, which makes
// it the single point every world-anchored HUD mark is placed through.
//
// engine.dll+0x7a6620 is the host's pause flag, sitting three fields ahead of
// the server-side level name at 0x7a6634. Found by snapshotting the module's
// writable pages across four pause/unpause transitions and asking for the bytes
// that read 1/0/1/0 (.lab/pause_probe.py); it was the only candidate in static
// data, the rest were transient heap. Being a host-side flag is fine here: the
// only session this mod engages on is the campaign, where the client IS the
// host.
//
// Offsets read off a live frame and a live process; see .lab/NOTES.md.
constexpr BuildProfile kSteamProfile_20171205 = {
    "steam-win64-20171205",
    { 0x5A271254u, 0x03D4C000u, 0x00000000u },   // client.dll
    { 0x5A271230u, 0x14DB3000u, 0x00000000u },   // engine.dll
    {
        .render_view_rva      = 0x3723B0u,
        .origin               = 0x00u,
        .basis                = 0x10u,      // rows forward/right/up, 4-float stride
        .view_matrix          = 0x40u,
        .proj_matrix          = 0x80u,
        .viewproj_matrix      = 0xC0u,
        .tan_fov              = 0x180u,     // tan(fovX/2), tan(fovY/2), znear
        .zoom                 = 0x1ACu,     // base FOV tangent / this frame's
        .world_view           = 0xA13C0u,   // CViewRender + this: world view
        .skybox_view          = 0xB55C0u,   // CViewRender + this: 3D skybox
        .main_view            = 0x12ECC0u,  // CViewRender + this: RenderView's arg
        .view_build_rva       = 0x359250u,  // builds a render view from a setup
        .setup_angles         = 0xF0u,      // the setup's QAngle (pitch,yaw,roll)
        .engine_client_ptr    = 0xC3D940u,
        .get_level_name_slot  = 0x6F0u,     // vtable slot 222
        .engine_paused_flag   = 0x7A6620u,  // engine.dll rva, NOT client.dll
        .crosshair_args_rva   = 0x158EB0u,
        .crosshair_submit_rva = 0x15EF90u,
        .player_ads_flag      = 0x1698u,    // player rva, non-zero = sights up
        .rui_instance_size    = 0x08u,      // (width, height) of the drawn frame
        .rui_instance_transform = 0x38u,    // -> shared transform block
        .rui_transform_origin = 0x10u,      // crosshair position, pixels
        .rui_create_rva       = 0x3092E0u,  // the client's RUI instance allocator
        .rui_instance_table   = 0x1E7A5C0u, // entries of 0x20, instance at +0x08
        .crosshair_state      = 0x22AC694u,
        .world_to_screen_rva  = 0x19A9F0u,  // world position -> screen pixels
        .trace_line_rva       = 0x348AE0u,
        .trace_endpos         = 0x10u,      // trace_t: hit position
        .trace_fraction       = 0x30u,      // trace_t: 1.0 = hit nothing
        .cockpit_calc_view_rva = 0x4E4000u,  // C_Titan_Cockpit::CalcView
        .cvar_interface_ptr   = 0x2E43F80u,
        .find_var_slot        = 0x80u,      // ICvar::FindVar
        .convar_float         = 0x58u,      // ConVar::m_fValue, m_nValue at 0x5c
    },
};

// Append-only. A patch that moves these gets a NEW entry on top; never edit an
// existing one, or every user still on that build is stranded.
constexpr BuildProfile kKnownProfiles[] = {
    kSteamProfile_20171205,
};

const BuildProfile* g_active = nullptr;
uintptr_t g_clientBase = 0;
uintptr_t g_engineBase = 0;

// Waits for a module the game loads slightly after our DllMain runs. client.dll
// and engine.dll are both up well before the first rendered frame; 20 seconds
// is a generous ceiling on a cold start off a slow disk.
HMODULE WaitForModule(const char* name) {
    HMODULE mod = nullptr;
    for (int i = 0; i < 200 && !mod; ++i) {
        mod = GetModuleHandleA(name);
        if (!mod) Sleep(100);
    }
    return mod;
}

// Every offset the mod cannot safely run without. A profile whose fingerprints
// match but whose offsets are still zero is a legitimate, deliberate state -
// `pixi run check-fingerprint` prints exactly that stub so a new build can be
// RECOGNISED the day it lands, before anyone has rederived an rva - and it must
// stay dormant.
//
// The list is not just the hook target. Zero is a live address, so a
// half-filled profile is worse than an empty one:
//   - get_level_name_slot 0 makes the level-name call `vtable[0]`, which on a
//     COM-style Source interface is the scalar-deleting destructor. The mod
//     would destroy the engine client once per rendered frame, and the SEH
//     guard around the call catches the fault only AFTER engine code has run.
//   - engine_client_ptr 0 reads client.dll's DOS header as a pointer. The
//     bytes there are non-null, so the null check passes and we dereference
//     0x0000000300905A4D as a vtable.
// Anything the safety gates read is therefore as load-bearing as the hook rva,
// not less, and belongs here. Fields whose real value IS zero (`origin` is at
// +0x00) cannot be checked this way and are deliberately absent.
bool ProfileComplete(const BuildProfile& p) {
    const auto& o = p.offsets;
    return o.render_view_rva != 0 && o.main_view != 0 && o.engine_client_ptr != 0
        && o.get_level_name_slot != 0 && o.engine_paused_flag != 0
        // Without these the crosshair cannot be moved onto the gun, and a
        // crosshair that marks the head rather than the aim is worse than no
        // head tracking - the player shoots where it points.
        && o.crosshair_args_rva != 0 && o.rui_instance_transform != 0
        // Without the sights detector the view keeps following the head while
        // aiming AND the positional lean keeps moving the eye off the sight
        // line, which is worse than no head tracking on that weapon.
        && o.crosshair_submit_rva != 0 && o.player_ads_flag != 0
        // The aim trace is NOT in this list. Without it the crosshair is
        // projected as a direction, which is exactly right until the player
        // leans and is a slow drift rather than a wrong picture after that -
        // worth having the rest of the mod for.
        && o.crosshair_state != 0;
}

const char* MismatchHint(const cameraunlock::memory::PeFingerprint& running,
                         const cameraunlock::memory::PeFingerprint& primary) {
    switch (cameraunlock::memory::ClassifyMismatch(running, primary)) {
        case cameraunlock::memory::FingerprintMismatch::Newer:
            return "game is newer than this mod knows about; check the releases page for an update";
        case cameraunlock::memory::FingerprintMismatch::Older:
            return "game is older; let Steam finish updating";
        default:
            return "tampered or repacked client.dll; mod will not engage";
    }
}

}  // namespace

bool SelectProfile() {
    HMODULE client = WaitForModule("client.dll");
    if (!client) {
        HT_LOG("[build] client.dll never loaded - staying dormant");
        return false;
    }
    HMODULE engine = WaitForModule("engine.dll");
    if (!engine) {
        HT_LOG("[build] engine.dll never loaded - staying dormant");
        return false;
    }

    cameraunlock::memory::PeFingerprint fp{};
    cameraunlock::memory::PeFingerprint engineFp{};
    if (!cameraunlock::memory::ReadPeFingerprint(client, fp)
            || !cameraunlock::memory::ReadPeFingerprint(engine, engineFp)) {
        HT_LOG("[build] could not read the module fingerprints - staying dormant");
        return false;
    }
    HT_LOG("[build] client.dll TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
           fp.TimeDateStamp, fp.SizeOfImage, fp.CheckSum);
    HT_LOG("[build] engine.dll TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
           engineFp.TimeDateStamp, engineFp.SizeOfImage, engineFp.CheckSum);

    // Every rejection CONTINUES rather than returning. The registry is
    // append-only and grows for the life of the mod, so "this profile is not
    // the one" must never be read as "no profile is". Returning early breaks
    // the guarantee the registry exists for: ship a new profile for a patch
    // that changed engine.dll and left client.dll alone, and a user still on
    // the old build would match the new profile's client fingerprint, fail its
    // engine fingerprint, and go dormant - stranded by a release that was
    // supposed to leave them untouched, with a perfect profile for their build
    // sitting one entry further down the array.
    for (const auto& p : kKnownProfiles) {
        if (!p.client_fingerprint.Matches(fp)) {
            HT_LOG("[build] profile '%s': client.dll does not match", p.name);
            continue;
        }
        // A client.dll we know paired with an engine.dll we do not is not a
        // build anyone shipped. The profile pins rvas in both, so half a match
        // means half the offsets are for some other binary.
        if (!p.engine_fingerprint.Matches(engineFp)) {
            HT_LOG("[build] profile '%s': client.dll matches but engine.dll does not", p.name);
            continue;
        }
        if (!ProfileComplete(p)) {
            HT_LOG("[build] profile '%s': matches, but its offsets are not filled in yet",
                   p.name);
            continue;
        }
        g_active = &p;
        g_clientBase = reinterpret_cast<uintptr_t>(client);
        g_engineBase = reinterpret_cast<uintptr_t>(engine);
        HT_LOG("[build] matched profile '%s'", p.name);
        return true;
    }

    // Diagnostic primary is the newest profile, at the top of the array.
    HT_LOG("[build] no profile matches - staying dormant, game runs vanilla (%s)",
           MismatchHint(fp, kKnownProfiles[0].client_fingerprint));
    return false;
}

bool HasActiveProfile() { return g_active != nullptr; }

const BuildProfile& ActiveProfile() { return *g_active; }

uintptr_t ClientBase() { return g_clientBase; }

uintptr_t EngineBase() { return g_engineBase; }

}  // namespace headtracking
