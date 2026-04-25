#include "skill_data.hpp"

#include <algorithm>

namespace mmo::client {

namespace {

struct SkillInfo {
    const char* id;
    const char* name;
    float cooldown_s;
};

// Default class loadouts. Five skills each, indexed [0..4] = key 1..5.
constexpr SkillInfo kWarriorSkills[] = {
    {"warrior_charge",      "Charge",      8.0f},
    {"warrior_ground_slam", "Ground Slam", 12.0f},
    {"warrior_battle_cry",  "Battle Cry",  25.0f},
    {"warrior_whirlwind",   "Whirlwind",   15.0f},
    {"warrior_execute",     "Execute",     20.0f},
};

constexpr SkillInfo kMageSkills[] = {
    {"mage_fireball",     "Fireball",      6.0f},
    {"mage_frost_nova",   "Frost Nova",    10.0f},
    {"mage_blink",        "Blink",         12.0f},
    {"mage_arcane_rain",  "Arcane Rain",   18.0f},
    {"mage_meteor",       "Meteor",        25.0f},
};

constexpr SkillInfo kPaladinSkills[] = {
    {"paladin_holy_strike",   "Holy Strike",   5.0f},
    {"paladin_consecrate",    "Consecrate",    10.0f},
    {"paladin_divine_shield", "Divine Shield", 30.0f},
    {"paladin_healing_aura",  "Healing Aura",  20.0f},
    {"paladin_judgment",      "Judgment",      8.0f},
};

constexpr SkillInfo kArcherSkills[] = {
    {"archer_evasive_roll",   "Evasive Roll",   6.0f},
    {"archer_multi_shot",     "Multi-Shot",     8.0f},
    {"archer_snare_trap",     "Snare Trap",     15.0f},
    {"archer_piercing_shot",  "Piercing Shot",  10.0f},
    {"archer_rain_of_arrows", "Rain of Arrows", 20.0f},
};

const SkillInfo* skills_for_class(int class_index) {
    switch (class_index) {
        case 1:  return kMageSkills;
        case 2:  return kPaladinSkills;
        case 3:  return kArcherSkills;
        default: return kWarriorSkills;
    }
}

} // namespace

void populate_default_skill_bar(HUDState& hud, int class_index) {
    const SkillInfo* skills = skills_for_class(class_index);
    for (int i = 0; i < 5; ++i) {
        auto& slot = hud.skill_slots[i];
        slot.skill_id = skills[i].id;
        slot.name = skills[i].name;
        slot.max_cooldown = skills[i].cooldown_s;
        slot.key_number = i + 1;
        slot.available = true;
    }
}

} // namespace mmo::client
