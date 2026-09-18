// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_hook.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "builds/build_registry.h"
#include "camera_transform.h"
#include "game_state.h"
#include "headtracking_mod.h"
#include "logging.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/memory/safe_memory.h"

namespace tt3_ht {

namespace {

using cameraunlock::memory::SafeRead;

//   int  Renderer::DoUpdate(Renderer*, GameContext*, unsigned)
using DoUpdateFn = int(__fastcall*)(void*, void*, unsigned);
//   void ViewContext::InitFromCamera(ViewContext*, const Camera*, const Viewport&, const Vector2&)
using InitFromCameraFn = void(__fastcall*)(void*, const void*, const void*, const void*);
//   void TransformMatrix::SetViewProjection(TransformMatrix*, const Matrix44&, const Matrix44&)
using SetViewProjectionFn = void(__fastcall*)(void*, const float*, const float*);
//   void ViewContext::RebuildFrustum(ViewContext*)
using RebuildFrustumFn = void(__fastcall*)(void*);
//   bool GameFSM::Update(GameFSM*) - returns whether the machine stepped.
using GameFsmUpdateFn = std::uint64_t(__fastcall*)(void*);
//   int  <rider>::AfterPhysicsStep(rider*) - places the helmet mesh.
using HelmetPlacementFn = int(__fastcall*)(void*);
//   void Actor::SetWorldMatrix(Actor*, const Matrix34&)
using ActorSetWorldMatrixFn = void(__fastcall*)(void*, const float*);

DoUpdateFn g_original_do_update = nullptr;
InitFromCameraFn g_original_init_from_camera = nullptr;
GameFsmUpdateFn g_original_game_fsm_update = nullptr;
HelmetPlacementFn g_original_helmet_placement = nullptr;
ActorSetWorldMatrixFn g_original_actor_set_world_matrix = nullptr;
SetViewProjectionFn g_set_view_projection = nullptr;
RebuildFrustumFn g_rebuild_frustum = nullptr;

builds::OffsetTable g_offsets{};
std::uintptr_t g_node_transforms_static = 0;
const bool* g_lock_culling_query = nullptr;

// The renderer whose DoUpdate is on the stack of this thread, or null. The
// main view is recognised as the view context at a fixed offset inside it,
// which keeps the probe, render-to-texture and cursor-pick views (the other
// InitFromCamera callers) out of it.
thread_local std::uintptr_t t_renderer = 0;

int __fastcall DoUpdateDetour(void* renderer, void* context, unsigned flags) {
    t_renderer = reinterpret_cast<std::uintptr_t>(renderer);
    const int result = g_original_do_update(renderer, context, flags);
    t_renderer = 0;
    EndFrame();
    return result;
}

bool IsMainView(std::uintptr_t view, std::uintptr_t camera) {
    if (t_renderer == 0) return false;
    if (view != t_renderer + g_offsets.renderer_main_view) return false;
    std::int32_t split_mode = -1;
    std::uintptr_t main_camera = 0;
    return SafeRead(t_renderer + g_offsets.renderer_split_mode, split_mode) && split_mode == 0
        && SafeRead(t_renderer + g_offsets.renderer_main_camera, main_camera)
        && main_camera == camera;
}

struct Matrix34 { float m[kMatrix34Floats]; };

// Where a GraphNode's world transform lives in the node pool.
bool WorldTransformAddress(std::uintptr_t graph_node, std::uintptr_t& address) {
    std::int32_t node = -1;
    std::uintptr_t transforms = 0;
    if (!SafeRead(graph_node + g_offsets.camera_node_index, node) || node < 0) return false;
    if (!SafeRead(g_node_transforms_static, transforms) || transforms == 0) return false;
    address = transforms + static_cast<std::uintptr_t>(node) * g_offsets.node_transform_stride;
    return true;
}

bool ReadWorldTransform(std::uintptr_t graph_node, float out[kMatrix34Floats]) {
    std::uintptr_t address = 0;
    Matrix34 matrix;
    if (!WorldTransformAddress(graph_node, address) || !SafeRead(address, matrix)) return false;
    std::memcpy(out, matrix.m, sizeof(matrix.m));
    return true;
}

// Rebuilds what InitFromCamera derived from the camera's world transform -
// the view matrix, the eye and the frustum - from the tracked one. The
// projection is the engine's own, copied out before SetViewProjection
// recomputes the combined matrices from it. The frustum the frame is culled
// against therefore covers what the turned head actually sees, and nothing at
// the edge of the view is culled out of it.
void RebuildViewFromTracked(std::uintptr_t view, const float tracked[kMatrix34Floats]) {
    const std::uintptr_t transform = view + g_offsets.view_transform;
    float projection[kMatrix44Floats];
    std::memcpy(projection,
                reinterpret_cast<const void*>(transform + g_offsets.transform_projection),
                sizeof(projection));
    float view_matrix[kMatrix44Floats];
    RigidViewMatrix(tracked, view_matrix);
    g_set_view_projection(reinterpret_cast<void*>(transform), view_matrix, projection);

    const float eye[3] = { tracked[3], tracked[7], tracked[11] };
    std::memcpy(reinterpret_cast<void*>(view + g_offsets.view_eye), eye, sizeof(eye));
    if (!*g_lock_culling_query) {
        std::memcpy(reinterpret_cast<void*>(view + g_offsets.view_culling_eye), eye, sizeof(eye));
    }
    g_rebuild_frustum(reinterpret_cast<void*>(view));
}

void __fastcall InitFromCameraDetour(void* view, const void* camera, const void* viewport,
                                     const void* offset) {
    g_original_init_from_camera(view, camera, viewport, offset);

    const std::uintptr_t view_address = reinterpret_cast<std::uintptr_t>(view);
    const std::uintptr_t camera_address = reinterpret_cast<std::uintptr_t>(camera);
    if (!IsMainView(view_address, camera_address)) return;

    float clean[kMatrix34Floats];
    if (!ReadWorldTransform(camera_address, clean)) return;
    float tracked[kMatrix34Floats];
    if (!TrackedCameraForFrame(camera_address, clean, tracked)) return;
    RebuildViewFromTracked(view_address, tracked);
}

// The helmet actor of the rider whose helmet placement is on this thread's
// stack, or 0. Every rider runs the placement, and only the one in the helmet
// view calls Actor::SetWorldMatrix from it, so this is what singles out the one
// call to carry.
thread_local std::uintptr_t t_placing_helmet = 0;

// The helmet view draws the rider's helmet shell around the eye. The game
// places it from the clean camera, so a turned head would otherwise look at the
// inside of the shell. Carried by the same head pose as the view, it stays
// where the rider's own helmet would stay: fixed around the eyes.
int __fastcall HelmetPlacementDetour(void* rider) {
    std::uintptr_t helmet = 0;
    SafeRead(reinterpret_cast<std::uintptr_t>(rider) + g_offsets.rider_helmet_actor, helmet);
    t_placing_helmet = helmet;
    const int result = g_original_helmet_placement(rider);
    t_placing_helmet = 0;
    return result;
}

void __fastcall ActorSetWorldMatrixDetour(void* actor, const float* matrix) {
    if (t_placing_helmet == 0 || reinterpret_cast<std::uintptr_t>(actor) != t_placing_helmet) {
        g_original_actor_set_world_matrix(actor, matrix);
        return;
    }
    const std::uintptr_t camera = PlayerGraphicsCamera();
    float clean[kMatrix34Floats];
    float tracked[kMatrix34Floats];
    if (camera == 0 || !ReadWorldTransform(camera, clean)
        || !TrackedCameraForFrame(camera, clean, tracked)) {
        g_original_actor_set_world_matrix(actor, matrix);
        return;
    }
    float carried[kMatrix34Floats];
    std::memcpy(carried, matrix, sizeof(carried));
    CarryWithHead(clean, tracked, carried);
    g_original_actor_set_world_matrix(actor, carried);
}

std::uint64_t __fastcall GameFsmUpdateDetour(void* fsm) {
    const std::uint64_t stepped = g_original_game_fsm_update(fsm);
    if (stepped != 0) RecordGameFsm(reinterpret_cast<std::uintptr_t>(fsm));
    return stepped;
}

bool Failed(cameraunlock::hooks::HookStatus status, const char* what) {
    using cameraunlock::hooks::HookStatus;
    if (status == HookStatus::Ok) return false;
    Log::Line("[camera] %s failed: %s", what, cameraunlock::hooks::HookStatusToString(status));
    return true;
}

bool Install(void* target, void* detour, void** original, const char* what) {
    cameraunlock::hooks::HookManager& hooks = cameraunlock::hooks::HookManager::Instance();
    if (Failed(hooks.CreateHook(target, detour, original), what)) return false;
    if (Failed(hooks.EnableHook(target), what)) {
        Failed(hooks.RemoveHook(target), "rolling back a detour that would not enable");
        return false;
    }
    return true;
}

void Remove(void* target, const char* what) {
    cameraunlock::hooks::HookManager& hooks = cameraunlock::hooks::HookManager::Instance();
    Failed(hooks.DisableHook(target), what);
    Failed(hooks.RemoveHook(target), what);
}

// TT3.exe exports the engine's decorated names. A profile RVA that disagrees
// with the export of the same name means the profile does not describe this
// binary, whatever the fingerprint said.
bool MatchesExport(HMODULE module, std::uintptr_t rva, const char* decorated, const char* what) {
    const FARPROC exported = GetProcAddress(module, decorated);
    const std::uintptr_t expected = reinterpret_cast<std::uintptr_t>(module) + rva;
    if (reinterpret_cast<std::uintptr_t>(exported) == expected) return true;
    Log::Line("[camera] %s: the profile says RVA 0x%llX, the export table says %p. Not patching.",
              what, static_cast<unsigned long long>(rva), reinterpret_cast<void*>(exported));
    return false;
}

}  // namespace

bool InstallCameraHook() {
    const builds::BuildProfile& profile = builds::ActiveProfile();
    g_offsets = profile.Offsets;

    const HMODULE module = GetModuleHandleW(nullptr);
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);

