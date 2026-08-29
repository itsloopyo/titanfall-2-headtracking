// Characterization tests for the parts of the mod that are pure arithmetic.
//
// Everything else in this mod needs Titanfall 2 running to mean anything: the
// hooks, the gates and the offset table are all statements about another
// process's memory. What is left over is the maths that every rendered frame
// passes through, and that maths is worth pinning precisely because a sign or
// an axis error in it produces a camera that points somewhere plausible - the
// picture moves, it just moves wrongly - which is the failure mode that gets
// through a play test.
//
// These lock CURRENT behaviour. If a change here fails, the question is whether
// the behaviour was meant to change, not whether the test is inconvenient.
//
// Deliberately no test framework: this runs in the same CMake project as a
// Windows game plugin, and adding a dependency to it to assert on twelve
// floats would cost more than it returns.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "ads.h"
#include "ads_blend.h"
#include "ads_gate.h"
#include "aim_projection.h"
#include "marker_projection.h"
#include "rui_transform.h"
#include "angle_units.h"
#include "config.h"
#include "fov_control.h"
#include "source_angles.h"
#include "view_matrix.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const char* file, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s:%d  %s\n", file, line, what);
}

void CheckNear(float got, float want, float tol, const char* what, const char* file, int line) {
    ++g_checks;
    if (std::isfinite(got) && std::fabs(got - want) <= tol) return;
    ++g_failures;
    std::printf("FAIL %s:%d  %s: got %.6f, want %.6f (tol %.6f)\n",
                file, line, what, got, want, tol);
}

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)
#define CHECK_NEAR(got, want, tol) CheckNear((got), (want), (tol), #got, __FILE__, __LINE__)

using namespace headtracking;

// ---- angle_units -----------------------------------------------------------
//
// These constants replaced three per-translation-unit copies. The copies were
// spelled `kPi / 180.0f` and `180.0f / kPi` off the same literal, so this is the
// statement that the consolidation moved nothing.
void TestAngleUnits() {
    constexpr float pi = 3.14159265358979323846f;
    CHECK(kPi == pi);
    CHECK(kDegToRad == pi / 180.0f);
    CHECK(kRadToDeg == 180.0f / pi);
    CHECK_NEAR(180.0f * kDegToRad, kPi, 1e-6f);
    CHECK_NEAR(kPi * kRadToDeg, 180.0f, 1e-4f);
}

// ---- source_angles ---------------------------------------------------------

// Source's world frame at zero angles: forward is +x, right is -y (world +y is
// LEFT), up is +z. Every sign correction in camera_hook.cpp is justified by
// reference to these three vectors, so they are the foundation the rest of the
// axis mapping stands on.
void TestAngleVectorsAtRest() {
    const float ang[3] = { 0.0f, 0.0f, 0.0f };
    float fwd[3], right[3], up[3];
    AngleVectors(ang, fwd, right, up);

    CHECK_NEAR(fwd[0], 1.0f, 1e-6f);
    CHECK_NEAR(fwd[1], 0.0f, 1e-6f);
    CHECK_NEAR(fwd[2], 0.0f, 1e-6f);

    CHECK_NEAR(right[0], 0.0f, 1e-6f);
    CHECK_NEAR(right[1], -1.0f, 1e-6f);
    CHECK_NEAR(right[2], 0.0f, 1e-6f);

    CHECK_NEAR(up[0], 0.0f, 1e-6f);
    CHECK_NEAR(up[1], 0.0f, 1e-6f);
    CHECK_NEAR(up[2], 1.0f, 1e-6f);
}

// Source pitch is positive DOWN and yaw positive LEFT. camera_hook.cpp negates
// the tracker's yaw and pitch because of exactly this, so if it ever stops being
// true the mod turns the view the wrong way on both axes.
void TestAngleVectorsSenses() {
    float fwd[3], right[3], up[3];

    const float pitchDown[3] = { 30.0f, 0.0f, 0.0f };
    AngleVectors(pitchDown, fwd, right, up);
    CHECK(fwd[2] < 0.0f);

    const float yawLeft[3] = { 0.0f, 30.0f, 0.0f };
    AngleVectors(yawLeft, fwd, right, up);
    CHECK(fwd[1] > 0.0f);

    // A positive roll tips `up` toward -y, i.e. tilts the view to the RIGHT.
    const float rollRight[3] = { 0.0f, 0.0f, 30.0f };
    AngleVectors(rollRight, fwd, right, up);
    CHECK(up[1] < 0.0f);
}

// The two directions have to be exact inverses: the render hook decomposes a
// view matrix to angles, adds a lean, and composes the basis straight back. Any
// drift in that round trip accumulates into the picture.
void TestAngleRoundTrip() {
    const float cases[][3] = {
        { 0.0f, 0.0f, 0.0f },
        { 12.5f, -47.25f, 0.0f },
        { -33.0f, 164.67f, 7.5f },
        { 89.0f, -179.0f, -22.0f },
        { -80.0f, 90.0f, 45.0f },
    };
    for (const auto& ang : cases) {
        float fwd[3], right[3], up[3];
        AngleVectors(ang, fwd, right, up);
        const float left[3] = { -right[0], -right[1], -right[2] };
        float back[3];
        BasisToAngles(fwd, left, up, back);

        float f2[3], r2[3], u2[3];
        AngleVectors(back, f2, r2, u2);
        for (int i = 0; i < 3; ++i) {
            CHECK_NEAR(f2[i], fwd[i], 1e-4f);
            CHECK_NEAR(r2[i], right[i], 1e-4f);
            CHECK_NEAR(u2[i], up[i], 1e-4f);
        }
    }
}

// Straight up and straight down: yaw and roll are the same axis there, so
// Source folds the rotation into yaw and zeroes roll. LeanOneView leans along a
// basis built from the recovered yaw, so this is the one place that answer is
// arbitrary - it must at least stay finite and roll-free.
void TestBasisToAnglesAtPole() {
    const float ang[3] = { 89.999f, 30.0f, 0.0f };
    float fwd[3], right[3], up[3];
    AngleVectors(ang, fwd, right, up);
    const float left[3] = { -right[0], -right[1], -right[2] };
    float out[3];
    BasisToAngles(fwd, left, up, out);
    CHECK(std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]));
    CHECK_NEAR(out[2], 0.0f, 1e-6f);
}

void TestClampPitch() {
    CHECK_NEAR(ClampPitch(0.0f), 0.0f, 0.0f);
    CHECK_NEAR(ClampPitch(88.9f), 88.9f, 1e-6f);
    CHECK_NEAR(ClampPitch(120.0f), 89.0f, 0.0f);
    CHECK_NEAR(ClampPitch(-120.0f), -89.0f, 0.0f);
}

