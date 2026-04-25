#pragma once

#include <cstdint>


namespace mmo::engine::ui_colors {
constexpr uint32_t WHITE = 0xFFFFFFFF;
constexpr uint32_t TEXT_DIM = 0xFFAAAAAA;
constexpr uint32_t TEXT_HINT = 0xFF888888;
constexpr uint32_t PANEL_BG = 0xCC222222;
constexpr uint32_t MENU_BG = 0xE0222222;
constexpr uint32_t TITLE_BG = 0xFF332211;
constexpr uint32_t SELECTION = 0x40FFFFFF;
constexpr uint32_t BORDER = 0xFF666666;

// Health bar
constexpr uint32_t HEALTH_HIGH = 0xFF00CC00;
constexpr uint32_t HEALTH_MED = 0xFF00CCCC;
constexpr uint32_t HEALTH_LOW = 0xFF0000CC;
constexpr uint32_t HEALTH_BG = 0xFF000066;
constexpr uint32_t HEALTH_FRAME = 0xFF000000;
constexpr uint32_t HEALTH_BAR_BG = 0xE6000066;
constexpr uint32_t HEALTH_3D_BG = 0xCC000000;

// Menu values
constexpr uint32_t VALUE_ON = 0xFF00FF00;
constexpr uint32_t VALUE_OFF = 0xFFFF6666;
constexpr uint32_t VALUE_SLIDER = 0xFF00AAFF;
constexpr uint32_t FPS_TEXT = 0xFF00FF00;

// Mana bar
constexpr uint32_t MANA_BAR = 0xFFFF4400;   // Bright blue (ABGR)
constexpr uint32_t MANA_BG = 0xFF660000;    // Dark blue bg
constexpr uint32_t MANA_FRAME = 0xFF000000; // Black frame

// XP bar
constexpr uint32_t XP_BAR = 0xFF00DDFF; // Gold/yellow
constexpr uint32_t XP_BG = 0xFF004466;  // Dark gold bg
constexpr uint32_t XP_FRAME = 0xFF000000;

// Gold display
constexpr uint32_t GOLD_TEXT = 0xFF00DDFF; // Gold color

// Skill bar
constexpr uint32_t SKILL_BG = 0xCC333333;
constexpr uint32_t SKILL_BORDER = 0xFF666666;
constexpr uint32_t SKILL_COOLDOWN = 0x88000000; // Semi-transparent dark overlay
constexpr uint32_t SKILL_READY = 0xFF00AA00;    // Green ready indicator
constexpr uint32_t SKILL_KEY_TEXT = 0xFFCCCCCC;

// Quest tracker
constexpr uint32_t QUEST_TITLE = 0xFF00DDFF;    // Gold quest name
constexpr uint32_t QUEST_COMPLETE = 0xFF00CC00; // Green completed
constexpr uint32_t QUEST_PROGRESS = 0xFFAAAAAA; // Gray in-progress

// Zone name
constexpr uint32_t ZONE_NAME = 0xFFFFFFFF;

// Level up
constexpr uint32_t LEVEL_UP = 0xFF00DDFF; // Gold

// Loot feed rarity colors
constexpr uint32_t LOOT_COMMON = 0xFFAAAAAA;
constexpr uint32_t LOOT_UNCOMMON = 0xFF00CC00;
constexpr uint32_t LOOT_RARE = 0xFF0088FF;
constexpr uint32_t LOOT_EPIC = 0xFFCC00CC;
constexpr uint32_t LOOT_LEGENDARY = 0xFF00AAFF;

// Panel backgrounds
constexpr uint32_t PANEL_TITLE_BG = 0xFF442211;
constexpr uint32_t SLOT_BG = 0xCC444444;
constexpr uint32_t SLOT_EMPTY = 0xCC333333;
constexpr uint32_t EQUIPPED_BG = 0xCC225522;

// Talent colors
constexpr uint32_t TALENT_UNLOCKED = 0xFF00CC00;
constexpr uint32_t TALENT_AVAILABLE = 0xFF00DDFF;
constexpr uint32_t TALENT_LOCKED = 0xFF555555;

} // namespace mmo::engine::ui_colors
