#include "server.hpp"
#include "asio/error_code.hpp"
#include "asio/io_context.hpp"
#include "protocol/protocol.hpp"
#include "server/game_config.hpp"
#include "server/game_types.hpp"
#include "server/session.hpp"
#include "server/ecs/game_components.hpp"
#include "systems/quest_system.hpp"
#include "systems/skill_system.hpp"
#include "systems/loot_system.hpp"
#include "systems/leveling_system.hpp"
#include <entt/entt.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mmo::server {

using namespace mmo::protocol;

Server::Server(asio::io_context& io_context, uint16_t port, const GameConfig& config)
    : io_context_(io_context)
    , acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    , config_(config)
    , world_(config)
    , tick_timer_(io_context)
    , last_tick_(std::chrono::steady_clock::now()) {
}

Server::~Server() {
    stop();
}

void Server::start() {
    running_ = true;
    accept();
    game_loop();
    std::cout << "Server started on port " << acceptor_.local_endpoint().port() << std::endl;
}

void Server::stop() {
    running_ = false;
    tick_timer_.cancel();
    acceptor_.close();
    
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        session->close();
    }
    sessions_.clear();
}

void Server::accept() {
    acceptor_.async_accept(
        [this](asio::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::cout << "New connection from " 
                          << socket.remote_endpoint().address().to_string() << std::endl;
                auto session = std::make_shared<Session>(std::move(socket), *this);
                session->start();
            }
            
            if (running_) {
                accept();
            }
        });
}

void Server::on_client_connect(std::shared_ptr<Session> session, const std::string& name) {
    session->set_player_name(name);
    std::cout << "Client '" << name << "' connected, sending class list" << std::endl;

    // Send connection accepted (player_id=0 means "not spawned yet, pick a class")
    {
        ConnectionAcceptedMsg msg;
        msg.player_id = 0;
        session->send(build_packet(MessageType::ConnectionAccepted, msg));
    }

    // Send world config so client knows dimensions, tick rate, etc.
    {
        NetWorldConfig wc;
        wc.world_width = config_.world().width;
        wc.world_height = config_.world().height;
        wc.tick_rate = config_.server().tick_rate;
        session->send(build_packet(MessageType::WorldConfig, wc));
    }

    // Send heightmap early so client can start loading terrain
    send_heightmap(session);

    // Build and send available classes from config
    {
        std::vector<ClassInfo> classes;
        for (int i = 0; i < config_.class_count(); ++i) {
            classes.push_back(config_.build_class_info(i));
        }
        session->send(build_packet(MessageType::ClassList, classes));
    }
}

void Server::on_class_select(std::shared_ptr<Session> session, uint8_t class_index) {
    PlayerClass player_class = static_cast<PlayerClass>(std::min(class_index, static_cast<uint8_t>(config_.class_count() - 1)));
    std::string name = session->player_name();

    uint32_t player_id = world_.add_player(name, player_class);
    session->set_player_id(player_id);

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[player_id] = session;
    }

    const auto& cls = config_.get_class(class_index);
    std::cout << "Player '" << name << "' (" << cls.name << ") spawned with ID " << player_id << std::endl;

    // Send real player ID now
    {
        ConnectionAcceptedMsg msg;
        msg.player_id = player_id;
        session->send(build_packet(MessageType::ConnectionAccepted, msg));
    }

    // Delta system will send EntityEnter for nearby entities on next tick
    // No need to send full WorldState - spatial filtering handles everything

    // Broadcast new player to others (they'll get EntityEnter for this new player)
    {
        auto player_state = world_.get_entity_state(player_id);
        broadcast_except(build_packet(MessageType::PlayerJoined, player_state), player_id);
    }

    // Announce arrival in chat.
    broadcast_system_chat(session->player_name() + " has entered the world.");

    // Tell client which NPCs have quests available for them
    send_quest_availability(player_id);

    // Send craftable recipes (level-gated) so the client can populate a UI.
    send_craft_recipes(player_id);

    // Send available skills for this class
    send_skill_list(player_id);

    // Send talent tree for this class, then initial talent state
    send_talent_tree(player_id);
    send_talent_sync(player_id);
}

void Server::on_player_disconnect(uint32_t player_id) {
    // Leave any party before clearing session state.
    on_party_leave(player_id);

    // Clear any invites they were part of.
    pending_invites_.erase(player_id);
    for (auto it = pending_invites_.begin(); it != pending_invites_.end(); ) {
        if (it->second == player_id) it = pending_invites_.erase(it);
        else ++it;
    }

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.erase(player_id);
    }

    {
        std::lock_guard<std::mutex> lock(client_views_mutex_);
        client_views_.erase(player_id);
    }

    world_.remove_player(player_id);

    std::cout << "Player " << player_id << " disconnected" << std::endl;

    {
        PlayerLeftMsg msg;
        msg.player_id = player_id;
        broadcast(build_packet(MessageType::PlayerLeft, msg));
    }
}

void Server::on_player_input(uint32_t player_id, const PlayerInput& input) {
    world_.update_player_input(player_id, input);
}

void Server::on_npc_interact(std::shared_ptr<Session> session, uint32_t player_id, uint32_t npc_id) {
    auto& registry = world_.registry();
    auto player_entity = world_.find_entity_by_network_id(player_id);
    auto npc_entity = world_.find_entity_by_network_id(npc_id);

    if (player_entity == entt::null || npc_entity == entt::null) return;
    if (!registry.all_of<ecs::EntityInfo, ecs::Name>(npc_entity)) return;

    auto& npc_info = registry.get<ecs::EntityInfo>(npc_entity);
    if (npc_info.type != protocol::EntityType::TownNPC) return;

    // Map npc_type index to string for quest lookup
    std::string npc_type = npc_type_to_string(npc_info.npc_type);

    // Auto turn-in any completable quests from this NPC type
    if (registry.all_of<ecs::QuestState>(player_entity)) {
        auto& quest_state = registry.get<ecs::QuestState>(player_entity);
        for (auto& active : quest_state.active_quests) {
            if (active.all_complete) {
                const auto* qcfg = config_.find_quest(active.quest_id);
                if (qcfg && qcfg->giver_type == npc_type) {
                    systems::turn_in_quest(registry, player_entity, active.quest_id, config_);
                }
            }
        }
    }

    // Get available quests for this NPC and send offers to client
    auto available = systems::get_available_quests(registry, player_entity, npc_type, config_);

    auto& npc_name = registry.get<ecs::Name>(npc_entity);

    // Send dialogue header so client knows how many quests to expect
    NPCDialogueMsg dlg;
    dlg.npc_id = npc_id;
    std::strncpy(dlg.npc_name, npc_name.value.c_str(), sizeof(dlg.npc_name) - 1);
    std::strncpy(dlg.dialogue, "What can I do for you?", sizeof(dlg.dialogue) - 1);
    dlg.quest_count = static_cast<uint8_t>(available.size());
    session->send(build_packet(MessageType::NPCDialogue, dlg));

    // If this NPC is a vendor, also send a VendorOpen so the client can show shop UI.
    if (const auto* vendor = config_.find_vendor(npc_type)) {
        VendorOpenMsg vmsg;
        vmsg.npc_id = npc_id;
        std::strncpy(vmsg.vendor_name, vendor->display_name.c_str(), sizeof(vmsg.vendor_name) - 1);
        vmsg.buy_price_multiplier = vendor->buy_price_multiplier;
        vmsg.sell_price_multiplier = vendor->sell_price_multiplier;
        uint8_t n = 0;
        for (const auto& sc : vendor->stock) {
            if (n >= VendorOpenMsg::MAX_STOCK) break;
            const ItemConfig* item = config_.find_item(sc.item_id);
            int price = sc.price > 0 ? sc.price :
                (item ? static_cast<int>(std::max(1.0f, item->sell_value * vendor->buy_price_multiplier)) : 1);
            std::strncpy(vmsg.stock[n].item_id, sc.item_id.c_str(), sizeof(vmsg.stock[n].item_id) - 1);
            vmsg.stock[n].price = price;
            vmsg.stock[n].stock = sc.stock;
            ++n;
        }
        vmsg.stock_count = n;
        session->send(build_packet(MessageType::VendorOpen, vmsg));
    }

    // Send each available quest as a QuestOffer
    for (const auto* quest : available) {
        QuestOfferMsg offer;
        std::strncpy(offer.quest_id, quest->id.c_str(), sizeof(offer.quest_id) - 1);
        std::strncpy(offer.quest_name, quest->name.c_str(), sizeof(offer.quest_name) - 1);
        std::strncpy(offer.dialogue, quest->dialogue.offer.c_str(), sizeof(offer.dialogue) - 1);
        offer.xp_reward = quest->rewards.xp;
        offer.gold_reward = quest->rewards.gold;
        offer.objective_count = static_cast<uint8_t>(
            std::min(quest->objectives.size(), static_cast<size_t>(QuestOfferMsg::MAX_OBJECTIVES)));
        for (int i = 0; i < offer.objective_count; ++i) {
            std::strncpy(offer.objectives[i].description,
                         quest->objectives[i].description.c_str(),
                         sizeof(offer.objectives[i].description) - 1);
            offer.objectives[i].count = quest->objectives[i].count;
            offer.objectives[i].location_x = quest->objectives[i].location_x;
            offer.objectives[i].location_z = quest->objectives[i].location_z;
            offer.objectives[i].radius = quest->objectives[i].radius;
        }
        session->send(build_packet(MessageType::QuestOffer, offer));
    }
}

