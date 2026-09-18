// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <windows.h>

#include <array>
#include <cstring>
#include <mutex>
#include <string_view>

#include "builds/build_registry.h"

#include "cameraunlock/memory/safe_memory.h"

namespace tt3_ht {

namespace {

using cameraunlock::memory::SafeRead;

struct Statics {
    std::uintptr_t game_engine = 0;
    std::uintptr_t network_manager = 0;
};
Statics g_statics;
builds::OffsetTable g_offsets{};

// The engine's strings are MSVC std::string: 16 bytes of inline buffer or a
// heap pointer, then the size, then the capacity.
struct EngineString {
    union {
        char inline_text[16];
        std::uintptr_t heap;
    };
    std::uint64_t size;
    std::uint64_t capacity;
};

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

constexpr std::uint64_t kInlineCapacity = 15;
constexpr std::uint64_t kLongestNameRead = 64;

// A name copied out of an engine string into fixed storage, so the flow
// machines, which step every frame, are read without a heap allocation.
struct EngineName {
    char text[kLongestNameRead]{};
    std::size_t size = 0;
    std::string_view View() const { return std::string_view(text, size); }
};

bool ReadEngineString(std::uintptr_t address, EngineName& out) {
    EngineString header{};
    if (!SafeRead(address, header)) return false;
    // A real std::string never holds more than its capacity. A header that
    // says otherwise is freed or unrelated memory, and trusting its size in the
    // inline case would copy past the 16-byte buffer read above.
    if (header.size > kLongestNameRead || header.size > header.capacity) return false;
    if (header.capacity > kInlineCapacity) {
        for (std::uint64_t i = 0; i < header.size; ++i) {
            if (!SafeRead(header.heap + i, out.text[i])) return false;
        }
    } else {
        std::memcpy(out.text, header.inline_text, static_cast<std::size_t>(header.size));
    }
    out.size = static_cast<std::size_t>(header.size);
    return true;
}

bool ReadPointer(std::uintptr_t address, std::uintptr_t& out) {
    return SafeRead(address, out) && out != 0;
}

// A GameFSM that stops stepping is one whose activity has ended; its memory is
// freed soon after. Entries are keyed by address and aged out by time, and the
// address is never dereferenced again once recorded.
constexpr ULONGLONG kLiveWindowMs = 500;
constexpr std::size_t kMaxMachines = 16;

struct MachineEntry {
    std::uintptr_t fsm = 0;
    ULONGLONG stepped_at = 0;
    bool riding = false;
    bool multiplayer = false;
    std::string channel;
    std::string leaf;
};

std::mutex g_machines_mutex;
std::array<MachineEntry, kMaxMachines> g_machines;

}  // namespace

void InitGameState() {
    g_offsets = builds::ActiveProfile().Offsets;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    g_statics.game_engine = base + g_offsets.game_engine_ptr_rva;
    g_statics.network_manager = base + g_offsets.network_manager_ptr_rva;
}

bool IsRidingState(std::string_view leaf) {
    return leaf == "State_FreeRoaming" || leaf == "State_Race" || leaf == "State_AfterFall";
}

bool IsMultiplayerChannel(std::string_view channel) {
    return channel.find("Multiplayer") != std::string_view::npos
        || channel.find("Online") != std::string_view::npos;
}

void RecordGameFsm(std::uintptr_t game_fsm) {
    EngineName channel;
    EngineName leaf;
    ReadEngineString(game_fsm + g_offsets.gamefsm_name, channel);
    std::uintptr_t hfsm = 0;
    std::uintptr_t state = 0;
    if (ReadPointer(game_fsm + g_offsets.gamefsm_hfsm, hfsm)
        && ReadPointer(hfsm + g_offsets.hfsm_current_state, state)) {
        ReadEngineString(state + g_offsets.state_name, leaf);
    }

    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_machines_mutex);
    MachineEntry* slot = nullptr;
    for (MachineEntry& entry : g_machines) {
        if (entry.fsm == game_fsm) { slot = &entry; break; }
    }
    if (slot == nullptr) {
        for (MachineEntry& entry : g_machines) {
            if (entry.fsm == 0 || now - entry.stepped_at > kLiveWindowMs) { slot = &entry; break; }
        }
    }
    // More than sixteen live flow machines has never been seen; the oldest
    // entry would be the wrong one to drop, so the new one is ignored instead
    // and the gate keeps its previous reading.
    if (slot == nullptr) return;
    slot->fsm = game_fsm;
    slot->stepped_at = now;
    slot->riding = IsRidingState(leaf.View());
    slot->multiplayer = IsMultiplayerChannel(channel.View());
    // assign() reuses the slot's buffer, so a machine that keeps stepping in
    // the same state allocates nothing.
    slot->channel.assign(channel.text, channel.size);
    slot->leaf.assign(leaf.text, leaf.size);
}

