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
                    if (rarity == "uncommon") {
                        color = 0xFF00CC00;
                    } else if (rarity == "rare") {
                        color = 0xFF0088FF;
                    } else if (rarity == "epic") {
                        color = 0xFFCC00CC;
                    } else if (rarity == "legendary") {
                        color = 0xFF00AAFF;
                    }

                    char buf[64];
                    if (count > 1) {
                        snprintf(buf, sizeof(buf), "Received: %s x%d", name.c_str(), count);
                    } else {
                        snprintf(buf, sizeof(buf), "Received: %s", name.c_str());
                    }
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
                    offer.objectives.push_back(
                        {std::string(msg.objectives[i].description, strnlen(msg.objectives[i].description, 64)),
                         msg.objectives[i].count, msg.objectives[i].location_x, msg.objectives[i].location_z,
                         msg.objectives[i].radius});
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
                uint16_t count = 0;
                std::memcpy(&count, payload.data(), sizeof(uint16_t));
                offset += sizeof(uint16_t);
                for (uint16_t i = 0; i < count && offset + sizeof(uint32_t) <= payload.size(); ++i) {
                    uint32_t encoded = 0;
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
        // ItemEquip/ItemUnequip are client-to-server only; server sends InventoryUpdate back
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
        case MessageType::ChatBroadcast:
            on_chat_broadcast(payload);
            return true;
        case MessageType::VendorOpen:
            on_vendor_open(payload);
            return true;
        case MessageType::PartyInviteOffer:
            on_party_invite_offer(payload);
            return true;
        case MessageType::PartyState:
            on_party_state(payload);
            return true;
        case MessageType::CraftRecipes:
            on_craft_recipes(payload);
            return true;
        case MessageType::CraftResult:
            on_craft_result(payload);
            return true;
        default:
            return false;
    }
}

void NetworkMessageHandler::on_combat_event(const std::vector<uint8_t>& payload) {
    if (payload.size() < CombatEventMsg::serialized_size()) {
        return;
    }

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
    if (payload.size() < EntityDeathMsg::serialized_size()) {
        return;
    }

    EntityDeathMsg msg;
    msg.deserialize(payload);

    if (msg.entity_id == ctx_.local_player_id) {
        ctx_.player_dead = true;
    }
}

void NetworkMessageHandler::on_quest_progress(const std::vector<uint8_t>& payload) {
    if (payload.size() < QuestProgressMsg::serialized_size()) {
        return;
    }

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
    if (payload.size() < QuestCompleteMsg::serialized_size()) {
        return;
    }

    QuestCompleteMsg msg;
    msg.deserialize(payload);

    std::string qid(msg.quest_id, strnlen(msg.quest_id, sizeof(msg.quest_id)));
    auto& quests = ctx_.hud_state.tracked_quests;
    quests.erase(
        std::remove_if(quests.begin(), quests.end(), [&](const QuestTrackerEntry& q) { return q.quest_id == qid; }),
        quests.end());

    Notification notif;
    notif.text = std::string("Quest Complete: ") + msg.quest_name;
    notif.timer = Notification::DURATION;
    notif.color = 0xFF00FFFF;
    ctx_.hud_state.notifications.push_back(notif);
}

void NetworkMessageHandler::on_inventory_update(const std::vector<uint8_t>& payload) {
    if (payload.size() < InventoryUpdateMsg::serialized_size()) {
        return;
    }

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
    if (payload.size() < SkillCooldownMsg::serialized_size()) {
        return;
    }

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
    if (payload.size() < SkillUnlockMsg::serialized_size()) {
        return;
    }

    SkillUnlockMsg msg;
    msg.deserialize(payload);

    // Replace the entire skill bar with the server's authoritative list
    for (int i = 0; i < 5; ++i) {
        ctx_.hud_state.skill_slots[i] = {};
    }
    for (int i = 0; i < msg.skill_count && i < 5; ++i) {
        std::string id(msg.skills[i].name, strnlen(msg.skills[i].name, sizeof(msg.skills[i].name)));
        std::string display(msg.skills[i].display_name,
                            strnlen(msg.skills[i].display_name, sizeof(msg.skills[i].display_name)));
        ctx_.hud_state.skill_slots[i].skill_id = id;
        ctx_.hud_state.skill_slots[i].name = display.empty() ? id : display;
        ctx_.hud_state.skill_slots[i].available = true;
        ctx_.hud_state.skill_slots[i].key_number = i + 1;
    }
}

void NetworkMessageHandler::on_talent_sync(const std::vector<uint8_t>& payload) {
    if (payload.size() < TalentSyncMsg::serialized_size()) {
        return;
    }

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
        std::cout << "[TalentTree] Payload too small: " << payload.size() << " < " << TalentTreeMsg::serialized_size()
                  << '\n';
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
    std::cout << "[TalentTree] Received " << ctx_.panel_state.talent_tree.size() << " talents" << '\n';
}

