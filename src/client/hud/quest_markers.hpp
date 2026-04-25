#pragma once

#include "engine/scene/ui_scene.hpp"

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <unordered_set>
#include <cstdint>

namespace mmo::client::hud {

// Per-frame inputs for the floating "!" / "?" markers above quest-giver NPCs.
// Held by const-ref so we don't copy the network-id sets.
struct QuestMarkerInputs {
    const std::unordered_set<uint32_t>& npcs_with_quests;   // gold "!"
    const std::unordered_set<uint32_t>& npcs_with_turnins;  // green "?"
    glm::mat4 view_projection;
    float screen_w = 0.0f;
    float screen_h = 0.0f;
    float player_x = 0.0f;
    float player_z = 0.0f;
    float visibility_radius = 800.0f;  // world units; markers fade out beyond this
};

// Draw quest markers above every TownNPC the player has quest business with.
// Pure scene-building — reads the registry, writes to `ui` only.
void build_quest_markers(engine::scene::UIScene& ui,
                         const entt::registry& registry,
                         const QuestMarkerInputs& in);

} // namespace mmo::client::hud
