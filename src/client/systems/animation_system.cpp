#include "animation_system.hpp"

#include "client/animation_loader.hpp"
#include "client/ecs/components.hpp"
#include "engine/animation/animation_state_machine.hpp"
#include "engine/animation/ik_solver.hpp"
#include "engine/model_loader.hpp"
#include "engine/model_utils.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace mmo::client::systems {

namespace {

namespace anim = mmo::engine::animation;
using mmo::protocol::EntityType;

} // namespace

void update_rotation_smoothing(entt::registry& registry, float dt) {
    auto rot_view = registry.view<ecs::EntityInfo, ecs::SmoothRotation>();
    for (auto entity : rot_view) {
        auto& info = rot_view.get<ecs::EntityInfo>(entity);
        if (info.type == EntityType::Building || info.type == EntityType::Environment) {
            continue;
        }

        auto& smooth = rot_view.get<ecs::SmoothRotation>(entity);
        bool has_target = false;
        float target_rotation = 0.0f;

        if (info.type == EntityType::Player) {
            if (auto* attack_dir = registry.try_get<ecs::AttackDirection>(entity)) {
                target_rotation = std::atan2(attack_dir->x, attack_dir->y);
                has_target = true;
            }
        } else if (auto* vel = registry.try_get<ecs::Velocity>(entity)) {
            if (vel->x != 0.0f || vel->z != 0.0f) {
                target_rotation = std::atan2(vel->x, vel->z);
                has_target = true;
            }
        }

        if (has_target) {
            smooth.smooth_toward(target_rotation, dt);
        } else {
            smooth.decay_turn_rate();
        }
    }
}

void update_animations(entt::registry& registry, float dt, engine::ModelManager& models,
                       const AnimationRegistry& animation_registry, const TerrainHeightFn& get_terrain_height,
                       const glm::vec3& camera_pos, float cull_distance) {
    auto anim_view = registry.view<ecs::Velocity, ecs::EntityInfo, ecs::Combat, ecs::AnimationInstance>();

    // Cull animation by distance from camera. The full skinned pipeline
    // (state machine + 64-bone matrices + foot IK terrain raycasts + body
    // lean) is the largest per-entity CPU cost in update_playing — and it
    // is pure waste for entities the renderer is going to distance-cull
    // anyway. Use a small grace margin so an entity that just crossed back
    // into draw range has correct bones on its first visible frame.
    const float cull_dist_sq = (cull_distance > 0.0f) ? (cull_distance + 50.0f) * (cull_distance + 50.0f) : -1.0f;

    auto update_one = [&](entt::entity entity) {
        auto&& [vel, info, combat, inst] = anim_view.get(entity);

        if (info.model_handle == mmo::engine::INVALID_MODEL_HANDLE) {
            return;
        }
        engine::Model* model = models.get_model(info.model_handle);
        if (!model || !model->has_skeleton) {
            return;
        }

        if (cull_dist_sq > 0.0f) {
            auto& transform = registry.get<ecs::Transform>(entity);
            const float dx = transform.x - camera_pos.x;
            const float dz = transform.z - camera_pos.z;
            if (dx * dx + dz * dz > cull_dist_sq) {
                return;
            }
        }

        if (!inst.bound) {
            if (!info.animation.empty()) {
                if (const auto* config = animation_registry.get_config(info.animation)) {
                    inst.state_machine = config->state_machine;
                    inst.procedural = config->procedural;
                }
            }
            inst.state_machine.bind_clips(model->animations);
            inst.bound = true;
        }

        const float horiz_speed_sq = vel.x * vel.x + vel.z * vel.z;
        inst.state_machine.set_float("speed", std::sqrt(horiz_speed_sq));
        inst.state_machine.set_bool("attacking", combat.is_attacking || combat.current_cooldown > 0.0f);

        inst.state_machine.update(inst.player);
        inst.player.update(model->skeleton, model->animations, dt);

        inst.attack_tilt = 0.0f;
        if (combat.is_attacking && combat.current_cooldown > 0.0f) {
            const float progress = std::min(combat.current_cooldown / inst.procedural.attack_tilt_cooldown, 1.0f);
            inst.attack_tilt = std::sin(progress * glm::pi<float>()) * inst.procedural.attack_tilt_max;
        }

        auto& transform = registry.get<ecs::Transform>(entity);
        auto* smooth = registry.try_get<ecs::SmoothRotation>(entity);
        const float rotation = smooth ? smooth->current : transform.rotation;

        const glm::vec3 position(transform.x, transform.y, transform.z);
        const float scale = (info.target_size * 1.5f) / model->max_dimension();
        const glm::mat4 model_mat =
            engine::build_model_transform(*model, position, rotation, info.target_size, inst.attack_tilt);

        if (model->foot_ik.valid && inst.procedural.foot_ik && get_terrain_height) {
            const auto& ik = model->foot_ik;
            auto world_xforms = inst.player.world_transforms();
            auto foot_world_pos = [&](int bone_idx) -> glm::vec3 {
                return glm::vec3(model_mat * glm::vec4(glm::vec3(world_xforms[bone_idx][3]), 1.0f));
            };
            const glm::vec3 lf = foot_world_pos(ik.left_foot);
            const glm::vec3 rf = foot_world_pos(ik.right_foot);
            const float base_terrain = get_terrain_height(transform.x, transform.z);
            const float left_offset = get_terrain_height(lf.x, lf.z) - base_terrain;
            const float right_offset = get_terrain_height(rf.x, rf.z) - base_terrain;

            inst.foot_ik_smoother.update(left_offset, right_offset, dt);

            anim::apply_foot_ik(inst.player.mutable_bone_matrices(), inst.player.mutable_world_transforms(),
                                model->skeleton, ik, model_mat, scale, inst.foot_ik_smoother.smoothed_left_offset,
                                inst.foot_ik_smoother.smoothed_right_offset);
        }

        if (model->foot_ik.valid && model->foot_ik.spine >= 0 && inst.procedural.lean) {
            float forward_lean = 0.0f;
            float lateral_lean = 0.0f;
            if (horiz_speed_sq > 1.0f) {
                forward_lean = std::min(std::sqrt(horiz_speed_sq) * inst.procedural.forward_lean_factor,
                                        inst.procedural.forward_lean_max);
            }
            if (smooth) {
                lateral_lean = std::clamp(-smooth->turn_rate * inst.procedural.lateral_lean_factor,
                                          -inst.procedural.lateral_lean_max, inst.procedural.lateral_lean_max);
            }
            anim::apply_body_lean(inst.player.mutable_bone_matrices(), inst.player.mutable_world_transforms(),
                                  model->skeleton, model->foot_ik.spine, forward_lean, lateral_lean);
        }
    };

    for (auto e : anim_view) update_one(e);
}

} // namespace mmo::client::systems
