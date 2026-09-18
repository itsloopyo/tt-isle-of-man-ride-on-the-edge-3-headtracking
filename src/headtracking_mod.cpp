// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>

#include "builds/build_registry.h"
#include "camera_hook.h"
#include "config.h"
#include "game_state.h"
#include "hotkeys.h"
#include "logging.h"
#include "render_diagnostics.h"

#include "cameraunlock/camera/zoom_compensation.h"
#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/os/game_window.h"
#include "cameraunlock/os/module_paths.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace tt3_ht {

namespace {

using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;
static_assert(Session::kHasRemoteConnection,
              "UdpReceiver must expose IsRemoteConnection() to select Local/RemoteSmoothing");

Config g_config;
cameraunlock::UdpReceiver g_receiver;
Session g_session(g_receiver);
cameraunlock::input::HotkeyPoller g_hotkeys;
cameraunlock::time::FrameClock g_frame_clock;

std::atomic<bool> g_tracking_enabled{false};
std::atomic<bool> g_active{false};
std::atomic<long long> g_frame_counter{0};

// Mode cycles pressed but not yet applied. CycleMode() resets interpolator and
// processor state the render thread reads inside a session update, so the
// hotkey thread only records the press and the render thread performs it
// between two updates.
std::atomic<int> g_pending_mode_cycles{0};

std::atomic<bool> g_pinned{false};

constexpr wchar_t kGameExeName[] = L"TT3.exe";
constexpr float kPi = 3.14159265358979323846f;

void ApplyConfigToPipeline(const Config& config, Session& session) {
    // No sensitivity, deadzone, response curve or axis inversion: the tracker
    // owns pose shaping. The protocol-to-engine sign conversion is a fixed part
    // of the boundary, in camera_transform.cpp.
    session.SetLocalSmoothing(config.local_smoothing);
    session.SetRemoteSmoothing(config.remote_smoothing);

    session.SetPositionSettings(cameraunlock::PositionSettings::Symmetric(
        1.0f, 1.0f, 1.0f,
        config.limit_x, config.limit_y, config.limit_z, config.limit_z_back,
        config.local_smoothing, config.remote_smoothing,
        false, false, false));

    session.SetMode(config.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                            : cameraunlock::TrackingMode::RotationOnly);
}

void ToggleTracking() {
    const bool on = !g_tracking_enabled.load();
    g_tracking_enabled.store(on);
    Log::Line("[input] tracking %s", on ? "enabled" : "disabled");
}

void CycleTrackingMode() {
    g_pending_mode_cycles.fetch_add(1, std::memory_order_relaxed);
}

void ApplyPendingModeCycles() {
    for (int pending = g_pending_mode_cycles.exchange(0, std::memory_order_relaxed);
         pending > 0; --pending) {
        const char* name = "";
        switch (g_session.CycleMode()) {
            case cameraunlock::TrackingMode::RotationAndPosition: name = "rotation and position"; break;
            case cameraunlock::TrackingMode::RotationOnly:        name = "rotation only"; break;
            case cameraunlock::TrackingMode::PositionOnly:        name = "position only"; break;
        }
        Log::Line("[input] tracking mode: %s", name);
    }
}

std::array<HotkeyBinding, 2> Bindings(const Config& config) {
    return {{
        { "toggle tracking",     config.toggle_key,     config.chord_toggle_key,     ToggleTracking },
        { "cycle tracking mode", config.cycle_mode_key, config.chord_cycle_mode_key, CycleTrackingMode },
    }};
}

// The ASI loader sits beside TT3.exe as version.dll, and crashpad_handler.exe
// in the same folder imports VERSION.dll too, so this module is loaded into
// the crash reporter as well. Nothing here belongs in that process, and a
// second copy would truncate the game's log and fight it for the UDP port.
bool IsGameProcess() {
    const std::wstring path = cameraunlock::os::ModuleFilePath(nullptr);
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    return _wcsicmp(name.c_str(), kGameExeName) == 0;
}

bool OpenLogAndResolveGameDirectory(std::string& exe_dir) {
    const std::wstring exe_dir_wide = cameraunlock::os::HostExeDirectory();
    Log::Open(exe_dir_wide.empty() ? std::wstring(L"TT3HeadTracking.log")
                                   : exe_dir_wide + L"\\TT3HeadTracking.log");
    Log::Line("=== TT Isle of Man: Ride on the Edge 3 Head Tracking ===");

    // The INI layer is ANSI-only (IniReader wraps GetPrivateProfile*A).
    if (exe_dir_wide.empty() || !cameraunlock::os::NarrowToAnsi(exe_dir_wide, exe_dir)) {
        Log::Line("[boot] could not resolve the game directory - mod is dormant, game runs vanilla.");
        return false;
    }
    Log::Line("[boot] game directory: %s", exe_dir.c_str());
    return true;
}

void LoadAndApplyConfig(const std::string& exe_dir) {
    WriteDefaultConfigIfMissing(exe_dir);
    LoadConfig(exe_dir, g_config);
    Log::Line("[boot] config: port=%u enableOnStartup=%d localSmoothing=%.2f "
              "remoteSmoothing=%.2f position=%d",
              static_cast<unsigned>(g_config.udp_port), g_config.enable_on_startup ? 1 : 0,
              g_config.local_smoothing, g_config.remote_smoothing,
              g_config.position_enabled ? 1 : 0);

    ApplyConfigToPipeline(g_config, g_session);
    g_tracking_enabled.store(g_config.enable_on_startup);
}

void StartReceiver() {
    g_receiver.SetLog([](const std::string& msg) { Log::Line("[udp] %s", msg.c_str()); });
    if (g_receiver.Start(g_config.udp_port)) {
        Log::Line("[boot] listening for OpenTrack data on UDP %u",
                  static_cast<unsigned>(g_config.udp_port));
    }
}

// Pins this module so an explicit FreeLibrary cannot unmap it while a detour,
// the receiver thread or the hotkey thread still runs inside it. The undo
// could only run from DllMain under the loader lock, where joining those
// threads deadlocks, so the unload is refused instead.
bool PinModule() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&ApplyConfigToPipeline),
                              &self) != FALSE;
}

