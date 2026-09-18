// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/memory/pe_fingerprint.h"

namespace tt3_ht::builds {

// Everything this mod pins to a specific TT3.exe build.
//
// TT3.exe exports its engine's decorated C++ names, so every function below
// that the engine exports is also checked against GetProcAddress at load: a
// profile whose RVA disagrees with the export table is refused rather than
// hooked. The GameFSM update and every struct offset have no export behind
// them and rest on the fingerprint alone.
struct OffsetTable {
    // PlayAll::Game::Renderer::DoUpdate(GameContext*, unsigned). Builds the
    // frame's main view contexts from the active cameras.
    unsigned int renderer_do_update_rva;
    // PlayAll::Graphics::ViewContext::InitFromCamera(const Camera*,
    // const Viewport&, const Vector2&). Inverts the camera's world transform
    // into the view matrix, builds the projection and rebuilds the frustum.
    unsigned int init_from_camera_rva;
    // TransformMatrix::SetViewProjection(const Matrix44& view,
    // const Matrix44& proj) and ViewContext::RebuildFrustum().
    unsigned int set_view_projection_rva;
    unsigned int rebuild_frustum_rva;
    // RWLib's GameFSM update: steps the flow's HFSM (at gamefsm_hfsm) once per
    // frame. Not exported.
    unsigned int game_fsm_update_rva;

    // Renderer layout, read inside DoUpdate: the split mode (0 is a single
    // full-screen view), the first view context and the camera it is built
    // from.
    unsigned int renderer_split_mode;
    unsigned int renderer_main_view;
    unsigned int renderer_main_camera;

    // ViewContext layout: the eye position the renderer reads, the eye the
    // culling queries read, and the TransformMatrix block. The projection is
    // TransformMatrix::Get(3).
    unsigned int view_eye;
    unsigned int view_culling_eye;
    unsigned int view_transform;
    unsigned int transform_projection;
    // PlayAll::Graphics::g_bLockCullingQuery: while raised, InitFromCamera
    // leaves the culling eye alone, and so does this mod.
    unsigned int lock_culling_query_rva;

    // Graphics::Camera: index of its node in the transform pool, and its field
    // of view in radians.
    unsigned int camera_node_index;
    unsigned int camera_fov;
    // GraphNodeChunkPool::s_oInstance + 8: the pool's node transform array,
    // Matrix34 per node.
    unsigned int node_transforms_ptr_rva;
    unsigned int node_transform_stride;

    // g_pGameEngine (KTUGameEngine*): the player slots and each slot's
    // KTUCamera; KTUCamera's Graphics::Camera and current mode; the mode's name.
    unsigned int game_engine_ptr_rva;
    unsigned int engine_player_count;
    unsigned int engine_players;
    unsigned int player_camera;
    unsigned int ktu_camera_graphics_camera;
    unsigned int ktu_camera_mode;
    unsigned int camera_mode_name;

    // GameFSM: its HFSM and its channel name. HFSM: the current leaf state.
    // State: its name.
    unsigned int gamefsm_hfsm;
    unsigned int gamefsm_name;
    unsigned int hfsm_current_state;
    unsigned int state_name;

    // The helmet shell the helmet view renders around the eye is an Actor of
    // its own, placed by TT3's rider update (not exported) from the camera
    // system's helmet matrix through Actor::SetWorldMatrix(const Matrix34&).
    // rider_helmet_actor is where the rider keeps that actor.
    unsigned int helmet_placement_rva;
    unsigned int actor_set_world_matrix_rva;
    unsigned int rider_helmet_actor;

    // PlayAll::Game::NetworkManager::s_pSingletonInstance, and its eNetStates
    // value. Zero while no network session exists.
    unsigned int network_manager_ptr_rva;
    unsigned int network_state;

    // Where the rendered field of view comes from. KTUCamera::Update takes the
    // FOV of its overlay mode when one is set and of its current mode
    // otherwise, from camera_mode_fov (radians, horizontal), and hands it to
    // Graphics::Camera::SetFov, widened only on screens wider than 16:9.
    unsigned int ktu_camera_overlay_mode;
    unsigned int camera_mode_fov;
    // The rider views' mode keeps a pointer to the bike's view presets: a count,
    // an array of view_preset_stride-sized entries and the current view's index.
    // view_preset_fov is that view's resting FOV in degrees, the value the
    // game's own FOV settings set, before the speed and acceleration terms.
    unsigned int chase_view_presets;
    unsigned int view_preset_count;
    unsigned int view_preset_array;
    unsigned int view_preset_current;
    unsigned int view_preset_stride;
    unsigned int view_preset_fov;
};

struct BuildProfile {
    const char* Name;
    cameraunlock::memory::PeFingerprint Fingerprint;
    OffsetTable Offsets;
};

inline bool IsProfileComplete(const BuildProfile& p) {
    return p.Offsets.renderer_do_update_rva != 0
        && p.Offsets.init_from_camera_rva != 0
        && p.Offsets.set_view_projection_rva != 0
        && p.Offsets.rebuild_frustum_rva != 0
        && p.Offsets.lock_culling_query_rva != 0
        && p.Offsets.game_fsm_update_rva != 0
        && p.Offsets.helmet_placement_rva != 0
        && p.Offsets.actor_set_world_matrix_rva != 0
        && p.Offsets.node_transforms_ptr_rva != 0
        && p.Offsets.game_engine_ptr_rva != 0
        && p.Offsets.network_manager_ptr_rva != 0;
}

}  // namespace tt3_ht::builds