void Server::on_quest_accept(uint32_t player_id, const std::string& quest_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;
    systems::accept_quest(registry, player, quest_id, config_);
    send_quest_availability(player_id);
}

void Server::on_quest_turnin(uint32_t player_id, const std::string& quest_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;
    systems::turn_in_quest(registry, player, quest_id, config_);
    send_quest_availability(player_id);
}

void Server::on_skill_use(uint32_t player_id, const std::string& skill_id, float dir_x, float dir_z) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    auto skill_result = systems::use_skill(registry, player, skill_id, dir_x, dir_z, config_);

    // Feed skill combat hits into the world's pending events for CombatEvent/EntityDeath
    if (!skill_result.hits.empty()) {
        world_.add_combat_hits(skill_result.hits);
    }

    // Send SkillCooldownMsg back to client
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit == sessions_.end() || !sit->second->is_open()) return;

    if (skill_result.success) {
        // Find the skill's slot index for the client
        auto unlocked = systems::get_unlocked_skills(registry, player, config_);
        uint16_t slot_idx = 0;
        const SkillConfig* skill_cfg = config_.find_skill(skill_id);
        for (uint16_t i = 0; i < unlocked.size() && i < 5; ++i) {
            if (unlocked[i]->id == skill_id) { slot_idx = i; break; }
        }

        // Send talent-modified cooldown, not raw config value
        float effective_cooldown = skill_cfg ? skill_cfg->cooldown : 0.0f;
        if (skill_cfg && registry.all_of<ecs::TalentPassiveState>(player)) {
            const auto& tp = registry.get<ecs::TalentPassiveState>(player);
            effective_cooldown = std::max(0.5f,
                effective_cooldown * tp.cooldown_mult * (1.0f - tp.global_cdr));
        }

        SkillCooldownMsg cd_msg;
        cd_msg.skill_id = slot_idx;
        cd_msg.cooldown_remaining = effective_cooldown;
        cd_msg.cooldown_total = effective_cooldown;
        sit->second->send(build_packet(MessageType::SkillCooldown, cd_msg));
    }
}

void Server::on_talent_unlock(uint32_t player_id, const std::string& talent_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    bool success = systems::unlock_talent(registry, player, talent_id, config_);
    if (success) {
        send_talent_sync(player_id);
    }
}

void Server::on_item_equip(uint32_t player_id, const std::string& item_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    bool success = systems::equip_item(registry, player, item_id, config_);
    if (success) {
        send_inventory_update(player_id);
    }
}

void Server::on_item_unequip(uint32_t player_id, const std::string& slot) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    bool success = systems::unequip_item(registry, player, slot, config_);
    if (success) {
        send_inventory_update(player_id);
    }
}

void Server::on_item_equip_by_slot(uint32_t player_id, uint8_t slot_index) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    auto* inv = registry.try_get<ecs::Inventory>(player);
    if (!inv || slot_index >= inv->used_slots) return;

    std::string item_id = inv->slots[slot_index].item_id;
    if (item_id.empty()) return;

    bool success = systems::equip_item(registry, player, item_id, config_);
    if (success) {
        send_inventory_update(player_id);
    }
}

void Server::on_item_unequip_slot(uint32_t player_id, uint8_t equip_slot) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    std::string slot = (equip_slot == 0) ? "weapon" : "armor";
    bool success = systems::unequip_item(registry, player, slot, config_);
    if (success) {
        send_inventory_update(player_id);
    }
}

void Server::on_item_use(uint32_t player_id, uint8_t slot_index) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    auto* inv = registry.try_get<ecs::Inventory>(player);
    if (!inv || slot_index >= inv->used_slots) return;

    std::string item_id = inv->slots[slot_index].item_id;
    if (item_id.empty()) return;

    bool success = systems::use_consumable(registry, player, item_id, config_);
    if (success) {
        send_inventory_update(player_id);
    }
}