bool BootstrapMod() {
    std::string exe_dir;
    if (!OpenLogAndResolveGameDirectory(exe_dir)) return false;

    if (!g_pinned.load()) {
        Log::Line("[boot] this module could not be pinned against unloading - mod is "
                  "dormant, game runs vanilla.");
        return false;
    }

    cameraunlock::diagnostics::InstallCrashHandler();

    if (builds::SelectProfile(GetModuleHandleW(nullptr)) != builds::ProfileSelection::Matched) {
        Log::Line("[boot] no usable build profile - mod is dormant, game runs vanilla.");
        return true;
    }
    InitGameState();

    LoadAndApplyConfig(exe_dir);
    StartReceiver();

    if (!InstallCameraHook()) {
        g_receiver.Stop();
        Log::Line("[boot] the camera could not be hooked - mod is inert.");
        return true;
    }

    const std::array<HotkeyBinding, 2> bindings = Bindings(g_config);
    AddBindings(g_hotkeys, bindings.data(), bindings.size());
    const bool hotkeys_started = g_hotkeys.Start();
    g_active.store(true, std::memory_order_release);

    if (!hotkeys_started) {
        Log::Line("[boot] ready, but the hotkey thread could not start - tracking cannot be "
                  "switched off or have its mode changed from the keyboard this session.");
        return true;
    }
    // Through %s: key names come from the keyboard layout and may contain '%'.
    Log::Line("[boot] ready. %s", DescribeHotkeys(bindings.data(), bindings.size()).c_str());
    return true;
}

void ForwardWindowLog(cameraunlock::os::WindowLogLevel, const char* message) {
    Log::Line("[window] %s", message);
}

// The engine creates its window a few seconds after the loader runs, and
// CenterGameWindowOnce spends its one attempt whether or not a window exists,
// so wait for one first.
void CenterGameWindowWhenItExists() {
    constexpr int kPollAttempts = 300;
    constexpr DWORD kPollIntervalMs = 100;
    for (int attempt = 0; attempt < kPollAttempts; ++attempt) {
        if (cameraunlock::os::FindGameWindow() != nullptr) {
            cameraunlock::os::CenterGameWindowOnce(&ForwardWindowLog);
            return;
        }
        Sleep(kPollIntervalMs);
    }
    Log::Line("[window] no game window appeared within %d seconds; not centring it.",
              kPollAttempts * static_cast<int>(kPollIntervalMs) / 1000);
}

