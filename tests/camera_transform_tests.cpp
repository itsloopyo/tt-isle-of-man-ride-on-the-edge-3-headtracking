// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Locks the camera composition: which way each pose axis moves the view, that
// every axis is camera-local, that the lean follows the clean basis, and that
// the view matrix is the inverse of the world transform. If one of these fails,
// what the player sees has changed.

#include "camera_transform.h"

#include "test_support.h"

#include <cmath>
#include <cstring>

using namespace tt3_ht;
using tt3_test::Check;
using tt3_test::CheckClose;

namespace {

// Matrix34, column-vector convention: columns right, up, back, position.
// A level camera looking along world +Y in the engine's Z-up world: right +X,
// up +Z, back -Y.
constexpr float kLevel[kMatrix34Floats] = {
    1.0f, 0.0f,  0.0f, 100.0f,
    0.0f, 0.0f, -1.0f, 200.0f,
    0.0f, 1.0f,  0.0f,  50.0f,
};

// The same camera with the bike banked 40 degrees to the right about its
// forward axis: up leans toward +X.
float g_banked[kMatrix34Floats];

struct V { float x, y, z; };

V Col(const float* m, int c) { return { m[c], m[4 + c], m[8 + c] }; }
V Forward(const float* m) { const V b = Col(m, 2); return { -b.x, -b.y, -b.z }; }
V Eye(const float* m) { return Col(m, 3); }
float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

void Compose(const float* base, const HeadPose& pose, float* out) {
    std::memcpy(out, base, kMatrix34Floats * sizeof(float));
    ApplyHeadPose(out, pose);
}

void BuildBanked() {
    const float a = 40.0f * 3.14159265f / 180.0f;
    // Columns: right (cos a, 0, -sin a), up (sin a, 0, cos a), back (0, -1, 0).
    const float m[kMatrix34Floats] = {
         std::cos(a), std::sin(a),  0.0f, 0.0f,
         0.0f,        0.0f,        -1.0f, 0.0f,
        -std::sin(a), std::cos(a),  0.0f, 0.0f,
    };
    std::memcpy(g_banked, m, sizeof(m));
}

void TestZeroPoseLeavesCameraUntouched() {
    float out[kMatrix34Floats];
    Compose(kLevel, HeadPose{}, out);
    bool same = true;
    for (unsigned i = 0; i < kMatrix34Floats; ++i) same = same && std::fabs(out[i] - kLevel[i]) < 1e-6f;
    Check(same, "a zero pose leaves the engine's transform exactly as it was");
}

void TestYawTurnsRight() {
    HeadPose pose; pose.yaw = 30.0f;
    float out[kMatrix34Floats];
    Compose(kLevel, pose, out);
    const V f = Forward(out);
    CheckClose(f.x, std::sin(30.0f * 3.14159265f / 180.0f), "positive yaw turns the view right (+X)");
    CheckClose(f.z, 0.0f, "yaw alone does not pitch the view");
    CheckClose(Eye(out).x, 100.0f, "rotation does not move the eye");
}

void TestPitchLooksUp() {
    HeadPose pose; pose.pitch = 20.0f;
    float out[kMatrix34Floats];
    Compose(kLevel, pose, out);
    const V f = Forward(out);
    CheckClose(f.z, std::sin(20.0f * 3.14159265f / 180.0f), "positive pitch looks up (+Z)");
    CheckClose(f.x, 0.0f, "pitch alone does not yaw the view");
}

void TestRollTiltsLeft() {
    HeadPose pose; pose.roll = 15.0f;
    float out[kMatrix34Floats];
    Compose(kLevel, pose, out);
    const V up = Col(out, 1);
    Check(up.x < -0.2f, "positive roll tilts the view's up axis to the left");
    const V f = Forward(out);
    CheckClose(f.y, 1.0f, "roll alone leaves the view direction where it was");
}

void TestLeanDirections() {
    float out[kMatrix34Floats];
    HeadPose x; x.lean_x = 0.1f;
    Compose(kLevel, x, out);
    CheckClose(Eye(out).x - 100.0f, -0.1f, "positive x moves the eye left");
    HeadPose y; y.lean_y = 0.1f;
    Compose(kLevel, y, out);
    CheckClose(Eye(out).z - 50.0f, 0.1f, "positive y moves the eye up");
    HeadPose z; z.lean_z = -0.2f;
    Compose(kLevel, z, out);
    CheckClose(Eye(out).y - 200.0f, 0.2f, "negative z leans the eye forward");
}

void TestLeanFollowsCleanBasis() {
    HeadPose pose; pose.yaw = 90.0f; pose.lean_x = 0.1f;
    float out[kMatrix34Floats];
    Compose(kLevel, pose, out);
    CheckClose(Eye(out).x - 100.0f, -0.1f,
               "a lean is carried by the bike's basis, not the turned head's");
    CheckClose(Eye(out).y - 200.0f, 0.0f, "a turned head does not redirect the lean");
}

void TestRotationIsCameraLocal() {
    BuildBanked();
    HeadPose pose; pose.yaw = 30.0f;
    float out[kMatrix34Floats];
    Compose(g_banked, pose, out);
    const V up_before = Col(g_banked, 1);
    const V up_after = Col(out, 1);
    CheckClose(Dot(up_before, up_after), 1.0f,
               "yaw on a banked bike turns about the camera's own up axis");
    const V f = Forward(out);
    CheckClose(Dot(f, Col(g_banked, 0)), std::sin(30.0f * 3.14159265f / 180.0f),
               "yaw on a banked bike swings the view toward the camera's own right");
}

void TestBasisStaysOrthonormal() {
    HeadPose pose; pose.yaw = 37.0f; pose.pitch = -22.0f; pose.roll = 11.0f;
    float out[kMatrix34Floats];
    Compose(kLevel, pose, out);
    const V r = Col(out, 0), u = Col(out, 1), b = Col(out, 2);
    CheckClose(Dot(r, r), 1.0f, "right stays unit length");
    CheckClose(Dot(u, u), 1.0f, "up stays unit length");
    CheckClose(Dot(b, b), 1.0f, "back stays unit length");
    CheckClose(Dot(r, u), 0.0f, "right and up stay orthogonal");
    CheckClose(Dot(r, b), 0.0f, "right and back stay orthogonal");
    CheckClose(Dot(u, b), 0.0f, "up and back stay orthogonal");
}

void TestRollDoesNotMoveTheViewDirection() {
    HeadPose a; a.yaw = 20.0f; a.pitch = 10.0f;
    HeadPose b = a; b.roll = 25.0f;
    float out_a[kMatrix34Floats], out_b[kMatrix34Floats];
    Compose(kLevel, a, out_a);
    Compose(kLevel, b, out_b);
    const V fa = Forward(out_a), fb = Forward(out_b);
    CheckClose(Dot(fa, fb), 1.0f, "roll is innermost: it spins the view without moving its centre");
}

void TestViewMatrixInvertsWorld() {
    HeadPose pose; pose.yaw = 12.0f; pose.pitch = 5.0f; pose.roll = -3.0f; pose.lean_x = 0.05f;
    float world[kMatrix34Floats];
    Compose(kLevel, pose, world);
    float view[kMatrix44Floats];
    RigidViewMatrix(world, view);
    const V e = Eye(world);
    for (int r = 0; r < 3; ++r) {
        const float v = view[r * 4] * e.x + view[r * 4 + 1] * e.y + view[r * 4 + 2] * e.z
                      + view[r * 4 + 3];
        CheckClose(v, 0.0f, "the view matrix maps the eye to the origin");
    }
    // With the eye already mapped to the origin, a point one metre ahead lands
    // at the rotation rows applied to the forward vector. Adding the forward
    // vector to an eye 200 m out and cancelling the translation again would
    // lose ~1.5e-5 to float rounding at that magnitude, more than kEpsilon.
    const V f = Forward(world);
    const float z = view[8] * f.x + view[9] * f.y + view[10] * f.z;
    CheckClose(z, -1.0f, "a point one metre ahead lands on -Z in view space");
    CheckClose(view[15], 1.0f, "the last row is (0,0,0,1)");
}

}  // namespace