void Server::on_chat_send(uint32_t player_id, uint8_t channel_raw, const std::string& message) {
    if (message.empty()) return;

    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    // Slash-command shortcuts. Non-chat commands never go on the wire as chat.
    auto send_system_to = [this](uint32_t pid, const std::string& text) {
        ChatBroadcastMsg out;
        out.channel = static_cast<uint8_t>(ChatChannel::System);
        out.sender_id = 0;
        std::strncpy(out.sender_name, "Server", sizeof(out.sender_name) - 1);
        std::strncpy(out.message, text.c_str(), sizeof(out.message) - 1);
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = sessions_.find(pid);
        if (sit != sessions_.end() && sit->second->is_open()) {
            sit->second->send(build_packet(MessageType::ChatBroadcast, out));
        }
    };

    if (!message.empty() && message[0] == '/') {
        auto space = message.find(' ');
        std::string cmd = message.substr(1, space == std::string::npos ? std::string::npos : space - 1);
        std::string rest = (space == std::string::npos) ? std::string{} : message.substr(space + 1);

        if (cmd == "who") {
            std::string reply = "Online: ";
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                bool first = true;
                for (auto& [id, s] : sessions_) {
                    if (!s || !s->is_open()) continue;
                    if (!first) reply += ", ";
                    reply += s->player_name();
                    first = false;
                }
            }
            send_system_to(player_id, reply);
            return;
        }

        // /w <name> <message>  (also /whisper, /tell)
        if (cmd == "w" || cmd == "whisper" || cmd == "tell") {
            auto sp = rest.find(' ');
            if (sp == std::string::npos || sp == 0) {
                send_system_to(player_id, "Usage: /w <player> <message>");
                return;
            }
            std::string target_name = rest.substr(0, sp);
            std::string body = rest.substr(sp + 1);
            uint32_t target_id = 0;
            std::string sender_name;
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                for (auto& [id, s] : sessions_) {
                    if (!s || !s->is_open()) continue;
                    if (s->player_name() == target_name) target_id = id;
                    if (id == player_id) sender_name = s->player_name();
                }
            }
            if (target_id == 0) {
                send_system_to(player_id, target_name + " is not online.");
                return;
            }

            ChatBroadcastMsg out;
            out.channel = static_cast<uint8_t>(ChatChannel::Whisper);
            out.sender_id = player_id;
            std::strncpy(out.sender_name, sender_name.c_str(), sizeof(out.sender_name) - 1);
            std::strncpy(out.message, body.c_str(), sizeof(out.message) - 1);
            auto packet = build_packet(MessageType::ChatBroadcast, out);
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto tit = sessions_.find(target_id);
            if (tit != sessions_.end() && tit->second->is_open()) tit->second->send(packet);
            // Also echo back to sender so they see the whisper in their log.
            auto sit = sessions_.find(player_id);
            if (sit != sessions_.end() && sit->second->is_open()) sit->second->send(packet);
            return;
        }
    }

    // Clamp to one of the allowed channels.
    ChatChannel channel = static_cast<ChatChannel>(channel_raw);
    if (channel_raw > static_cast<uint8_t>(ChatChannel::Whisper)) channel = ChatChannel::Zone;

    ChatBroadcastMsg out;
    out.channel = static_cast<uint8_t>(channel);
    out.sender_id = player_id;
    std::string name;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = sessions_.find(player_id);
        if (sit != sessions_.end()) name = sit->second->player_name();
    }
    std::strncpy(out.sender_name, name.c_str(), sizeof(out.sender_name) - 1);
    std::strncpy(out.message, message.c_str(), sizeof(out.message) - 1);

    auto packet = build_packet(MessageType::ChatBroadcast, out);

    if (channel == ChatChannel::Global || channel == ChatChannel::System) {
        broadcast(packet);
        return;
    }

    // Zone/Say: route to players in the same zone as sender (by zone config lookup).
    auto* sender_tx = registry.try_get<ecs::Transform>(player);
    if (!sender_tx) { broadcast(packet); return; }
    const auto* sender_zone = config_.find_zone_at(sender_tx->x, sender_tx->z);
    std::string sender_zone_id = sender_zone ? sender_zone->id : std::string{};

    const float say_range = 600.0f;  // Local "say" radius in world units
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        if (!session->is_open()) continue;
        auto other = world_.find_entity_by_network_id(id);
        if (other == entt::null) continue;
        auto* otx = registry.try_get<ecs::Transform>(other);
        if (!otx) continue;
        if (channel == ChatChannel::Say) {
            float dx = otx->x - sender_tx->x;
            float dz = otx->z - sender_tx->z;
            if (dx * dx + dz * dz > say_range * say_range) continue;
        } else { // Zone
            const auto* z = config_.find_zone_at(otx->x, otx->z);
            std::string other_zone = z ? z->id : std::string{};
            if (other_zone != sender_zone_id) continue;
        }
        session->send(packet);
    }
}

void Server::broadcast_system_chat(const std::string& message) {
    ChatBroadcastMsg out;
    out.channel = static_cast<uint8_t>(ChatChannel::System);
    out.sender_id = 0;
    std::strncpy(out.sender_name, "Server", sizeof(out.sender_name) - 1);
    std::strncpy(out.message, message.c_str(), sizeof(out.message) - 1);
    broadcast(build_packet(MessageType::ChatBroadcast, out));
}

void Server::on_vendor_buy(uint32_t player_id, uint32_t npc_id, uint8_t stock_index, uint8_t quantity) {
    if (quantity == 0) quantity = 1;
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    auto npc = world_.find_entity_by_network_id(npc_id);
    if (player == entt::null || npc == entt::null) return;
    if (!registry.all_of<ecs::EntityInfo>(npc)) return;

    auto& info = registry.get<ecs::EntityInfo>(npc);
    if (info.type != protocol::EntityType::TownNPC) return;
    const auto* vendor = config_.find_vendor(npc_type_to_string(info.npc_type));
    if (!vendor) return;
    if (stock_index >= vendor->stock.size()) return;

    // Require player to be near the NPC.
    auto* ptx = registry.try_get<ecs::Transform>(player);
    auto* ntx = registry.try_get<ecs::Transform>(npc);
    if (!ptx || !ntx) return;
    float dx = ptx->x - ntx->x, dz = ptx->z - ntx->z;
    if (dx * dx + dz * dz > 200.0f * 200.0f) return;

    const auto& stock = vendor->stock[stock_index];
    const ItemConfig* item = config_.find_item(stock.item_id);
    if (!item) return;

    int unit_price = stock.price > 0 ? stock.price :
        static_cast<int>(std::max(1.0f, item->sell_value * vendor->buy_price_multiplier));
    int total = unit_price * quantity;

    auto* level = registry.try_get<ecs::PlayerLevel>(player);
    auto* inv = registry.try_get<ecs::Inventory>(player);
    if (!level || !inv) return;
    if (level->gold < total) return;

    if (!inv->add_item(item->id, quantity, item->stack_size)) return;
    level->gold -= total;

    send_inventory_update(player_id);
    // Notify the client's gold changed
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit != sessions_.end() && sit->second->is_open()) {
        std::vector<uint8_t> payload(2 * sizeof(int32_t));
        BufferWriter w(std::span<uint8_t>(payload.data(), payload.size()));
        w.write<int32_t>(-total);
        w.write<int32_t>(level->gold);
        sit->second->send(build_packet(MessageType::GoldChange, payload));
    }
}

void Server::on_vendor_sell(uint32_t player_id, uint32_t npc_id, uint8_t inventory_slot, uint8_t quantity) {
    if (quantity == 0) quantity = 1;
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    auto npc = world_.find_entity_by_network_id(npc_id);
    if (player == entt::null || npc == entt::null) return;
    if (!registry.all_of<ecs::EntityInfo>(npc)) return;

    auto& info = registry.get<ecs::EntityInfo>(npc);
    if (info.type != protocol::EntityType::TownNPC) return;
    const auto* vendor = config_.find_vendor(npc_type_to_string(info.npc_type));
    if (!vendor) return;

    auto* ptx = registry.try_get<ecs::Transform>(player);
    auto* ntx = registry.try_get<ecs::Transform>(npc);
    if (!ptx || !ntx) return;
    float dx = ptx->x - ntx->x, dz = ptx->z - ntx->z;
    if (dx * dx + dz * dz > 200.0f * 200.0f) return;

    auto* inv = registry.try_get<ecs::Inventory>(player);
    auto* level = registry.try_get<ecs::PlayerLevel>(player);
    if (!inv || !level) return;
    if (inventory_slot >= inv->used_slots) return;

    std::string item_id = inv->slots[inventory_slot].item_id;
    if (item_id.empty()) return;
    int have = inv->slots[inventory_slot].count;
    int sell_count = std::min<int>(quantity, have);

    const ItemConfig* item = config_.find_item(item_id);
    if (!item) return;
    int unit = static_cast<int>(std::max(1.0f, item->sell_value * vendor->sell_price_multiplier));
    int total = unit * sell_count;

    inv->remove_item(item_id, sell_count);
    level->gold += total;

    send_inventory_update(player_id);
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit != sessions_.end() && sit->second->is_open()) {
        std::vector<uint8_t> payload(2 * sizeof(int32_t));
        BufferWriter w(std::span<uint8_t>(payload.data(), payload.size()));
        w.write<int32_t>(total);
        w.write<int32_t>(level->gold);
        sit->second->send(build_packet(MessageType::GoldChange, payload));
    }
}

