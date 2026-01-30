#pragma once

#include "common/protocol.hpp"
#include <cstdint>

namespace mmo::ecs {

struct PlayerTag {};

struct NPCTag {};

struct InputState {
    PlayerInput input;
};

struct AIState {
    uint32_t target_id = 0;
    float aggro_range = 300.0f;
};

// Town NPC AI - wanders around home position
struct TownNPCAI {
    float home_x = 0.0f;
    float home_y = 0.0f;
    float wander_radius = 50.0f;
    float idle_timer = 0.0f;
    float move_timer = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    bool is_moving = false;
};

// Safe zone marker
struct SafeZone {
    float center_x = 0.0f;
    float center_y = 0.0f;
    float radius = 0.0f;
};

} // namespace mmo::ecs
