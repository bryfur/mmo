#include "npc_interaction.hpp"

#include "client/ecs/components.hpp"
#include "protocol/gameplay_msgs.hpp"
#include "protocol/protocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mmo::client::systems {

namespace {

using namespace mmo::protocol;

// Auto-turn-in: when the player walks up to a turn-in NPC, send
// QuestTurnIn for every tracked quest whose objectives are all complete.
void try_turn_in_quests(NPCInteractionFrame& f) {
    if (!f.npcs_with_turnins.count(f.npc_interaction.npc_id)) return;
    for (const auto& quest : f.hud.tracked_quests) {
        if (quest.quest_id.empty() || quest.objectives.empty()) continue;
        bool all_complete = true;
        for (const auto& obj : quest.objectives) {
            if (!obj.complete) { all_complete = false; break; }
        }
        if (!all_complete) continue;

        QuestTurnInMsg msg;
        std::strncpy(msg.quest_id, quest.quest_id.c_str(), sizeof(msg.quest_id) - 1);
        f.network.send_raw(build_packet(MessageType::QuestTurnIn, msg));
    }
}

// Add the currently-selected dialogue quest to the player's tracker, push
// objective markers for the world map and minimap, and tell the server.
void accept_selected_quest(NPCInteractionFrame& f) {
    auto& npc = f.npc_interaction;
    if (npc.available_quests.empty()) return;
    if (npc.selected_quest >= static_cast<int>(npc.available_quests.size())) return;

    const auto& quest = npc.available_quests[npc.selected_quest];

    QuestAcceptMsg accept_msg;
    std::strncpy(accept_msg.quest_id, quest.quest_id.c_str(), 31);
    f.network.send_raw(build_packet(MessageType::QuestAccept, accept_msg));

    QuestTrackerEntry tracker;
    tracker.quest_id = quest.quest_id;
    tracker.quest_name = quest.quest_name;
    for (const auto& obj : quest.objectives) {
        tracker.objectives.push_back({obj.description, 0, obj.count, false});
    }
    f.hud.tracked_quests.push_back(std::move(tracker));

    for (const auto& obj : quest.objectives) {
        if (obj.radius <= 0.0f) continue;
        MapQuestMarker marker;
        marker.quest_name = quest.quest_name;
        marker.world_x = obj.loc_x;
        marker.world_z = obj.loc_z;
        marker.radius = obj.radius;
        marker.complete = false;
        f.panel.map_quest_markers.push_back(std::move(marker));

        HUDState::MinimapState::ObjectiveArea area;
        area.world_x = obj.loc_x;
        area.world_z = obj.loc_z;
        area.radius = obj.radius;
        f.hud.minimap.objective_areas.push_back(area);
    }

    npc.close();
}

// E pressed and not already in dialog: walk the registry, pick the closest
// TownNPC, open the dialog, and trigger turn-ins / NPCInteract round-trip.
void try_open_nearest_npc(NPCInteractionFrame& f) {
    auto local_it = f.network_to_entity.find(f.local_player_id);
    if (local_it == f.network_to_entity.end()) return;
    if (!f.registry.valid(local_it->second)) return;

    const auto& tf = f.registry.get<ecs::Transform>(local_it->second);
    auto best = find_closest_npc(f.registry, tf.x, tf.z, 200.0f);
    if (!best) return;

    auto& npc = f.npc_interaction;
    npc.npc_id = best->network_id;
    npc.npc_name = best->name;
    npc.available_quests.clear();
    npc.selected_quest = 0;
    npc.showing_quest_detail = false;
    npc.showing_dialogue = true;
    f.hud.dialogue.visible = false;  // suppress legacy popup

    try_turn_in_quests(f);

    NPCInteractMsg msg;
    msg.npc_id = best->network_id;
    f.network.send_raw(build_packet(MessageType::NPCInteract, msg));
}

void handle_dialogue_navigation(NPCInteractionFrame& f) {
    auto& npc = f.npc_interaction;

    if (f.menu_toggle_pressed) {
        if (npc.showing_quest_detail) {
            npc.showing_quest_detail = false;
        } else {
            npc.close();
        }
    }

    if (!npc.showing_quest_detail && !npc.available_quests.empty()) {
        if (f.key_w_just_pressed) {
            npc.selected_quest = std::max(0, npc.selected_quest - 1);
        }
        if (f.key_s_just_pressed) {
            const int last = static_cast<int>(npc.available_quests.size()) - 1;
            npc.selected_quest = std::min(last, npc.selected_quest + 1);
        }
    }

    if (f.key_enter_just_pressed) {
        if (npc.showing_quest_detail) {
            accept_selected_quest(f);
        } else if (!npc.available_quests.empty()) {
            npc.showing_quest_detail = true;
        }
    }

    if (f.key_q_just_pressed) {
        if (npc.showing_quest_detail) {
            npc.showing_quest_detail = false;
        } else {
            npc.close();
        }
    }
}

} // namespace

void update_npc_interaction(NPCInteractionFrame& f) {
    auto& npc = f.npc_interaction;

    // E (or "interact") doubles as accept/advance when a dialog is open.
    if (f.interact_pressed) {
        if (npc.showing_dialogue) {
            if (npc.showing_quest_detail) {
                accept_selected_quest(f);
            } else if (!npc.available_quests.empty()) {
                npc.showing_quest_detail = true;
            }
        } else {
            try_open_nearest_npc(f);
        }
    }

    if (npc.showing_dialogue) {
        handle_dialogue_navigation(f);
    }
}

std::optional<ClosestNPC> find_closest_npc(const entt::registry& registry,
                                           float player_x, float player_z,
                                           float max_distance) {
    std::optional<ClosestNPC> best;
    float best_dist = max_distance;

    auto view = registry.view<const ecs::NetworkId, const ecs::Transform,
                              const ecs::EntityInfo, const ecs::Name>();
    for (auto entity : view) {
        const auto& info = view.get<const ecs::EntityInfo>(entity);
        if (info.type != mmo::protocol::EntityType::TownNPC) continue;

        const auto& tf = view.get<const ecs::Transform>(entity);
        const float dx = tf.x - player_x;
        const float dz = tf.z - player_z;
        const float dist = std::sqrt(dx * dx + dz * dz);

        if (dist < best_dist) {
            best_dist = dist;
            best = ClosestNPC{
                view.get<const ecs::NetworkId>(entity).id,
                view.get<const ecs::Name>(entity).value,
                dist,
            };
        }
    }

    return best;
}

} // namespace mmo::client::systems