void Server::broadcast(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        if (session->is_open()) {
            session->send(data);
        }
    }
}

void Server::broadcast_except(const std::vector<uint8_t>& data, uint32_t exclude_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        if (id != exclude_id && session->is_open()) {
            session->send(data);
        }
    }
}

void Server::game_loop() {
    if (!running_) return;

    // Initialize absolute tick time on first call
    if (next_tick_time_ == std::chrono::steady_clock::time_point{}) {
        next_tick_time_ = std::chrono::steady_clock::now();
    }

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_tick_).count();
    last_tick_ = now;

    world_.update(dt);

    // Heartbeat: send Ping and check for Pong timeouts
    ping_timer_ += dt;
    if (ping_timer_ >= PING_INTERVAL) {
        ping_timer_ -= PING_INTERVAL;
        auto ping_packet = build_packet(MessageType::Ping, std::vector<uint8_t>{});
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        std::vector<uint32_t> timed_out;
        for (auto& [id, session] : sessions_) {
            if (!session->is_open()) continue;
            auto elapsed = std::chrono::steady_clock::now() - session->last_pong_time();
            if (std::chrono::duration<float>(elapsed).count() > PONG_TIMEOUT) {
                std::cout << "Player " << id << " timed out (no Pong), disconnecting" << std::endl;
                timed_out.push_back(id);
            } else {
                session->send(ping_packet);
            }
        }
        for (uint32_t id : timed_out) {
            auto it = sessions_.find(id);
            if (it != sessions_.end()) {
                it->second->close();
            }
        }
    }

    // Send progression events (XP, loot, level ups, zone changes) to clients
    send_progression_updates();

    // Broadcast party frames (HP/mana) periodically.
    party_broadcast_timer_ += dt;
    if (party_broadcast_timer_ >= PARTY_BROADCAST_INTERVAL) {
        party_broadcast_timer_ = 0.0f;
        for (auto& [pid, party] : parties_) {
            send_party_state_to_all(pid);
        }
    }

    // Use delta compression instead of full world state broadcasts
    send_entity_deltas();
    // broadcast_world_state();  // Old method - kept for reference/rollback

    float tick_duration = 1.0f / config_.server().tick_rate;
    next_tick_time_ += std::chrono::microseconds(static_cast<int>(tick_duration * 1000000));
    tick_timer_.expires_at(next_tick_time_);
    tick_timer_.async_wait([this](asio::error_code ec) {
        if (!ec && running_) {
            game_loop();
        }
    });
}

void Server::send_quest_availability(uint32_t player_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit == sessions_.end() || !sit->second->is_open()) return;

    // Collect NPC network IDs that have quests available for this player
    std::vector<uint32_t> npc_ids_with_quests;

    auto npc_view = registry.view<ecs::NetworkId, ecs::EntityInfo, ecs::Name>();
    for (auto entity : npc_view) {
        auto& info = npc_view.get<ecs::EntityInfo>(entity);
        if (info.type != protocol::EntityType::TownNPC) continue;

        // Map NPC type to quest giver type string
        std::string npc_type = npc_type_to_string(info.npc_type);

        auto available = systems::get_available_quests(registry, player, npc_type, config_);

        // Also check for completable quests
        bool has_completable = false;
        if (registry.all_of<ecs::QuestState>(player)) {
            auto& qs = registry.get<ecs::QuestState>(player);
            for (auto& active : qs.active_quests) {
                if (!active.all_complete) continue;
                const auto* qcfg = config_.find_quest(active.quest_id);
                if (qcfg && qcfg->giver_type == npc_type) {
                    has_completable = true;
                    break;
                }
            }
        }

        if (!available.empty() || has_completable) {
            auto& net_id = npc_view.get<ecs::NetworkId>(entity);
            // Encode: high bit = has completable quest (show "?"), otherwise "!"
            uint32_t encoded_id = net_id.id;
            if (has_completable) encoded_id |= 0x80000000;
            npc_ids_with_quests.push_back(encoded_id);
        }
    }

    // Send QuestList message: count(u16) + npc_ids(u32 each)
    std::vector<uint8_t> data;
    BufferWriter w(data);
    PacketHeader hdr;
    hdr.type = MessageType::QuestList;
    uint16_t count = static_cast<uint16_t>(std::min(npc_ids_with_quests.size(), static_cast<size_t>(100)));
    hdr.payload_size = sizeof(uint16_t) + count * sizeof(uint32_t);
    hdr.serialize(w);
    w.write(count);
    for (uint16_t i = 0; i < count; ++i) {
        w.write(npc_ids_with_quests[i]);
    }
    sit->second->send(data);
}

void Server::broadcast_world_state() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Per-client spatial filtering
    for (auto& [client_id, session] : sessions_) {
        if (!session->is_open()) continue;

        // Get client's player entity and position
        auto player_entity = world_.find_entity_by_network_id(client_id);
        if (player_entity == entt::null) continue;

        const auto& transform = world_.registry().get<ecs::Transform>(player_entity);
        const auto& network_config = config_.network();

        // Query entities near this client using spatial grid
        // Use max view distance to get all potentially visible entities
        auto nearby_ids = world_.query_entities_near(transform.x, transform.z, network_config.max_view_distance());

        // Get entity states for nearby entities only and filter by per-entity view distance
        std::vector<protocol::NetEntityState> visible_entities;

        for (uint32_t entity_id : nearby_ids) {
            // Get entity state for this specific entity (much faster than querying all!)
            auto entity_state = world_.get_entity_state(entity_id);
            if (entity_state.id == 0) continue;  // Skip if entity not found

            // Calculate distance to client
            float dx = entity_state.x - transform.x;
            float dz = entity_state.z - transform.z;
            float dist_sq = dx*dx + dz*dz;

            // Get view distance for this entity type (directly from type, no lookup!)
            float view_dist = network_config.get_view_distance_for_type(entity_state.type);

            // Check if within view distance
            if (dist_sq <= view_dist * view_dist) {
                visible_entities.push_back(entity_state);
            }
        }

        // WorldState is no longer sent - the delta compression system
        // (EntityEnter/EntityUpdate/EntityExit) handles all entity sync.
        (void)visible_entities;
    }
}

void Server::send_heightmap(std::shared_ptr<Session> session) {
    const auto& heightmap = world_.heightmap();

    std::vector<uint8_t> payload;
    heightmap.serialize(payload);
    auto data = build_packet(MessageType::HeightmapChunk, payload);

    std::cout << "[Server] Sending heightmap to player " << session->player_id()
              << " (" << data.size() / 1024 << " KB)" << std::endl;

    session->send(data);
}

