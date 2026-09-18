// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "render_diagnostics.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "logging.h"

#include "cameraunlock/math/smoothing_utils.h"

namespace tt3_ht::diag {

namespace {

constexpr long long kDiagnosticFrames = 3;
constexpr int kTrackerPresenceChanges = 10;
constexpr float kDeliberateMovementDegrees = 5.0f;
constexpr ULONGLONG kPoseSampleIntervalMs = 2000;
// Rider views blend their FOV with speed, so while riding it changes every
// frame and this interval alone sets how often the line is written.
constexpr ULONGLONG kFovLogIntervalMs = 5000;
constexpr float kFovChangeRadians = 0.001f;
constexpr float kZoomFactorChange = 0.0005f;
constexpr float kRadiansToDegrees = 57.29578f;

std::string Degrees(float radians) {
    if (radians < 0.0f) return "n/a";
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f deg", radians * kRadiansToDegrees);
    return text;
}

}  // namespace

void LogConnectionLocality(bool is_remote, float local_smoothing, float remote_smoothing) {
    static bool last_remote = false;
    static bool known = false;

    if (known && is_remote == last_remote) return;
    last_remote = is_remote;
    known = true;

    Log::Line("[udp] tracker source is %s - smoothing=%.2f",
              is_remote ? "a remote device" : "on this machine",
              cameraunlock::math::GetEffectiveSmoothing(local_smoothing, remote_smoothing,
                                                        is_remote));
}

void LogTrackerPresence(bool receiving) {
    static bool last_receiving = false;
    static int changes = 0;

    if (receiving == last_receiving) return;
    last_receiving = receiving;

    if (changes >= kTrackerPresenceChanges) return;
    ++changes;

    Log::Line("[udp] tracker data %s", receiving
                  ? "is arriving"
                  : "stopped arriving - the last pose is held until it resumes");
    if (changes == kTrackerPresenceChanges) {
        Log::Line("[udp] the tracker has come and gone %d times; further changes are not logged.",
                  kTrackerPresenceChanges);
    }
}

void LogGateChange(bool tracking_enabled, const CameraReading& camera, const FlowReading& flow,
                   std::uint32_t network_state, bool following) {
    static bool known = false;
    static bool last_enabled = false;
    static bool last_player_camera = false;
    static std::string last_mode;
    static std::string last_flow;
    static std::uint32_t last_network = 0;
    static bool last_following = false;

    if (known && tracking_enabled == last_enabled && camera.player_camera == last_player_camera
        && camera.mode == last_mode && flow.description == last_flow
        && network_state == last_network && following == last_following) {
        return;
    }
    known = true;
    last_enabled = tracking_enabled;
    last_player_camera = camera.player_camera;
    last_mode = camera.mode;
    last_flow = flow.description;
    last_network = network_state;
    last_following = following;

    Log::Line("[state] tracking %s, %s camera, mode '%s', flow [%s]%s, network state %u - "
              "head tracking %s",
              tracking_enabled ? "on" : "off",
              camera.player_camera ? "player" : "non-player",
              camera.mode.c_str(), flow.description.c_str(),
              flow.multiplayer ? " (online activity)" : "",
              network_state, following ? "following" : "off");
}

void LogFieldOfView(const CameraReading& camera, bool zoom_known, float zoom_factor) {
    static bool logged = false;
    static CameraReading last;
    static bool last_known = false;
    static float last_factor = 0.0f;
    static ULONGLONG last_time = 0;

    const bool changed = !logged || zoom_known != last_known
        || std::fabs(camera.fov - last.fov) >= kFovChangeRadians
        || std::fabs(camera.mode_fov - last.mode_fov) >= kFovChangeRadians
        || std::fabs(camera.base_fov - last.base_fov) >= kFovChangeRadians
        || (zoom_known && std::fabs(zoom_factor - last_factor) >= kZoomFactorChange);
    if (!changed) return;
    const ULONGLONG now = GetTickCount64();
    if (logged && now - last_time < kFovLogIntervalMs) return;
    logged = true;
    last = camera;
    last_known = zoom_known;
    last_factor = zoom_factor;
    last_time = now;

    // All three are horizontal. The rendered FOV is the mode's, widened by the
    // engine on a screen wider than 16:9, so the two differ only there.
    const char* const zoom = zoom_known ? "" : camera.mode == kRiderViewMode
        ? " - the view's resting FOV could not be read, no zoom compensation"
        : " - not a rider view, no zoom compensation";
    Log::Line("[camera] horizontal FOV: rendered %s, camera mode %s, rider view resting %s, "
              "zoom factor %.4f%s",
              Degrees(camera.fov).c_str(), Degrees(camera.mode_fov).c_str(),
              Degrees(camera.base_fov).c_str(), zoom_known ? zoom_factor : 1.0f, zoom);
}

void LogFirstFrames(long long frame, const float* clean, bool have_rotation, const HeadPose& pose) {
    if (frame >= kDiagnosticFrames) return;

    Log::Line("[camera] frame %lld  tracker=%s  yaw=%.2f pitch=%.2f roll=%.2f  lean=%.3f %.3f %.3f",
              frame, have_rotation ? "yes" : "no", pose.yaw, pose.pitch, pose.roll,
              pose.lean_x, pose.lean_y, pose.lean_z);
    for (unsigned row = 0; row < 3; ++row) {
        Log::Line("[camera]   m[%u] % 12.5f % 12.5f % 12.5f % 14.4f", row,
                  clean[row * 4 + 0], clean[row * 4 + 1], clean[row * 4 + 2], clean[row * 4 + 3]);
    }
}

void LogFirstPoseReachingCamera(long long frame, bool have_rotation, const HeadPose& pose) {
    static bool logged = false;
    if (!have_rotation || logged) return;
    logged = true;
    Log::Line("[camera] head pose reached the camera hook on frame %lld: "
              "yaw=%.2f pitch=%.2f roll=%.2f", frame, pose.yaw, pose.pitch, pose.roll);
}

void LogComposedPose(const HeadPose& pose, const float* clean, const float* tracked) {
    static bool first_logged = false;
    static ULONGLONG last_sample = 0;

    const bool deliberate = std::fabs(pose.yaw) >= kDeliberateMovementDegrees
                         || std::fabs(pose.pitch) >= kDeliberateMovementDegrees;
    const ULONGLONG now = GetTickCount64();
    if (!first_logged) {
        if (!deliberate) return;
        first_logged = true;
    } else if (now - last_sample < kPoseSampleIntervalMs) {
        return;
    }
    last_sample = now;

    // Forward is -Z, the negated third column.
    Log::Line("[camera] composed yaw=%.2f pitch=%.2f roll=%.2f lean=%.3f %.3f %.3f | "
              "clean fwd %.3f %.3f %.3f up %.3f %.3f %.3f | tracked fwd %.3f %.3f %.3f "
              "up %.3f %.3f %.3f | eye moved %.3f %.3f %.3f",
              pose.yaw, pose.pitch, pose.roll, pose.lean_x, pose.lean_y, pose.lean_z,
              -clean[2], -clean[6], -clean[10], clean[1], clean[5], clean[9],
              -tracked[2], -tracked[6], -tracked[10], tracked[1], tracked[5], tracked[9],
              tracked[3] - clean[3], tracked[7] - clean[7], tracked[11] - clean[11]);
}

}  // namespace tt3_ht::diag