// World-space yaw adds the delta straight onto the QAngle, which is what makes
// it horizon-locked, and clamps pitch at the pole so the frame cannot invert.
void TestWorldSpaceRotation() {
    float ang[3] = { 10.0f, 20.0f, 0.0f };
    ApplyWorldSpaceRotation(ang, 5.0f, -30.0f, 3.0f);
    CHECK_NEAR(ang[0], 15.0f, 1e-4f);
    CHECK_NEAR(ang[1], -10.0f, 1e-4f);
    CHECK_NEAR(ang[2], 3.0f, 1e-4f);

    float steep[3] = { 85.0f, 0.0f, 0.0f };
    ApplyWorldSpaceRotation(steep, 30.0f, 0.0f, 0.0f);
    CHECK_NEAR(steep[0], 89.0f, 0.0f);
}

// Camera-local composition must be the identity for a zero delta, and must
// agree with the world-space path whenever the camera is level - that is the
// case where "about world up" and "about the camera's own up" are the same
// axis, so the two modes may not disagree there.
void TestCameraLocalRotation() {
    float zero[3] = { -12.0f, 64.0f, 5.0f };
    const float before[3] = { zero[0], zero[1], zero[2] };
    ApplyCameraLocalRotation(zero, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 3; ++i) CHECK_NEAR(zero[i], before[i], 1e-3f);

    float local[3] = { 0.0f, 40.0f, 0.0f };
    float world[3] = { 0.0f, 40.0f, 0.0f };
    ApplyCameraLocalRotation(local, 0.0f, 25.0f, 0.0f);
    ApplyWorldSpaceRotation(world, 0.0f, 25.0f, 0.0f);
    CHECK_NEAR(local[0], world[0], 1e-3f);
    CHECK_NEAR(local[1], world[1], 1e-3f);
    CHECK_NEAR(local[2], world[2], 1e-3f);

    // Looking steeply down, a head yaw is a roll about the view axis rather
    // than a pan - which is the whole reason the two modes exist.
    float steep[3] = { 80.0f, 0.0f, 0.0f };
    ApplyCameraLocalRotation(steep, 0.0f, 30.0f, 0.0f);
    CHECK(std::fabs(steep[2]) > 1.0f);
}

// The rotation the head puts into the CAMERA, carried onto something that is
// not the camera. This is what holds the Titan cockpit still in the picture,
// and the cases below are the ones that separate it from the wrong answer -
// composing the delta onto the cockpit's own angles - which agrees only while
// the cockpit and the camera point the same way.
void TestCarryRotation() {
    // A camera that did not move carries nothing, whatever the cockpit is at.
    const float still[3] = { 12.0f, -40.0f, 3.0f };
    float cockpit[3] = { 5.0f, -38.0f, 1.0f };
    const float before[3] = { cockpit[0], cockpit[1], cockpit[2] };
    CarryRotation(still, still, cockpit);
    for (int i = 0; i < 3; ++i) CHECK_NEAR(cockpit[i], before[i], 1e-3f);

    // A cockpit that IS the camera ends up exactly where the camera did: the
    // carry reduces to the delta, which is the sanity check that it is the same
    // rotation and not merely a similar one.
    const float clean[3] = { 0.0f, 30.0f, 0.0f };
    float drawn[3] = { clean[0], clean[1], clean[2] };
    ApplyWorldSpaceRotation(drawn, 10.0f, -20.0f, 6.0f);
    float same[3] = { clean[0], clean[1], clean[2] };
    CarryRotation(clean, drawn, same);
    for (int i = 0; i < 3; ++i) CHECK_NEAR(same[i], drawn[i], 1e-3f);

    // The case the mod is actually in: the game holds the cockpit back to a
    // fraction of the player's pitch, so it is NOT the camera. A pure head yaw
    // about world up has to move the cockpit's yaw by the same amount and leave
    // its held-back pitch alone.
    const float pitched[3] = { 40.0f, 30.0f, 0.0f };
    float held[3] = { 16.0f, 30.0f, 0.0f };   // pitch scaled by 0.4
    float yawed[3] = { pitched[0], pitched[1], pitched[2] };
    ApplyWorldSpaceRotation(yawed, 0.0f, 25.0f, 0.0f);
    CarryRotation(pitched, yawed, held);
    CHECK_NEAR(held[0], 16.0f, 1e-3f);
    CHECK_NEAR(held[1], 55.0f, 1e-3f);
    CHECK_NEAR(held[2], 0.0f, 1e-3f);

    // And the difference from the wrong answer is real, not a rounding tie: on
    // a combined delta with the cockpit pitched away from the view, carrying it
    // and composing onto it land in different places.
    float carried[3] = { 16.0f, 30.0f, 0.0f };
    float composed[3] = { 16.0f, 30.0f, 0.0f };
    float combined[3] = { pitched[0], pitched[1], pitched[2] };
    ApplyWorldSpaceRotation(combined, 12.0f, 20.0f, 15.0f);
    CarryRotation(pitched, combined, carried);
    ApplyWorldSpaceRotation(composed, 12.0f, 20.0f, 15.0f);
    float apart = 0.0f;
    for (int i = 0; i < 3; ++i) apart += std::fabs(carried[i] - composed[i]);
    CHECK(apart > 1.0f);
}

// ---- view_matrix -----------------------------------------------------------

void MakeViewFromAngles(const float ang[3], const float org[3], float out[16]) {
    float fwd[3], right[3], up[3];
    AngleVectors(ang, fwd, right, up);
    ComposeView(out, fwd, right, up, org);
}

// Compose then decompose has to return the camera unchanged - the hook does
// exactly this every frame it applies a lean.
void TestViewRoundTrip() {
    const float ang[3] = { 1.67f, 164.67f, 0.0f };
    const float org[3] = { -7525.5f, 362.0f, 204.0f };
    float v[16];
    MakeViewFromAngles(ang, org, v);

    float fwd[3], right[3], up[3], back[3];
    DecomposeView(v, fwd, right, up, back);
    for (int i = 0; i < 3; ++i) CHECK_NEAR(back[i], org[i], 1e-2f);

    float outAng[3];
    const float left[3] = { -right[0], -right[1], -right[2] };
    BasisToAngles(fwd, left, up, outAng);
    CHECK_NEAR(outAng[0], ang[0], 1e-2f);
    CHECK_NEAR(outAng[1], ang[1], 1e-2f);
    CHECK_NEAR(outAng[2], ang[2], 1e-2f);
}