FlowReading ReadFlow() {
    FlowReading reading;
    reading.all_riding = true;
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_machines_mutex);
    for (const MachineEntry& entry : g_machines) {
        if (entry.fsm == 0 || now - entry.stepped_at > kLiveWindowMs) continue;
        reading.any_active = true;
        reading.all_riding = reading.all_riding && entry.riding;
        reading.multiplayer = reading.multiplayer || entry.multiplayer;
        if (!reading.description.empty()) reading.description += ", ";
        reading.description += entry.channel;
        reading.description += ':';
        reading.description += entry.leaf;
    }
    if (!reading.any_active) reading.all_riding = false;
    return reading;
}

namespace {

bool ReadPlayerCameras(std::uintptr_t& ktu_camera, std::uintptr_t& graphics_camera) {
    std::uintptr_t engine = 0;
    if (!ReadPointer(g_statics.game_engine, engine)) return false;
    std::int32_t player_count = 0;
    if (!SafeRead(engine + g_offsets.engine_player_count, player_count) || player_count < 1) {
        return false;
    }
    std::uintptr_t players = 0;
    return ReadPointer(engine + g_offsets.engine_players, players)
        && ReadPointer(players + g_offsets.player_camera, ktu_camera)
        && ReadPointer(ktu_camera + g_offsets.ktu_camera_graphics_camera, graphics_camera);
}

}  // namespace

std::uintptr_t PlayerGraphicsCamera() {
    std::uintptr_t ktu_camera = 0;
    std::uintptr_t camera = 0;
    return ReadPlayerCameras(ktu_camera, camera) ? camera : 0;
}

CameraReading ReadCamera(std::uintptr_t graphics_camera) {
    CameraReading reading;
    SafeRead(graphics_camera + g_offsets.camera_fov, reading.fov);
    std::uintptr_t ktu_camera = 0;
    std::uintptr_t camera = 0;
    if (!ReadPlayerCameras(ktu_camera, camera)) return reading;
    reading.player_camera = camera == graphics_camera;
    std::uintptr_t mode = 0;
    if (!ReadPointer(ktu_camera + g_offsets.ktu_camera_mode, mode)) return reading;
    EngineName mode_name;
    if (ReadEngineString(mode + g_offsets.camera_mode_name, mode_name)) {
        reading.mode.assign(mode_name.text, mode_name.size);
    }

    std::uintptr_t fov_source = mode;
    std::uintptr_t overlay = 0;
    if (ReadPointer(ktu_camera + g_offsets.ktu_camera_overlay_mode, overlay)) fov_source = overlay;
    SafeRead(fov_source + g_offsets.camera_mode_fov, reading.mode_fov);

    // chase_view_presets is a member of the rider views' mode class only.
    if (reading.mode != kRiderViewMode) return reading;
    std::uintptr_t presets = 0;
    std::int32_t count = 0;
    std::int32_t current = -1;
    std::uintptr_t array = 0;
    float base_degrees = -1.0f;
    if (ReadPointer(mode + g_offsets.chase_view_presets, presets)
        && SafeRead(presets + g_offsets.view_preset_count, count)
        && SafeRead(presets + g_offsets.view_preset_current, current)
        && current >= 0 && current < count
        && ReadPointer(presets + g_offsets.view_preset_array, array)
        && SafeRead(array + static_cast<std::uintptr_t>(current) * g_offsets.view_preset_stride
                        + g_offsets.view_preset_fov,
                    base_degrees)) {
        reading.base_fov = base_degrees * kDegreesToRadians;
    }
    return reading;
}

bool IsNetworkActive(std::uint32_t* raw_state) {
    std::uintptr_t manager = 0;
    std::uint32_t state = 0;
    // No manager, or one that cannot be read, is offline: the singleton is
    // created at boot and lives for the whole process, so neither happens
    // while a session could exist.
    if (ReadPointer(g_statics.network_manager, manager)) {
        SafeRead(manager + g_offsets.network_state, state);
    }
    if (raw_state != nullptr) *raw_state = state;
    return state != 0;
}

bool ShouldFollowHead(bool tracking_enabled, const CameraReading& camera,
                      const FlowReading& flow, bool network_active) {
    return tracking_enabled
        && camera.player_camera
        && camera.mode == kRiderViewMode
        && flow.any_active
        && flow.all_riding
        && !flow.multiplayer
        && !network_active;
}

}  // namespace tt3_ht
