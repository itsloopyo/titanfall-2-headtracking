#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace headtracking {

// Everything this mod pins to a specific Titanfall 2 build: the render view we
// mutate, and the two things that say whether we are allowed to mutate it - the
// map name and the pause flag. Everything is a client.dll RVA except the one
// field named engine_*, which is an engine.dll RVA.
struct OffsetTable {
    // Offsets within the render view struct RenderView is handed. All three
    // matrices are row-major 4x4 floats.
    uint32_t render_view_rva;   // CViewRender::RenderView
    uint32_t origin;            // camera origin (Vector)
    uint32_t basis;             // 3x4 camera basis, rows forward / right / up
    uint32_t view_matrix;       // world -> camera
    uint32_t proj_matrix;       // camera -> clip
    uint32_t viewproj_matrix;   // proj * view, what the shaders sample
    uint32_t tan_fov;           // tan(fovX/2), tan(fovY/2), znear (diagnostics)
    uint32_t zoom;              // base FOV tangent / this frame's: 1.0 unzoomed
    // CViewRender::Render preps THREE render view structs and then hands
    // RenderView the third. These are the byte offsets of all three from the
    // CViewRender `this` pointer. Which is which is load-bearing, not
    // decorative: the main one is the only one carrying the zoom field, and the
    // skybox deliberately takes the rotation without the lean.
    uint32_t world_view;   // CViewRender + this: the world pass
    uint32_t skybox_view;  // CViewRender + this: the 3D skybox
    uint32_t main_view;    // CViewRender + this: the struct RenderView is handed
    // The function that builds the frame's camera from a setup struct, and the
    // byte offset of that struct's QAngle. The head rotation goes into those
    // angles for exactly the span of that call and comes straight back out, so
    // everything derived from the camera - the world view, the skybox, the
    // culling frustum - is built pointing where the head is looking
    // (view_angles_hook.h).
    uint32_t view_build_rva;
    uint32_t setup_angles;
    // IVEngineClient*, and the byte offset of GetLevelName within its vtable.
    // Asked per frame for the level actually loaded - see game_state.cpp.
    uint32_t engine_client_ptr;
    uint32_t get_level_name_slot;
    // ENGINE.DLL rva. The host's pause flag: 1 while the campaign pause menu is
    // up, 0 during play. Read per frame to keep tracking out of the pause menu.
    uint32_t engine_paused_flag;
    // Moving the game's own crosshair onto the gun (crosshair_hook.h).
    // crosshair_args_rva fills one crosshair RUI instance's arguments and is
    // called only by the crosshair submitter, so a detour there sees every
    // crosshair element of the frame and nothing else. From the instance:
    // rui_instance_size is the frame's render size, rui_instance_transform holds
    // the shared transform, and rui_transform_origin within it is the whole
    // crosshair's position in pixels.
    uint32_t crosshair_args_rva;
    // The crosshair submitter, hooked for one thing only: it is handed the local
    // player once per frame by RenderView, and player_ads_flag on that player is
    // the byte the game's own zoom-fraction maths branches on - which is how the
    // mod knows the sights are up on a weapon that does not magnify
    // (ads_state.h).
    uint32_t crosshair_submit_rva;
    uint32_t player_ads_flag;
    uint32_t rui_instance_size;
    uint32_t rui_instance_transform;
    uint32_t rui_transform_origin;
    // The client's RUI instance allocator, which every RUI the client script
    // creates goes through, and the table it fills. Hooked to catch the hit
    // indicator the game makes on each hit and move it onto the crosshair with
    // everything else (hit_indicator.h).
    uint32_t rui_create_rva;
    uint32_t rui_instance_table;
    // client.dll rva of the crosshair-state global the CROSSHAIR_STATE_* script
    // constants write: 0 shows the crosshair, 1 leaves only the hit markers, 2
    // hides the lot. Set to 1 only when the head has turned so far that the gun
    // is behind the picture and there is nowhere honest to draw the crosshair.
    uint32_t crosshair_state;
    // The client's world trace, and the two fields of the trace_t it fills that
    // the mod reads. Used to find how far away what the gun is pointing at is,
    // so the crosshair can be projected as a POINT and stays on target when a
    // positional lean moves the eye off the shot line (aim_trace.h).
    // Trace(start, end, mins, maxs, mask, trace_out).
    uint32_t trace_line_rva;
    uint32_t trace_endpos;
    uint32_t trace_fraction;
    // ICvar*, the byte offset of FindVar within its vtable, and the offset of a
    // ConVar's float value. Used to hold cl_fovScale wider than the field of view
    // being drawn, so the engine stops culling what the head can turn to look at,
    // and to read the player's own FOV setting back out (fov_control.cpp).
    uint32_t cvar_interface_ptr;
    uint32_t find_var_slot;
    uint32_t convar_float;
};

// Both modules are fingerprinted, not just client.dll: the profile pins an
// engine.dll rva too, and a client/engine pair that did not ship together is
// exactly the case where one of the two sets of offsets is wrong.
struct BuildProfile {
    const char* name;
    cameraunlock::memory::PeFingerprint client_fingerprint;
    cameraunlock::memory::PeFingerprint engine_fingerprint;
    OffsetTable offsets;
};

// Fingerprints the loaded client.dll AND engine.dll and selects the first
// profile that matches both and has its offsets filled in, logging every
// comparison either way. Nothing in the mod may touch game memory - no hooks,
// no reads - until this has returned true.
bool SelectProfile();

bool HasActiveProfile();
const BuildProfile& ActiveProfile();

uintptr_t ClientBase();
uintptr_t EngineBase();

}  // namespace headtracking
