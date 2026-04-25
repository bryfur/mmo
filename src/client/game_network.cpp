// Network message handling — gameplay-irrelevant connection / entity lifecycle
// / delta-compression handlers belonging to mmo::client::Game. Lives in its
// own translation unit so game.cpp keeps a manageable size; the methods below
// are still members of the class declared in game.hpp.

#include "game.hpp"
#include "client/ecs/components.hpp"
#include "client/effect_loader.hpp"
#include "engine/heightmap.hpp"
#include "engine/model_loader.hpp"
#include "protocol/heightmap.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace mmo::client {

using namespace mmo::protocol;

void Game::handle_network_message(MessageType type, const std::vector<uint8_t>& payload) {
    // Delegate gameplay messages (combat, quests, inventory, skills, talents, dialogue)
    if (msg_handler_->try_handle(type, payload)) {
        return;
    }

    // Handle connection, world-setup, and entity messages here
    switch (type) {
        case MessageType::ConnectionAccepted:
            on_connection_accepted(payload);
            break;
        case MessageType::WorldConfig:
            if (payload.size() >= NetWorldConfig::serialized_size()) {
                world_config_.deserialize(payload);
                world_config_received_ = true;
                // Interpolate over 2x the snapshot interval so we always render
                // one snapshot behind — keeps entities smoothly mid-interp every
                // frame instead of snapping to each new target. Fixed-cost 1-tick
                // latency for guaranteed smooth movement.
                network_smoother_.set_interpolation_time(2.0f / world_config_.tick_rate);
                std::cout << "Received world config: " << world_config_.world_width << "x" << world_config_.world_height
                          << " tick_rate=" << world_config_.tick_rate << std::endl;
            }
            break;
        case MessageType::ClassList:
            on_class_list(payload);
            break;
        case MessageType::HeightmapChunk:
            on_heightmap_chunk(payload);
            break;
        case MessageType::PlayerJoined:
            on_player_joined(payload);
            break;
        case MessageType::PlayerLeft:
            on_player_left(payload);
            break;
        case MessageType::EntityEnter:
            on_entity_enter(payload);
            break;
        case MessageType::EntityUpdate:
            on_entity_update(payload);
            break;
        case MessageType::EntityExit:
            on_entity_exit(payload);
            break;
        default:
            break;
    }
}

void Game::on_connection_accepted(const std::vector<uint8_t>& payload) {
    if (payload.size() >= ConnectionAcceptedMsg::serialized_size()) {
        ConnectionAcceptedMsg msg;
        msg.deserialize(payload);
        if (msg.player_id == 0) {
            std::cout << "Connection accepted, waiting for class list..." << std::endl;
        } else {
            local_player_id_ = msg.player_id;
            std::cout << "Spawned with player ID: " << local_player_id_ << std::endl;
            if (game_state_ == GameState::Spawning) {
                game_state_ = GameState::Playing;
            }
        }
    }
}

void Game::on_class_list(const std::vector<uint8_t>& payload) {
    if (payload.empty()) return;

    BufferReader r(payload);
    uint16_t count = r.get_array_size();

    // Ensure buffer has enough capacity (only reallocates if needed)
    if (available_classes_.capacity() < count) {
        available_classes_.reserve(count);
    }
    available_classes_.resize(count);

    r.read_array_into(std::span(available_classes_), count);

    std::cout << "Received " << available_classes_.size() << " classes from server" << std::endl;

    if (game_state_ == GameState::Connecting) {
        game_state_ = GameState::ClassSelect;
        selected_class_index_ = 0;
    }
}

void Game::on_heightmap_chunk(const std::vector<uint8_t>& payload) {
    heightmap_ = std::make_unique<HeightmapChunk>();
    if (heightmap_->deserialize(payload)) {
        heightmap_received_ = true;
        std::cout << "Received heightmap: " << heightmap_->resolution << "x" << heightmap_->resolution
                  << " covering " << heightmap_->world_size << "x" << heightmap_->world_size << " world units" << std::endl;

        engine::Heightmap engine_hm;
        engine_hm.resolution = heightmap_->resolution;
        engine_hm.world_origin_x = heightmap_->world_origin_x;
        engine_hm.world_origin_z = heightmap_->world_origin_z;
        engine_hm.world_size = heightmap_->world_size;
        engine_hm.min_height = heightmap_config::MIN_HEIGHT;
        engine_hm.max_height = heightmap_config::MAX_HEIGHT;
        engine_hm.height_data = heightmap_->height_data;

        set_heightmap(engine_hm);
    } else {
        std::cerr << "Failed to deserialize heightmap!" << std::endl;
        heightmap_.reset();
    }
}

void Game::on_player_joined(const std::vector<uint8_t>& payload) {
    if (payload.size() >= EntityState::serialized_size()) {
        NetEntityState state;
        state.deserialize(payload);

        entt::entity entity = find_or_create_entity(state.id);
        update_entity_from_state(entity, state);

        std::cout << "Player joined: " << state.name << " (ID: " << state.id << ")" << std::endl;
    }
}

