// Game rendering — scene/UI building for in-game and pre-game screens.
// Methods of mmo::client::Game (declared in game.hpp), peeled off so game.cpp
// stays focused on the lifecycle / state machine.

#include "game.hpp"
#include "client/ecs/components.hpp"
#include "client/game_state.hpp"
#include "client/hud/debug_hud.hpp"
#include "client/hud/floating_text.hpp"
#include "client/hud/npc_dialogue.hpp"
#include "client/hud/panels.hpp"
#include "client/hud/quest_markers.hpp"
#include "client/hud/screens.hpp"
#include "client/hud/widgets.hpp"
#include "client/menu_system.hpp"
#include "client/ui_colors.hpp"
#include "engine/model_loader.hpp"
#include "engine/model_utils.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace mmo::client {

using engine::scene::UIScene;
using namespace mmo::engine;
using namespace mmo::engine::scene;
using namespace mmo::engine::systems;
using namespace mmo::engine::ui_colors;
using namespace mmo::protocol;

void Game::render_class_select() {
    player_x_ = 4000.0f;  // Town center from editor save
    player_z_ = 4000.0f;
    camera().set_yaw(0.0f);
    camera().set_pitch(30.0f);
    update_camera_smooth(last_dt_);

    render_scene_.clear();
    render_scene_.set_draw_skybox(false);
    render_scene_.set_draw_ground(false);
    render_scene_.set_draw_grass(false);
    ui_scene_.clear();

    build_class_select_ui(ui_scene_);

    menu_system_->build_ui(ui_scene_, static_cast<float>(screen_width()), static_cast<float>(screen_height()));

    render_frame(render_scene_, ui_scene_, get_camera_state(), last_dt_);
}
void Game::render_spawning() {
    render_connecting();
}
void Game::render_connecting() {
    player_x_ = 4000.0f;  // Town center from editor save
    player_z_ = 4000.0f;
    camera().set_yaw(0.0f);
    camera().set_pitch(30.0f);
    update_camera_smooth(last_dt_);

    render_scene_.clear();
    render_scene_.set_draw_skybox(false);
    render_scene_.set_draw_ground(false);
    render_scene_.set_draw_grass(false);
    ui_scene_.clear();

    build_connecting_ui(ui_scene_);

    render_frame(render_scene_, ui_scene_, get_camera_state(), last_dt_);
}
void Game::render_playing() {
    render_scene_.clear();
    ui_scene_.clear();

    const auto& gfx = menu_system_->graphics_settings();
    render_scene_.set_draw_skybox(gfx.skybox_enabled);
    render_scene_.set_draw_rocks(gfx.rocks_enabled);
    render_scene_.set_draw_trees(gfx.trees_enabled);
    render_scene_.set_draw_ground(true);
    render_scene_.set_draw_grass(gfx.grass_enabled);

    // Cache VP matrix for world-to-screen projection in UI
    auto cam_state = get_camera_state();
    cached_vp_matrix_ = cam_state.view_projection;
    cached_screen_w_ = static_cast<float>(screen_width());
    cached_screen_h_ = static_cast<float>(screen_height());

    // Add entities to scene as model commands
    // Distance cull from player position (camera is 200-350 units behind player)
    float draw_dist = gfx.get_draw_distance();
    float ENTITY_DRAW_DISTANCE_SQ = draw_dist * draw_dist;

    auto entity_view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::Health, ecs::EntityInfo, ecs::Name>();
    for (auto entity : entity_view) {
        auto&& [net_id, transform, health, info, name] = entity_view.get(entity);
        bool is_local = (net_id.id == local_player_id_);

        // Filter trees/rocks by scene flags
        if (info.type == EntityType::Environment) {
            bool is_tree = (info.model_name.compare(0, 5, "tree_") == 0);
            if (is_tree && !render_scene_.should_draw_trees()) continue;
            if (!is_tree && !render_scene_.should_draw_rocks()) continue;
        }

        // Distance culling from player position (always render local player)
        if (!is_local) {
            float dx = transform.x - player_x_;
            float dz = transform.z - player_z_;
            if (dx * dx + dz * dz > ENTITY_DRAW_DISTANCE_SQ) continue;
        }

        add_entity_to_scene(entity, is_local);
    }

    build_playing_ui(ui_scene_);

    menu_system_->build_ui(ui_scene_, static_cast<float>(screen_width()), static_cast<float>(screen_height()));

    render_frame(render_scene_, ui_scene_, get_camera_state(), last_dt_);
}

