#include "network_message_handler.hpp"
#include <cstring>
#include <iostream>

namespace mmo::client {

using namespace mmo::protocol;

bool NetworkMessageHandler::try_handle(MessageType type, const std::vector<uint8_t>& payload) {
    switch (type) {
        case MessageType::XPGain: {
            if (payload.size() >= 4 * sizeof(int32_t)) {
                BufferReader r(payload);
                int32_t xp_gained = r.read<int32_t>();
                int32_t total_xp = r.read<int32_t>();
                int32_t xp_to_next = r.read<int32_t>();
                int32_t level = r.read<int32_t>();
                (void)xp_gained;

                int old_level = ctx_.hud_state.level;
                ctx_.hud_state.xp = total_xp;
                ctx_.hud_state.xp_to_next_level = xp_to_next;
                ctx_.hud_state.level = level;

                if (level > old_level) {
                    ctx_.hud_state.show_level_up(level);
                }
            }
            return true;
        }
        case MessageType::LevelUp: {
            if (payload.size() >= LevelUpMsg::serialized_size()) {
                LevelUpMsg msg;
                msg.deserialize(payload);
                ctx_.hud_state.show_level_up(msg.new_level);
                ctx_.hud_state.level = msg.new_level;
                ctx_.hud_state.max_health = msg.new_max_health;
            }
            return true;
        }
        case MessageType::GoldChange: {
            if (payload.size() >= 2 * sizeof(int32_t)) {
                BufferReader r(payload);
                int32_t gold_change = r.read<int32_t>();
                int32_t total_gold = r.read<int32_t>();
                ctx_.hud_state.gold = total_gold;
                if (gold_change > 0) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "+%d Gold", gold_change);
                    ctx_.hud_state.add_loot(buf, 0xFF00DDFF);
                }
            }
            return true;
        }
        case MessageType::LootDrop: {
            if (payload.size() >= sizeof(int32_t) + 1) {
                BufferReader r(payload);
                int32_t gold = r.read<int32_t>();
                uint8_t item_count = r.read<uint8_t>();

                if (gold > 0) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "+%d Gold", gold);
                    ctx_.hud_state.add_loot(buf, 0xFF00DDFF);
                }

                for (uint8_t i = 0; i < item_count && r.remaining_size() >= 49; ++i) {
                    std::string name = r.read_fixed_string(32);
                    std::string rarity = r.read_fixed_string(16);
                    uint8_t count = r.read<uint8_t>();

                    uint32_t color = 0xFFAAAAAA;
                    if (rarity == "uncommon") color = 0xFF00CC00;
                    else if (rarity == "rare") color = 0xFF0088FF;
                    else if (rarity == "epic") color = 0xFFCC00CC;
                    else if (rarity == "legendary") color = 0xFF00AAFF;

                    char buf[64];
                    if (count > 1)
                        snprintf(buf, sizeof(buf), "Received: %s x%d", name.c_str(), count);
                    else
                        snprintf(buf, sizeof(buf), "Received: %s", name.c_str());
                    ctx_.hud_state.add_loot(buf, color);
                }
            }
            return true;
        }
        case MessageType::QuestOffer: {
            if (payload.size() >= QuestOfferMsg::serialized_size()) {
                QuestOfferMsg msg;
                msg.deserialize(payload);

                QuestOfferData offer;
                offer.quest_id = std::string(msg.quest_id, strnlen(msg.quest_id, 32));
                offer.quest_name = std::string(msg.quest_name, strnlen(msg.quest_name, 64));
                offer.description = std::string(msg.description, strnlen(msg.description, 256));
                offer.dialogue = std::string(msg.dialogue, strnlen(msg.dialogue, 256));
                offer.xp_reward = msg.xp_reward;
                offer.gold_reward = msg.gold_reward;

                for (uint8_t i = 0; i < msg.objective_count && i < QuestOfferMsg::MAX_OBJECTIVES; ++i) {
                    offer.objectives.push_back({
                        std::string(msg.objectives[i].description, strnlen(msg.objectives[i].description, 64)),
                        msg.objectives[i].count,
                        msg.objectives[i].location_x,
                        msg.objectives[i].location_z,
                        msg.objectives[i].radius
                    });
                }

                ctx_.npc_interaction.available_quests.push_back(std::move(offer));
            }
            return true;
        }
        case MessageType::ZoneChange: {
            if (payload.size() >= 64) {
                char zone_buf[64] = {};
                std::memcpy(zone_buf, payload.data(), 64);
                ctx_.hud_state.set_zone(std::string(zone_buf, strnlen(zone_buf, 64)));
            }
            return true;
        }
        case MessageType::QuestList: {
            if (payload.size() >= sizeof(uint16_t)) {
                ctx_.npcs_with_quests.clear();
                ctx_.npcs_with_turnins.clear();
                size_t offset = 0;
                uint16_t count;
                std::memcpy(&count, payload.data(), sizeof(uint16_t));
                offset += sizeof(uint16_t);
                for (uint16_t i = 0; i < count && offset + sizeof(uint32_t) <= payload.size(); ++i) {
                    uint32_t encoded;
                    std::memcpy(&encoded, payload.data() + offset, sizeof(uint32_t));
                    offset += sizeof(uint32_t);
                    uint32_t npc_id = encoded & 0x7FFFFFFF;
                    bool has_turnin = (encoded & 0x80000000) != 0;
                    if (has_turnin) {
                        ctx_.npcs_with_turnins.insert(npc_id);
                    } else {
                        ctx_.npcs_with_quests.insert(npc_id);
                    }
                }
            }
            return true;
        }
        case MessageType::CombatEvent:
            on_combat_event(payload);
            return true;
        case MessageType::EntityDeath:
            on_entity_death(payload);
            return true;
        case MessageType::QuestProgress:
            on_quest_progress(payload);
            return true;
        case MessageType::QuestComplete:
            on_quest_complete(payload);
            return true;
        case MessageType::InventoryUpdate:
            on_inventory_update(payload);
            return true;
        case MessageType::ItemEquip:
        case MessageType::ItemUnequip:
            on_inventory_update(payload);
            return true;
        case MessageType::SkillCooldown:
            on_skill_cooldown(payload);
            return true;
        case MessageType::SkillUnlock:
            on_skill_unlock(payload);
            return true;
        case MessageType::TalentSync:
            on_talent_sync(payload);
            return true;
        case MessageType::TalentTree:
            on_talent_tree(payload);
            return true;
        case MessageType::NPCDialogue:
            on_npc_dialogue(payload);
            return true;
        default:
            return false;
    }
}