    if (!MatchesExport(module, g_offsets.renderer_do_update_rva,
                       "?DoUpdate@Renderer@Game@PlayAll@@IEAAHPEAVGameContext@23@I@Z",
                       "Renderer::DoUpdate")
        || !MatchesExport(module, g_offsets.init_from_camera_rva,
                          "?InitFromCamera@ViewContext@Graphics@PlayAll@@QEAAXPEBVCamera@23@AEBUViewport@23@AEBVVector2@Math@3@@Z",
                          "ViewContext::InitFromCamera")
        || !MatchesExport(module, g_offsets.set_view_projection_rva,
                          "?SetViewProjection@TransformMatrix@Graphics@PlayAll@@QEAAXAEBVMatrix44@Math@3@0@Z",
                          "TransformMatrix::SetViewProjection")
        || !MatchesExport(module, g_offsets.rebuild_frustum_rva,
                          "?RebuildFrustum@ViewContext@Graphics@PlayAll@@QEAAXXZ",
                          "ViewContext::RebuildFrustum")
        || !MatchesExport(module, g_offsets.actor_set_world_matrix_rva,
                          "?SetWorldMatrix@Actor@GameServices@PlayAll@@QEAAXAEBVMatrix34@Math@3@@Z",
                          "Actor::SetWorldMatrix")
        || !MatchesExport(module, g_offsets.lock_culling_query_rva,
                          "?g_bLockCullingQuery@Graphics@PlayAll@@3_NA",
                          "g_bLockCullingQuery")
        || !MatchesExport(module, g_offsets.game_engine_ptr_rva,
                          "?g_pGameEngine@@3PEAVKTUGameEngine@@EA", "g_pGameEngine")
        || !MatchesExport(module, g_offsets.network_manager_ptr_rva,
                          "?s_pSingletonInstance@NetworkManager@Game@PlayAll@@1PEAV123@EA",
                          "NetworkManager::s_pSingletonInstance")
        || !MatchesExport(module, g_offsets.node_transforms_ptr_rva - 8,
                          "?s_oInstance@GraphNodeChunkPool@Graphics@PlayAll@@0V123@A",
                          "GraphNodeChunkPool::s_oInstance")) {
        return false;
    }