void NetworkMessageHandler::on_npc_dialogue(const std::vector<uint8_t>& payload) {
    if (payload.size() < NPCDialogueMsg::serialized_size()) {
        return;
    }

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

void NetworkMessageHandler::on_chat_broadcast(const std::vector<uint8_t>& payload) {
    if (payload.size() < ChatBroadcastMsg::serialized_size()) {
        return;
    }
    ChatBroadcastMsg msg;
    msg.deserialize(payload);
    std::string sender(msg.sender_name, strnlen(msg.sender_name, sizeof(msg.sender_name)));
    std::string text(msg.message, strnlen(msg.message, sizeof(msg.message)));
    ctx_.hud_state.chat.add_line(msg.channel, sender, text);
}

void NetworkMessageHandler::on_party_invite_offer(const std::vector<uint8_t>& payload) {
    if (payload.size() < PartyInviteOfferMsg::serialized_size()) {
        return;
    }
    PartyInviteOfferMsg msg;
    msg.deserialize(payload);
    auto& p = ctx_.hud_state.party;
    p.pending_inviter_id = msg.inviter_id;
    p.pending_inviter_name = std::string(msg.inviter_name, strnlen(msg.inviter_name, sizeof(msg.inviter_name)));
    p.pending_invite_timer = PartyState::INVITE_POPUP_DURATION;
}

void NetworkMessageHandler::on_party_state(const std::vector<uint8_t>& payload) {
    if (payload.size() < PartyStateMsg::serialized_size()) {
        return;
    }
    PartyStateMsg msg;
    msg.deserialize(payload);
    auto& p = ctx_.hud_state.party;
    p.leader_id = msg.leader_id;
    p.members.clear();
    for (int i = 0; i < msg.member_count && i < PartyStateMsg::MAX_MEMBERS; ++i) {
        const auto& m = msg.members[i];
        PartyMember pm;
        pm.player_id = m.player_id;
        pm.name = std::string(m.name, strnlen(m.name, sizeof(m.name)));
        pm.player_class = m.player_class;
        pm.level = m.level;
        pm.health = m.health;
        pm.max_health = m.max_health;
        pm.mana = m.mana;
        pm.max_mana = m.max_mana;
        p.members.push_back(std::move(pm));
    }
}

void NetworkMessageHandler::on_craft_recipes(const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(uint16_t)) {
        return;
    }
    BufferReader r(payload);
    uint16_t count = r.read<uint16_t>();
    ctx_.hud_state.crafting.recipes.clear();
    for (uint16_t i = 0; i < count; ++i) {
        CraftRecipeInfo info;
        info.deserialize(r);
        CraftRecipe recipe;
        recipe.id = std::string(info.id, strnlen(info.id, sizeof(info.id)));
        recipe.name = std::string(info.name, strnlen(info.name, sizeof(info.name)));
        recipe.output_item_id =
            std::string(info.output_item_id, strnlen(info.output_item_id, sizeof(info.output_item_id)));
        recipe.output_count = info.output_count;
        recipe.gold_cost = info.gold_cost;
        recipe.required_level = info.required_level;
        for (int j = 0; j < info.ingredient_count && j < CraftRecipeInfo::MAX_INGREDIENTS; ++j) {
            CraftIngredientClient c;
            c.item_id = std::string(info.ingredients[j].item_id,
                                    strnlen(info.ingredients[j].item_id, sizeof(info.ingredients[j].item_id)));
            c.count = info.ingredients[j].count;
            recipe.ingredients.push_back(std::move(c));
        }
        ctx_.hud_state.crafting.recipes.push_back(std::move(recipe));
    }
}

void NetworkMessageHandler::on_craft_result(const std::vector<uint8_t>& payload) {
    if (payload.size() < CraftResultMsg::serialized_size()) {
        return;
    }
    CraftResultMsg msg;
    msg.deserialize(payload);
    auto& cs = ctx_.hud_state.crafting;
    std::string rid(msg.recipe_id, strnlen(msg.recipe_id, sizeof(msg.recipe_id)));
    std::string reason(msg.reason, strnlen(msg.reason, sizeof(msg.reason)));
    if (msg.success) {
        cs.last_result = "Crafted: " + rid;
        cs.last_result_color = 0xFF00CC00;
    } else {
        cs.last_result = "Craft failed: " + (reason.empty() ? rid : reason);
        cs.last_result_color = 0xFF0000CC;
    }
    cs.last_result_timer = 3.0f;

    Notification notif;
    notif.text = cs.last_result;
    notif.timer = Notification::DURATION;
    notif.color = cs.last_result_color;
    ctx_.hud_state.notifications.push_back(notif);
}

void NetworkMessageHandler::on_vendor_open(const std::vector<uint8_t>& payload) {
    if (payload.size() < VendorOpenMsg::serialized_size()) {
        return;
    }
    VendorOpenMsg msg;
    msg.deserialize(payload);

    // Empty stock + zero npc = close signal (player walked away).
    if (msg.stock_count == 0 && msg.npc_id == 0) {
        ctx_.hud_state.vendor.close();
        return;
    }

    auto& v = ctx_.hud_state.vendor;
    v.visible = true;
    v.npc_id = msg.npc_id;
    v.vendor_name = std::string(msg.vendor_name, strnlen(msg.vendor_name, sizeof(msg.vendor_name)));
    v.buy_mult = msg.buy_price_multiplier;
    v.sell_mult = msg.sell_price_multiplier;
    v.stock.clear();
    for (int i = 0; i < msg.stock_count && i < VendorOpenMsg::MAX_STOCK; ++i) {
        VendorStockSlot slot;
        slot.item_id = std::string(msg.stock[i].item_id, strnlen(msg.stock[i].item_id, sizeof(msg.stock[i].item_id)));
        slot.item_name =
            std::string(msg.stock[i].item_name, strnlen(msg.stock[i].item_name, sizeof(msg.stock[i].item_name)));
        if (slot.item_name.empty()) {
            slot.item_name = slot.item_id;
        }
        slot.rarity = std::string(msg.stock[i].rarity, strnlen(msg.stock[i].rarity, sizeof(msg.stock[i].rarity)));
        slot.price = msg.stock[i].price;
        slot.stock = msg.stock[i].stock;
        v.stock.push_back(std::move(slot));
    }
    v.cursor = 0;
    v.buying = true;
}

} // namespace mmo::client