void NetworkMessageHandler::on_combat_event(const std::vector<uint8_t>& payload) {
    if (payload.size() < CombatEventMsg::serialized_size()) return;

    CombatEventMsg msg;
    msg.deserialize(payload);

    DamageNumber dn;
    dn.x = msg.target_x;
    dn.y = msg.target_y + 30.0f;
    dn.z = msg.target_z;
    dn.damage = msg.damage;
    dn.timer = DamageNumber::DURATION;
    dn.is_heal = (msg.is_heal != 0);
    ctx_.hud_state.damage_numbers.push_back(dn);
}

void NetworkMessageHandler::on_entity_death(const std::vector<uint8_t>& payload) {
    if (payload.size() < EntityDeathMsg::serialized_size()) return;

    EntityDeathMsg msg;
    msg.deserialize(payload);

    if (msg.entity_id == ctx_.local_player_id) {
        ctx_.player_dead = true;
    }
}

void NetworkMessageHandler::on_quest_progress(const std::vector<uint8_t>& payload) {
    if (payload.size() < QuestProgressMsg::serialized_size()) return;

    QuestProgressMsg msg;
    msg.deserialize(payload);

    std::string qid(msg.quest_id, strnlen(msg.quest_id, sizeof(msg.quest_id)));
    for (auto& quest : ctx_.hud_state.tracked_quests) {
        if (quest.quest_id == qid) {
            if (msg.objective_index < quest.objectives.size()) {
                quest.objectives[msg.objective_index].current = msg.current;
                quest.objectives[msg.objective_index].required = msg.required;
                quest.objectives[msg.objective_index].complete = (msg.complete != 0);
            }
            break;
        }
    }
}

void NetworkMessageHandler::on_quest_complete(const std::vector<uint8_t>& payload) {
    if (payload.size() < QuestCompleteMsg::serialized_size()) return;

    QuestCompleteMsg msg;
    msg.deserialize(payload);

    std::string qid(msg.quest_id, strnlen(msg.quest_id, sizeof(msg.quest_id)));
    auto& quests = ctx_.hud_state.tracked_quests;
    quests.erase(
        std::remove_if(quests.begin(), quests.end(),
            [&](const QuestTrackerEntry& q) { return q.quest_id == qid; }),
        quests.end());

    Notification notif;
    notif.text = std::string("Quest Complete: ") + msg.quest_name;
    notif.timer = Notification::DURATION;
    notif.color = 0xFF00FFFF;
    ctx_.hud_state.notifications.push_back(notif);
}

void NetworkMessageHandler::on_inventory_update(const std::vector<uint8_t>& payload) {
    if (payload.size() < InventoryUpdateMsg::serialized_size()) return;

    InventoryUpdateMsg msg;
    msg.deserialize(payload);

    for (int i = 0; i < PanelState::MAX_INVENTORY_SLOTS; ++i) {
        ctx_.panel_state.inventory_slots[i].item_id = msg.slots[i].item_id;
        ctx_.panel_state.inventory_slots[i].count = msg.slots[i].count;
    }
    ctx_.panel_state.equipped_weapon = msg.equipped_weapon;
    ctx_.panel_state.equipped_armor = msg.equipped_armor;
}