    g_set_view_projection = reinterpret_cast<SetViewProjectionFn>(base + g_offsets.set_view_projection_rva);
    g_rebuild_frustum = reinterpret_cast<RebuildFrustumFn>(base + g_offsets.rebuild_frustum_rva);
    g_node_transforms_static = base + g_offsets.node_transforms_ptr_rva;
    g_lock_culling_query = reinterpret_cast<const bool*>(base + g_offsets.lock_culling_query_rva);

    cameraunlock::hooks::HookManager& hooks = cameraunlock::hooks::HookManager::Instance();
    if (Failed(hooks.Initialize(), "MinHook init")) return false;

    struct Detour {
        std::uintptr_t rva;
        void* detour;
        void** original;
        const char* hooking;
        const char* unhooking;
    };
    // Installed in this order; a failure rolls back the ones already placed.
    const Detour detours[] = {
        { g_offsets.game_fsm_update_rva, reinterpret_cast<void*>(&GameFsmUpdateDetour),
          reinterpret_cast<void**>(&g_original_game_fsm_update),
          "hooking the GameFSM update", "unhooking the GameFSM update" },
        { g_offsets.renderer_do_update_rva, reinterpret_cast<void*>(&DoUpdateDetour),
          reinterpret_cast<void**>(&g_original_do_update),
          "hooking Renderer::DoUpdate", "unhooking Renderer::DoUpdate" },
        { g_offsets.init_from_camera_rva, reinterpret_cast<void*>(&InitFromCameraDetour),
          reinterpret_cast<void**>(&g_original_init_from_camera),
          "hooking ViewContext::InitFromCamera", "unhooking ViewContext::InitFromCamera" },
        { g_offsets.actor_set_world_matrix_rva, reinterpret_cast<void*>(&ActorSetWorldMatrixDetour),
          reinterpret_cast<void**>(&g_original_actor_set_world_matrix),
          "hooking Actor::SetWorldMatrix", "unhooking Actor::SetWorldMatrix" },
        { g_offsets.helmet_placement_rva, reinterpret_cast<void*>(&HelmetPlacementDetour),
          reinterpret_cast<void**>(&g_original_helmet_placement),
          "hooking the helmet placement", "unhooking the helmet placement" },
    };
    constexpr std::size_t kDetourCount = sizeof(detours) / sizeof(detours[0]);

    for (std::size_t i = 0; i < kDetourCount; ++i) {
        void* target = reinterpret_cast<void*>(base + detours[i].rva);
        if (Install(target, detours[i].detour, detours[i].original, detours[i].hooking)) continue;
        while (i-- > 0) {
            Remove(reinterpret_cast<void*>(base + detours[i].rva), detours[i].unhooking);
        }
        return false;
    }

    Log::Line("[camera] hooked Renderer::DoUpdate at %p, ViewContext::InitFromCamera at %p, "
              "the GameFSM update at %p, Actor::SetWorldMatrix at %p and the helmet placement "
              "at %p (profile %s)",
              reinterpret_cast<void*>(base + g_offsets.renderer_do_update_rva),
              reinterpret_cast<void*>(base + g_offsets.init_from_camera_rva),
              reinterpret_cast<void*>(base + g_offsets.game_fsm_update_rva),
              reinterpret_cast<void*>(base + g_offsets.actor_set_world_matrix_rva),
              reinterpret_cast<void*>(base + g_offsets.helmet_placement_rva), profile.Name);
    return true;
}

}  // namespace tt3_ht
