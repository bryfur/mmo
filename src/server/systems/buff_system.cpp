#include "buff_system.hpp"
#include "server/ecs/game_components.hpp"
#include <algorithm>
#include <cmath>

namespace mmo::server::systems {

void update_buffs(entt::registry& registry, float dt) {
    auto view = registry.view<ecs::BuffState>();

    for (auto entity : view) {
        auto& buff_state = view.get<ecs::BuffState>(entity);

        if (buff_state.effects.empty()) {
            continue;
        }

        // Process each effect
        for (auto& effect : buff_state.effects) {
            // Decrement duration
            effect.duration -= dt;

            // Tick DoT/HoT effects
            if (effect.type == ecs::StatusEffect::Type::Burn || effect.type == ecs::StatusEffect::Type::Poison) {
                effect.tick_timer -= dt;
                if (effect.tick_timer <= 0.0f) {
                    effect.tick_timer += effect.tick_interval;
                    // Apply damage
                    if (registry.all_of<ecs::Health>(entity)) {
                        auto& health = registry.get<ecs::Health>(entity);
                        health.current = std::max(0.0f, health.current - effect.value);
                    }
                }
            }

            if (effect.type == ecs::StatusEffect::Type::Heal) {
                effect.tick_timer -= dt;
                if (effect.tick_timer <= 0.0f) {
                    effect.tick_timer += effect.tick_interval;
                    // Apply healing (with healing_received_mult from talents)
                    if (registry.all_of<ecs::Health>(entity)) {
                        auto& health = registry.get<ecs::Health>(entity);
                        float heal = effect.value;
                        if (registry.all_of<ecs::TalentStats>(entity)) {
                            heal *= registry.get<ecs::TalentStats>(entity).healing_received_mult;
                        }
                        health.current = std::min(health.max, health.current + heal);
                    }
                }
            }
        }

        // Remove expired effects
        buff_state.effects.erase(std::remove_if(buff_state.effects.begin(), buff_state.effects.end(),
                                                [](const ecs::StatusEffect& e) { return e.duration <= 0.0f; }),
                                 buff_state.effects.end());

        // Don't remove BuffState component - it's a permanent component on all
        // entities that can receive effects. Removing it while iterating
        // invalidates the view iterator.
    }
}

void apply_effect(entt::registry& registry, entt::entity target, ecs::StatusEffect effect) {
    if (!registry.valid(target)) {
        return;
    }

    // CC immunity: block crowd control effects on players with the talent
    if (registry.all_of<ecs::TalentPassiveState>(target)) {
        if (registry.get<ecs::TalentPassiveState>(target).cc_immunity) {
            switch (effect.type) {
                case ecs::StatusEffect::Type::Stun:
                case ecs::StatusEffect::Type::Root:
                case ecs::StatusEffect::Type::Freeze:
                case ecs::StatusEffect::Type::Slow:
                    return; // Blocked by Unstoppable Force
                default:
                    break;
            }
        }
    }

    if (!registry.all_of<ecs::BuffState>(target)) {
        registry.emplace<ecs::BuffState>(target);
    }

    auto& buff_state = registry.get<ecs::BuffState>(target);

    // For CC effects (Stun, Root, Freeze, Slow), replace existing effect of same type
    // if new one has longer duration. For buffs/DoTs, just stack.
    switch (effect.type) {
        case ecs::StatusEffect::Type::Stun:
        case ecs::StatusEffect::Type::Root:
        case ecs::StatusEffect::Type::Freeze:
        case ecs::StatusEffect::Type::Slow:
        case ecs::StatusEffect::Type::SpeedBoost:
        case ecs::StatusEffect::Type::DamageBoost:
        case ecs::StatusEffect::Type::DefenseBoost:
        case ecs::StatusEffect::Type::Invulnerable:
        case ecs::StatusEffect::Type::Lifesteal: {
            // Replace if same type exists and new has longer duration
            for (auto& e : buff_state.effects) {
                if (e.type == effect.type) {
                    if (effect.duration > e.duration) {
                        e = effect;
                    }
                    return;
                }
            }
            buff_state.add(effect);
            break;
        }
        case ecs::StatusEffect::Type::Burn:
        case ecs::StatusEffect::Type::Poison:
        case ecs::StatusEffect::Type::Heal:
        case ecs::StatusEffect::Type::Shield:
            // These can stack
            buff_state.add(effect);
            break;
    }
}

void remove_effect(entt::registry& registry, entt::entity target, ecs::StatusEffect::Type type) {
    if (!registry.valid(target)) {
        return;
    }
    if (!registry.all_of<ecs::BuffState>(target)) {
        return;
    }

    auto& buff_state = registry.get<ecs::BuffState>(target);
    buff_state.effects.erase(std::remove_if(buff_state.effects.begin(), buff_state.effects.end(),
                                            [type](const ecs::StatusEffect& e) { return e.type == type; }),
                             buff_state.effects.end());
}

void clear_effects(entt::registry& registry, entt::entity target) {
    if (!registry.valid(target)) {
        return;
    }
    if (registry.all_of<ecs::BuffState>(target)) {
        registry.get<ecs::BuffState>(target).effects.clear();
    }
}

} // namespace mmo::server::systems