// A view matrix's bottom row is the projective identity. The renderer relies on
// it; ComposeView is the only thing that writes it.
void TestComposeViewBottomRow() {
    const float fwd[3] = { 1.0f, 0.0f, 0.0f };
    const float right[3] = { 0.0f, -1.0f, 0.0f };
    const float up[3] = { 0.0f, 0.0f, 1.0f };
    const float org[3] = { 5.0f, 6.0f, 7.0f };
    float v[16];
    std::memset(v, 0x7f, sizeof(v));
    ComposeView(v, fwd, right, up, org);

    CHECK_NEAR(v[12], 0.0f, 0.0f);
    CHECK_NEAR(v[13], 0.0f, 0.0f);
    CHECK_NEAR(v[14], 0.0f, 0.0f);
    CHECK_NEAR(v[15], 1.0f, 0.0f);

    // Row w terms are -dot(axis, origin): the origin is what they encode.
    CHECK_NEAR(v[3], -(right[0] * org[0] + right[1] * org[1] + right[2] * org[2]), 1e-4f);
    CHECK_NEAR(v[7], -(up[0] * org[0] + up[1] * org[1] + up[2] * org[2]), 1e-4f);
}

// Translating the origin along the camera's own right vector must move the
// decoded origin by exactly that much and leave the basis alone. This is the
// positional lean, reduced to its arithmetic.
void TestViewOriginTranslation() {
    const float ang[3] = { 0.0f, 90.0f, 0.0f };
    const float org[3] = { 100.0f, 200.0f, 300.0f };
    float v[16];
    MakeViewFromAngles(ang, org, v);

    float fwd[3], right[3], up[3], decoded[3];
    DecomposeView(v, fwd, right, up, decoded);

    float leaned[3];
    for (int i = 0; i < 3; ++i) leaned[i] = decoded[i] + right[i] * 11.81f;
    float v2[16];
    ComposeView(v2, fwd, right, up, leaned);

    float f2[3], r2[3], u2[3], org2[3];
    DecomposeView(v2, f2, r2, u2, org2);
    for (int i = 0; i < 3; ++i) {
        CHECK_NEAR(org2[i], decoded[i] + right[i] * 11.81f, 1e-2f);
        CHECK_NEAR(f2[i], fwd[i], 1e-5f);
        CHECK_NEAR(r2[i], right[i], 1e-5f);
        CHECK_NEAR(u2[i], up[i], 1e-5f);
    }
}

void TestMulMat4Identity() {
    float id[16] = {};
    for (int i = 0; i < 4; ++i) id[i * 4 + i] = 1.0f;

    float a[16];
    for (int i = 0; i < 16; ++i) a[i] = static_cast<float>(i) * 0.5f - 3.0f;

    float out[16];
    MulMat4(id, a, out);
    for (int i = 0; i < 16; ++i) CHECK_NEAR(out[i], a[i], 1e-5f);

    MulMat4(a, id, out);
    for (int i = 0; i < 16; ++i) CHECK_NEAR(out[i], a[i], 1e-5f);
}

// The hook calls MulMat4(proj, view, viewproj) where viewproj may be any of the
// three - aliasing is why it writes through a temporary.
void TestMulMat4Aliasing() {
    float a[16], b[16];
    for (int i = 0; i < 16; ++i) {
        a[i] = static_cast<float>((i * 7) % 11) - 5.0f;
        b[i] = static_cast<float>((i * 5) % 13) - 6.0f;
    }
    float expected[16];
    MulMat4(a, b, expected);

    float aliased[16];
    std::memcpy(aliased, a, sizeof(a));
    MulMat4(aliased, b, aliased);
    for (int i = 0; i < 16; ++i) CHECK_NEAR(aliased[i], expected[i], 1e-4f);

    std::memcpy(aliased, b, sizeof(b));
    MulMat4(a, aliased, aliased);
    for (int i = 0; i < 16; ++i) CHECK_NEAR(aliased[i], expected[i], 1e-4f);
}

// Row-major, so out[r][c] = sum_k a[r][k] * b[k][c]. Checked against a hand
// evaluation rather than another loop of the same shape.
void TestMulMat4Order() {
    float a[16] = {};
    float b[16] = {};
    for (int i = 0; i < 4; ++i) { a[i * 4 + i] = 1.0f; b[i * 4 + i] = 1.0f; }
    a[1] = 2.0f;   // a[0][1]
    b[4] = 3.0f;   // b[1][0]

    float out[16];
    MulMat4(a, b, out);
    // out[0][0] = a[0][0]*b[0][0] + a[0][1]*b[1][0] = 1 + 2*3 = 7
    CHECK_NEAR(out[0], 7.0f, 1e-5f);
    // out[0][1] = a[0][0]*b[0][1] + a[0][1]*b[1][1] = 0 + 2*1 = 2
    CHECK_NEAR(out[1], 2.0f, 1e-5f);
}

// ---- config defaults -------------------------------------------------------
//
// Every numeric key is read as "clamp into range, falling back on the default
// if it is not a number", so a default that sits OUTSIDE its own range would be
// silently altered on the way in. The INI a user is handed would then document
// one value while the loader used another, and the two would only disagree for
// the user who deleted the key - which is the hardest version of this bug to
// hear about. These are not compiler tautologies: they are the invariant that
// makes the defaults and the limits one coherent set.
void TestDefaultsAreWithinLimits() {
    using namespace headtracking::defaults;
    using namespace headtracking::limits;

    CHECK(kPort >= kMinPort && kPort <= kMaxPort);

    CHECK(kSensitivity >= kMinSensitivity && kSensitivity <= kMaxSensitivity);

    CHECK(kLocalSmoothing >= 0.0f && kLocalSmoothing <= kMaxSmoothing);
    CHECK(kRemoteSmoothing >= 0.0f && kRemoteSmoothing <= kMaxSmoothing);

    CHECK(kPositionSensitivity >= kMinPositionSensitivity
          && kPositionSensitivity <= kMaxPositionSensitivity);

    CHECK(kLimitX >= kMinPositionLimit && kLimitX <= kMaxPositionLimit);
    CHECK(kLimitY >= kMinPositionLimit && kLimitY <= kMaxPositionLimit);
    CHECK(kLimitZ >= kMinPositionLimit && kLimitZ <= kMaxPositionLimit);
    CHECK(kLimitZBack >= kMinPositionLimit && kLimitZBack <= kMaxPositionLimit);

    CHECK(kWorldScale >= kMinWorldScale && kWorldScale <= kMaxWorldScale);
    CHECK(kCullFovScale >= kMinCullFovScale && kCullFovScale <= kMaxCullFovScale);

    // Deadzone is read as "positive or nothing", so its default has to be the
    // nothing.
    CHECK(kDeadzone == 0.0f);
    // FieldOfView 0 is the sentinel for "follow the game's own slider"; any
    // positive default would silently override it instead.
    CHECK(kFieldOfView == 0.0f);
}

// The asymmetric Z envelope is a domain rule, not a coincidence: more room to
// lean IN than to pull back, so the camera does not clip through the player.
// See the position-tracking section of AGENTS.md.
void TestDefaultPositionEnvelope() {
    using namespace headtracking::defaults;
    CHECK(kLimitZ > kLimitZBack);
}