void Game::on_player_left(const std::vector<uint8_t>& payload) {
    if (payload.size() >= PlayerLeftMsg::serialized_size()) {
        PlayerLeftMsg msg;
        msg.deserialize(payload);

        auto it = network_to_entity_.find(msg.player_id);
        if (it != network_to_entity_.end()) {
            if (registry_.valid(it->second)) {
                auto* name = registry_.try_get<ecs::Name>(it->second);
                if (name) {
                    std::cout << "Player left: " << name->value << " (ID: " << msg.player_id << ")" << std::endl;
                }
            }
            remove_entity(msg.player_id);
            prev_attacking_.erase(msg.player_id);
        }
    }
}

// ============================================================================
// ECS Entity Management
// ============================================================================

entt::entity Game::find_or_create_entity(uint32_t network_id) {
    auto it = network_to_entity_.find(network_id);
    if (it != network_to_entity_.end() && registry_.valid(it->second)) {
        return it->second;
    }

    entt::entity entity = registry_.create();
    registry_.emplace<ecs::NetworkId>(entity, network_id);
    registry_.emplace<ecs::Transform>(entity);
    registry_.emplace<ecs::Velocity>(entity);
    registry_.emplace<ecs::Health>(entity);
    registry_.emplace<ecs::EntityInfo>(entity);
    registry_.emplace<ecs::Name>(entity);
    registry_.emplace<ecs::Combat>(entity);
    registry_.emplace<ecs::Interpolation>(entity);
    registry_.emplace<ecs::SmoothRotation>(entity);
    registry_.emplace<ecs::AnimationInstance>(entity);

    network_to_entity_[network_id] = entity;
    return entity;
}

void Game::update_entity_from_state(entt::entity entity, const NetEntityState& state) {
    auto& transform = registry_.get<ecs::Transform>(entity);
    auto& velocity = registry_.get<ecs::Velocity>(entity);
    auto& health = registry_.get<ecs::Health>(entity);
    auto& info = registry_.get<ecs::EntityInfo>(entity);
    auto& name = registry_.get<ecs::Name>(entity);
    auto& combat = registry_.get<ecs::Combat>(entity);
    auto& interp = registry_.get<ecs::Interpolation>(entity);

    interp.prev_x = transform.x;
    interp.prev_y = transform.y;
    interp.prev_z = transform.z;
    interp.target_x = state.x;
    interp.target_y = state.y;
    interp.target_z = state.z;
    interp.alpha = 0.0f;

    transform.rotation = state.rotation;

    velocity.x = state.vx;
    velocity.z = state.vy;  // protocol vy = velocity on Z axis (ground plane), not vertical (Y-up world)

    health.current = state.health;
    health.max = state.max_health;

    info.type = state.type;
    info.player_class = state.player_class;
    info.npc_type = state.npc_type;
    info.building_type = state.building_type;
    info.environment_type = state.environment_type;
    info.color = state.color;
    info.model_name = state.model_name;
    info.model_handle = models().get_handle(state.model_name);
    info.target_size = state.target_size;
    info.effect_type = state.effect_type;
    info.animation = state.animation;
    info.cone_angle = state.cone_angle;
    info.shows_reticle = state.shows_reticle;

    name.value = state.name;

    combat.is_attacking = state.is_attacking;
    combat.current_cooldown = state.attack_cooldown;

    // Status effect bitmask (stuns, slows, burns, shields, etc.).
    registry_.emplace_or_replace<ecs::StatusEffects>(entity, ecs::StatusEffects{state.effects_mask});

    if (!registry_.all_of<ecs::AttackDirection>(entity)) {
        registry_.emplace<ecs::AttackDirection>(entity);
    }
    auto& attack_dir = registry_.get<ecs::AttackDirection>(entity);
    attack_dir.x = state.attack_dir_x;
    attack_dir.y = state.attack_dir_y;

    if (!registry_.all_of<ecs::Scale>(entity)) {
        registry_.emplace<ecs::Scale>(entity);
    }
    registry_.get<ecs::Scale>(entity).value = state.scale;
}

void Game::remove_entity(uint32_t network_id) {
    auto it = network_to_entity_.find(network_id);
    if (it != network_to_entity_.end()) {
        if (registry_.valid(it->second)) {
            registry_.destroy(it->second);
        }
        network_to_entity_.erase(it);
    }
}

void Game::on_entity_enter(const std::vector<uint8_t>& payload) {
    if (payload.size() < NetEntityState::serialized_size()) return;

    // Deserialize full entity state
    NetEntityState state;
    state.deserialize(payload);

    // Create or find entity
    entt::entity entity = find_or_create_entity(state.id);
    update_entity_from_state(entity, state);

    // Track attack state for spawning attack effects
    prev_attacking_[state.id] = state.is_attacking;
}

void Game::on_entity_update(const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(uint32_t) + sizeof(uint8_t)) return;

    // Deserialize delta
    mmo::protocol::EntityDeltaUpdate delta;
    delta.deserialize(payload);

    // Find entity
    auto it = network_to_entity_.find(delta.id);
    if (it == network_to_entity_.end()) {
        return;  // Unknown entity (shouldn't happen)
    }

    entt::entity entity = it->second;
    if (!registry_.valid(entity)) return;

    // Apply delta to entity components
    apply_delta_to_entity(entity, delta);
}

