// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tt3_ht {

// Resolves the engine statics the gate reads from the active build profile.
// Runs after builds::SelectProfile() has matched.
void InitGameState();

// ---------------------------------------------------------------------------
// The game's flow
//
// TT3 drives every screen through RWLib GameFSMs, one per channel ("Game", and
// one for the running activity: "FreeRoaming", "QuickRace", ...), each stepping
// a hierarchical state machine once per frame. The leaf state of each is what
// the player is doing: "State_FreeRoaming" riding the island, "State_Race" in
// an event, "State_AfterFall" back on the bike after a crash while the
// crash message is still on screen (about 20 s), and something else in every menu, map, pause screen, results
// screen and loading flow that sits on top of them.
// ---------------------------------------------------------------------------

// Called from the GameFSM update detour, after the original has stepped the
// machine, so the leaf it records is the one this frame will render.
void RecordGameFsm(std::uintptr_t game_fsm);

// Whether a leaf state is one in which the rider is on the bike and in
// control. An allow-list: a state this mod has not seen is treated as a menu.
bool IsRidingState(std::string_view leaf);

// Whether a channel name belongs to an online activity.
bool IsMultiplayerChannel(std::string_view channel);

struct FlowReading {
    // At least one GameFSM stepped in the last half second.
    bool any_active = false;
    // Every GameFSM that stepped in the last half second sits in a riding
    // state. A pause screen or menu in ANY live machine clears it - the race
    // machine goes to State_Pause while the Game machine stays in State_Race.
    bool all_riding = false;
    // A live machine belongs to an online activity.
    bool multiplayer = false;
    // The live machines as "channel:leaf", for the log.
    std::string description;
};

// Reads the table RecordGameFsm filled. Call from the render thread.
FlowReading ReadFlow();

// ---------------------------------------------------------------------------
// The camera being rendered
// ---------------------------------------------------------------------------

// The camera mode every rider view (both chase cameras, helmet, cockpit, front)
// runs in.
inline constexpr char kRiderViewMode[] = "Chase";

struct CameraReading {
    // The camera is player one's KTUCamera's graphics camera.
    bool player_camera = false;
    // The name of that KTUCamera's current mode. Every rider view (both chase
    // cameras, helmet, cockpit, front) is the "Chase" mode; the map, menus,
    // cinematics and replays are not.
    std::string mode;
    // The graphics camera's field of view in radians, or a negative value when
    // it could not be read. Horizontal: MatrixPerspectiveFovRH divides x by
    // tan(fov/2) and y by tan(fov/2) / aspect.
    float fov = -1.0f;
    // The FOV the camera mode handed the graphics camera this frame, before the
    // engine widens it for a screen wider than 16:9. Radians, horizontal, or
    // negative when unreadable.
    float mode_fov = -1.0f;
    // In a rider view, that view's resting FOV: the value its FOV setting holds,
    // before the speed and acceleration terms the chase views add. Radians,
    // horizontal, same stage as mode_fov, or negative when unreadable or not in
    // a rider view.
    float base_fov = -1.0f;
};

CameraReading ReadCamera(std::uintptr_t graphics_camera);

// Player one's graphics camera, or 0 when there is no player camera to read.
std::uintptr_t PlayerGraphicsCamera();

// Whether the engine's network manager is in any session state. Zero is
// offline; anything else is a lobby, matchmaking or an online race.
bool IsNetworkActive(std::uint32_t* raw_state);

// ---------------------------------------------------------------------------
// The gate
// ---------------------------------------------------------------------------

// Pure, so the classification can be tested without a game running.
//
// Head tracking follows only when the player has it switched on, the frame is
// player one's camera in a rider view, every live flow machine is in a riding
// state, and nothing online is going on. Online play is excluded outright:
// this is a racing game with online races, and head tracking is off there.
bool ShouldFollowHead(bool tracking_enabled, const CameraReading& camera,
                      const FlowReading& flow, bool network_active);

}  // namespace tt3_ht