// A default-constructed Config is what a user gets when the INI cannot be
// opened at all, so it has to be the same configuration a freshly written INI
// describes.
void TestDefaultConfigMatchesConstants() {
    const headtracking::Config c;
    using namespace headtracking::defaults;

    CHECK(c.port == kPort);
    CHECK(c.enabled_on_startup == kEnableOnStartup);
    CHECK_NEAR(c.sens_yaw, kSensitivity, 0.0f);
    CHECK_NEAR(c.sens_pitch, kSensitivity, 0.0f);
    CHECK_NEAR(c.sens_roll, kSensitivity, 0.0f);
    CHECK_NEAR(c.local_smoothing, kLocalSmoothing, 0.0f);
    CHECK_NEAR(c.remote_smoothing, kRemoteSmoothing, 0.0f);
    CHECK_NEAR(c.pos_limit_x, kLimitX, 0.0f);
    CHECK_NEAR(c.pos_limit_y, kLimitY, 0.0f);
    CHECK_NEAR(c.pos_limit_z, kLimitZ, 0.0f);
    CHECK_NEAR(c.pos_limit_z_back, kLimitZBack, 0.0f);
    CHECK_NEAR(c.pos_world_scale, kWorldScale, 0.0f);
    CHECK_NEAR(c.cull_fov_scale, kCullFovScale, 0.0f);
    CHECK(c.toggle_vk == kToggleVk);
    CHECK(c.yaw_mode_vk == kYawModeVk);
    CHECK(c.mode_cycle_vk == kModeCycleVk);
    CHECK(c.ads_mode_vk == kAdsModeVk);
    CHECK(c.ads_mode == kAdsMode);
    CHECK(c.world_space_yaw == kWorldSpaceYaw);
}

// The nav-cluster hotkeys must stay distinct, or one action becomes
// unreachable and the collision is invisible until someone presses the key.
void TestHotkeyDefaultsAreDistinct() {
    using namespace headtracking::defaults;
    const int keys[] = { kToggleVk, kYawModeVk, kModeCycleVk, kAdsModeVk };
    constexpr int n = static_cast<int>(sizeof(keys) / sizeof(keys[0]));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) CHECK(keys[i] != keys[j]);
    }
}

// ---- fov_control -----------------------------------------------------------
//
// The field-of-view arithmetic ends in a value written into a LIVE game cvar on
// every frame, and the mod re-asserts it - so a wrong number here is not one bad
// frame, it is the picture for the rest of the session. That is why the
// conversions and the two plausibility predicates are pinned here.

// The measured pair from a live frame on this build: cl_fovScale 1.0 renders
// tanfov (0.934, 0.525) at exactly 70 degrees, and 1.7 renders (2.264, 1.273) at
// exactly 119. Both directions have to reproduce them.
void TestFovTangentConversion() {
    using namespace headtracking::fov;

    CHECK_NEAR(FovFromTanY(0.525f), 70.0f, 0.2f);
    CHECK_NEAR(FovFromTanY(1.273f), 119.0f, 0.2f);
    CHECK_NEAR(TanYFromFov(70.0f), 0.525f, 1e-3f);
    CHECK_NEAR(TanYFromFov(119.0f), 1.273f, 2e-3f);

    // Round trip across the whole accepted drawn range.
    for (float deg = 50.0f; deg <= 150.0f; deg += 5.0f) {
        CHECK_NEAR(FovFromTanY(TanYFromFov(deg)), deg, 1e-2f);
    }
}

// cl_fovScale is a plain multiplier on the FOV ANGLE, which is the property the
// whole widen-then-narrow scheme rests on: 1.7 units of it is 1.7 times the
// degrees, not 1.7 times the tangent.
void TestFovScaleIsLinearInDegrees() {
    using namespace headtracking::fov;
    const float perScale = FovFromTanY(0.525f) / 1.0f;
    CHECK_NEAR(FovFromTanY(1.273f) / 1.7f, perScale, 0.2f);
}

// The predicate that decides whether a number is allowed to be written into the
// game's cl_fovScale. Anything it accepts is held there on every frame.
void TestPlausibleScale() {
    using namespace headtracking::fov;

    CHECK(PlausibleScale(1.0f));
    CHECK(PlausibleScale(1.7f));
    // [View] FieldOfView=130 with the default headroom asks for about 2.1.
    CHECK(PlausibleScale(2.14f));

    CHECK(!PlausibleScale(0.0f));
    CHECK(!PlausibleScale(-1.0f));
    CHECK(!PlausibleScale(0.05f));
    CHECK(!PlausibleScale(20.0f));
    // The shape a mis-measured degrees-per-unit produces: culled / a tiny
    // per-scale, which would otherwise be written into the cvar every frame.
    CHECK(!PlausibleScale(1970.0f));
    CHECK(!PlausibleScale(std::numeric_limits<float>::infinity()));
    CHECK(!PlausibleScale(std::nanf("")));
}

// The measurement takes the NARROWEST base tangent over its settling window, so
// a single frame rendered through a scripted narrow view carries the session.
// This is the band that rejects one.
void TestPlausibleFovPerScale() {
    using namespace headtracking::fov;

    // What this build actually measures.
    CHECK(PlausibleFovPerScale(70.0f));
    CHECK(PlausibleFovPerScale(kMinFovPerScale));
    CHECK(PlausibleFovPerScale(kMaxFovPerScale));

    // A cinematic rendered at a very narrow vertical tangent, normalised by a
    // player scale of 1: degrees-per-unit collapses and the cvar value computed
    // from it explodes.
    const float cinematic = FovFromTanY(0.02f);
    CHECK(!PlausibleFovPerScale(cinematic));
    CHECK(!PlausibleFovPerScale(0.0f));
    CHECK(!PlausibleFovPerScale(-70.0f));
    CHECK(!PlausibleFovPerScale(3580.0f));
    CHECK(!PlausibleFovPerScale(std::nanf("")));
    CHECK(!PlausibleFovPerScale(std::numeric_limits<float>::infinity()));
}

// A per-scale the band accepts, at the widest culling the mod will ask for, must
// still produce a cvar value the write guard accepts - otherwise the two guards
// disagree and the field of view is refused on a perfectly ordinary build.
void TestGuardsAgreeAcrossTheAcceptedBand() {
    using namespace headtracking::fov;
    constexpr float kMaxCulledFov = 150.0f;  // fov_control.cpp
    for (float perScale = kMinFovPerScale; perScale <= kMaxFovPerScale; perScale += 1.0f) {
        CHECK(PlausibleScale(kMaxCulledFov / perScale));
    }
}

