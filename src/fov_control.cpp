#include "fov_control.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>

#include "angle_units.h"
#include "build_profile.h"
#include "debug_log.h"
#include "engine_interface.h"

namespace headtracking {

namespace {

// What the drawn field of view is allowed to be. Wide enough for anyone who
// wants the fish-eye, narrow enough that the tangent stays a number the
// projection can carry.
constexpr float kMinDrawnFov = 50.0f;
constexpr float kMaxDrawnFov = 130.0f;
// And what the culled one is allowed to be. tan grows without bound towards 180
// degrees, so the headroom multiplier has to stop somewhere; at 150 the cone is
// already 40 degrees wider each side than the widest view anyone draws.
constexpr float kMaxCulledFov = 150.0f;

// Outside this the ratio is not a field-of-view ratio, so scaling the projection
// by it would wreck the frame rather than correct it.
constexpr float kMaxRatio = 20.0f;

using fov::FovFromTanY;
using fov::PlausibleFovPerScale;
using fov::PlausibleScale;
using fov::TanYFromFov;

float Clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ICvar::FindVar(const char*), reached through the same interface pointer and
// slot the game's own code uses - client.dll calls
// (**(code**)(*g_pCVar + 0x80))(g_pCVar, "sv_cheats") in several places.
using FindVarFn = void*(*)(void* self, const char* name);

void* FindCvar(const char* name) {
    if (!HasActiveProfile()) return nullptr;
    const auto& off = ActiveProfile().offsets;
    if (off.cvar_interface_ptr == 0) return nullptr;

    void* cvar = nullptr;
    __try {
        void* iface = EngineInterface(off.cvar_interface_ptr);
        auto fn = InterfaceMethod<FindVarFn>(iface, off.find_var_slot);
        if (fn) cvar = fn(iface, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cvar = nullptr;
    }
    return cvar;
}

float* CvarFloat(void* cvar) {
    return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(cvar)
                                    + ActiveProfile().offsets.convar_float);
}

// Reads the cvar without disturbing it. Returns 0 if it could not be read.
float ReadScale(void* cvar) {
    float v = 0.0f;
    __try {
        v = *CvarFloat(cvar);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v = 0.0f;
    }
    return v;
}

}  // namespace

FovControl& GetFovControl() {
    static FovControl instance;
    return instance;
}

bool FovControl::Initialize(float overrideFovDegrees, float cullHeadroom) {
    m_overrideFov = (overrideFovDegrees > 0.0f)
                        ? Clamp(overrideFovDegrees, kMinDrawnFov, kMaxDrawnFov)
                        : 0.0f;
    m_headroom = Clamp(cullHeadroom, 1.0f, 3.0f);

    m_cvar = FindCvar("cl_fovScale");
    if (!m_cvar) {
        HT_LOG("[fov] cl_fovScale not found - the field of view is the game's own and cannot be "
               "changed, and the engine will keep culling to the aim frustum, so turning your "
               "head far enough will show gaps in the world");
        return false;
    }

    const float scale = ReadScale(m_cvar);
    if (!PlausibleScale(scale)) {
        HT_LOG("[fov] cl_fovScale found at %p but reads %.3f, which is not a plausible scale - "
               "not touching it. The profile's convar_float offset is wrong for this build.",
               m_cvar, scale);
        m_cvar = nullptr;
        return false;
    }
    m_playerScale = scale;

    // Layout confirmed on this build by dumping the ConVar live: m_fValue at
    // +0x58, m_nValue at +0x5c, and the min/max pair at +0x64/+0x6c reading 1.0
    // and 1.7. We write m_fValue directly, so that pair never sees the write -
    // though the clamp that actually decides the field of view is the one on the
    // READ (client.dll+0x2c59f0), which caps the cone at 1.7x whatever is
    // written here. That ceiling stopped mattering when the frustum started
    // being aimed at the head instead of stretched to cover it.
    HT_LOG("[fov] cl_fovScale at %p, player value %.3f", m_cvar, m_playerScale);
    return true;
}

void FovControl::NoteBaseTangent(float baseTangentY) {
    if (m_released) return;
    if (!m_cvar || !(baseTangentY > 0.01f) || !(baseTangentY < 100.0f)) return;

    if (m_fovPerScale == 0.0f) {
        const float scale = ReadScale(m_cvar);
        if (!PlausibleScale(scale)) return;
        m_playerScale = scale;

        // Each sample is normalised by the cvar value that produced it, so a
        // player who moves the FOV slider during the first few seconds shifts
        // neither the measurement nor the candidate it is competing against.
        const float perScale = FovFromTanY(baseTangentY) / scale;
        if (m_candidatePerScale == 0.0f || perScale < m_candidatePerScale) {
            m_candidatePerScale = perScale;
        }
        if (++m_samples < kSettleFrames) return;

        // The candidate is the NARROWEST base tangent seen over the settling
        // window, so one frame rendered through a scripted narrow view - a
        // cinematic, a camera the zoom field does not describe - carries the
        // whole session. What is computed from it is the multiplier a live game
        // cvar is then held at on every frame, so an out-by-an-order-of-magnitude
        // measurement is a permanently wrecked picture, not a soft failure. Say
        // so and leave the field of view alone.
        if (!PlausibleFovPerScale(m_candidatePerScale)) {
            // Logged once. The window re-arms below, so an engine that really
            // does render this way would otherwise repeat the line every few
            // seconds for the whole session.
            if (!m_perScaleWarned) {
                m_perScaleWarned = true;
                HT_LOG("[fov] measured %.2f degrees per unit of cl_fovScale, which is not a "
                       "plausible field of view for this engine (expected %.0f-%.0f) - not "
                       "touching the field of view. The engine keeps culling to the aim "
                       "frustum, so turning your head far enough will show gaps in the world. "
                       "Please report this log.",
                       m_candidatePerScale, fov::kMinFovPerScale, fov::kMaxFovPerScale);
            }
            // Re-arm rather than latch: the measurement is thrown away and the
            // next window gets a clean run at it, so a session that started on a
            // scripted camera still ends up with a working field of view.
            m_candidatePerScale = 0.0f;
            m_samples = 0;
            return;
        }

        m_fovPerScale = m_candidatePerScale;
        HT_LOG("[fov] measured %.2f degrees per unit of cl_fovScale, so the game's own field of "
               "view is %.1f degrees at its current %.3f",
               m_fovPerScale, m_fovPerScale * m_playerScale, m_playerScale);
        Recompute();
        return;
    }

    m_currentBaseTanY = baseTangentY;
    ReportCulling();
}

void FovControl::ReportCulling() {
    if (!m_active || m_cullingReported || m_targetTanY <= 0.0f) return;

    if (m_lastBaseTanY <= 0.0f
            || std::fabs(m_currentBaseTanY - m_lastBaseTanY) > m_lastBaseTanY * 0.01f) {
        m_lastBaseTanY = m_currentBaseTanY;
        m_cullSettleFrames = 0;
        return;
    }
    if (++m_cullSettleFrames < kCullSettleFrames) return;

    m_cullingReported = true;
    const float drawn = FovFromTanY(m_targetTanY);
    const float culled = FovFromTanY(m_currentBaseTanY);

    if (culled < drawn * 0.99f) {
        HT_LOG("[fov] the engine will not cull wider than %.1f degrees whatever cl_fovScale is "
               "set to, so the %.1f asked for cannot be drawn - the view is capped at %.1f. "
               "Lower [View] FieldOfView.",
               culled, drawn, culled);
        return;
    }
    // No arithmetic here about how far the head can turn before the holes start:
    // it can turn as far as it likes, because the frustum is built from the
    // head-rotated angles and follows it. The only thing a margin still covers is
    // the positional lean, which moves the eye without moving the cone.
    HT_LOG("[fov] drawing at %.1f degrees, culling at %.1f", drawn, culled);
}

void FovControl::Recompute() {
    if (m_fovPerScale <= 0.0f) return;

    const float playerFov = m_fovPerScale * m_playerScale;
    const float drawn = (m_overrideFov > 0.0f) ? m_overrideFov
                                               : Clamp(playerFov, kMinDrawnFov, kMaxDrawnFov);
    const float culled = Clamp(drawn * m_headroom, drawn, kMaxCulledFov);

    // Nothing is written into the game's cvar that this mod would itself reject
    // as a field-of-view scale when reading it back. Enforce() re-asserts this
    // value on every frame, so a single implausible number is not one bad frame
    // - it is the picture for the rest of the session.
    const float written = culled / m_fovPerScale;
    if (!PlausibleScale(written)) {
        HT_LOG("[fov] culling at %.1f degrees would need cl_fovScale held at %.3f, which is not "
               "a plausible scale - not touching the field of view. Lower [View] CullFovScale "
               "or clear [View] FieldOfView.", culled, written);
        m_active = false;
        return;
    }

    m_targetTanY = TanYFromFov(drawn);
    m_requestedCulledFov = culled;
    m_written = written;
    m_active = true;
    // Whatever the culling settles at now is a different answer to the one
    // already reported, so let it be reported again.
    m_cullingReported = false;
    m_lastBaseTanY = 0.0f;
    m_cullSettleFrames = 0;

    HT_LOG("[fov] drawing at %.1f degrees (%s), culling at %.1f so the head can turn without "
           "the engine discarding what it turns to look at - cl_fovScale held at %.3f",
           drawn, (m_overrideFov > 0.0f) ? "[View] FieldOfView" : "the game's own setting",
           culled, m_written);
}

void FovControl::NotePlayerScale(float scale) {
    if (std::fabs(scale - m_playerScale) < 1e-4f) return;
    m_playerScale = scale;
    if (m_fovPerScale <= 0.0f) return;

    if (m_overrideFov > 0.0f) {
        HT_LOG("[fov] the game's own FOV setting moved to %.1f degrees, which [View] FieldOfView "
               "overrides - clear that key to let the in-game slider drive the view again",
               m_fovPerScale * scale);
        return;
    }
    HT_LOG("[fov] the game's own FOV setting moved to %.1f degrees (cl_fovScale %.3f)",
           m_fovPerScale * scale, scale);
    Recompute();
}

void FovControl::Release() {
    if (m_released) return;
    m_released = true;

    const bool wasActive = m_active;
    m_active = false;
    if (!m_cvar || !wasActive) return;

    // Put the player's own value back rather than just stopping. The engine
    // pushes `setting.cl_fovScale` into the cvar on its own schedule, so simply
    // going quiet would leave whatever we last wrote standing until something
    // else happened to overwrite it - the widened cone would outlive us.
    __try {
        *CvarFloat(m_cvar) = m_playerScale;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    HT_LOG("[fov] multiplayer session - cl_fovScale handed back to the player at %.3f and the "
           "field of view left alone for the rest of this session", m_playerScale);
}

void FovControl::Enforce() {
    if (!m_cvar || !m_active) return;

    float observed = 0.0f;
    bool read = false;
    __try {
        observed = *CvarFloat(m_cvar);
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
    }
    if (!read) return;

    // Anything in the cvar that is not the value we last wrote is the engine
    // pushing the player's own setting back into it, which is the only channel
    // an in-game slider change reaches us through. Handled out here rather than
    // inside the __try: a fault taken while the logger holds its mutex would
    // orphan the lock, and SEH unwinding runs no destructors to release it.
    //
    // It also has to run BEFORE the value is re-asserted, because that is what
    // decides what gets re-asserted. Writing first left the previous hold
    // standing in the cvar for the frame in which NotePlayerScale moved
    // m_written past it, and the next frame read that back and could not tell it
    // from the player: the mod's own hold is always wider than the setting it
    // was computed from, so every real slider move bounced the drawn field of
    // view up one hold and pinned it at the engine's ceiling within two frames.
    if (observed != m_written && PlausibleScale(observed)) NotePlayerScale(observed);

    // Recompute can stand the hold down, in which case there is nothing to
    // re-assert and the cvar is the player's again.
    if (!m_active) return;

    __try {
        float* v = CvarFloat(m_cvar);
        if (*v != m_written) *v = m_written;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

float FovControl::ProjectionRatio() const {
    if (!m_active || m_targetTanY <= 0.0f || m_currentBaseTanY <= 0.0f) return 1.0f;

    // Below 1 the engine is culling to a NARROWER cone than we are trying to
    // draw, and widening the projection to match would render geometry that was
    // never submitted - so the field of view is capped at what the engine gave
    // us. Above kMaxRatio the number is not a field-of-view ratio at all and
    // scaling by it would wreck the frame rather than correct it. Both leave the
    // projection alone, which is the direction that still produces a picture;
    // ReportCulling is what says so in the log.
    const float r = m_currentBaseTanY / m_targetTanY;
    return (r > 1.0f && r < kMaxRatio) ? r : 1.0f;
}

void FovControl::NoteDrawnTangents(float tanX, float tanY) {
    m_drawnTanX = tanX;
    m_drawnTanY = tanY;
}

float FovControl::DrawnFovDegrees() const {
    return (m_drawnTanY > 0.0f) ? FovFromTanY(m_drawnTanY) : 0.0f;
}

float FovControl::CulledFovDegrees() const {
    return (m_currentBaseTanY > 0.0f) ? FovFromTanY(m_currentBaseTanY) : 0.0f;
}

}  // namespace headtracking
