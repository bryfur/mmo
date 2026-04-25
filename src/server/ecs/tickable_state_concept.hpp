#pragma once

// TickableState<T>: contract for ECS components (or plain structs) that
// hold time-decaying state and expose a `tick(float dt)` that advances it.
//
// Used by SkillState (cooldown timers), ConsumableCooldowns (potion
// cooldowns), and any future self-contained timer component. Components
// whose tick requires the ECS registry (buff DoT damage, channeled skill
// damage ticks) do NOT fit this contract - their tick lives in the
// corresponding system.

#include <concepts>

namespace mmo::server::ecs {

template<typename T>
concept TickableState = requires(T t, float dt) {
    { t.tick(dt) } -> std::same_as<void>;
};

} // namespace mmo::server::ecs