// ---- aim_projection --------------------------------------------------------
//
// Where the gun points in the picture the head is looking at. The reticle is
// drawn at whatever this returns, so a sign error here puts the mark on the
// wrong side of the screen and every shot lands somewhere the player did not
// point - a failure that looks like the camera being wrong rather than the
// reticle.
//
// The camera hook hands over the vectors it actually wrote into the view matrix,
// so these build the same way: a QAngle through AngleVectors, exactly as the
// hook composes it.
void TestAimAtCentreWhenHeadIsCentred() {
    const float ang[3] = { 12.0f, -73.0f, 0.0f };
    float fwd[3], right[3], up[3];
    AngleVectors(ang, fwd, right, up);

    float x = 1.0f, y = 1.0f;
    CHECK(ProjectAimToNdc(fwd, fwd, right, up, 0.934f, 0.525f, x, y));
    CHECK_NEAR(x, 0.0f, 1e-5f);
    CHECK_NEAR(y, 0.0f, 1e-5f);
}

// Head turns right, so the aim is now to the LEFT of what is on screen. The
// reticle has to follow the gun, not the head.
void TestHeadYawMovesAimOppositeWay() {
    const float clean[3] = { 0.0f, 0.0f, 0.0f };
    float aim[3], cleanRight[3], cleanUp[3];
    AngleVectors(clean, aim, cleanRight, cleanUp);

    float turned[3] = { clean[0], clean[1], clean[2] };
    ApplyWorldSpaceRotation(turned, 0.0f, -20.0f, 0.0f);  // Source yaw down = view right
    float fwd[3], right[3], up[3];
    AngleVectors(turned, fwd, right, up);

    float x = 0.0f, y = 0.0f;
    CHECK(ProjectAimToNdc(aim, fwd, right, up, 0.934f, 0.525f, x, y));
    CHECK(x < 0.0f);
    CHECK_NEAR(y, 0.0f, 1e-5f);
    // tan(20 degrees) / tan(half the horizontal field of view).
    CHECK_NEAR(x, -std::tan(20.0f * kDegToRad) / 0.934f, 1e-4f);
}

// Head looks up, so the aim sits BELOW the centre of the picture. NDC y is up.
void TestHeadPitchMovesAimDown() {
    const float clean[3] = { 0.0f, 40.0f, 0.0f };
    float aim[3], cleanRight[3], cleanUp[3];
    AngleVectors(clean, aim, cleanRight, cleanUp);

    float raised[3] = { clean[0], clean[1], clean[2] };
    ApplyWorldSpaceRotation(raised, -15.0f, 0.0f, 0.0f);  // Source pitch down = look up
    float fwd[3], right[3], up[3];
    AngleVectors(raised, fwd, right, up);

    float x = 0.0f, y = 0.0f;
    CHECK(ProjectAimToNdc(aim, fwd, right, up, 0.934f, 0.525f, x, y));
    CHECK(y < 0.0f);
    CHECK_NEAR(x, 0.0f, 1e-5f);
    CHECK_NEAR(y, -std::tan(15.0f * kDegToRad) / 0.525f, 1e-4f);
}

// Roll alone cannot move the aim off the centre - the view spins about the very
// axis the gun points down. The reticle litmus test in AGENTS.md, in one check.
void TestRollAloneLeavesAimAtCentre() {
    const float clean[3] = { -8.0f, 116.0f, 0.0f };
    float aim[3], cleanRight[3], cleanUp[3];
    AngleVectors(clean, aim, cleanRight, cleanUp);

    float rolled[3] = { clean[0], clean[1], clean[2] };
    ApplyWorldSpaceRotation(rolled, 0.0f, 0.0f, 25.0f);
    float fwd[3], right[3], up[3];
    AngleVectors(rolled, fwd, right, up);

    float x = 1.0f, y = 1.0f;
    CHECK(ProjectAimToNdc(aim, fwd, right, up, 0.934f, 0.525f, x, y));
    CHECK_NEAR(x, 0.0f, 1e-5f);
    CHECK_NEAR(y, 0.0f, 1e-5f);
}

// Turned far enough that the gun is behind the picture there is no screen
// position to draw at, and the caller has to be told rather than handed a
// number that projects to the far side of the frame.
// A positional lean moves the eye the frame is drawn from, but not the eye the
// shot comes from. The crosshair has to swing by the parallax or it slides off
// the thing the player was aiming at - and the closer the target, the further it
// slides. This is the case that needs a hit distance and cannot be answered by
// projecting a direction.
void TestLeanMovesTheAimPointAgainstTheEye() {
    const float clean[3] = { 0.0f, 0.0f, 0.0f };
    float fwd[3], right[3], up[3];
    AngleVectors(clean, fwd, right, up);

    const float distance = 1000.0f;   // Source units to the target
    const float lean = 12.0f;         // eye moves this far along `right`
    float rel[3], len = 0.0f;
    for (int i = 0; i < 3; ++i) {
        rel[i] = fwd[i] * distance - right[i] * lean;
        len += rel[i] * rel[i];
    }
    len = std::sqrt(len);
    for (int i = 0; i < 3; ++i) rel[i] /= len;

    float x = 0.0f, y = 0.0f;
    CHECK(ProjectAimToNdc(rel, fwd, right, up, 0.934f, 0.525f, x, y));
    // Eye right, target fixed: the target is now to the LEFT of the picture.
    CHECK(x < 0.0f);
    CHECK_NEAR(x, -(lean / distance) / 0.934f, 1e-4f);
    CHECK_NEAR(y, 0.0f, 1e-6f);

    // Twice as far away, half the parallax.
    float far_[3] = {};
    len = 0.0f;
    for (int i = 0; i < 3; ++i) {
        far_[i] = fwd[i] * distance * 2.0f - right[i] * lean;
        len += far_[i] * far_[i];
    }
    len = std::sqrt(len);
    for (int i = 0; i < 3; ++i) far_[i] /= len;
    float x2 = 0.0f, y2 = 0.0f;
    CHECK(ProjectAimToNdc(far_, fwd, right, up, 0.934f, 0.525f, x2, y2));
    CHECK_NEAR(x2, x * 0.5f, 1e-4f);
}

void TestAimBehindTheViewIsRejected() {
    const float clean[3] = { 0.0f, 0.0f, 0.0f };
    float aim[3], cleanRight[3], cleanUp[3];
    AngleVectors(clean, aim, cleanRight, cleanUp);

    float turned[3] = { clean[0], clean[1], clean[2] };
    ApplyWorldSpaceRotation(turned, 0.0f, 120.0f, 0.0f);
    float fwd[3], right[3], up[3];
    AngleVectors(turned, fwd, right, up);

    float x = 0.0f, y = 0.0f;
    CHECK(!ProjectAimToNdc(aim, fwd, right, up, 0.934f, 0.525f, x, y));
}

// ---- marker_projection -----------------------------------------------------
//
// The world-anchored HUD marks are placed by the GAME's world-to-screen, which
// projects with the clean camera. The mod moves the world point instead of the
// mark, so the invariant that matters is the round trip: the moved point seen
// from the CLEAN camera must land exactly where the real point lands seen from
// the DRAWN one. Everything else - the ellipse clamp, the behind test - is
// downstream of that one equality.

