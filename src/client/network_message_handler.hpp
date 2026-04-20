#pragma once

#include "protocol/message_type.hpp"
#include "protocol/gameplay_msgs.hpp"
#include "client/gameplay_hud.hpp"
#include "client/gameplay_panels.hpp"
#include "client/game_state.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <string>

namespace mmo::client {

// Shared structs for NPC interaction (moved out of Game so handler can use them)
struct QuestOfferData {
    std::string quest_id;
    std::string quest_name;
    std::string description;
    std::string dialogue;
    int xp_reward = 0;
    int gold_reward = 0;
    struct Objective { std::string description; int count; float loc_x = 0; float loc_z = 0; float radius = 0; };
    std::vector<Objective> objectives;
};

struct NPCInteractionState {
    bool showing_dialogue = false;
    uint32_t npc_id = 0;
    std::string npc_name;
    std::vector<QuestOfferData> available_quests;
    int selected_quest = 0;
    bool showing_quest_detail = false;

    void close() { showing_dialogue = false; available_quests.clear(); selected_quest = 0; showing_quest_detail = false; }
};

// Handles gameplay-related network messages (combat, quests, inventory, skills, talents, dialogue).
// Connection, entity, and world-setup messages remain in Game.
class NetworkMessageHandler {
public:
    // References to shared game state needed by gameplay handlers
    struct Context {
        HUDState& hud_state;
        PanelState& panel_state;
        NPCInteractionState& npc_interaction;
        std::unordered_set<uint32_t>& npcs_with_quests;
        std::unordered_set<uint32_t>& npcs_with_turnins;
        uint32_t& local_player_id;
        bool& player_dead;
    };

    explicit NetworkMessageHandler(Context ctx) : ctx_(ctx) {}

    // Returns true if the message was handled, false if caller should handle it
    bool try_handle(mmo::protocol::MessageType type, const std::vector<uint8_t>& payload);

private:
    void on_combat_event(const std::vector<uint8_t>& payload);
    void on_entity_death(const std::vector<uint8_t>& payload);
    void on_quest_progress(const std::vector<uint8_t>& payload);
    void on_quest_complete(const std::vector<uint8_t>& payload);
    void on_inventory_update(const std::vector<uint8_t>& payload);
    void on_skill_cooldown(const std::vector<uint8_t>& payload);
    void on_skill_unlock(const std::vector<uint8_t>& payload);
    void on_talent_sync(const std::vector<uint8_t>& payload);
    void on_talent_tree(const std::vector<uint8_t>& payload);
    void on_npc_dialogue(const std::vector<uint8_t>& payload);
    void on_chat_broadcast(const std::vector<uint8_t>& payload);
    void on_vendor_open(const std::vector<uint8_t>& payload);
    void on_party_invite_offer(const std::vector<uint8_t>& payload);
    void on_party_state(const std::vector<uint8_t>& payload);
    void on_craft_recipes(const std::vector<uint8_t>& payload);
    void on_craft_result(const std::vector<uint8_t>& payload);

    Context ctx_;
};

} // namespace mmo::client