// ============================================================================
// Entity → Scene Command
// ============================================================================

void Game::add_entity_to_scene(entt::entity entity, bool is_local) {
    auto& transform = registry_.get<ecs::Transform>(entity);
    auto& health = registry_.get<ecs::Health>(entity);
    auto& info = registry_.get<ecs::EntityInfo>(entity);

    if (info.model_handle == mmo::engine::INVALID_MODEL_HANDLE && info.model_name.empty()) return;

    Model* model = (info.model_handle != mmo::engine::INVALID_MODEL_HANDLE)
        ? models().get_model(info.model_handle)
        : models().get_model(info.model_name);
    if (!model) return;

    // Read rotation (already smoothed in update_playing)
    // Buildings and environment objects use fixed rotation (no smooth rotation, no dynamic updates)
    float rotation = 0.0f;
    if (info.type != EntityType::Building && info.type != EntityType::Environment) {
        rotation = transform.rotation;
        if (auto* smooth = registry_.try_get<ecs::SmoothRotation>(entity)) {
            rotation = smooth->current;
        }
    }

    // Read attack tilt (already computed in update_playing)
    float attack_tilt = 0.0f;
    auto* anim_inst = registry_.try_get<ecs::AnimationInstance>(entity);
    if (anim_inst) attack_tilt = anim_inst->attack_tilt;

    // Build transform matrix
    glm::vec3 position(transform.x, transform.y, transform.z);
    glm::mat4 model_mat = engine::build_model_transform(
        *model, position, rotation, info.target_size, attack_tilt);

    glm::vec4 tint(1.0f);

    // Submit draw command (bone matrices already have IK/lean applied from update_playing)
    if (model->has_skeleton && anim_inst && anim_inst->bound) {
        render_scene_.add_skinned_model(info.model_handle, model_mat,
                                         anim_inst->player.bone_matrix_array(), tint);
    } else if (model->has_skeleton) {
        static const auto identity_bones = []() {
            std::array<glm::mat4, mmo::engine::animation::MAX_BONES> arr;
            arr.fill(glm::mat4(1.0f));
            return arr;
        }();
        render_scene_.add_skinned_model(info.model_handle, model_mat, identity_bones, tint);
    } else {
        render_scene_.add_model(info.model_handle, model_mat, tint, attack_tilt != 0.0f);
    }

    // Health bar
    bool show_health_bar = (info.type != EntityType::Building &&
                            info.type != EntityType::Environment &&
                            info.type != EntityType::TownNPC);
    if (show_health_bar && !is_local) {
        float health_ratio = health.current / health.max;
        float bar_height_offset = transform.y + info.target_size * 1.3f;
        render_scene_.add_billboard_3d(transform.x, bar_height_offset, transform.z,
                                       info.target_size * 0.8f, health_ratio,
                                       ui_colors::HEALTH_HIGH, ui_colors::HEALTH_BAR_BG, ui_colors::HEALTH_3D_BG);
    }

}
void Game::build_class_select_ui(UIScene& ui) {
    hud::build_class_select(ui, available_classes_, selected_class_index_,
                            static_cast<float>(screen_width()),
                            static_cast<float>(screen_height()));
}

void Game::build_connecting_ui(UIScene& ui) {
    hud::build_connecting(ui, host_, port_, connecting_timer_,
                          static_cast<float>(screen_width()),
                          static_cast<float>(screen_height()));
}

