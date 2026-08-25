#include "aim_trace.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "build_profile.h"
#include "debug_log.h"

namespace headtracking {

namespace {

// void Trace(const Vector* start, const Vector* end, const Vector* mins,
//            const Vector* maxs, unsigned mask, trace_t* out)
using TraceFn = void(*)(const float*, const float*, const float*, const float*, uint32_t, void*);

// The mask client.dll+0x14be60 uses for the same shape of question - where does
// this line stop. Solid world and props; not a bullet mask, because what the
// crosshair has to sit on is the first thing the player can see it hit.
constexpr uint32_t kTraceMask = 0x1400Bu;

// 200 m. Past this the parallax correction is smaller than a pixel anyway, so
// there is nothing to gain from tracing further, and it caps the cost.
constexpr float kMaxRangeUnits = 7874.0f;

// A ray, not a hull: the crosshair marks a point, and a fat trace would stop
// short of it by the hull's radius on every glancing surface.
constexpr float kZeroHull[3] = { 0.0f, 0.0f, 0.0f };

// 15 Hz. The answer is a world query whose value changes slowly next to a frame,
// and it is smoothed on top.
constexpr unsigned long long kTraceIntervalMs = 66;
// Time constant of the distance smoothing. Long enough to ride over the jump
// when the aim crosses an edge, short enough that it has arrived by the time the
// player has finished moving their head.
constexpr float kSmoothingMs = 120.0f;

float g_smoothed = 0.0f;
bool g_haveSample = false;
unsigned long long g_lastTraceMs = 0;
unsigned long long g_lastUpdateMs = 0;

// Runs the game's trace and returns the distance to what it hit, or 0 when it
// hit nothing within range.
float TraceOnce(const float eye[3], const float fwd[3]) {
    const auto& off = ActiveProfile().offsets;
    auto trace = reinterpret_cast<TraceFn>(ClientBase() + off.trace_line_rva);

    const float end[3] = { eye[0] + fwd[0] * kMaxRangeUnits,
                           eye[1] + fwd[1] * kMaxRangeUnits,
                           eye[2] + fwd[2] * kMaxRangeUnits };
    // Bigger than the fields read below, zeroed, and aligned: the engine writes
    // a whole trace_t through this and its vectors are 16-byte aligned.
    alignas(16) uint8_t result[0x100];
    std::memset(result, 0, sizeof(result));

    __try {
        trace(eye, end, kZeroHull, kZeroHull, kTraceMask, result);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HT_LOG("[aim] trace faulted - crosshair parallax correction is off for this session");
        return 0.0f;
    }

    float fraction = 0.0f;
    float hit[3] = {};
    std::memcpy(&fraction, result + off.trace_fraction, sizeof(fraction));
    std::memcpy(hit, result + off.trace_endpos, sizeof(hit));
    if (!std::isfinite(fraction) || fraction >= 1.0f) return 0.0f;

    const float dx = hit[0] - eye[0], dy = hit[1] - eye[1], dz = hit[2] - eye[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    // A hit closer than a few units is the player's own weapon or a surface they
    // are stood inside; correcting to that would swing the crosshair wildly.
    return (std::isfinite(d) && d > 8.0f && d < kMaxRangeUnits) ? d : 0.0f;
}

}  // namespace

void ResetAimDistance() {
    g_haveSample = false;
    g_smoothed = 0.0f;
}

bool AimDistance(const float eye[3], const float fwd[3], float& outUnits) {
    if (!HasActiveProfile() || ActiveProfile().offsets.trace_line_rva == 0) return false;

    const unsigned long long now = GetTickCount64();
    if (!g_haveSample || now - g_lastTraceMs >= kTraceIntervalMs) {
        g_lastTraceMs = now;
        const float d = TraceOnce(eye, fwd);
        if (d <= 0.0f) {
            // Nothing in range: the aim is at infinity as far as the crosshair
            // is concerned, and the caller's direction projection is right.
            g_haveSample = false;
            return false;
        }
        if (!g_haveSample) {
            g_smoothed = d;
            g_haveSample = true;
            static bool s_logged = false;
            if (!s_logged) {
                s_logged = true;
                // One line, once: the whole parallax correction rides on this
                // number, and a wrong trace_t offset shows up here as a distance
                // that is not a distance rather than as a subtly wrong crosshair.
                HT_LOG("[aim] first traced distance %.1f units (%.1f m) from the eye", d,
                       d / 39.37f);
            }
        } else {
            const float dt = static_cast<float>(now - g_lastUpdateMs);
            const float t = 1.0f - std::exp(-dt / kSmoothingMs);
            g_smoothed += (d - g_smoothed) * t;
        }
    }
    g_lastUpdateMs = now;
    if (!g_haveSample) return false;
    outUnits = g_smoothed;
    return true;
}

}  // namespace headtracking
