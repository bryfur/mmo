// Game input handling — keyboard / mouse dispatch for the in-game state.
// Lives in its own translation unit so game.cpp keeps a manageable size; the
// methods below are still members of mmo::client::Game declared in game.hpp.

#include "client/ecs/components.hpp"
#include "client/game_state.hpp"
#include "client/systems/npc_interaction.hpp"
#include "game.hpp"
#include "protocol/gameplay_msgs.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <string>
#include <vector>

namespace mmo::client {

using namespace mmo::protocol;

// ---------------------------------------------------------------------------
// Per-frame helpers called from Game::update_playing(). These were inline
// blocks until they were extracted; their behavior has not changed.
// ---------------------------------------------------------------------------

void Game::send_player_input(float dt) {
    const float send_interval = world_config_.tick_rate > 0 ? (1.0f / world_config_.tick_rate) : 0.05f;
    input_send_timer_ += dt;
    if (input_send_timer_ < send_interval) {
        return;
    }
    input_send_timer_ -= send_interval;

    const auto& eng_input = input().get_input();
    PlayerInput net_input;
    net_input.move_up = eng_input.move_up;
    net_input.move_down = eng_input.move_down;
    net_input.move_left = eng_input.move_left;
    net_input.move_right = eng_input.move_right;
    net_input.move_dir_x = eng_input.move_dir_x;
    net_input.move_dir_y = eng_input.move_dir_y;
    net_input.attacking = input_bindings_->attacking();
    net_input.sprinting = input_bindings_->sprinting();
    net_input.attack_dir_x = eng_input.attack_dir_x;
    net_input.attack_dir_y = eng_input.attack_dir_y;

    // Skip the wire if nothing changed — saves bandwidth on idle frames.
    if (net_input != last_sent_input_) {
        network_.send_input(net_input);
        last_sent_input_ = net_input;
    }
    input_bindings_->consume_attack();
}

void Game::process_global_hotkeys() {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool menu_open = menu_system_->is_open();

    if (key_i_.just_pressed(keys[SDL_SCANCODE_I]) && !menu_open) {
        panel_state_.toggle_panel(ActivePanel::Inventory);
    }
    if (key_l_.just_pressed(keys[SDL_SCANCODE_L]) && !menu_open) {
        panel_state_.toggle_panel(ActivePanel::QuestLog);
    }
    if (key_t_.just_pressed(keys[SDL_SCANCODE_T]) && !menu_open) {
        panel_state_.toggle_panel(ActivePanel::Talents);
    }
    if (key_m_.just_pressed(keys[SDL_SCANCODE_M]) && !menu_open) {
        panel_state_.toggle_panel(ActivePanel::WorldMap);
    }

    // Open chat on Enter — only when no other modal is grabbing input and the
    // player is alive (death overlay must block all input).
    if (key_chat_open_.just_pressed(keys[SDL_SCANCODE_RETURN]) && !menu_open && !panel_state_.is_panel_open() &&
        !hud_state_.dialogue.visible && !hud_state_.vendor.visible && !player_dead_) {
        hud_state_.chat.input_active = true;
        hud_state_.chat.input_buffer.clear();
        input().set_text_input_enabled(true);
    }
}

void Game::process_party_invite_input() {
    if (hud_state_.party.pending_inviter_id == 0) {
        return;
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool yes = key_party_y_.just_pressed(keys[SDL_SCANCODE_Y]);
    const bool no = key_party_n_.just_pressed(keys[SDL_SCANCODE_N]);
    if (!yes && !no) {
        return;
    }

    PartyInviteRespondMsg m;
    m.inviter_id = hud_state_.party.pending_inviter_id;
    m.accept = yes ? 1 : 0;
    network_.send_raw(build_packet(MessageType::PartyInviteRespond, m));
    hud_state_.party.pending_inviter_id = 0;
    hud_state_.party.pending_inviter_name.clear();
}

void Game::update_npc_interaction_frame() {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool menu_toggle = input().menu_toggle_pressed();
    const bool dialog_was_open = npc_interaction_.showing_dialogue;

    systems::NPCInteractionFrame frame{
        registry_,
        network_,
        npc_interaction_,
        hud_state_,
        panel_state_,
        network_to_entity_,
        npcs_with_turnins_,
        local_player_id_,
        input_bindings_->interact_pressed(),
        menu_toggle,
        key_npc_w_.just_pressed(keys[SDL_SCANCODE_W]),
        key_npc_s_.just_pressed(keys[SDL_SCANCODE_S]),
        key_npc_enter_.just_pressed(keys[SDL_SCANCODE_RETURN]),
        key_npc_q_.just_pressed(keys[SDL_SCANCODE_Q]),
    };
    systems::update_npc_interaction(frame);

    // ESC consumed by the dialog must not also pop the settings menu.
    if (menu_toggle && dialog_was_open) {
        input().clear_menu_inputs();
    }
}

void Game::process_skill_keys() {
    const int skill_key = input_bindings_->skill_pressed();
    if (skill_key < 1 || skill_key > 5) {
        return;
    }
    if (panel_state_.any_panel_open()) {
        return;
    }

    auto& slot = hud_state_.skill_slots[skill_key - 1];
    if (!slot.available || slot.cooldown > 0.0f) {
        return;
    }

    const auto& eng_input = input().get_input();
    SkillUseMsg msg;
    std::strncpy(msg.skill_id, slot.skill_id.c_str(), 31);
    msg.dir_x = eng_input.attack_dir_x;
    msg.dir_z = eng_input.attack_dir_y;
    network_.send_raw(build_packet(MessageType::SkillUse, msg));

    // Local cooldown for immediate visual feedback; server confirms.
    slot.cooldown = slot.max_cooldown;
}

void Game::tick_combat_cooldowns(float dt) {
    auto view = registry_.view<ecs::Combat>();
    for (auto entity : view) {
        auto& combat = view.get<ecs::Combat>(entity);
        if (combat.current_cooldown > 0.0f) {
            combat.current_cooldown -= dt;
            if (combat.current_cooldown < 0.0f) {
                combat.current_cooldown = 0.0f;
            }
        }
    }
}

void Game::sync_local_player_to_camera_and_hud() {
    auto it = network_to_entity_.find(local_player_id_);
    if (it == network_to_entity_.end() || !registry_.valid(it->second)) {
        return;
    }

    const auto entity = it->second;
    const auto& transform = registry_.get<ecs::Transform>(entity);
    player_x_ = transform.x;
    player_z_ = transform.z;

    if (auto* vel = registry_.try_get<ecs::Velocity>(entity)) {
        camera().set_target_velocity(glm::vec3(vel->x, 0.0f, vel->z));
    }
    if (auto* combat = registry_.try_get<ecs::Combat>(entity)) {
        combat_camera_->set_in_combat(combat->is_attacking || combat->current_cooldown > 0.0f);
    }
    if (auto* health = registry_.try_get<ecs::Health>(entity)) {
        hud_state_.health = health->current;
        hud_state_.max_health = health->max;
    }
    if (auto* eff = registry_.try_get<ecs::StatusEffects>(entity)) {
        local_effects_mask_ = eff->mask;
    } else {
        local_effects_mask_ = 0;
    }
}

void Game::update_camera_for_player() {
    camera().set_yaw(input().get_camera_yaw());
    camera().set_pitch(input().get_camera_pitch());

    const bool sprinting = input_bindings_->sprinting() && (input().move_forward() || input().move_backward() ||
                                                            input().move_left() || input().move_right());
    camera().set_config(sprinting ? sprint_camera_config_ : exploration_camera_config_);

    const float zoom_delta = input().get_camera_zoom_delta();
    if (zoom_delta != 0.0f) {
        camera().adjust_zoom(zoom_delta);
    }
    input().reset_camera_deltas();
}

// ---------------------------------------------------------------------------
// Original input handlers below.
// ---------------------------------------------------------------------------

void Game::update_panel_input(float /*dt*/) {
    const bool* keys = SDL_GetKeyboardState(nullptr);

    bool i_down = keys[SDL_SCANCODE_I];
    bool t_down = keys[SDL_SCANCODE_T];
    bool l_down = keys[SDL_SCANCODE_L];
    bool e_down = keys[SDL_SCANCODE_E];
    bool esc_down = keys[SDL_SCANCODE_ESCAPE];
    bool space_down = keys[SDL_SCANCODE_SPACE];

    // Handle death respawn
    if (player_dead_ && panel_space_.just_pressed(space_down)) {
        player_dead_ = false;
        // Respawn is handled server-side; just clear the overlay
    }

    // Close dialogue on ESC or E
    if (hud_state_.dialogue.visible) {
        if (panel_esc_.just_pressed(esc_down) || panel_e_.just_pressed(e_down)) {
            hud_state_.dialogue.visible = false;
        }

        // Navigate dialogue options
        if (hud_state_.dialogue.quest_count > 0) {
            bool dlg_up_now = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
            bool dlg_down_now = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
            if (panel_dlg_up_.just_pressed(dlg_up_now) && hud_state_.dialogue.selected_option > 0) {
                hud_state_.dialogue.selected_option--;
            }
            if (panel_dlg_down_.just_pressed(dlg_down_now) &&
                hud_state_.dialogue.selected_option < hud_state_.dialogue.quest_count - 1) {
                hud_state_.dialogue.selected_option++;
            }

            // Accept quest on Enter/Space - close legacy dialogue
            // (Quest accept is handled by the npc_interaction_ system which has proper quest IDs)
            if (keys[SDL_SCANCODE_RETURN] || panel_space_.just_pressed(space_down)) {
                hud_state_.dialogue.visible = false;
            }
        }

        // Dialogue consumes input, skip panel toggles -- still update edges
        panel_i_.just_pressed(i_down);
        panel_t_.just_pressed(t_down);
        panel_l_.just_pressed(l_down);
        return;
    }

    // Update edges for keys not consumed above
    panel_esc_.just_pressed(esc_down);
    panel_e_.just_pressed(e_down);

    // Panel toggles are handled in update_playing's key block above
    // This function only handles panel-specific interactions

    // Close panel on ESC
    if (esc_down && !panel_esc_.prev && panel_state_.is_panel_open()) {
        panel_state_.active_panel = ActivePanel::None;
    }

    // Panel-specific interaction
    if (panel_state_.active_panel == ActivePanel::Inventory) {
        // Navigate inventory with arrow keys
        bool up_now = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        bool down_now = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];

        if (panel_inv_up_.just_pressed(up_now) && panel_state_.inventory_cursor > 0) {
            panel_state_.inventory_cursor--;
        }
        if (panel_inv_down_.just_pressed(down_now) &&
            panel_state_.inventory_cursor < PanelState::MAX_INVENTORY_SLOTS - 1) {
            panel_state_.inventory_cursor++;
        }

        // Equip item on Enter
        if (panel_inv_enter_.just_pressed(keys[SDL_SCANCODE_RETURN])) {
            auto& slot = panel_state_.inventory_slots[panel_state_.inventory_cursor];
            if (!slot.empty()) {
                ItemEquipMsg msg;
                msg.slot_index = static_cast<uint8_t>(panel_state_.inventory_cursor);
                network_.send_raw(build_packet(MessageType::ItemEquip, msg));
            }
        }

        // Unequip weapon on 1, armor on 2
        if (panel_inv_key1_.just_pressed(keys[SDL_SCANCODE_1]) && panel_state_.equipped_weapon > 0) {
            ItemUnequipMsg msg;
            msg.equip_slot = 0;
            network_.send_raw(build_packet(MessageType::ItemUnequip, msg));
        }
        if (panel_inv_key2_.just_pressed(keys[SDL_SCANCODE_2]) && panel_state_.equipped_armor > 0) {
            ItemUnequipMsg msg;
            msg.equip_slot = 1;
            network_.send_raw(build_packet(MessageType::ItemUnequip, msg));
        }

        // Use consumable on U
        if (panel_inv_u_.just_pressed(keys[SDL_SCANCODE_U])) {
            auto& slot = panel_state_.inventory_slots[panel_state_.inventory_cursor];
            if (!slot.empty()) {
                ItemUseMsg msg;
                msg.slot_index = static_cast<uint8_t>(panel_state_.inventory_cursor);
                network_.send_raw(build_packet(MessageType::ItemUse, msg));
            }
        }
    }

    if (panel_state_.active_panel == ActivePanel::Talents) {
        bool up_now = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        bool down_now = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
        bool enter_now = keys[SDL_SCANCODE_RETURN];

        int max_cursor = static_cast<int>(panel_state_.talent_tree.size()) - 1;
        if (max_cursor < 0) {
            max_cursor = 0;
        }

        if (panel_talent_up_.just_pressed(up_now) && panel_state_.talent_cursor > 0) {
            panel_state_.talent_cursor--;
        }
        if (panel_talent_down_.just_pressed(down_now) && panel_state_.talent_cursor < max_cursor) {
            panel_state_.talent_cursor++;
        }

        // Unlock talent on Enter if we have points and a valid selection
        if (panel_talent_enter_.just_pressed(enter_now) && panel_state_.talent_points > 0 &&
            panel_state_.talent_cursor < static_cast<int>(panel_state_.talent_tree.size())) {
            const auto& talent = panel_state_.talent_tree[panel_state_.talent_cursor];
            // Only unlock if not already unlocked
            bool already_unlocked = false;
            for (const auto& ut : panel_state_.unlocked_talents) {
                if (ut == talent.id) {
                    already_unlocked = true;
                    break;
                }
            }
            if (!already_unlocked) {
                TalentUnlockMsg msg;
                std::strncpy(msg.talent_id, talent.id.c_str(), sizeof(msg.talent_id) - 1);
                network_.send_raw(build_packet(MessageType::TalentUnlock, msg));
            }
        }
    }

    if (panel_state_.active_panel == ActivePanel::QuestLog) {
        bool up_now = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        bool down_now = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
        bool del_now = keys[SDL_SCANCODE_DELETE] || keys[SDL_SCANCODE_X];

        if (panel_quest_up_.just_pressed(up_now) && panel_state_.quest_cursor > 0) {
            panel_state_.quest_cursor--;
        }
        if (panel_quest_down_.just_pressed(down_now) &&
            panel_state_.quest_cursor < static_cast<int>(hud_state_.tracked_quests.size()) - 1) {
            panel_state_.quest_cursor++;
        }

        // Abandon quest on Delete/X (local removal)
        if (panel_quest_del_.just_pressed(del_now) &&
            panel_state_.quest_cursor < static_cast<int>(hud_state_.tracked_quests.size())) {
            hud_state_.tracked_quests.erase(hud_state_.tracked_quests.begin() + panel_state_.quest_cursor);
            if (panel_state_.quest_cursor > 0 &&
                panel_state_.quest_cursor >= static_cast<int>(hud_state_.tracked_quests.size())) {
                panel_state_.quest_cursor--;
            }
        }
    }

    // Update remaining key edges that weren't consumed by specific panels
    panel_i_.just_pressed(i_down);
    panel_t_.just_pressed(t_down);
    panel_l_.just_pressed(l_down);
    panel_space_.just_pressed(space_down);
}

void Game::update_damage_numbers(float dt) {
    for (auto& dn : hud_state_.damage_numbers) {
        dn.timer -= dt;
        dn.y += 40.0f * dt; // Float upward
    }
    // Remove expired
    hud_state_.damage_numbers.erase(std::remove_if(hud_state_.damage_numbers.begin(), hud_state_.damage_numbers.end(),
                                                   [](const DamageNumber& d) { return d.timer <= 0.0f; }),
                                    hud_state_.damage_numbers.end());
}

void Game::update_notifications(float dt) {
    for (auto& n : hud_state_.notifications) {
        n.timer -= dt;
    }
    hud_state_.notifications.erase(std::remove_if(hud_state_.notifications.begin(), hud_state_.notifications.end(),
                                                  [](const Notification& n) { return n.timer <= 0.0f; }),
                                   hud_state_.notifications.end());
}


// ============================================================================
// Chat input
// ============================================================================

void Game::update_chat_input() {
    auto& chat = hud_state_.chat;

    // Defensive: if the game is not in the Playing state (e.g. disconnect
    // threw us back to Connecting), or the local player died, make sure
    // chat input isn't left stuck.
    if ((game_state_ != GameState::Playing || player_dead_) && chat.input_active) {
        chat.input_active = false;
        chat.input_buffer.clear();
        input().set_text_input_enabled(false);
    }

    if (!chat.input_active) {
        return;
    }

    auto& ih = input();

    // Consume characters typed since the last frame.
    std::string typed = ih.take_text_input();
    for (char c : typed) {
        if (chat.input_buffer.size() < 180) {
            chat.input_buffer.push_back(c);
        }
    }

    if (ih.text_backspace_pressed() && !chat.input_buffer.empty()) {
        chat.input_buffer.pop_back();
    }

    if (ih.text_escape_pressed()) {
        chat.input_active = false;
        chat.input_buffer.clear();
        ih.set_text_input_enabled(false);
        ih.clear_text_events();
        return;
    }

    if (ih.text_enter_pressed()) {
        // Block other panels from reacting to Enter this frame (chat just
        // consumed it). Flag is cleared at the end of the frame in
        // update_vendor_input / update_panel_input.
        suppress_enter_this_frame_ = true;
        if (!chat.input_buffer.empty()) {
            // Local slash-commands for party actions.
            const std::string& txt = chat.input_buffer;
            if (txt.rfind("/invite ", 0) == 0) {
                std::string target = txt.substr(8);
                while (!target.empty() && target.front() == ' ') target.erase(target.begin());
                if (!target.empty()) {
                    protocol::PartyInviteMsg m;
                    std::strncpy(m.target_name, target.c_str(), sizeof(m.target_name) - 1);
                    network_.send_raw(protocol::build_packet(protocol::MessageType::PartyInvite, m));
                }
            } else if (txt == "/leave" || txt == "/disband") {
                network_.send_raw(protocol::build_packet(protocol::MessageType::PartyLeave, std::vector<uint8_t>{}));
            } else if (txt.rfind("/craft ", 0) == 0) {
                std::string rid = txt.substr(7);
                while (!rid.empty() && rid.front() == ' ') rid.erase(rid.begin());
                if (!rid.empty()) {
                    protocol::CraftRequestMsg m;
                    std::strncpy(m.recipe_id, rid.c_str(), sizeof(m.recipe_id) - 1);
                    network_.send_raw(protocol::build_packet(protocol::MessageType::CraftRequest, m));
                }
            } else if (txt == "/recipes") {
                // Local: print recipe list into chat as a system echo.
                ChatLine header{3, "System", "Available recipes:"};
                hud_state_.chat.lines.push_back(header);
                for (const auto& r : hud_state_.crafting.recipes) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "  /craft %s  (%s, lvl %d)", r.id.c_str(), r.name.c_str(),
                             r.required_level);
                    hud_state_.chat.add_line(3, "System", buf);
                }
            } else {
                protocol::ChatSendMsg msg;
                msg.channel = chat.selected_channel;
                std::strncpy(msg.message, txt.c_str(), sizeof(msg.message) - 1);
                network_.send_raw(protocol::build_packet(protocol::MessageType::ChatSend, msg));
            }
        }
        chat.input_active = false;
        chat.input_buffer.clear();
        ih.set_text_input_enabled(false);
    }

    ih.clear_text_events();
}

