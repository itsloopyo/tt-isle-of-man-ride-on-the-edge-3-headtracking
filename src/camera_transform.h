// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace tt3_ht {

// The engine's camera world transform: a Matrix34, three rows of four floats,
// column-vector convention. Columns 0, 1 and 2 are the camera's right, up and
// BACK axes (the camera looks down -Z, right-handed), column 3 its position.
constexpr unsigned kMatrix34Floats = 12;
// A Matrix44 in the same row layout, as the engine's view and projection
// matrices are stored.
constexpr unsigned kMatrix44Floats = 16;

// A processed head pose as the core pipeline hands it over: degrees of
// rotation and metres of lean, in the tracker's convention. Every sign
// conversion to the engine's convention happens inside ApplyHeadPose.
struct HeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float lean_x = 0.0f;
    float lean_y = 0.0f;
    float lean_z = 0.0f;
};

// The pose rescaled so it moves the picture as far as it would at the view's
// resting FOV. `zoom_factor` is tan(fov/2) / tan(resting fov/2), exactly 1.0
// whenever the game is not widening or narrowing the view. Yaw, pitch and the
// lean scale; roll rotates the picture by the same angle at any FOV and does
// not.
HeadPose ScaleForZoom(const HeadPose& pose, float zoom_factor);

// Composes `pose` into `world` in place. All three rotation axes are
// camera-local: a bike leans hard into every corner, and a world-locked yaw
// axis would turn the view about something with no relation to where the
// rider is looking. The lean is carried through the clean camera basis, so it
// follows the bike and not the direction the head is turned.
void ApplyHeadPose(float world[kMatrix34Floats], const HeadPose& pose);

// The view matrix for a rigid camera world transform, in the layout
// ViewContext::InitFromCamera stores it: the inverse of [world; 0 0 0 1].
void RigidViewMatrix(const float world[kMatrix34Floats], float view[kMatrix44Floats]);

// Moves an object that the game placed relative to the clean camera so that it
// sits in the same place relative to the tracked one:
// object = tracked * inverse(clean) * object. `clean` must be rigid.
void CarryWithHead(const float clean[kMatrix34Floats], const float tracked[kMatrix34Floats],
                   float object[kMatrix34Floats]);

}  // namespace tt3_ht