FrameCameras BuildCameras(const float cleanAng[3], float dpitch, float dyaw, float droll,
                          float leanRight, float leanUp, float leanFwd) {
    FrameCameras c = {};
    AngleVectors(cleanAng, c.cleanFwd, c.cleanRight, c.cleanUp);
    float drawnAng[3] = { cleanAng[0], cleanAng[1], cleanAng[2] };
    ApplyCameraLocalRotation(drawnAng, dpitch, dyaw, droll);
    AngleVectors(drawnAng, c.drawnFwd, c.drawnRight, c.drawnUp);
    for (int i = 0; i < 3; ++i) {
        c.cleanEye[i] = 0.0f;
        c.drawnEye[i] = c.cleanRight[i] * leanRight + c.cleanUp[i] * leanUp
                      + c.cleanFwd[i] * leanFwd;
    }
    return c;
}

bool ProjectFromEye(const float point[3], const float eye[3], const float fwd[3],
                    const float right[3], const float up[3], float& x, float& y) {
    float rel[3], len = 0.0f;
    for (int i = 0; i < 3; ++i) {
        rel[i] = point[i] - eye[i];
        len += rel[i] * rel[i];
    }
    len = std::sqrt(len);
    for (int i = 0; i < 3; ++i) rel[i] /= len;
    return ProjectAimToNdc(rel, fwd, right, up, 0.934f, 0.525f, x, y);
}

void TestMarkerUntrackedFrameIsIdentity() {
    const float clean[3] = { 4.0f, 137.0f, -6.0f };
    const FrameCameras c = BuildCameras(clean, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    const float p[3] = { 512.0f, -73.0f, 220.0f };
    float moved[3];
    ReprojectWorldPoint(c, p, moved);
    for (int i = 0; i < 3; ++i) CHECK_NEAR(moved[i], p[i], 1e-2f);
}

void TestMarkerLandsWhereTheDrawnCameraPutsIt() {
    // Combined poses, not one axis at a time: a formula that is right on single
    // axes and wrong on combinations is exactly the bug that survives testing.
    const float poses[][3] = {
        { 0.0f, 25.0f, 0.0f },
        { 12.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 20.0f },
        { 10.0f, -18.0f, 14.0f },
    };
    const float cleanAngles[][3] = {
        { 0.0f, 0.0f, 0.0f },
        { -8.0f, 210.0f, 3.0f },
    };
    const float points[][3] = {
        { 900.0f, 0.0f, 0.0f },
        { 300.0f, 450.0f, 120.0f },
        { -600.0f, 200.0f, -80.0f },
    };

    for (const auto& clean : cleanAngles) {
        for (const auto& pose : poses) {
            const FrameCameras c = BuildCameras(clean, pose[0], pose[1], pose[2],
                                                9.0f, -4.0f, 6.0f);
            for (const auto& p : points) {
                float wantX = 0.0f, wantY = 0.0f;
                if (!ProjectFromEye(p, c.drawnEye, c.drawnFwd, c.drawnRight, c.drawnUp,
                                    wantX, wantY)) {
                    continue;   // behind the drawn view: nothing to agree about
                }
                float moved[3];
                ReprojectWorldPoint(c, p, moved);
                float gotX = 0.0f, gotY = 0.0f;
                CHECK(ProjectFromEye(moved, c.cleanEye, c.cleanFwd, c.cleanRight, c.cleanUp,
                                     gotX, gotY));
                CHECK_NEAR(gotX, wantX, 1e-3f);
                CHECK_NEAR(gotY, wantY, 1e-3f);
            }
        }
    }
}

void TestMarkerKeepsItsDistance() {
    // Depth has to survive the move, or the behind-the-camera test the callers
    // branch on answers about a point that is not where the marker is.
    const float clean[3] = { 5.0f, 40.0f, 0.0f };
    const FrameCameras c = BuildCameras(clean, -7.0f, 30.0f, 11.0f, 12.0f, 3.0f, -5.0f);
    const float p[3] = { 250.0f, -400.0f, 60.0f };
    float moved[3];
    ReprojectWorldPoint(c, p, moved);

    float before = 0.0f, after = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float a = p[i] - c.drawnEye[i];
        const float b = moved[i] - c.cleanEye[i];
        before += a * a;
        after += b * b;
    }
    CHECK_NEAR(std::sqrt(after), std::sqrt(before), 1e-2f);
}

// ---- ads_pose --------------------------------------------------------------
//
// The entry pose is what makes the tracked ADS modes swing onto the aim and then
// keep tracking from there, and it is the one piece of the shared ADS module the
// camera boundary here feeds directly - degrees and Source units, straight out
// of ComputeFrameDelta. Its four rules are pinned at the source too
// (cameraunlock-core, cpp/tests/ads_tests.cpp); these are the cases this mod
// would break on, and they are cheap.
//
// The cycle strings and the transition shape are NOT retested here. They are a
// fleet-wide contract owned by the core, and a copy of them in every mod is a
// second place for them to drift.

namespace {
AdsEntryPose::Pose MakePose(float pitch, float yaw, float roll,
                            float x = 0.0f, float y = 0.0f, float z = 0.0f) {
    AdsEntryPose::Pose p;
    p.pitch = pitch; p.yaw = yaw; p.roll = roll;
    p.x = x; p.y = y; p.z = z;
    return p;
}
}  // namespace

// Hip fire is untouched: whatever the tracker says reaches the camera.
// ---- rui_transform ---------------------------------------------------------
//
// The crosshair and the hit indicator are drawn through the same kind of block,
// so they share this conversion. What it pins is the y flip: NDC y is up, the
// block's pixels run down, and both marks have to agree about that or a hit
// lands on the mirror image of where the rounds went.

void TestNdcToPixelsIsCentredAtZero() {
    float px = 1.0f, py = 1.0f;
    RuiNdcToPixels(0.0f, 0.0f, 1920.0f, 1080.0f, px, py);
    CHECK_NEAR(px, 0.0f, 1e-5f);
    CHECK_NEAR(py, 0.0f, 1e-5f);
}

void TestNdcToPixelsSpansHalfTheFrame() {
    float px = 0.0f, py = 0.0f;
    RuiNdcToPixels(1.0f, 1.0f, 1920.0f, 1080.0f, px, py);
    // The edge of the frame is half its width from the centre, and NDC +1 in y
    // is the TOP, which is negative pixels.
    CHECK_NEAR(px, 960.0f, 1e-3f);
    CHECK_NEAR(py, -540.0f, 1e-3f);
}

void TestAdsPoseHipPassesThrough() {
    AdsEntryPose entry;
    const auto out = entry.Relative(false, true, MakePose(5.0f, -12.0f, 3.0f, 1, 2, 3));
    CHECK_NEAR(out.pitch, 5.0f, 1e-6f);
    CHECK_NEAR(out.yaw, -12.0f, 1e-6f);
    CHECK_NEAR(out.roll, 3.0f, 1e-6f);
    CHECK_NEAR(out.x, 1.0f, 1e-6f);
    CHECK(!entry.HasEntry());
}

