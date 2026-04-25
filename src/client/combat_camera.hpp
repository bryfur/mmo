#pragma once

#include "engine/systems/camera_controller.hpp"
#include <glm/glm.hpp>

namespace mmo::client {

// Combat-aware wrapper around the engine camera. Translates game semantics
// (in-combat / target / attack-fired / hit-taken) into the engine's generic
// shake, focus-bias and fov-bias primitives. Engine cameras stay game-agnostic.
class CombatCamera {
public:
    explicit CombatCamera(engine::systems::CameraController& camera) noexcept : camera_(camera) {}

    void set_in_combat(bool in_combat) noexcept {
        in_combat_ = in_combat;
        push_state();
    }

    void set_combat_target(const glm::vec3* target) noexcept {
        combat_target_ = target;
        push_state();
    }

    void notify_attack() { camera_.add_shake(engine::systems::ShakeType::Impact, 0.3f, 0.08f); }

    void notify_hit(const glm::vec3& hit_direction, float damage) {
        const float intensity = std::min(damage / 100.0f, 1.5f);
        camera_.add_directional_shake(hit_direction, intensity, 0.15f);
    }

    bool in_combat() const noexcept { return in_combat_; }

private:
    void push_state() noexcept {
        const bool active = in_combat_ && combat_target_ != nullptr;
        camera_.set_focus_target(active ? combat_target_ : nullptr);
        camera_.set_focus_strength(active ? 0.6f : 0.0f);
        camera_.set_fov_bias(in_combat_ ? -3.0f : 0.0f);
    }

    engine::systems::CameraController& camera_;
    bool in_combat_ = false;
    const glm::vec3* combat_target_ = nullptr;
};

} // namespace mmo::client
