#include "quest_markers.hpp"
#include "world_projection.hpp"

#include "client/ecs/components.hpp"
#include "protocol/protocol.hpp"

#include <cmath>

namespace mmo::client::hud {

using engine::scene::UIScene;
using protocol::EntityType;

void build_quest_markers(UIScene& ui, const entt::registry& registry, const QuestMarkerInputs& in) {
    const float radius_sq = in.visibility_radius * in.visibility_radius;

    auto npc_view = registry.view<const ecs::NetworkId, const ecs::Transform, const ecs::EntityInfo>();
    for (auto entity : npc_view) {
        const auto& info = npc_view.get<const ecs::EntityInfo>(entity);
        if (info.type != EntityType::TownNPC) {
            continue;
        }

        const auto& net_id = npc_view.get<const ecs::NetworkId>(entity);
        const bool has_quest = in.npcs_with_quests.count(net_id.id) > 0;
        const bool has_turnin = in.npcs_with_turnins.count(net_id.id) > 0;
        if (!has_quest && !has_turnin) {
            continue;
        }

        const auto& tf = npc_view.get<const ecs::Transform>(entity);

        // Lift the marker above the NPC's head before projecting.
        const float marker_world_y = tf.y + info.target_size * 1.6f;
        auto sp = world_to_screen(in.view_projection, glm::vec3(tf.x, marker_world_y, tf.z), in.screen_w, in.screen_h);
        if (!sp) {
            continue;
        }

        // Cull off-screen with a generous margin so half-clipped markers
        // don't pop in/out at the edge.
        if (sp->x < -50.0f || sp->x > in.screen_w + 50.0f || sp->y < -50.0f || sp->y > in.screen_h + 50.0f) {
            continue;
        }

        const float dx = tf.x - in.player_x;
        const float dz = tf.z - in.player_z;
        const float dist_sq = dx * dx + dz * dz;
        if (dist_sq > radius_sq) {
            continue;
        }

        const float dist = std::sqrt(dist_sq);
        const float scale = distance_scale(dist, 1000.0f, 0.6f);
        const float circle_r = 14.0f * scale;
        const float box_x = sp->x - circle_r;
        const float box_y = sp->y - circle_r;
        const float box_w = circle_r * 2.0f;

        if (has_turnin) {
            // Green "?": completed quest ready to turn in.
            ui.add_filled_rect(box_x, box_y, box_w, box_w, 0xCC002200);
            ui.add_rect_outline(box_x, box_y, box_w, box_w, 0xFF00FF00, 2.0f);
            ui.add_text("?", sp->x - 5.0f * scale, sp->y - 10.0f * scale, 1.4f * scale, 0xFF00FF00);
        } else {
            // Gold "!": new quest available.
            ui.add_filled_rect(box_x, box_y, box_w, box_w, 0xCC003344);
            ui.add_rect_outline(box_x, box_y, box_w, box_w, 0xFF00DDFF, 2.0f);
            ui.add_text("!", sp->x - 4.0f * scale, sp->y - 10.0f * scale, 1.4f * scale, 0xFF00DDFF);
        }
    }
}

} // namespace mmo::client::hud