void Server::send_entity_deltas() {
    std::lock_guard<std::mutex> sessions_lock(sessions_mutex_);
    std::lock_guard<std::mutex> views_lock(client_views_mutex_);

    for (auto& [client_id, session] : sessions_) {
        if (!session->is_open()) continue;

        // Get or create client view state
        if (client_views_.find(client_id) == client_views_.end()) {
            client_views_[client_id] = std::make_unique<ClientViewState>(client_id);
        }
        auto& view = client_views_[client_id];

        // Get client position
        auto player_entity = world_.find_entity_by_network_id(client_id);
        if (player_entity == entt::null) continue;

        const auto& transform = world_.registry().get<ecs::Transform>(player_entity);
        const auto& network_config = config_.network();

        // Query visible entities (spatial partitioning with tiered view distances)
        auto nearby_ids = world_.query_entities_near(transform.x, transform.z, network_config.max_view_distance());

        // Build state cache: call get_entity_state once per entity
        std::unordered_map<uint32_t, protocol::NetEntityState> state_cache;
        state_cache.reserve(nearby_ids.size());
        for (uint32_t entity_id : nearby_ids) {
            auto entity_state = world_.get_entity_state(entity_id);
            if (entity_state.id == 0) continue;
            state_cache.emplace(entity_id, entity_state);
        }

        // Filter by per-entity view distance
        std::vector<uint32_t> visible_now;
        visible_now.reserve(state_cache.size());

        for (auto& [entity_id, entity_state] : state_cache) {
            float dx = entity_state.x - transform.x;
            float dz = entity_state.z - transform.z;
            float dist_sq = dx*dx + dz*dz;

            float view_dist = network_config.get_view_distance_for_type(entity_state.type);

            if (dist_sq <= view_dist * view_dist) {
                visible_now.push_back(entity_id);
            }
        }

        const auto& known = view->known_entities();

        // ENTER: in visible_now but not in known
        for (uint32_t id : visible_now) {
            if (!view->knows_entity(id)) {
                const auto& state = state_cache[id];
                session->send(build_packet(MessageType::EntityEnter, state));
                view->add_known_entity(id, state);
            }
        }

        // EXIT: in known but not in visible_now
        // Sort visible_now and use binary_search instead of building an unordered_set
        std::sort(visible_now.begin(), visible_now.end());
        std::vector<uint32_t> to_remove;
        for (uint32_t id : known) {
            if (!std::binary_search(visible_now.begin(), visible_now.end(), id)) {
                EntityExitMsg msg;
                msg.entity_id = id;
                session->send(build_packet(MessageType::EntityExit, msg));
                to_remove.push_back(id);
            }
        }
        for (uint32_t id : to_remove) {
            view->remove_known_entity(id);
        }

        // UPDATE: in both (check for changes)
        for (uint32_t id : visible_now) {
            if (!view->knows_entity(id)) continue;  // Just entered, skip update

            const auto& current_state = state_cache[id];
            auto last_state = view->get_last_state(id);
            if (!last_state) continue;

            // Check if anything changed
            if (!has_changes(current_state, *last_state)) {
                continue;  // No update needed
            }

            // Rate limiting
            float min_interval = get_update_interval(current_state.type);
            if (!view->can_send_update(id, min_interval)) {
                continue;  // Too soon since last update
            }

            // Build delta update
            auto delta = create_delta(current_state, *last_state);

            // Only send if there are actual changes flagged
            if (delta.flags != 0) {
                session->send(build_packet(MessageType::EntityUpdate, delta));

                view->update_last_state(id, current_state);
                view->mark_sent(id);
            }
        }
    }
}

void Server::send_inventory_update(uint32_t player_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit == sessions_.end() || !sit->second->is_open()) return;

    auto* inv = registry.try_get<ecs::Inventory>(player);
    auto* equip = registry.try_get<ecs::Equipment>(player);

    // Helper to map string item_id to uint16_t index (1-based, 0 = empty)
    auto item_to_index = [this](const std::string& item_id) -> uint16_t {
        if (item_id.empty()) return 0;
        const auto& items = config_.items();
        for (uint16_t i = 0; i < items.size(); ++i) {
            if (items[i].id == item_id) return i + 1;  // 1-based
        }
        return 0;
    };

    InventoryUpdateMsg msg;
    msg.slot_count = inv ? static_cast<uint8_t>(inv->used_slots) : 0;
    for (int i = 0; i < msg.slot_count && i < InventoryUpdateMsg::MAX_SLOTS; ++i) {
        msg.slots[i].item_id = item_to_index(inv->slots[i].item_id);
        msg.slots[i].count = static_cast<uint16_t>(inv->slots[i].count);
    }
    msg.equipped_weapon = equip ? item_to_index(equip->weapon_id) : 0;
    msg.equipped_armor = equip ? item_to_index(equip->armor_id) : 0;

    sit->second->send(build_packet(MessageType::InventoryUpdate, msg));
}

void Server::send_talent_sync(uint32_t player_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit == sessions_.end() || !sit->second->is_open()) return;

    auto* talent_state = registry.try_get<ecs::TalentState>(player);
    if (!talent_state) return;

    TalentSyncMsg msg;
    msg.talent_points = talent_state->talent_points;
    msg.unlocked_count = static_cast<uint8_t>(
        std::min(talent_state->unlocked_talents.size(), static_cast<size_t>(TalentSyncMsg::MAX_TALENTS)));
    for (int i = 0; i < msg.unlocked_count; ++i) {
        std::strncpy(msg.unlocked_ids[i], talent_state->unlocked_talents[i].c_str(),
                     sizeof(msg.unlocked_ids[i]) - 1);
    }
    sit->second->send(build_packet(MessageType::TalentSync, msg));
}

void Server::send_talent_tree(uint32_t player_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit == sessions_.end() || !sit->second->is_open()) return;

    auto* info = registry.try_get<ecs::EntityInfo>(player);
    if (!info) return;

    const char* player_class = systems::class_name_for_index(info->player_class);
    TalentTreeMsg msg;
    int idx = 0;
    for (const auto& tree : config_.talent_trees()) {
        if (tree.class_name != player_class) continue;
        for (const auto& branch : tree.branches) {
            for (const auto& t : branch.talents) {
                if (idx >= TalentTreeMsg::MAX_TALENTS) break;
                std::strncpy(msg.talents[idx].id, t.id.c_str(), 31);
                std::strncpy(msg.talents[idx].name, t.name.c_str(), 31);
                std::strncpy(msg.talents[idx].description, t.description.c_str(), 127);
                msg.talents[idx].tier = static_cast<uint8_t>(t.tier);
                std::strncpy(msg.talents[idx].prerequisite, t.prerequisite.c_str(), 31);
                std::strncpy(msg.talents[idx].branch_name, branch.name.c_str(), 31);
                idx++;
            }
        }
    }
    msg.talent_count = static_cast<uint8_t>(idx);
    sit->second->send(build_packet(MessageType::TalentTree, msg));
}

void Server::send_skill_list(uint32_t player_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit == sessions_.end() || !sit->second->is_open()) return;

    auto unlocked = systems::get_unlocked_skills(registry, player, config_);

    // Send all unlocked skills in a single SkillUnlockMsg
    SkillUnlockMsg msg;
    msg.skill_count = static_cast<uint8_t>(
        std::min(unlocked.size(), static_cast<size_t>(SkillUnlockMsg::MAX_SKILLS)));
    for (int i = 0; i < msg.skill_count; ++i) {
        msg.skills[i].skill_id = static_cast<uint16_t>(i);
        std::strncpy(msg.skills[i].name, unlocked[i]->id.c_str(), sizeof(msg.skills[i].name) - 1);
        std::strncpy(msg.skills[i].display_name, unlocked[i]->name.c_str(), sizeof(msg.skills[i].display_name) - 1);
    }
    sit->second->send(build_packet(MessageType::SkillUnlock, msg));
}

