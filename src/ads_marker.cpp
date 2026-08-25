#include "ads_marker.h"

#include <atomic>

// The shared marker is header-only with one implementation translation unit, and
// this is it. Nothing else in the mod may define either of these.
#define CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_DX11_IMPLEMENTATION
#include "cameraunlock/rendering/aim_marker_dx11.h"

#include "debug_log.h"

namespace headtracking {

namespace {

cameraunlock::rendering::AimMarkerDX11 g_marker;

// Logged once each way, because the install is asynchronous and its outcome is
// the difference between `marker` drawing a mark and behaving like `tracked`.
std::atomic<bool> g_loggedReady{false};

}  // namespace

bool EnsureAdsMarker() {
    static const bool wired = [] {
        g_marker.SetLogger([](const char* msg) { HT_LOG("[ads] %s", msg); });
        return true;
    }();
    (void)wired;

    if (!g_marker.Ensure()) return false;
    if (!g_loggedReady.exchange(true)) HT_LOG("[ads] aim marker overlay is drawing");
    return true;
}

void PublishAdsMarker(bool visible, float ndcX, float ndcY) {
    g_marker.Publish(visible, ndcX, ndcY);
}

}  // namespace headtracking
