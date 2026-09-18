// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_transform.h"

#include <cstring>

#include "cameraunlock/camera/zoom_compensation.h"
#include "cameraunlock/math/quat4.h"
#include "cameraunlock/math/vec3.h"

namespace tt3_ht {

namespace {

constexpr int kRows = 3;
constexpr int kStride = 4;
constexpr int kTranslationColumn = 3;

constexpr int At(int row, int column) { return row * kStride + column; }

cameraunlock::math::Vec3 Column(const float m[kMatrix34Floats], int column) {
    return cameraunlock::math::Vec3(m[At(0, column)], m[At(1, column)], m[At(2, column)]);
}

}  // namespace

HeadPose ScaleForZoom(const HeadPose& pose, float zoom_factor) {
    using cameraunlock::camera::ScaleAngleForZoom;
    HeadPose scaled = pose;
    scaled.yaw = ScaleAngleForZoom(pose.yaw, zoom_factor);
    scaled.pitch = ScaleAngleForZoom(pose.pitch, zoom_factor);
    scaled.lean_x = pose.lean_x * zoom_factor;
    scaled.lean_y = pose.lean_y * zoom_factor;
    scaled.lean_z = pose.lean_z * zoom_factor;
    return scaled;
}

void ApplyHeadPose(float world[kMatrix34Floats], const HeadPose& pose) {
    using cameraunlock::math::Quat4;
    using cameraunlock::math::Vec3;

    // The tracker convention, as Wreckfest 2 established it in a running game
    // through the same core pipeline: positive yaw turns the view right,
    // positive pitch looks up, positive roll tilts the head to the left,
    // positive x moves the head left and negative z leans forward. TT3's
    // camera is right-handed with -Z forward, so a right turn is a negative
    // rotation about its up axis and a leftward head is a negative offset
    // along its right axis. Pitch, roll, y and z already agree with it.
    const float engine_yaw = -pose.yaw;
    const float engine_pitch = pose.pitch;
    const float engine_roll = pose.roll;
    const Vec3 local_lean(-pose.lean_x, pose.lean_y, pose.lean_z);

    float clean[kMatrix34Floats];
    std::memcpy(clean, world, sizeof(clean));

    const Vec3 basis[kRows] = { Column(clean, 0), Column(clean, 1), Column(clean, 2) };
    const Quat4 head = Quat4::FromYawPitchRoll(engine_yaw, engine_pitch, engine_roll);
    const Vec3 unit[kRows] = { Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
                               Vec3(0.0f, 0.0f, 1.0f) };

    // Column c of the tracked basis is the clean basis applied to the head
    // rotation's column c, which keeps every axis camera-local.
    for (int c = 0; c < kRows; ++c) {
        const Vec3 h = head.Rotate(unit[c]);
        const Vec3 axis = basis[0] * h.x + basis[1] * h.y + basis[2] * h.z;
        world[At(0, c)] = axis.x;
        world[At(1, c)] = axis.y;
        world[At(2, c)] = axis.z;
    }

    const Vec3 offset = basis[0] * local_lean.x + basis[1] * local_lean.y
                      + basis[2] * local_lean.z;
    world[At(0, kTranslationColumn)] = clean[At(0, kTranslationColumn)] + offset.x;
    world[At(1, kTranslationColumn)] = clean[At(1, kTranslationColumn)] + offset.y;
    world[At(2, kTranslationColumn)] = clean[At(2, kTranslationColumn)] + offset.z;
}

void RigidViewMatrix(const float world[kMatrix34Floats], float view[kMatrix44Floats]) {
    // inverse([R t; 0 1]) = [R^T  -R^T t; 0 1]. Row r of R^T is column r of R.
    for (int r = 0; r < kRows; ++r) {
        float dot = 0.0f;
        for (int c = 0; c < kRows; ++c) {
            view[r * 4 + c] = world[At(c, r)];
            dot += world[At(c, r)] * world[At(c, kTranslationColumn)];
        }
        view[r * 4 + 3] = -dot;
    }
    view[12] = 0.0f;
    view[13] = 0.0f;
    view[14] = 0.0f;
    view[15] = 1.0f;
}

void CarryWithHead(const float clean[kMatrix34Floats], const float tracked[kMatrix34Floats],
                   float object[kMatrix34Floats]) {
    // The object in the clean camera's frame, then that frame swapped for the
    // tracked camera. The view matrix is exactly inverse(clean) padded to 4x4.
    float to_camera[kMatrix44Floats];
    RigidViewMatrix(clean, to_camera);

    float local[kMatrix34Floats];
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kStride; ++c) {
            float sum = c == kTranslationColumn ? to_camera[r * 4 + 3] : 0.0f;
            for (int k = 0; k < kRows; ++k) sum += to_camera[r * 4 + k] * object[At(k, c)];
            local[At(r, c)] = sum;
        }
    }
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kStride; ++c) {
            float sum = c == kTranslationColumn ? tracked[At(r, kTranslationColumn)] : 0.0f;
            for (int k = 0; k < kRows; ++k) sum += tracked[At(r, k)] * local[At(k, c)];
            object[At(r, c)] = sum;
        }
    }
}

}  // namespace tt3_ht