void Server::send_progression_updates() {
    auto events = world_.take_events();
    if (events.empty()) return;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& evt : events) {
        auto sit = sessions_.find(evt.player_id);
        if (sit == sessions_.end() || !sit->second->is_open()) continue;

        switch (evt.type) {
            case World::GameplayEvent::Type::XPGain: {
                XPGainMsg msg;
                msg.xp_gained = evt.xp_gained;
                msg.total_xp = evt.total_xp;
                msg.xp_to_next = evt.xp_to_next;
                msg.current_level = evt.new_level;
                sit->second->send(build_packet(MessageType::XPGain, msg));
                break;
            }
            case World::GameplayEvent::Type::LevelUp: {
                LevelUpMsg msg;
                msg.new_level = evt.new_level;
                msg.new_max_health = evt.new_max_health;
                msg.new_damage = evt.new_damage;
                sit->second->send(build_packet(MessageType::LevelUp, msg));

                // On level up, send updated skill list (new skills may have unlocked)
                // Note: sessions_mutex_ is already held, so call send directly
                {
                    auto player = world_.find_entity_by_network_id(evt.player_id);
                    if (player != entt::null) {
                        auto unlocked = systems::get_unlocked_skills(world_.registry(), player, config_);
                        SkillUnlockMsg skill_msg;
                        skill_msg.skill_count = static_cast<uint8_t>(
                            std::min(unlocked.size(), static_cast<size_t>(SkillUnlockMsg::MAX_SKILLS)));
                        for (int si = 0; si < skill_msg.skill_count; ++si) {
                            skill_msg.skills[si].skill_id = static_cast<uint16_t>(si);
                            std::strncpy(skill_msg.skills[si].name, unlocked[si]->id.c_str(),
                                         sizeof(skill_msg.skills[si].name) - 1);
                            std::strncpy(skill_msg.skills[si].display_name, unlocked[si]->name.c_str(),
                                         sizeof(skill_msg.skills[si].display_name) - 1);
                        }
                        sit->second->send(build_packet(MessageType::SkillUnlock, skill_msg));
                    }
                }

                // Send updated talent points (inline — sessions_mutex_ already held)
                {
                    auto player = world_.find_entity_by_network_id(evt.player_id);
                    if (player != entt::null) {
                        auto* talent_state = world_.registry().try_get<ecs::TalentState>(player);
                        if (talent_state) {
                            TalentSyncMsg talent_msg;
                            talent_msg.talent_points = talent_state->talent_points;
                            talent_msg.unlocked_count = static_cast<uint8_t>(
                                std::min(talent_state->unlocked_talents.size(),
                                         static_cast<size_t>(TalentSyncMsg::MAX_TALENTS)));
                            for (int ti = 0; ti < talent_msg.unlocked_count; ++ti) {
                                std::strncpy(talent_msg.unlocked_ids[ti],
                                             talent_state->unlocked_talents[ti].c_str(),
                                             sizeof(talent_msg.unlocked_ids[ti]) - 1);
                            }
                            sit->second->send(build_packet(MessageType::TalentSync, talent_msg));
                        }
                    }
                }
                break;
            }
            case World::GameplayEvent::Type::LootDrop: {
                std::vector<uint8_t> payload;
                BufferWriter pw(payload);
                pw.write(static_cast<int32_t>(evt.loot_gold));
                pw.write(static_cast<int32_t>(evt.total_gold));
                uint8_t count = static_cast<uint8_t>(std::min(evt.loot_items.size(), static_cast<size_t>(5)));
                pw.write(count);
                for (int i = 0; i < count; ++i) {
                    pw.write_fixed_string(evt.loot_items[i].name, 32);
                    pw.write_fixed_string(evt.loot_items[i].rarity, 16);
                    pw.write(static_cast<uint8_t>(evt.loot_items[i].count));
                }
                sit->second->send(build_packet(MessageType::LootDrop, payload));
                break;
            }
            case World::GameplayEvent::Type::ZoneChange: {
                ZoneChangeMsg msg;
                std::strncpy(msg.zone_name, evt.zone_name.c_str(), sizeof(msg.zone_name) - 1);
                sit->second->send(build_packet(MessageType::ZoneChange, msg));
                break;
            }
            case World::GameplayEvent::Type::QuestProgress: {
                QuestProgressMsg msg;
                std::strncpy(msg.quest_id, evt.quest_id.c_str(), sizeof(msg.quest_id) - 1);
                msg.objective_index = evt.objective_index;
                msg.current = evt.obj_current;
                msg.required = evt.obj_required;
                msg.complete = evt.obj_complete ? 1 : 0;
                sit->second->send(build_packet(MessageType::QuestProgress, msg));
                break;
            }
            case World::GameplayEvent::Type::QuestComplete: {
                QuestCompleteMsg msg;
                std::strncpy(msg.quest_id, evt.quest_id.c_str(), sizeof(msg.quest_id) - 1);
                std::strncpy(msg.quest_name, evt.quest_name.c_str(), sizeof(msg.quest_name) - 1);
                sit->second->send(build_packet(MessageType::QuestComplete, msg));
                break;
            }
            case World::GameplayEvent::Type::InventoryUpdate: {
                // Build InventoryUpdateMsg using same struct format as send_inventory_update
                auto player = world_.find_entity_by_network_id(evt.player_id);
                if (player != entt::null) {
                    auto& registry = world_.registry();
                    auto* inv = registry.try_get<ecs::Inventory>(player);
                    auto* equip = registry.try_get<ecs::Equipment>(player);

                    auto item_to_index = [this](const std::string& item_id) -> uint16_t {
                        if (item_id.empty()) return 0;
                        const auto& items = config_.items();
                        for (uint16_t i = 0; i < items.size(); ++i) {
                            if (items[i].id == item_id) return i + 1;
                        }
                        return 0;
                    };

                    InventoryUpdateMsg inv_msg;
                    inv_msg.slot_count = inv ? static_cast<uint8_t>(inv->used_slots) : 0;
                    for (int i = 0; i < inv_msg.slot_count && i < InventoryUpdateMsg::MAX_SLOTS; ++i) {
                        inv_msg.slots[i].item_id = item_to_index(inv->slots[i].item_id);
                        inv_msg.slots[i].count = static_cast<uint16_t>(inv->slots[i].count);
                    }
                    inv_msg.equipped_weapon = equip ? item_to_index(equip->weapon_id) : 0;
                    inv_msg.equipped_armor = equip ? item_to_index(equip->armor_id) : 0;
                    sit->second->send(build_packet(MessageType::InventoryUpdate, inv_msg));
                }
                break;
            }
            case World::GameplayEvent::Type::CombatEvent: {
                // Send combat event: attacker(u32) + target(u32) + damage(f32)
                std::vector<uint8_t> payload;
                BufferWriter pw(payload);
                pw.write(evt.attacker_id);
                pw.write(evt.target_id);
                pw.write(evt.damage_amount);
                sit->second->send(build_packet(MessageType::CombatEvent, payload));
                break;
            }
            case World::GameplayEvent::Type::EntityDeath: {
                // Send entity death: dead_id(u32) + killer_id(u32)
                std::vector<uint8_t> payload;
                BufferWriter pw(payload);
                pw.write(evt.dead_entity_id);
                pw.write(evt.killer_entity_id);
                sit->second->send(build_packet(MessageType::EntityDeath, payload));
                break;
            }
        }
    }
}