// The entry frame is identity, which is what puts the view on the aim point the
// reticle was marking rather than wherever the head happens to be.
void TestAdsPoseEntryFrameIsIdentity() {
    AdsEntryPose entry;
    const auto out = entry.Relative(true, true, MakePose(20.0f, -35.0f, 0.0f, 4, 5, 6));
    CHECK(entry.HasEntry());
    CHECK_NEAR(out.pitch, 0.0f, 1e-6f);
    CHECK_NEAR(out.yaw, 0.0f, 1e-6f);
    CHECK_NEAR(out.x, 0.0f, 1e-6f);
    CHECK_NEAR(out.y, 0.0f, 1e-6f);
    CHECK_NEAR(out.z, 0.0f, 1e-6f);
}

// Roll moves no aim point, so zeroing it would yank a head tilt the player is
// actively holding back to level and lean it in again as they move: two horizon
// jolts per aim, buying nothing.
void TestAdsPoseRollStaysAbsolute() {
    AdsEntryPose entry;
    const auto first = entry.Relative(true, true, MakePose(0.0f, 0.0f, 14.0f));
    CHECK_NEAR(first.roll, 14.0f, 1e-6f);
    const auto later = entry.Relative(true, true, MakePose(0.0f, 0.0f, -6.0f));
    CHECK_NEAR(later.roll, -6.0f, 1e-6f);
}

// Yaw arrives wrapped into -180..180, so a plain subtraction reads a 10 degree
// move across the seam as -350 and whips the view a full turn the wrong way.
void TestAdsPoseYawCrossesTheSeamTheShortWay() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 175.0f, 0.0f));
    const auto out = entry.Relative(true, true, MakePose(0.0f, -175.0f, 0.0f));
    CHECK_NEAR(out.yaw, 10.0f, 1e-4f);

    AdsEntryPose back;
    back.Relative(true, true, MakePose(0.0f, -175.0f, 0.0f));
    CHECK_NEAR(back.Relative(true, true, MakePose(0.0f, 175.0f, 0.0f)).yaw, -10.0f, 1e-4f);
}

// Pitch is bounded by the tracker's own asin and cannot wrap, so it stays a
// plain difference - and position goes relative with it.
void TestAdsPosePitchAndPositionAreRelative() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(10.0f, 0.0f, 0.0f, 3.0f, -1.0f, 2.0f));
    const auto out = entry.Relative(true, true, MakePose(-5.0f, 0.0f, 0.0f, 4.0f, -3.0f, 2.5f));
    CHECK_NEAR(out.pitch, -15.0f, 1e-6f);
    CHECK_NEAR(out.x, 1.0f, 1e-6f);
    CHECK_NEAR(out.y, -2.0f, 1e-6f);
    CHECK_NEAR(out.z, 0.5f, 1e-6f);
}

// The path that hits this: aim, open a menu, move your head, close it with the
// sights still up. The interpolators publish nothing until a fresh packet lands,
// so capturing on a dead frame would freeze a pre-suppression pose and hold the
// whole aim at that offset.
void TestAdsPoseCaptureWaitsForALiveRotation() {
    AdsEntryPose entry;
    entry.Relative(true, false, MakePose(9.0f, 9.0f, 0.0f));
    CHECK(!entry.HasEntry());
    const auto out = entry.Relative(true, true, MakePose(30.0f, -20.0f, 0.0f));
    CHECK(entry.HasEntry());
    CHECK_NEAR(out.pitch, 0.0f, 1e-6f);
    CHECK_NEAR(out.yaw, 0.0f, 1e-6f);
}

// Lowering the weapon drops it, so the next aim re-enters from wherever the head
// is then rather than from a pose two firefights old.
void TestAdsPoseLoweringTheWeaponDropsTheEntry() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(10.0f, 40.0f, 0.0f));
    const auto down = entry.Relative(false, true, MakePose(12.0f, 45.0f, 0.0f));
    CHECK(!entry.HasEntry());
    CHECK_NEAR(down.yaw, 45.0f, 1e-6f);
    const auto again = entry.Relative(true, true, MakePose(12.0f, 45.0f, 0.0f));
    CHECK_NEAR(again.yaw, 0.0f, 1e-6f);
}

// ---- ads_blend -------------------------------------------------------------
//
// What the ADS fade does to the frame's pose. The one rule that is not obvious
// from either end of it is that ROLL is not in the fade at all: a head tilt
// moves neither the eye off the barrel nor the aim off the middle of the frame,
// and levelling it every time the sights come up is two horizon jolts per aim
// for nothing. It shipped faded in `paused` and was reported from the chair as
// roll switching off in a Titan the moment the sights came up.

void TestAdsBlendHipIsTheHeadPose() {
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto relative = MakePose(0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f);
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        const auto out = BlendAdsPose(mode, 1.0f, absolute, relative);
        CHECK_NEAR(out.pitch, 5.0f, 1e-6f);
        CHECK_NEAR(out.yaw, -12.0f, 1e-6f);
        CHECK_NEAR(out.roll, 3.0f, 1e-6f);
        CHECK_NEAR(out.z, 3.0f, 1e-6f);
    }
}

// Sights fully up in `paused`: the view is the game's again, apart from the tilt
// the player is holding.
void TestAdsBlendPausedKeepsRollAndDropsTheRest() {
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto out = BlendAdsPose(AdsMode::Paused, 0.0f, absolute, MakePose(0, 0, 3.0f));
    CHECK_NEAR(out.pitch, 0.0f, 1e-6f);
    CHECK_NEAR(out.yaw, 0.0f, 1e-6f);
    CHECK_NEAR(out.x, 0.0f, 1e-6f);
    CHECK_NEAR(out.y, 0.0f, 1e-6f);
    CHECK_NEAR(out.z, 0.0f, 1e-6f);
    CHECK_NEAR(out.roll, 3.0f, 1e-6f);
}

// And halfway through the fade the tilt is still whole - it does not sag toward
// level and come back.
void TestAdsBlendPausedDoesNotFadeRollThroughTheTransition() {
    const auto absolute = MakePose(8.0f, -20.0f, 3.0f, 0.0f, 0.0f, 4.0f);
    const auto out = BlendAdsPose(AdsMode::Paused, 0.5f, absolute, MakePose(0, 0, 3.0f));
    CHECK_NEAR(out.pitch, 4.0f, 1e-6f);
    CHECK_NEAR(out.yaw, -10.0f, 1e-6f);
    CHECK_NEAR(out.z, 2.0f, 1e-6f);
    CHECK_NEAR(out.roll, 3.0f, 1e-6f);
}