void NetworkMessageHandler::on_skill_cooldown(const std::vector<uint8_t>& payload) {
    if (payload.size() < SkillCooldownMsg::serialized_size()) return;

    SkillCooldownMsg msg;
    msg.deserialize(payload);

    for (int i = 0; i < 5; ++i) {
        if (i == msg.skill_id) {
            ctx_.hud_state.skill_slots[i].cooldown = msg.cooldown_remaining;
            ctx_.hud_state.skill_slots[i].max_cooldown = msg.cooldown_total;
            break;
        }
    }
}

void NetworkMessageHandler::on_skill_unlock(const std::vector<uint8_t>& payload) {
    if (payload.size() < SkillResultMsg::serialized_size()) return;

    SkillResultMsg msg;
    msg.deserialize(payload);

    std::string sid(msg.skill_id, strnlen(msg.skill_id, sizeof(msg.skill_id)));

    for (int i = 0; i < 5; ++i) {
        if (ctx_.hud_state.skill_slots[i].skill_id == sid) {
            ctx_.hud_state.skill_slots[i].max_cooldown = msg.cooldown;
            return;
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (!ctx_.hud_state.skill_slots[i].available) {
            ctx_.hud_state.skill_slots[i].skill_id = sid;
            ctx_.hud_state.skill_slots[i].name = sid;
            ctx_.hud_state.skill_slots[i].max_cooldown = msg.cooldown;
            ctx_.hud_state.skill_slots[i].available = true;
            ctx_.hud_state.skill_slots[i].key_number = i + 1;
            return;
        }
    }
}

void NetworkMessageHandler::on_talent_sync(const std::vector<uint8_t>& payload) {
    if (payload.size() < TalentSyncMsg::serialized_size()) return;

    TalentSyncMsg msg;
    msg.deserialize(payload);

    ctx_.panel_state.talent_points = msg.talent_points;
    ctx_.panel_state.talent_points_display = msg.talent_points;
    ctx_.panel_state.unlocked_talents.clear();
    for (int i = 0; i < msg.unlocked_count && i < TalentSyncMsg::MAX_TALENTS; ++i) {
        std::string id(msg.unlocked_ids[i], strnlen(msg.unlocked_ids[i], 32));
        if (!id.empty()) {
            ctx_.panel_state.unlocked_talents.push_back(id);
        }
    }
}

void NetworkMessageHandler::on_talent_tree(const std::vector<uint8_t>& payload) {
    if (payload.size() < TalentTreeMsg::serialized_size()) {
        std::cout << "[TalentTree] Payload too small: " << payload.size()
                  << " < " << TalentTreeMsg::serialized_size() << std::endl;
        return;
    }

    TalentTreeMsg msg;
    msg.deserialize(payload);

    ctx_.panel_state.talent_tree.clear();
    for (int i = 0; i < msg.talent_count && i < TalentTreeMsg::MAX_TALENTS; ++i) {
        PanelState::ClientTalent t;
        t.id = std::string(msg.talents[i].id, strnlen(msg.talents[i].id, 32));
        t.name = std::string(msg.talents[i].name, strnlen(msg.talents[i].name, 32));
        t.description = std::string(msg.talents[i].description, strnlen(msg.talents[i].description, 128));
        t.tier = msg.talents[i].tier;
        t.prerequisite = std::string(msg.talents[i].prerequisite, strnlen(msg.talents[i].prerequisite, 32));
        t.branch_name = std::string(msg.talents[i].branch_name, strnlen(msg.talents[i].branch_name, 32));
        ctx_.panel_state.talent_tree.push_back(std::move(t));
    }
    std::cout << "[TalentTree] Received " << ctx_.panel_state.talent_tree.size() << " talents" << std::endl;
}

void NetworkMessageHandler::on_npc_dialogue(const std::vector<uint8_t>& payload) {
    if (payload.size() < NPCDialogueMsg::serialized_size()) return;

    NPCDialogueMsg msg;
    msg.deserialize(payload);

    auto& dlg = ctx_.hud_state.dialogue;
    dlg.visible = true;
    dlg.npc_id = msg.npc_id;
    dlg.npc_name = msg.npc_name;
    dlg.dialogue = msg.dialogue;
    dlg.quest_count = msg.quest_count;
    dlg.selected_option = 0;
    for (int i = 0; i < 4; ++i) {
        dlg.quest_ids[i] = msg.quest_ids[i];
        dlg.quest_names[i] = msg.quest_names[i];
    }
}

} // namespace mmo::client