float Server::get_update_interval(protocol::EntityType type) const {
    // Rate limiting: max update frequency per entity type
    switch(type) {
        case protocol::EntityType::Building:    return 999.0f;  // Never (static)
        case protocol::EntityType::Environment: return 999.0f;  // Never (static)
        case protocol::EntityType::Player:      return 0.0167f;   // 60 Hz
        case protocol::EntityType::NPC:         return 0.033f;    // 30 Hz
        case protocol::EntityType::TownNPC:     return 0.2f;      // 5 Hz
        default: return 0.05f;
    }
}

bool Server::has_changes(const protocol::NetEntityState& current,
                         const protocol::NetEntityState& last) const {
    return current.x != last.x ||
           current.y != last.y ||
           current.z != last.z ||
           current.vx != last.vx ||
           current.vy != last.vy ||
           current.health != last.health ||
           current.max_health != last.max_health ||
           current.is_attacking != last.is_attacking ||
           current.attack_dir_x != last.attack_dir_x ||
           current.attack_dir_y != last.attack_dir_y ||
           current.rotation != last.rotation ||
           current.mana != last.mana;
}

protocol::EntityDeltaUpdate Server::create_delta(const protocol::NetEntityState& current,
                                                  const protocol::NetEntityState& last) const {
    protocol::EntityDeltaUpdate delta;
    delta.id = current.id;
    delta.flags = 0;

    if (current.x != last.x || current.y != last.y || current.z != last.z) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_POSITION;
        delta.x = current.x;
        delta.y = current.y;
        delta.z = current.z;
    }

    if (current.vx != last.vx || current.vy != last.vy) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_VELOCITY;
        delta.vx = current.vx;
        delta.vy = current.vy;
    }

    if (current.health != last.health) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_HEALTH;
        delta.health = current.health;
    }

    if (current.max_health != last.max_health) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_MAX_HEALTH;
        delta.max_health = current.max_health;
    }

    if (current.is_attacking != last.is_attacking) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_ATTACKING;
        delta.is_attacking = current.is_attacking ? 1 : 0;
    }

    if (current.attack_dir_x != last.attack_dir_x ||
        current.attack_dir_y != last.attack_dir_y) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_ATTACK_DIR;
        delta.attack_dir_x = current.attack_dir_x;
        delta.attack_dir_y = current.attack_dir_y;
    }

    if (current.rotation != last.rotation) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_ROTATION;
        delta.rotation = current.rotation;
    }

    if (current.mana != last.mana) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_MANA;
        delta.mana = current.mana;
    }

    return delta;
}

// ============================================================================
// Party system
// ============================================================================

uint32_t Server::find_party_id(uint32_t player_id) const {
    auto it = player_party_.find(player_id);
    return it != player_party_.end() ? it->second : 0;
}

void Server::send_party_state(uint32_t player_id) {
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = sessions_.find(player_id);
        if (sit == sessions_.end() || !sit->second->is_open()) return;
        session = sit->second;
    }

    PartyStateMsg msg;
    uint32_t party_id = find_party_id(player_id);
    auto pit = parties_.find(party_id);
    if (party_id == 0 || pit == parties_.end()) {
        msg.leader_id = 0;
        msg.member_count = 0;
        session->send(build_packet(MessageType::PartyState, msg));
        return;
    }

    const auto& party = pit->second;
    msg.leader_id = party.leader_id;
    uint8_t count = 0;
    auto& registry = world_.registry();
    for (uint32_t mid : party.member_ids) {
        if (count >= PartyStateMsg::MAX_MEMBERS) break;
        auto& m = msg.members[count];
        m.player_id = mid;
        std::string mname;
        uint8_t pclass = 0;
        {
            std::lock_guard<std::mutex> slock(sessions_mutex_);
            auto sit = sessions_.find(mid);
            if (sit != sessions_.end()) mname = sit->second->player_name();
        }
        std::strncpy(m.name, mname.c_str(), sizeof(m.name) - 1);

        auto ent = world_.find_entity_by_network_id(mid);
        if (ent != entt::null) {
            if (auto* info = registry.try_get<ecs::EntityInfo>(ent)) pclass = info->player_class;
            if (auto* hp = registry.try_get<ecs::Health>(ent)) {
                m.health = hp->current;
                m.max_health = hp->max;
            }
            if (auto* lvl = registry.try_get<ecs::PlayerLevel>(ent)) {
                m.level = static_cast<uint8_t>(std::min(lvl->level, 255));
                m.mana = lvl->mana;
                m.max_mana = lvl->max_mana;
            }
        }
        m.player_class = pclass;
        ++count;
    }
    msg.member_count = count;
    session->send(build_packet(MessageType::PartyState, msg));
}

void Server::send_party_state_to_all(uint32_t party_id) {
    auto pit = parties_.find(party_id);
    if (pit == parties_.end()) return;
    for (uint32_t mid : pit->second.member_ids) {
        send_party_state(mid);
    }
}

void Server::disband_party_if_small(uint32_t party_id) {
    auto pit = parties_.find(party_id);
    if (pit == parties_.end()) return;
    if (pit->second.member_ids.size() >= 2) return;

    // Solo party - disband.
    for (uint32_t mid : pit->second.member_ids) {
        player_party_.erase(mid);
        // Send empty state to signal left-party.
        PartyStateMsg empty;
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = sessions_.find(mid);
        if (sit != sessions_.end() && sit->second->is_open()) {
            sit->second->send(build_packet(MessageType::PartyState, empty));
        }
    }
    parties_.erase(pit);
}

void Server::on_party_invite(uint32_t player_id, const std::string& target_name) {
    // Find the target player by name.
    uint32_t target_id = 0;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& [id, s] : sessions_) {
            if (!s || !s->is_open()) continue;
            if (s->player_name() == target_name) { target_id = id; break; }
        }
    }
    if (target_id == 0 || target_id == player_id) return;
    // Can't invite someone who's already in a party, or invite while our
    // party is full.
    if (find_party_id(target_id) != 0) return;
    uint32_t my_party = find_party_id(player_id);
    if (my_party != 0) {
        auto pit = parties_.find(my_party);
        if (pit != parties_.end()
            && pit->second.member_ids.size() >= PartyStateMsg::MAX_MEMBERS) return;
        if (pit != parties_.end() && pit->second.leader_id != player_id) return;
    }

    pending_invites_[player_id] = target_id;

    // Send offer to target.
    PartyInviteOfferMsg offer;
    offer.inviter_id = player_id;
    std::string my_name;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = sessions_.find(player_id);
        if (sit != sessions_.end()) my_name = sit->second->player_name();
        auto tit = sessions_.find(target_id);
        if (tit != sessions_.end() && tit->second->is_open()) {
            std::strncpy(offer.inviter_name, my_name.c_str(), sizeof(offer.inviter_name) - 1);
            tit->second->send(build_packet(MessageType::PartyInviteOffer, offer));
        }
    }
}