void Game::on_entity_exit(const std::vector<uint8_t>& payload) {
    if (payload.size() >= EntityExitMsg::serialized_size()) {
        EntityExitMsg msg;
        msg.deserialize(payload);

        remove_entity(msg.entity_id);
        prev_attacking_.erase(msg.entity_id);
    }
}

void Game::apply_delta_to_entity(entt::entity entity, const mmo::protocol::EntityDeltaUpdate& delta) {
    // Update transform (position)
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_POSITION) {
        if (registry_.all_of<ecs::Transform, ecs::Interpolation>(entity)) {
            auto& transform = registry_.get<ecs::Transform>(entity);
            auto& interp = registry_.get<ecs::Interpolation>(entity);

            interp.prev_x = transform.x;
            interp.prev_y = transform.y;
            interp.prev_z = transform.z;
            interp.target_x = delta.x;
            interp.target_y = delta.y;
            interp.target_z = delta.z;
            interp.alpha = 0.0f;
        }
    }

    // Update velocity
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_VELOCITY) {
        if (registry_.all_of<ecs::Velocity>(entity)) {
            auto& velocity = registry_.get<ecs::Velocity>(entity);
            velocity.x = delta.vx;
            velocity.z = delta.vy;
        }
    }

    // Status effect bitmask
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_EFFECTS) {
        registry_.emplace_or_replace<ecs::StatusEffects>(entity,
            ecs::StatusEffects{delta.effects_mask});
    }

    // Update health
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_HEALTH) {
        if (registry_.all_of<ecs::Health>(entity)) {
            auto& health = registry_.get<ecs::Health>(entity);
            health.current = delta.health;
        }
        // Auto-clear death overlay when local player's health rises above 0
        // (server respawns automatically; we react to the next entity update).
        if (player_dead_ && delta.health > 0.0f) {
            auto it = network_to_entity_.find(local_player_id_);
            if (it != network_to_entity_.end() && it->second == entity) {
                player_dead_ = false;
            }
        }
    }

    // Update max health
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_MAX_HEALTH) {
        if (registry_.all_of<ecs::Health>(entity)) {
            auto& health = registry_.get<ecs::Health>(entity);
            health.max = delta.max_health;
        }
    }

    // Update attacking state
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_ATTACKING) {
        if (registry_.all_of<ecs::Combat>(entity)) {
            auto& combat = registry_.get<ecs::Combat>(entity);
            bool was_attacking = combat.is_attacking;
            combat.is_attacking = (delta.is_attacking != 0);

            // Set cooldown so the attack tilt animation plays
            if (combat.is_attacking) {
                combat.current_cooldown = 0.5f;
            } else {
                combat.current_cooldown = 0.0f;
            }

            // Spawn attack effect if transitioning to attacking
            if (combat.is_attacking && !was_attacking) {
                if (registry_.all_of<ecs::NetworkId, ecs::Transform, ecs::EntityInfo>(entity)) {
                    uint32_t net_id = registry_.get<ecs::NetworkId>(entity).id;
                    prev_attacking_[net_id] = true;

                    // Get attack direction
                    float dir_x = 0.0f;
                    float dir_y = 1.0f;
                    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_ATTACK_DIR) {
                        dir_x = delta.attack_dir_x;
                        dir_y = delta.attack_dir_y;
                    } else if (registry_.all_of<ecs::AttackDirection>(entity)) {
                        auto& dir = registry_.get<ecs::AttackDirection>(entity);
                        dir_x = dir.x;
                        dir_y = dir.y;
                    }

                    float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                    if (len < 0.001f) {
                        dir_x = 0; dir_y = 1;
                    } else {
                        dir_x /= len; dir_y /= len;
                    }

                    // Build state from cached entity components for effect spawning
                    auto& transform = registry_.get<ecs::Transform>(entity);
                    auto& info = registry_.get<ecs::EntityInfo>(entity);
                    NetEntityState state;
                    state.id = net_id;
                    state.x = transform.x;
                    state.z = transform.z;
                    std::strncpy(state.effect_type, info.effect_type.c_str(), 15);
                    state.effect_type[15] = '\0';
                    state.cone_angle = info.cone_angle;

                    spawn_attack_effect(state, dir_x, dir_y);

                    if (net_id == local_player_id_) {
                        combat_camera_->notify_attack();
                    }
                }
            }
        }
    }

    // Update attack direction
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_ATTACK_DIR) {
        if (!registry_.all_of<ecs::AttackDirection>(entity)) {
            registry_.emplace<ecs::AttackDirection>(entity);
        }
        auto& attack_dir = registry_.get<ecs::AttackDirection>(entity);
        attack_dir.x = delta.attack_dir_x;
        attack_dir.y = delta.attack_dir_y;
    }

    // Update rotation
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_ROTATION) {
        if (registry_.all_of<ecs::Transform>(entity)) {
            auto& transform = registry_.get<ecs::Transform>(entity);
            transform.rotation = delta.rotation;
        }
    }
}

} // namespace mmo::client
