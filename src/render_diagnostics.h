// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "camera_transform.h"
#include "game_state.h"

// Everything the mod writes to the log from the render path. Every function is
// edge-triggered or rate-limited: this runs once per rendered frame.
namespace tt3_ht::diag {

// Which of the two smoothing values is in effect, on every change of source.
void LogConnectionLocality(bool is_remote, float local_smoothing, float remote_smoothing);

// Edge-triggered and capped: the tracker coming and going.
void LogTrackerPresence(bool receiving);

// One line whenever any input to the gate changes, naming each of them and the
// answer.
void LogGateChange(bool tracking_enabled, const CameraReading& camera, const FlowReading& flow,
                   std::uint32_t network_state, bool following);

// Every term of the zoom compensation: the rendered FOV, the FOV the camera
// mode set, the rider view's resting FOV and the factor the pose is scaled by.
// Logged on the first camera update, then whenever a term moves, at most once a
// second. The factor reads 1.0000 whenever the chase views' speed and
// acceleration terms are at rest; anything else at rest is a units fault.
void LogFieldOfView(const CameraReading& camera, bool zoom_known, float zoom_factor);

// The first few camera updates in full: the engine's world transform and the
// pose, to confirm the hook runs and the matrix is still a camera transform.
void LogFirstFrames(long long frame, const float* clean, bool have_rotation, const HeadPose& pose);

// Latched: the first pose that reached the camera hook at all.
void LogFirstPoseReachingCamera(long long frame, bool have_rotation, const HeadPose& pose);

// The first deliberate pose composed into a frame, and then one sample every
// two seconds while head tracking follows: the applied pose, the clean and the
// tracked forward axis and the eye offset. Uncapped, so it answers questions
// asked late in a session as well as early ones.
void LogComposedPose(const HeadPose& pose, const float* clean, const float* tracked);

}  // namespace tt3_ht::diag
