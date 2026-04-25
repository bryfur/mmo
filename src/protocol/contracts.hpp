#pragma once

// Central `static_assert(NetMessage<X>)` table. Every protocol struct
// defined in this module lives here so adding a new message without
// satisfying the NetMessage contract gives a single-location compile
// error.
//
// This file exists purely for the compile-time checks; nothing else
// includes it at runtime. The protocol library CMake pulls it in so the
// asserts fire on any build.

#include "protocol/class_info.hpp"
#include "protocol/class_select_msg.hpp"
#include "protocol/connect_msg.hpp"
#include "protocol/connection_accepted_msg.hpp"
#include "protocol/entity_delta.hpp"
#include "protocol/entity_exit_msg.hpp"
#include "protocol/entity_state.hpp"
#include "protocol/gameplay_msgs.hpp"
#include "protocol/net_message_concept.hpp"
#include "protocol/packet.hpp"
#include "protocol/player_input.hpp"
#include "protocol/player_left_msg.hpp"
#include "protocol/world_config.hpp"

namespace mmo::protocol {

// Packet framing
static_assert(NetMessage<PacketHeader>);

// Connection + class select
static_assert(NetMessage<ConnectMsg>);
static_assert(NetMessage<ClassSelectMsg>);
static_assert(NetMessage<ConnectionAcceptedMsg>);
static_assert(NetMessage<PlayerLeftMsg>);
static_assert(NetMessage<EntityExitMsg>);

// World / config / class list
static_assert(NetMessage<NetWorldConfig>);
static_assert(NetMessage<ClassInfo>);

// Entity state + deltas
static_assert(NetMessage<NetEntityState>);
static_assert(NetMessage<EntityDeltaUpdate>);
static_assert(NetMessage<PlayerInput>);

// Combat + progression
static_assert(NetMessage<CombatEventMsg>);
static_assert(NetMessage<EntityDeathMsg>);
static_assert(NetMessage<XPGainMsg>);
static_assert(NetMessage<LevelUpMsg>);
static_assert(NetMessage<GoldChangeMsg>);
static_assert(NetMessage<LootDropMsg>);

// Inventory
static_assert(NetMessage<InventorySlot>);
static_assert(NetMessage<InventoryUpdateMsg>);
static_assert(NetMessage<ItemEquipMsg>);
static_assert(NetMessage<ItemUnequipMsg>);
static_assert(NetMessage<ItemUseMsg>);

// Quests
static_assert(NetMessage<QuestOfferMsg>);
static_assert(NetMessage<QuestAcceptMsg>);
static_assert(NetMessage<QuestProgressMsg>);
static_assert(NetMessage<QuestCompleteMsg>);
static_assert(NetMessage<QuestTurnInMsg>);

// Skills + talents
static_assert(NetMessage<SkillUseMsg>);
static_assert(NetMessage<SkillCooldownMsg>);
static_assert(NetMessage<SkillSlotInfo>);
static_assert(NetMessage<SkillUnlockMsg>);
static_assert(NetMessage<TalentUnlockMsg>);
static_assert(NetMessage<TalentSyncMsg>);

// NPC interaction + zone
static_assert(NetMessage<NPCInteractMsg>);
static_assert(NetMessage<NPCDialogueMsg>);
static_assert(NetMessage<ZoneChangeMsg>);

// Chat
static_assert(NetMessage<ChatSendMsg>);
static_assert(NetMessage<ChatBroadcastMsg>);

// Vendor
static_assert(NetMessage<VendorStockEntry>);
static_assert(NetMessage<VendorOpenMsg>);
static_assert(NetMessage<VendorBuyMsg>);
static_assert(NetMessage<VendorSellMsg>);

// Party
static_assert(NetMessage<PartyInviteMsg>);
static_assert(NetMessage<PartyInviteOfferMsg>);
static_assert(NetMessage<PartyInviteRespondMsg>);
static_assert(NetMessage<PartyKickMsg>);
static_assert(NetMessage<PartyMemberInfo>);
static_assert(NetMessage<PartyStateMsg>);

// Crafting
static_assert(NetMessage<CraftIngredient>);
static_assert(NetMessage<CraftRecipeInfo>);
static_assert(NetMessage<CraftRequestMsg>);
static_assert(NetMessage<CraftResultMsg>);

} // namespace mmo::protocol