void Game::build_playing_ui(UIScene& ui) {
    const float sw = static_cast<float>(screen_width());
    const float sh = static_cast<float>(screen_height());

    // Open the mouse UI frame so every widget drawn below can register hit
    // regions before process_mouse_ui() resolves clicks at the end.
    {
        auto& ih = input();
        mouse_ui_.viewport_w = sw;
        mouse_ui_.viewport_h = sh;
        mouse_ui_.begin_frame(
            ih.mouse_x(), ih.mouse_y(),
            ih.mouse_left_pressed(), ih.mouse_left_held(), ih.mouse_left_released(),
            ih.mouse_right_pressed());
    }

    // Crosshair reticle for ranged-weapon classes.
    if (selected_class_index_ >= 0
        && selected_class_index_ < static_cast<int>(available_classes_.size())
        && available_classes_[selected_class_index_].shows_reticle) {
        hud::build_reticle(ui, sw, sh);
    }

    // Local-player health bar (uses the live ECS Health component, not the
    // HUD mirror, so it reflects the very latest snapshot).
    if (auto it = network_to_entity_.find(local_player_id_);
        it != network_to_entity_.end() && registry_.valid(it->second)) {
        const auto& health = registry_.get<ecs::Health>(it->second);
        hud::build_player_health_bar(ui, health.current, health.max, sw, sh);
    }

    {
        const auto& gfx = menu_system_->graphics_settings();
        hud::DebugHUDInputs dbg{
            render_stats(),
            network_.network_stats(),
            gpu_driver_name(),
            screen_width(), screen_height(),
            fps(),
            gfx.show_fps,
            gfx.show_debug_hud,
        };
        hud::build_debug_hud(ui, dbg);
    }

    // Quest markers ("!" / "?") above NPCs the local player has business with.
    {
        hud::QuestMarkerInputs qm{
            npcs_with_quests_, npcs_with_turnins_,
            cached_vp_matrix_,
            cached_screen_w_, cached_screen_h_,
            player_x_, player_z_,
            800.0f,
        };
        hud::build_quest_markers(ui, registry_, qm);
    }

    hud::build_npc_dialogue(ui, npc_interaction_, mouse_ui_, sw, sh);

    // Sync local-player status effects into the HUD state so icons render.
    hud_state_.effects_mask = local_effects_mask_;

    // Persistent gameplay HUD overlay + panel widgets.
    build_gameplay_hud(ui, hud_state_, mouse_ui_, sw, sh);
    build_gameplay_panels(ui, panel_state_, sw, sh);

    hud::build_notifications(ui, hud_state_, sw);
    hud::build_damage_numbers(ui, hud_state_, get_camera_state().view_projection, sw, sh);

    // Legacy dialog only renders if the richer npc_interaction popup isn't.
    if (!npc_interaction_.showing_dialogue) {
        hud::build_legacy_dialogue(ui, hud_state_, sw, sh);
    }

    // Modal panels (drawn on top).
    switch (panel_state_.active_panel) {
        case ActivePanel::Inventory:
            hud::build_inventory_panel(ui, panel_state_, mouse_ui_, sw, sh);
            break;
        case ActivePanel::Talents:
            hud::build_talent_panel(ui, panel_state_, mouse_ui_, sw, sh);
            break;
        case ActivePanel::QuestLog:
            hud::build_quest_log_panel(ui, hud_state_, panel_state_, mouse_ui_, sw, sh);
            break;
        case ActivePanel::WorldMap:
            // Drawn by build_gameplay_panels above.
            break;
        case ActivePanel::None:
            break;
    }

    if (player_dead_) {
        hud::build_death_overlay(ui, sw, sh);
    }

    // Resolve mouse drags + clicks against the just-built UI.
    process_mouse_ui();
}

} // namespace mmo::client