void Bootstrap() {
    if (BootstrapMod()) CenterGameWindowWhenItExists();
}

}  // namespace

namespace {

struct FrameDecision {
    bool made = false;
    bool following = false;
    std::uintptr_t camera = 0;
    HeadPose pose;
};

// Written and read only on the thread that runs the game's frame: the helmet
// placement and Renderer::DoUpdate both run inside GameContext::RenderOneFrame.
FrameDecision g_frame;

bool IsFieldOfView(float radians) {
    return std::isfinite(radians) && radians > 0.0f && radians < kPi;
}

// Both FOVs are horizontal and taken at the same stage, before the engine
// widens the camera's FOV for a screen wider than 16:9. That widening scales
// tan(fov/2) by one constant per screen, so it cancels out of the ratio.
bool ZoomFactor(const CameraReading& camera, float& factor) {
    if (!IsFieldOfView(camera.mode_fov) || !IsFieldOfView(camera.base_fov)) return false;
    factor = cameraunlock::camera::FovZoomFactor(std::tan(camera.mode_fov * 0.5f),
                                                 std::tan(camera.base_fov * 0.5f));
    return true;
}

void DecideFrame(std::uintptr_t graphics_camera, const float clean[kMatrix34Floats]) {
    ApplyPendingModeCycles();

    const float dt = g_frame_clock.Tick();

    // The pipeline advances whatever the gate says, so the first frame after a
    // menu closes is composed from where the head is now.
    if (g_session.Update(dt)) {
        diag::LogConnectionLocality(g_session.IsRemoteConnection(), g_config.local_smoothing,
                                    g_config.remote_smoothing);
    }
    diag::LogTrackerPresence(g_receiver.IsReceiving());

    const long long frame = g_frame_counter.fetch_add(1, std::memory_order_relaxed);

    HeadPose pose;
    const bool have_rotation = g_session.GetRotation(pose.yaw, pose.pitch, pose.roll);
    g_session.GetPositionOffset(pose.lean_x, pose.lean_y, pose.lean_z);

    diag::LogFirstFrames(frame, clean, have_rotation, pose);
    diag::LogFirstPoseReachingCamera(frame, have_rotation, pose);

    const CameraReading camera = ReadCamera(graphics_camera);
    float zoom_factor = 1.0f;
    const bool zoom_known = ZoomFactor(camera, zoom_factor);
    diag::LogFieldOfView(camera, zoom_known, zoom_factor);
    const FlowReading flow = ReadFlow();
    std::uint32_t network_state = 0;
    const bool network_active = IsNetworkActive(&network_state);
    const bool tracking_enabled = g_tracking_enabled.load(std::memory_order_relaxed);
    const bool following = ShouldFollowHead(tracking_enabled, camera, flow, network_active);
    diag::LogGateChange(tracking_enabled, camera, flow, network_state, following);

    g_frame.made = true;
    g_frame.camera = graphics_camera;
    g_frame.following = following && have_rotation;
    g_frame.pose = zoom_known ? ScaleForZoom(pose, zoom_factor) : pose;
}

}  // namespace

bool TrackedCameraForFrame(std::uintptr_t graphics_camera,
                           const float clean[kMatrix34Floats],
                           float tracked[kMatrix34Floats]) {
    if (!g_active.load(std::memory_order_acquire)) return false;

    if (!g_frame.made) DecideFrame(graphics_camera, clean);
    if (!g_frame.following || g_frame.camera != graphics_camera) return false;

    std::memcpy(tracked, clean, kMatrix34Floats * sizeof(float));
    ApplyHeadPose(tracked, g_frame.pose);
    diag::LogComposedPose(g_frame.pose, clean, tracked);
    return true;
}

void EndFrame() {
    g_frame.made = false;
}

void Initialize() {
    if (!IsGameProcess()) return;

    g_pinned.store(PinModule());

    // DllMain runs under the loader lock; the bootstrap opens files, reads the
    // INI and places hooks, so it runs on its own thread.
    std::thread(Bootstrap).detach();
}

}  // namespace tt3_ht
