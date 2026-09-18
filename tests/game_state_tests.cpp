// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The gate's classification: which flow states count as riding, which
// channels are online, and what the combination of inputs decides. Reading the
// engine needs the game; deciding what a reading means does not.

#include "game_state.h"

#include <cstdint>
#include <cstring>

#include "test_support.h"

using namespace tt3_ht;
using tt3_test::Check;

namespace {

CameraReading RiderCamera() {
    CameraReading camera;
    camera.player_camera = true;
    camera.mode = "Chase";
    return camera;
}

FlowReading Riding() {
    FlowReading flow;
    flow.any_active = true;
    flow.all_riding = true;
    return flow;
}

void TestRidingStates() {
    Check(IsRidingState("State_FreeRoaming"), "riding the island is riding");
    Check(IsRidingState("State_Race"), "an event in progress is riding");
    Check(IsRidingState("State_AfterFall"), "back on the bike under the crash message is riding");
    Check(!IsRidingState("State_VehicleFall"), "the crash itself is not riding");
    Check(!IsRidingState("State_InGame_Menu"), "the in-game menu is not riding");
    Check(!IsRidingState("State_Map"), "the map is not riding");
    Check(!IsRidingState("State_Pause"), "the race pause screen is not riding");
    Check(!IsRidingState(""), "an unreadable state is not riding");
    Check(!IsRidingState("State_Race_Replay"), "a state name is matched whole, not by prefix");
}

void TestMultiplayerChannels() {
    Check(IsMultiplayerChannel("MultiplayerRace"), "the multiplayer race activity is online");
    Check(!IsMultiplayerChannel("QuickRace"), "a custom event is offline");
    Check(!IsMultiplayerChannel("FreeRoaming"), "free roam is offline");
    Check(!IsMultiplayerChannel("Game"), "the top-level game flow is offline");
}

void TestGate() {
    Check(ShouldFollowHead(true, RiderCamera(), Riding(), false),
          "riding, rider view, offline, switched on: follows");
    Check(!ShouldFollowHead(false, RiderCamera(), Riding(), false), "switched off: off");

    CameraReading other = RiderCamera();
    other.player_camera = false;
    Check(!ShouldFollowHead(true, other, Riding(), false), "another camera's view: off");

    CameraReading map = RiderCamera();
    map.mode = "2130371654";
    Check(!ShouldFollowHead(true, map, Riding(), false), "a non-rider camera mode: off");

    CameraReading none = RiderCamera();
    none.mode.clear();
    Check(!ShouldFollowHead(true, none, Riding(), false), "no camera mode (menus, loading): off");

    FlowReading paused = Riding();
    paused.all_riding = false;
    Check(!ShouldFollowHead(true, RiderCamera(), paused, false),
          "one flow machine in a menu or pause state: off");

    FlowReading idle;
    Check(!ShouldFollowHead(true, RiderCamera(), idle, false), "no live flow machine: off");

    FlowReading online = Riding();
    online.multiplayer = true;
    Check(!ShouldFollowHead(true, RiderCamera(), online, false), "an online activity: off");

    Check(!ShouldFollowHead(true, RiderCamera(), Riding(), true), "any network session: off");
}

// An engine string whose header claims the inline buffer (capacity 15) but a
// size past it cannot be a real MSVC std::string, and is what a freed or
// half-built flow machine reads as. Its size must not be trusted as a length
// to copy out of the 16-byte inline buffer.
void TestInconsistentEngineStringIsRefused() {
    // Offsets are zero before InitGameState, so the name and the HFSM pointer
    // are both read from the start of this block. The zeroed inline text makes
    // the HFSM pointer null, so only the name is read.
    struct FakeFlowMachine {
        char inline_text[16];
        std::uint64_t size;
        std::uint64_t capacity;
    } fsm{};
    fsm.size = 40;
    fsm.capacity = 15;

    RecordGameFsm(reinterpret_cast<std::uintptr_t>(&fsm));
    const FlowReading flow = ReadFlow();
    Check(flow.any_active, "the malformed machine is still recorded as live");
    Check(flow.description == ":",
          "a size past the inline capacity is refused, not copied out of the inline buffer");
    Check(!flow.all_riding, "a machine whose state could not be read is not riding");
}

}  // namespace

int main() {
    TestRidingStates();
    TestMultiplayerChannels();
    TestGate();
    TestInconsistentEngineStringIsRefused();
    return tt3_test::Summary("game_state");
}