void TestZoomScalingKeepsScreenDisplacement() {
    const HeadPose pose{ 20.0f, -10.0f, 15.0f, 0.1f, -0.05f, -0.2f };

    const HeadPose same = ScaleForZoom(pose, 1.0f);
    CheckClose(same.yaw, pose.yaw, "zoom factor 1: yaw unchanged");
    CheckClose(same.pitch, pose.pitch, "zoom factor 1: pitch unchanged");
    CheckClose(same.lean_z, pose.lean_z, "zoom factor 1: lean unchanged");

    // A 105 degree view widened to 115 by the chase camera's acceleration term.
    const float rad = 3.14159265f / 180.0f;
    const float factor = std::tan(57.5f * rad) / std::tan(52.5f * rad);
    const HeadPose wide = ScaleForZoom(pose, factor);
    CheckClose(std::tan(wide.yaw * rad) / std::tan(57.5f * rad),
               std::tan(pose.yaw * rad) / std::tan(52.5f * rad),
               "zoomed yaw lands at the same fraction of the screen as at rest");
    CheckClose(std::tan(wide.pitch * rad) / std::tan(57.5f * rad),
               std::tan(pose.pitch * rad) / std::tan(52.5f * rad),
               "zoomed pitch lands at the same fraction of the screen as at rest");
    CheckClose(wide.roll, pose.roll, "roll is not scaled for zoom");
    CheckClose(wide.lean_x, pose.lean_x * factor, "lean x scales with the tangent ratio");
    CheckClose(wide.lean_y, pose.lean_y * factor, "lean y scales with the tangent ratio");
    CheckClose(wide.lean_z, pose.lean_z * factor, "lean z scales with the tangent ratio");
}

int main() {
    TestZeroPoseLeavesCameraUntouched();
    TestYawTurnsRight();
    TestPitchLooksUp();
    TestRollTiltsLeft();
    TestLeanDirections();
    TestLeanFollowsCleanBasis();
    TestRotationIsCameraLocal();
    TestBasisStaysOrthonormal();
    TestRollDoesNotMoveTheViewDirection();
    TestViewMatrixInvertsWorld();
    TestZoomScalingKeepsScreenDisplacement();
    return tt3_test::Summary("camera_transform");
}