// The tracked modes land on the entry-relative pose, whose roll is the absolute
// one already - so the two branches agree about roll and about nothing else.
void TestAdsBlendTrackedLandsOnTheEntryRelativePose() {
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto relative = MakePose(2.0f, -4.0f, 3.0f, 0.5f, 0.5f, 1.0f);
    for (const AdsMode mode : { AdsMode::Marker, AdsMode::Tracked }) {
        const auto out = BlendAdsPose(mode, 0.0f, absolute, relative);
        CHECK_NEAR(out.pitch, 2.0f, 1e-6f);
        CHECK_NEAR(out.yaw, -4.0f, 1e-6f);
        CHECK_NEAR(out.x, 0.5f, 1e-6f);
        CHECK_NEAR(out.z, 1.0f, 1e-6f);
        CHECK_NEAR(out.roll, 3.0f, 1e-6f);
    }
}

// ---- ads_gate --------------------------------------------------------------
//
// The verdict walk. ADS is tested last so a menu still reports its own reason
// when both are true at once, and no early return may leave the sights flag set.

void TestGatePausedModeClosesTheGateAndStillReportsTheSights() {
    const auto s = DecideTracking(SessionKind::Campaign, true, true, AdsMode::Paused);
    CHECK(s.verdict == TrackingVerdict::AdsSuspended);
    // The gate says whether tracking applies; the flag says the sights are up.
    // The per-frame code needs both, so `paused` still reports the sights.
    CHECK(s.aiming);
    // A pose still reaches the camera, because suspending is an ease-out rather
    // than a switch. Dropping it on the falling edge would throw the smoothing
    // state away and swing the view back through the head angle on the way out.
    CHECK(PoseApplies(s.verdict));
}

void TestGateTrackedModesStayOpenThroughAnAim() {
    for (const AdsMode mode : { AdsMode::Marker, AdsMode::Tracked }) {
        const auto s = DecideTracking(SessionKind::Campaign, true, true, mode);
        CHECK(s.verdict == TrackingVerdict::Active);
        CHECK(s.aiming);
        CHECK(PoseApplies(s.verdict));
    }
}

void TestGateHipFireIsActiveInEveryMode() {
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        const auto s = DecideTracking(SessionKind::Campaign, true, false, mode);
        CHECK(s.verdict == TrackingVerdict::Active);
        CHECK(!s.aiming);
    }
}

// A menu, a loading screen or a multiplayer map outranks ADS in the reported
// reason, and clears the flag with it: a stale flag through a menu would leave
// the marker placed against a weapon that is not raised.
void TestGateSuppressionOutranksAdsAndClearsTheFlag() {
    const struct { SessionKind session; TrackingVerdict verdict; } cases[] = {
        { SessionKind::NoLevel,     TrackingVerdict::NoLevel },
        { SessionKind::Loading,     TrackingVerdict::Loading },
        { SessionKind::Multiplayer, TrackingVerdict::Multiplayer },
        { SessionKind::Paused,      TrackingVerdict::GamePaused },
    };
    for (const auto& c : cases) {
        for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
            const auto s = DecideTracking(c.session, true, true, mode);
            CHECK(s.verdict == c.verdict);
            CHECK(!s.aiming);
            CHECK(!PoseApplies(s.verdict));
        }
    }
}

// No tracker is not an ADS verdict either, and it must not report the sights.
void TestGateNoTrackerReportsItsOwnReason() {
    const auto s = DecideTracking(SessionKind::Campaign, false, true, AdsMode::Tracked);
    CHECK(s.verdict == TrackingVerdict::NoTracker);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// The state is recomputed from the game every frame rather than latched on an
// edge, so an exit event that never arrives - a state machine that transitions
// without one, an aim released while firing - heals on the next frame instead of
// stranding the player in ADS behaviour.
void TestGateHealsWithoutAnExitEdge() {
    const auto aimed = DecideTracking(SessionKind::Campaign, true, true, AdsMode::Paused);
    CHECK(aimed.verdict == TrackingVerdict::AdsSuspended);
    const auto healed = DecideTracking(SessionKind::Campaign, true, false, AdsMode::Paused);
    CHECK(healed.verdict == TrackingVerdict::Active);
    CHECK(!healed.aiming);
}

}  // namespace

int main() {
    TestAngleUnits();
    TestAngleVectorsAtRest();
    TestAngleVectorsSenses();
    TestAngleRoundTrip();
    TestBasisToAnglesAtPole();
    TestClampPitch();
    TestWorldSpaceRotation();
    TestCarryRotation();
    TestCameraLocalRotation();
    TestViewRoundTrip();
    TestComposeViewBottomRow();
    TestViewOriginTranslation();
    TestMulMat4Identity();
    TestMulMat4Aliasing();
    TestMulMat4Order();
    TestDefaultsAreWithinLimits();
    TestDefaultPositionEnvelope();
    TestDefaultConfigMatchesConstants();
    TestHotkeyDefaultsAreDistinct();
    TestFovTangentConversion();
    TestFovScaleIsLinearInDegrees();
    TestPlausibleScale();
    TestPlausibleFovPerScale();
    TestGuardsAgreeAcrossTheAcceptedBand();
    TestAimAtCentreWhenHeadIsCentred();
    TestHeadYawMovesAimOppositeWay();
    TestHeadPitchMovesAimDown();
    TestRollAloneLeavesAimAtCentre();
    TestLeanMovesTheAimPointAgainstTheEye();
    TestAimBehindTheViewIsRejected();
    TestNdcToPixelsIsCentredAtZero();
    TestNdcToPixelsSpansHalfTheFrame();
    TestMarkerUntrackedFrameIsIdentity();
    TestMarkerLandsWhereTheDrawnCameraPutsIt();
    TestMarkerKeepsItsDistance();
    TestAdsPoseHipPassesThrough();
    TestAdsPoseEntryFrameIsIdentity();
    TestAdsPoseRollStaysAbsolute();
    TestAdsPoseYawCrossesTheSeamTheShortWay();
    TestAdsPosePitchAndPositionAreRelative();
    TestAdsPoseCaptureWaitsForALiveRotation();
    TestAdsPoseLoweringTheWeaponDropsTheEntry();
    TestAdsBlendHipIsTheHeadPose();
    TestAdsBlendPausedKeepsRollAndDropsTheRest();
    TestAdsBlendPausedDoesNotFadeRollThroughTheTransition();
    TestAdsBlendTrackedLandsOnTheEntryRelativePose();
    TestGatePausedModeClosesTheGateAndStillReportsTheSights();
    TestGateTrackedModesStayOpenThroughAnAim();
    TestGateHipFireIsActiveInEveryMode();
    TestGateSuppressionOutranksAdsAndClearsTheFlag();
    TestGateNoTrackerReportsItsOwnReason();
    TestGateHealsWithoutAnExitEdge();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
