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

#include "cameraunlock/ads/ads_fade.h"
#include "ads_gate.h"
#include "frame_pose.h"
#include "projection.h"
#include "rui_transform.h"
#include "angle_units.h"
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

// ---- aim projection (cameraunlock/rendering/aim_ndc_projection.h) -----------
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

// ---- world reprojection (cameraunlock/rendering/world_reprojection.h) -------
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

// ---- frame_pose: the lean easing and the zoom compensation -----------------
//
// Head tracking carries straight on through an aim. The sights ease the lean out
// on core's AdsFade and touch nothing else, and the zoom scales yaw, pitch and
// the lean so a scope does not magnify the head.

namespace {
HeadPose MakePose(float pitch, float yaw, float roll, float x, float y, float z) {
    HeadPose p;
    p.pitch = pitch; p.yaw = yaw; p.roll = roll;
    p.x = x; p.y = y; p.z = z;
    return p;
}

void CheckPose(const HeadPose& got, const HeadPose& want, const char* file, int line) {
    CheckNear(got.pitch, want.pitch, 1e-5f, "pitch", file, line);
    CheckNear(got.yaw, want.yaw, 1e-5f, "yaw", file, line);
    CheckNear(got.roll, want.roll, 1e-5f, "roll", file, line);
    CheckNear(got.x, want.x, 1e-5f, "x", file, line);
    CheckNear(got.y, want.y, 1e-5f, "y", file, line);
    CheckNear(got.z, want.z, 1e-5f, "z", file, line);
}
#define CHECK_POSE(got, want) CheckPose((got), (want), __FILE__, __LINE__)

using cameraunlock::ads::AdsFade;
}  // namespace

// At the hip the fade holds 1 and the pose goes through untouched.
void TestHipPassesThePoseThrough() {
    AdsFade fade;
    const HeadPose in = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const float scale = fade.Update(false, 1000);
    CHECK_NEAR(scale, 1.0f, 0.0f);
    CHECK_POSE(EaseLeanForSights(in, scale), in);
}

// With the sights fully up, rotation - roll included - is absolute and
// unscaled, and the lean is gone.
void TestSightsUpDropsTheLeanAndKeepsRotation() {
    AdsFade fade;
    fade.Update(true, 0);
    const float scale = fade.Update(true, AdsFade::kLowerMs + 1);
    CHECK_NEAR(scale, 0.0f, 0.0f);
    const HeadPose out = EaseLeanForSights(MakePose(20.0f, -35.0f, 14.0f, 4.0f, 5.0f, 6.0f), scale);
    CHECK_POSE(out, MakePose(20.0f, -35.0f, 14.0f, 0.0f, 0.0f, 0.0f));
}

// Halfway down, the lean is scaled by the fade and rotation is still whole.
void TestMidTransitionScalesOnlyTheLean() {
    AdsFade fade;
    fade.Update(true, 0);
    const float scale = fade.Update(true, AdsFade::kLowerMs / 2);
    CHECK(scale > 0.0f && scale < 1.0f);
    const HeadPose out = EaseLeanForSights(MakePose(8.0f, -20.0f, 3.0f, 2.0f, -4.0f, 6.0f), scale);
    CHECK_POSE(out, MakePose(8.0f, -20.0f, 3.0f, 2.0f * scale, -4.0f * scale, 6.0f * scale));
}

// A tap of the aim button reverses the fade mid-leg. It must continue from where
// it was, not jump to either end.
void TestReversalContinuesFromWhereItWas() {
    AdsFade fade;
    fade.Update(true, 0);
    const float before = fade.Update(true, AdsFade::kLowerMs / 2);
    const float after = fade.Update(false, AdsFade::kLowerMs / 2);
    CHECK_NEAR(after, before, 1e-6f);
    const float later = fade.Update(false, AdsFade::kLowerMs / 2 + 20);
    CHECK(later > after && later < 1.0f);
}

// Unzoomed, the compensation is the identity.
void TestZoomAtTheHipIsIdentity() {
    const HeadPose in = MakePose(10.0f, -30.0f, 7.0f, 1.0f, 2.0f, 3.0f);
    CHECK_POSE(CompensateZoom(in, 1.0f), in);
}

// Through a zoom, yaw and pitch shrink so their SCREEN displacement matches the
// un-zoomed one (tangents scale by the factor), the lean scales linearly, and
// roll is left alone. 2.6116 is the gauntlet sniper's measured zoom.
void TestZoomScalesYawPitchAndLeanNotRoll() {
    const float factor = cameraunlock::camera::FovZoomFactor(0.93361f / 2.6116f, 0.93361f);
    CHECK_NEAR(factor, 1.0f / 2.6116f, 1e-6f);
    const HeadPose out = CompensateZoom(MakePose(10.0f, -30.0f, 7.0f, 1.0f, 2.0f, 3.0f), factor);
    CHECK_NEAR(std::tan(out.pitch * kDegToRad), std::tan(10.0f * kDegToRad) * factor, 1e-5f);
    CHECK_NEAR(std::tan(out.yaw * kDegToRad), std::tan(-30.0f * kDegToRad) * factor, 1e-5f);
    CHECK_NEAR(out.roll, 7.0f, 0.0f);
    CHECK_NEAR(out.x, 1.0f * factor, 1e-6f);
    CHECK_NEAR(out.y, 2.0f * factor, 1e-6f);
    CHECK_NEAR(out.z, 3.0f * factor, 1e-6f);
}

// ---- ads_gate --------------------------------------------------------------
//
// The verdict walk. The sights never close the gate; ADS is taken last so a menu
// still reports its own reason, and no early return may leave the flag set.

void TestGateStaysOpenThroughAnAim() {
    const auto s = DecideTracking(SessionKind::Campaign, true, true);
    CHECK(s.verdict == TrackingVerdict::Active);
    CHECK(s.aiming);
}

void TestGateHipFireIsActive() {
    const auto s = DecideTracking(SessionKind::Campaign, true, false);
    CHECK(s.verdict == TrackingVerdict::Active);
    CHECK(!s.aiming);
}

void TestGateSuppressionOutranksAdsAndClearsTheFlag() {
    const struct { SessionKind session; TrackingVerdict verdict; } cases[] = {
        { SessionKind::NoLevel,     TrackingVerdict::NoLevel },
        { SessionKind::Loading,     TrackingVerdict::Loading },
        { SessionKind::Multiplayer, TrackingVerdict::Multiplayer },
        { SessionKind::Paused,      TrackingVerdict::GamePaused },
    };
    for (const auto& c : cases) {
        const auto s = DecideTracking(c.session, true, true);
        CHECK(s.verdict == c.verdict);
        CHECK(!s.aiming);
    }
}

void TestGateNoTrackerReportsItsOwnReason() {
    const auto s = DecideTracking(SessionKind::Campaign, false, true);
    CHECK(s.verdict == TrackingVerdict::NoTracker);
    CHECK(!s.aiming);
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
    TestHipPassesThePoseThrough();
    TestSightsUpDropsTheLeanAndKeepsRotation();
    TestMidTransitionScalesOnlyTheLean();
    TestReversalContinuesFromWhereItWas();
    TestZoomAtTheHipIsIdentity();
    TestZoomScalesYawPitchAndLeanNotRoll();
    TestGateStaysOpenThroughAnAim();
    TestGateHipFireIsActive();
    TestGateSuppressionOutranksAdsAndClearsTheFlag();
    TestGateNoTrackerReportsItsOwnReason();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