void Server::on_party_invite_respond(uint32_t player_id, uint32_t inviter_id, bool accept) {
    auto it = pending_invites_.find(inviter_id);
    if (it == pending_invites_.end() || it->second != player_id) return;
    pending_invites_.erase(it);
    if (!accept) return;

    // Form or join a party.
    uint32_t party_id = find_party_id(inviter_id);
    if (party_id == 0) {
        // Create new party with inviter as leader.
        party_id = next_party_id_++;
        Party p;
        p.id = party_id;
        p.leader_id = inviter_id;
        p.member_ids.push_back(inviter_id);
        parties_[party_id] = std::move(p);
        player_party_[inviter_id] = party_id;
    }

    auto pit = parties_.find(party_id);
    if (pit == parties_.end()) return;
    if (pit->second.member_ids.size() >= PartyStateMsg::MAX_MEMBERS) return;

    // Add player to the party (if not already in one).
    if (find_party_id(player_id) != 0) return;
    pit->second.member_ids.push_back(player_id);
    player_party_[player_id] = party_id;

    // Chat notice.
    std::string name;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = sessions_.find(player_id);
        if (sit != sessions_.end()) name = sit->second->player_name();
    }
    for (uint32_t mid : pit->second.member_ids) {
        ChatBroadcastMsg msg;
        msg.channel = static_cast<uint8_t>(ChatChannel::System);
        msg.sender_id = 0;
        std::strncpy(msg.sender_name, "Party", sizeof(msg.sender_name) - 1);
        std::string t = name + " joined the party.";
        std::strncpy(msg.message, t.c_str(), sizeof(msg.message) - 1);
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = sessions_.find(mid);
        if (sit != sessions_.end() && sit->second->is_open())
            sit->second->send(build_packet(MessageType::ChatBroadcast, msg));
    }

    send_party_state_to_all(party_id);
}

void Server::on_party_leave(uint32_t player_id) {
    uint32_t party_id = find_party_id(player_id);
    if (party_id == 0) return;
    auto pit = parties_.find(party_id);
    if (pit == parties_.end()) return;

    auto& members = pit->second.member_ids;
    members.erase(std::remove(members.begin(), members.end(), player_id), members.end());
    player_party_.erase(player_id);

    // Tell the leaver they're not in a party anymore.
    PartyStateMsg empty;
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit != sessions_.end() && sit->second->is_open()) {
        sit->second->send(build_packet(MessageType::PartyState, empty));
    }

    // If the leader left, promote the next member.
    if (pit->second.leader_id == player_id && !members.empty()) {
        pit->second.leader_id = members.front();
    }

    disband_party_if_small(party_id);
    if (parties_.find(party_id) != parties_.end()) send_party_state_to_all(party_id);
}

void Server::send_craft_recipes(uint32_t player_id) {
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = sessions_.find(player_id);
        if (sit == sessions_.end() || !sit->second->is_open()) return;
        session = sit->second;
    }

    int player_level = 1;
    auto& registry = world_.registry();
    auto ent = world_.find_entity_by_network_id(player_id);
    if (ent != entt::null) {
        if (auto* lvl = registry.try_get<ecs::PlayerLevel>(ent)) player_level = lvl->level;
    }

    std::vector<CraftRecipeInfo> recipes;
    for (const auto& r : config_.recipes()) {
        if (r.required_level > player_level) continue;
        CraftRecipeInfo info;
        std::strncpy(info.id, r.id.c_str(), sizeof(info.id) - 1);
        std::strncpy(info.name, r.name.c_str(), sizeof(info.name) - 1);
        std::strncpy(info.output_item_id, r.output_item_id.c_str(), sizeof(info.output_item_id) - 1);
        info.output_count = static_cast<uint16_t>(r.output_count);
        info.gold_cost = r.gold_cost;
        info.required_level = static_cast<uint8_t>(r.required_level);
        uint8_t n = 0;
        for (const auto& ing : r.ingredients) {
            if (n >= CraftRecipeInfo::MAX_INGREDIENTS) break;
            std::strncpy(info.ingredients[n].item_id, ing.item_id.c_str(),
                         sizeof(info.ingredients[n].item_id) - 1);
            info.ingredients[n].count = static_cast<uint16_t>(ing.count);
            ++n;
        }
        info.ingredient_count = n;
        recipes.push_back(info);
    }

    session->send(build_packet(MessageType::CraftRecipes, recipes));
}

void Server::on_craft_request(uint32_t player_id, const std::string& recipe_id) {
    auto& registry = world_.registry();
    auto player = world_.find_entity_by_network_id(player_id);
    if (player == entt::null) return;

    auto send_result = [&](bool ok, const std::string& reason) {
        CraftResultMsg res;
        std::strncpy(res.recipe_id, recipe_id.c_str(), sizeof(res.recipe_id) - 1);
        res.success = ok ? 1 : 0;
        std::strncpy(res.reason, reason.c_str(), sizeof(res.reason) - 1);
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto sit = sessions_.find(player_id);
        if (sit != sessions_.end() && sit->second->is_open())
            sit->second->send(build_packet(MessageType::CraftResult, res));
    };

    const auto* recipe = config_.find_recipe(recipe_id);
    if (!recipe) { send_result(false, "unknown recipe"); return; }

    auto* inv = registry.try_get<ecs::Inventory>(player);
    auto* level = registry.try_get<ecs::PlayerLevel>(player);
    if (!inv || !level) { send_result(false, "no character"); return; }
    if (level->level < recipe->required_level) { send_result(false, "level too low"); return; }
    if (level->gold < recipe->gold_cost) { send_result(false, "not enough gold"); return; }

    // Check ingredients are present.
    auto have_count = [&](const std::string& id) -> int {
        int total = 0;
        for (int i = 0; i < inv->used_slots; ++i) {
            if (inv->slots[i].item_id == id) total += inv->slots[i].count;
        }
        return total;
    };
    for (const auto& ing : recipe->ingredients) {
        if (have_count(ing.item_id) < ing.count) {
            send_result(false, std::string("missing ") + ing.item_id);
            return;
        }
    }

    // Consume ingredients + gold and grant output.
    for (const auto& ing : recipe->ingredients) {
        inv->remove_item(ing.item_id, ing.count);
    }
    level->gold -= recipe->gold_cost;

    const ItemConfig* out = config_.find_item(recipe->output_item_id);
    int stack = out ? out->stack_size : 1;
    if (!inv->add_item(recipe->output_item_id, recipe->output_count, stack)) {
        // Refund if inventory is full.
        for (const auto& ing : recipe->ingredients) inv->add_item(ing.item_id, ing.count, 99);
        level->gold += recipe->gold_cost;
        send_result(false, "inventory full");
        return;
    }

    send_inventory_update(player_id);
    send_result(true, "");
    // Push gold update.
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(player_id);
    if (sit != sessions_.end() && sit->second->is_open()) {
        std::vector<uint8_t> payload(2 * sizeof(int32_t));
        BufferWriter w(std::span<uint8_t>(payload.data(), payload.size()));
        w.write<int32_t>(-recipe->gold_cost);
        w.write<int32_t>(level->gold);
        sit->second->send(build_packet(MessageType::GoldChange, payload));
    }
}

void Server::on_party_kick(uint32_t player_id, uint32_t target_id) {
    uint32_t party_id = find_party_id(player_id);
    if (party_id == 0) return;
    auto pit = parties_.find(party_id);
    if (pit == parties_.end()) return;
    if (pit->second.leader_id != player_id) return;

    auto& members = pit->second.member_ids;
    members.erase(std::remove(members.begin(), members.end(), target_id), members.end());
    player_party_.erase(target_id);

    // Notify kicked player.
    PartyStateMsg empty;
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto sit = sessions_.find(target_id);
    if (sit != sessions_.end() && sit->second->is_open()) {
        sit->second->send(build_packet(MessageType::PartyState, empty));
    }
    disband_party_if_small(party_id);
    if (parties_.find(party_id) != parties_.end()) send_party_state_to_all(party_id);
}

} // namespace mmo::server