// ============================================================================
// Mouse-driven UI interaction
// ============================================================================

void Game::process_mouse_ui() {
    auto& mui = mouse_ui_;
    mui.process();

    // If the mouse is over any UI region (or a drag is active), suppress
    // world click-to-attack so clicks on windows don't fire weapons.
    auto& ih = input();
    bool over_ui = mui.over_any_ui() || mui.dragging_window != WidgetId::None;
    if (over_ui && ih.mouse_left_pressed()) {
        input_bindings_->suppress_attack_for_ui_click();
    }

    // Dispatch clicked widgets.
    WidgetId w = mui.clicked;
    if (w == WidgetId::None) {
        ih.clear_mouse_edges();
        return;
    }
    int raw = static_cast<int>(w);

    // Close buttons
    if (w == WidgetId::CloseVendor) {
        hud_state_.vendor.close();
    } else if (w == WidgetId::CloseInventory) {
        panel_state_.active_panel = ActivePanel::None;
    } else if (w == WidgetId::CloseQuestLog) {
        panel_state_.active_panel = ActivePanel::None;
    } else if (w == WidgetId::CloseTalents) {
        panel_state_.active_panel = ActivePanel::None;
    } else if (w == WidgetId::CloseWorldMap) {
        panel_state_.active_panel = ActivePanel::None;
    } else if (w == WidgetId::CloseCrafting) {
        // Crafting panel closes from its own toggle; noop here.
    } else if (w == WidgetId::CloseDialogue) {
        npc_interaction_.close();
        hud_state_.dialogue.visible = false;
    }
    // Bottom-right shortcut bar: each button toggles its panel (or opens
    // the settings menu) just like its keyboard hotkey would.
    else if (w == WidgetId::MenuBarInventory) {
        panel_state_.toggle_panel(ActivePanel::Inventory);
    } else if (w == WidgetId::MenuBarQuestLog) {
        panel_state_.toggle_panel(ActivePanel::QuestLog);
    } else if (w == WidgetId::MenuBarTalents) {
        panel_state_.toggle_panel(ActivePanel::Talents);
    } else if (w == WidgetId::MenuBarWorldMap) {
        panel_state_.toggle_panel(ActivePanel::WorldMap);
    } else if (w == WidgetId::MenuBarMenu) {
        menu_system_->open();
    }
    // Vendor tab toggle
    else if (w == WidgetId::VendorTab) {
        hud_state_.vendor.buying = !hud_state_.vendor.buying;
        hud_state_.vendor.cursor = 0;
        hud_state_.vendor.sell_cursor = hud_state_.vendor.buying ? -1 : 0;
    }
    // Vendor row click → select + purchase
    else if (raw >= static_cast<int>(WidgetId::VendorRowFirst) && raw <= static_cast<int>(WidgetId::VendorRowLast)) {
        int idx = raw - static_cast<int>(WidgetId::VendorRowFirst);
        hud_state_.vendor.cursor = idx;
        if (hud_state_.vendor.buying && idx < static_cast<int>(hud_state_.vendor.stock.size())) {
            protocol::VendorBuyMsg msg;
            msg.npc_id = hud_state_.vendor.npc_id;
            msg.stock_index = static_cast<uint8_t>(idx);
            msg.quantity = 1;
            network_.send_raw(protocol::build_packet(protocol::MessageType::VendorBuy, msg));
        }
    }
    // Inventory slot click → select + double-click-to-equip-or-use
    // Dialogue quest list click: first click selects, second click on same
    // entry opens the quest detail view (matches Enter key behavior).
    else if (raw >= static_cast<int>(WidgetId::QuestRowFirst) && raw <= static_cast<int>(WidgetId::QuestRowLast) &&
             npc_interaction_.showing_dialogue && !npc_interaction_.showing_quest_detail) {
        int idx = raw - static_cast<int>(WidgetId::QuestRowFirst);
        if (idx >= 0 && idx < static_cast<int>(npc_interaction_.available_quests.size())) {
            if (npc_interaction_.selected_quest == idx) {
                npc_interaction_.showing_quest_detail = true;
            } else {
                npc_interaction_.selected_quest = idx;
            }
        }
    }
    // Talent row click → select the row; clicking the already-selected row
    // unlocks the talent (two-step so misclicks don't burn points).
    else if (raw >= static_cast<int>(WidgetId::TalentRowFirst) && raw <= static_cast<int>(WidgetId::TalentRowLast)) {
        int idx = raw - static_cast<int>(WidgetId::TalentRowFirst);
        if (idx >= 0 && idx < static_cast<int>(panel_state_.talent_tree.size())) {
            if (panel_state_.talent_cursor == idx) {
                // Try unlock (server validates points + prerequisites).
                const auto& t = panel_state_.talent_tree[idx];
                protocol::TalentUnlockMsg msg;
                std::strncpy(msg.talent_id, t.id.c_str(), sizeof(msg.talent_id) - 1);
                network_.send_raw(protocol::build_packet(protocol::MessageType::TalentUnlock, msg));
            } else {
                panel_state_.talent_cursor = idx;
            }
        }
    }
    // Skill bar click → fire skill at the mouse direction.
    else if (raw >= static_cast<int>(WidgetId::SkillSlotFirst) && raw <= static_cast<int>(WidgetId::SkillSlotLast)) {
        int idx = raw - static_cast<int>(WidgetId::SkillSlotFirst);
        if (idx >= 0 && idx < 5) {
            auto& slot = hud_state_.skill_slots[idx];
            if (slot.available && slot.cooldown <= 0.0f && !slot.skill_id.empty()) {
                const auto& eng_input = input().get_input();
                protocol::SkillUseMsg msg;
                std::strncpy(msg.skill_id, slot.skill_id.c_str(), 31);
                msg.dir_x = eng_input.attack_dir_x;
                msg.dir_z = eng_input.attack_dir_y;
                network_.send_raw(protocol::build_packet(protocol::MessageType::SkillUse, msg));
                slot.cooldown = slot.max_cooldown;
            }
        }
    } else if (raw >= static_cast<int>(WidgetId::InventorySlotFirst) &&
               raw <= static_cast<int>(WidgetId::InventorySlotLast)) {
        int idx = raw - static_cast<int>(WidgetId::InventorySlotFirst);
        if (idx < 0 || idx >= PanelState::MAX_INVENTORY_SLOTS) {
            ih.clear_mouse_edges();
            return;
        }
        const auto& slot = panel_state_.inventory_slots[idx];

        if (hud_state_.vendor.visible && !hud_state_.vendor.buying) {
            // Sell mode: single click = sell.
            protocol::VendorSellMsg msg;
            msg.npc_id = hud_state_.vendor.npc_id;
            msg.inventory_slot = static_cast<uint8_t>(idx);
            msg.quantity = 1;
            network_.send_raw(protocol::build_packet(protocol::MessageType::VendorSell, msg));
        } else {
            // Normal inventory: first click selects, clicking the already-
            // selected slot equips (weapon/armor) or uses (consumable).
            bool was_selected = (panel_state_.inventory_cursor == idx);
            panel_state_.inventory_cursor = idx;
            if (was_selected && !slot.empty()) {
                // Potions occupy item_id 5/6 in the legacy lookup; use them.
                bool is_consumable = (slot.item_id == 5 || slot.item_id == 6);
                if (is_consumable) {
                    protocol::ItemUseMsg msg;
                    msg.slot_index = static_cast<uint8_t>(idx);
                    network_.send_raw(protocol::build_packet(protocol::MessageType::ItemUse, msg));
                } else {
                    protocol::ItemEquipMsg msg;
                    msg.slot_index = static_cast<uint8_t>(idx);
                    network_.send_raw(protocol::build_packet(protocol::MessageType::ItemEquip, msg));
                }
            }
        }
    }

    // Right-click context actions. Currently the only consumer is party
    // frames → leader kicks a member.
    if (mui.right_clicked != WidgetId::None) {
        int rraw = static_cast<int>(mui.right_clicked);
        if (rraw >= static_cast<int>(WidgetId::PartyKickFirst) && rraw <= static_cast<int>(WidgetId::PartyKickLast)) {
            int idx = rraw - static_cast<int>(WidgetId::PartyKickFirst);
            const auto& party = hud_state_.party;
            if (idx >= 0 && idx < static_cast<int>(party.members.size()) && party.leader_id == local_player_id_ &&
                party.members[idx].player_id != local_player_id_) {
                protocol::PartyKickMsg m;
                m.target_id = party.members[idx].player_id;
                network_.send_raw(protocol::build_packet(protocol::MessageType::PartyKick, m));
            }
        }
    }

    ih.clear_mouse_edges();
}

