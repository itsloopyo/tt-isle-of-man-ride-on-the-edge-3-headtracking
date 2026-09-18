// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_profile.h"

// Every Steam build profile lives here, append-only. Never edit an existing
// profile's numbers to "fix" a patch and never delete one: a user who has held
// back on an older build must keep matching their old profile from the same
// mod binary. Adding a profile is the only correct response to a patch.

namespace tt3_ht::builds {

// TT Isle of Man: Ride on the Edge 3, Steam app 1924170, TT3.exe built
// 2024-12-12 21:10:45 UTC. The linker never stamped a CheckSum, so the
// fingerprint rests on TimeDateStamp + SizeOfImage.
extern const BuildProfile kSteamProfile_20241212 = {
    "steam-win64-20241212",
    { 0x675B5155, 0x01E78000, 0x00000000 },
    {
        /* renderer_do_update_rva      */ 0x0086F6C0,
        /* init_from_camera_rva        */ 0x00C6BE40,
        /* set_view_projection_rva     */ 0x00C65800,
        /* rebuild_frustum_rva         */ 0x00C6C440,
        /* game_fsm_update_rva         */ 0x00F8D900,
        /* renderer_split_mode         */ 0xC90,
        /* renderer_main_view          */ 0x30,
        /* renderer_main_camera        */ 0xE50,
        /* view_eye                    */ 0x00,
        /* view_culling_eye            */ 0x0C,
        /* view_transform              */ 0x18,
        /* transform_projection        */ 0xC0,
        /* lock_culling_query_rva      */ 0x01C4F308,
        /* camera_node_index           */ 0x50,
        /* camera_fov                  */ 0x98,
        /* node_transforms_ptr_rva     */ 0x01C07528,
        /* node_transform_stride       */ 0x30,
        /* game_engine_ptr_rva         */ 0x01C5A260,
        /* engine_player_count         */ 0x3EC,
        /* engine_players              */ 0x3F0,
        /* player_camera               */ 0x18,
        /* ktu_camera_graphics_camera  */ 0x10,
        /* ktu_camera_mode             */ 0x48,
        /* camera_mode_name            */ 0x38,
        /* gamefsm_hfsm                */ 0xD0,
        /* gamefsm_name                */ 0x68,
        /* hfsm_current_state          */ 0x78,
        /* state_name                  */ 0x40,
        /* helmet_placement_rva        */ 0x005A3B30,
        /* actor_set_world_matrix_rva  */ 0x006587C0,
        /* rider_helmet_actor          */ 0x1010,
        /* network_manager_ptr_rva     */ 0x01BCDC00,
        /* network_state               */ 0x10,
        /* ktu_camera_overlay_mode     */ 0x68,
        /* camera_mode_fov             */ 0x80,
        /* chase_view_presets          */ 0x418,
        /* view_preset_count           */ 0x5C,
        /* view_preset_array           */ 0x60,
        /* view_preset_current         */ 0x68,
        /* view_preset_stride          */ 0x150,
        /* view_preset_fov             */ 0x40,
    },
};

}  // namespace tt3_ht::builds
