#pragma once

// Compile-time concept checks for ECS components.
//
// Every component that holds time-decaying state and exposes a self-
// contained `tick(float dt)` is asserted here. Components whose ticking
// requires the registry (BuffState applies DoT damage, ChannelingSkill
// re-runs damage passes) intentionally are NOT TickableState - their
// tick lives in the corresponding system.
//
// Pulling this header into the server-test target forces the asserts
// to fire on every build.

#include "server/ecs/game_components.hpp"
#include "server/ecs/tickable_state_concept.hpp"

namespace mmo::server::ecs {

// SkillState: per-player active cooldowns; tick decrements remaining time.
static_assert(TickableState<SkillState>);

// ConsumableCooldowns: per-item potion cooldowns; tick decrements timers.
static_assert(TickableState<ConsumableCooldowns>);

} // namespace mmo::server::ecs