// ============================================================================
// Vendor input
// ============================================================================

void Game::update_vendor_input() {
    auto& v = hud_state_.vendor;
    if (!v.visible) {
        return;
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);

    if (vendor_esc_.just_pressed(keys[SDL_SCANCODE_ESCAPE])) {
        v.close();
        return;
    }

    if (!v.stock.empty()) {
        int count = static_cast<int>(v.stock.size());
        if (vendor_up_.just_pressed(keys[SDL_SCANCODE_UP]) && v.cursor > 0) {
            v.cursor--;
        }
        if (vendor_down_.just_pressed(keys[SDL_SCANCODE_DOWN]) && v.cursor < count - 1) {
            v.cursor++;
        }

        bool enter_active = keys[SDL_SCANCODE_RETURN] && !suppress_enter_this_frame_;
        if (vendor_enter_.just_pressed(enter_active)) {
            if (v.buying) {
                protocol::VendorBuyMsg msg;
                msg.npc_id = v.npc_id;
                msg.stock_index = static_cast<uint8_t>(v.cursor);
                msg.quantity = 1;
                network_.send_raw(protocol::build_packet(protocol::MessageType::VendorBuy, msg));
            } else if (v.sell_cursor >= 0) {
                protocol::VendorSellMsg msg;
                msg.npc_id = v.npc_id;
                msg.inventory_slot = static_cast<uint8_t>(v.sell_cursor);
                msg.quantity = 1;
                network_.send_raw(protocol::build_packet(protocol::MessageType::VendorSell, msg));
            }
        }
    }

    if (vendor_tab_.just_pressed(keys[SDL_SCANCODE_TAB])) {
        v.buying = !v.buying;
        v.cursor = 0;
        v.sell_cursor = v.buying ? -1 : 0;
    }
}

} // namespace mmo::client
