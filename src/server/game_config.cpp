#include "game_config.hpp"
#include "nlohmann/json_fwd.hpp"
#include "protocol/protocol.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

using json = nlohmann::json;

namespace mmo::server {

using namespace mmo::protocol;

uint32_t GameConfig::parse_color(const std::string& s) {
    if (s.size() >= 3 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
    }
    return static_cast<uint32_t>(std::stoul(s));
}

bool GameConfig::load(const std::string& data_dir) {
    bool ok = true;
    ok = load_server(data_dir + "/server.json") && ok;
    ok = load_world(data_dir + "/world.json") && ok;
    ok = load_network(data_dir + "/network.json") && ok;
    ok = load_classes(data_dir + "/classes.json") && ok;
    ok = load_monsters(data_dir + "/monsters.json") && ok;
    ok = load_town(data_dir + "/town.json") && ok;
    // New gameplay configs (non-fatal if missing)
    load_monster_types(data_dir + "/monster_types.json");
    load_zones(data_dir + "/zones.json");
    load_items(data_dir + "/items.json");
    load_loot_tables(data_dir + "/loot_tables.json");
    load_leveling(data_dir + "/leveling.json");
    load_skills(data_dir + "/skills.json");
    load_talents(data_dir + "/talents.json");
    load_quests(data_dir + "/quests.json");
    build_indexes();
    return ok;
}

bool GameConfig::load_server(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[GameConfig] Failed to open " << path << std::endl;
        return false;
    }
    try {
        json j = json::parse(f);
        server_.tick_rate = j.value("tick_rate", 60.0f);
        server_.default_port = j.value("default_port", 7777);
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool GameConfig::load_world(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[GameConfig] Failed to open " << path << std::endl;
        return false;
    }
    try {
        json j = json::parse(f);
        world_.width = j.value("width", 32000.0f);
        world_.height = j.value("height", 32000.0f);
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool GameConfig::load_network(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[GameConfig] Failed to open " << path << std::endl;
        return false;
    }
    try {
        json j = json::parse(f);
        network_.player_view_distance = j.value("player_view_distance", 1500.0f);
        network_.npc_view_distance = j.value("npc_view_distance", 1200.0f);
        network_.town_npc_view_distance = j.value("town_npc_view_distance", 1000.0f);
        network_.building_view_distance = j.value("building_view_distance", 3000.0f);
        network_.environment_view_distance = j.value("environment_view_distance", 2000.0f);
        network_.spatial_grid_cell_size = j.value("spatial_grid_cell_size", 500.0f);
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool GameConfig::load_classes(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[GameConfig] Failed to open " << path << std::endl;
        return false;
    }
    try {
        json j = json::parse(f);
        classes_.clear();
        for (const auto& c : j) {
            ClassConfig cls;
            cls.name = c.value("name", "Unknown");
            cls.model = c.value("model", "warrior");
            cls.health = c.value("health", 100.0f);
            cls.damage = c.value("damage", 10.0f);
            cls.attack_range = c.value("attack_range", 50.0f);
            cls.attack_cooldown = c.value("attack_cooldown", 1.0f);
            cls.color = parse_color(c.value("color", "0xFFFFFFFF"));
            cls.select_color = parse_color(c.value("select_color", "0xFFFFFFFF"));
            cls.ui_color = parse_color(c.value("ui_color", "0xFFFFFFFF"));
            cls.short_desc = c.value("short_desc", "");
            cls.desc_line1 = c.value("desc_line1", "");
            cls.desc_line2 = c.value("desc_line2", "");
            cls.shows_reticle = c.value("shows_reticle", false);
            cls.effect_type = c.value("effect_type", "");
            cls.animation = c.value("animation", "");
            cls.cone_angle = c.value("cone_angle", 0.5f);
            cls.speed = c.value("speed", 200.0f);
            cls.size = c.value("size", 32.0f);
            classes_.push_back(std::move(cls));
        }
        std::cout << "[GameConfig] Loaded " << classes_.size() << " classes" << std::endl;
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool GameConfig::load_monsters(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[GameConfig] Failed to open " << path << std::endl;
        return false;
    }
    try {
        json j = json::parse(f);
        monster_.size = j.value("size", 36.0f);
        monster_.speed = j.value("speed", 100.0f);
        monster_.health = j.value("health", 100.0f);
        monster_.damage = j.value("damage", 15.0f);
        monster_.attack_range = j.value("attack_range", 50.0f);
        monster_.attack_cooldown = j.value("attack_cooldown", 1.2f);
        monster_.aggro_range = j.value("aggro_range", 300.0f);
        monster_.count = j.value("count", 10);
        monster_.model = j.value("model", "npc_enemy");
        monster_.animation = j.value("animation", "");
        monster_.color = parse_color(j.value("color", "0xFF4444FF"));
        std::cout << "[GameConfig] Loaded monster config: " << monster_.count << " monsters" << std::endl;
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool GameConfig::load_town(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[GameConfig] Failed to open " << path << std::endl;
        return false;
    }
    try {
        json j = json::parse(f);

        if (j.contains("wall")) {
            const auto& w = j["wall"];
            wall_.model = w.value("model", "wooden_log");
            wall_.distance = w.value("distance", 500.0f);
            wall_.spacing = w.value("spacing", 35.0f);
            wall_.gate_width = w.value("gate_width", 80.0f);
            wall_.target_size = w.value("target_size", 60.0f);
        }

        if (j.contains("corner_towers")) {
            const auto& ct = j["corner_towers"];
            corner_towers_.model = ct.value("model", "log_tower");
            corner_towers_.target_size = ct.value("target_size", 140.0f);
        }

        safe_zone_radius_ = j.value("safe_zone_radius", 400.0f);

        buildings_.clear();
        if (j.contains("buildings")) {
            for (const auto& b : j["buildings"]) {
                BuildingConfig bld;
                bld.type = b.value("type", "house");
                bld.model = b.value("model", "building_house");
                bld.x = b.value("x", 0.0f);
                bld.y = b.value("y", 0.0f);
                bld.name = b.value("name", "Building");
                bld.rotation = b.value("rotation", 0.0f);
                bld.target_size = b.value("target_size", 100.0f);
                buildings_.push_back(std::move(bld));
            }
        }

        town_npcs_.clear();
        if (j.contains("npcs")) {
            for (const auto& n : j["npcs"]) {
                TownNPCConfig npc;
                npc.type = n.value("type", "villager");
                npc.x = n.value("x", 0.0f);
                npc.y = n.value("y", 0.0f);
                npc.name = n.value("name", "NPC");
                npc.wanders = n.value("wanders", false);
                npc.model = n.value("model", "npc_villager");
                npc.color = parse_color(n.value("color", "0xFFAAAAAA"));
                town_npcs_.push_back(std::move(npc));
            }
        }

        std::cout << "[GameConfig] Loaded town: " << buildings_.size() << " buildings, "
                  << town_npcs_.size() << " NPCs" << std::endl;
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << std::endl;
        return false;
    }
}

const ClassConfig& GameConfig::get_class(int index) const {
    static ClassConfig default_class;
    if (index >= 0 && index < static_cast<int>(classes_.size())) {
        return classes_[index];
    }
    return default_class;
}

ClassInfo GameConfig::build_class_info(int index) const {
    ClassInfo info{};
    const auto& cls = get_class(index);
    std::strncpy(info.name, cls.name.c_str(), sizeof(info.name) - 1);
    std::strncpy(info.short_desc, cls.short_desc.c_str(), sizeof(info.short_desc) - 1);
    std::strncpy(info.desc_line1, cls.desc_line1.c_str(), sizeof(info.desc_line1) - 1);
    std::strncpy(info.desc_line2, cls.desc_line2.c_str(), sizeof(info.desc_line2) - 1);
    std::strncpy(info.model_name, cls.model.c_str(), sizeof(info.model_name) - 1);
    info.color = cls.color;
    info.select_color = cls.select_color;
    info.ui_color = cls.ui_color;
    info.shows_reticle = cls.shows_reticle;
    return info;
}

// ============================================================================
// New Gameplay Config Loaders
// ============================================================================

bool GameConfig::load_monster_types(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        monster_types_.clear();
        for (const auto& m : j["monster_types"]) {
            MonsterTypeConfig mt;
            mt.id = m.value("id", "");
            mt.name = m.value("name", "Monster");
            mt.model = m.value("model", "npc_enemy");
            mt.animation = m.value("animation", "humanoid");
            mt.color = parse_color(m.value("color", "0xFF4444FF"));
            mt.health = m.value("health", 100.0f);
            mt.damage = m.value("damage", 15.0f);
            mt.attack_range = m.value("attack_range", 50.0f);
            mt.attack_cooldown = m.value("attack_cooldown", 1.2f);
            mt.aggro_range = m.value("aggro_range", 300.0f);
            mt.speed = m.value("speed", 100.0f);
            mt.size = m.value("size", 36.0f);
            mt.xp_reward = m.value("xp_reward", 25);
            mt.gold_reward = m.value("gold_reward", 5);
            mt.level = m.value("level", 1);
            mt.description = m.value("description", "");
            monster_types_.push_back(std::move(mt));
        }
        std::cout << "[GameConfig] Loaded " << monster_types_.size() << " monster types\n";
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << "\n";
        return false;
    }
}

bool GameConfig::load_zones(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        zones_.clear();
        for (const auto& z : j["zones"]) {
            ZoneConfig zc;
            zc.id = z.value("id", "");
            zc.name = z.value("name", "Unknown Zone");
            zc.center_x = z.value("center_x", 0.0f);
            zc.center_z = z.value("center_z", 0.0f);
            zc.radius = z.value("radius", 1000.0f);
            zc.level_min = z.value("level_min", 1);
            zc.level_max = z.value("level_max", 5);
            zc.monster_density = z.value("monster_density", 0.5f);
            if (z.contains("monster_types")) {
                for (const auto& mt : z["monster_types"]) {
                    zc.monster_types.push_back(mt.get<std::string>());
                }
            }
            zc.description = z.value("description", "");
            zones_.push_back(std::move(zc));
        }
        std::cout << "[GameConfig] Loaded " << zones_.size() << " zones\n";
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << "\n";
        return false;
    }
}

bool GameConfig::load_items(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        items_.clear();
        for (const auto& i : j["items"]) {
            ItemConfig item;
            item.id = i.value("id", "");
            item.name = i.value("name", "Item");
            item.type = i.value("type", "material");
            item.subtype = i.value("subtype", "");
            item.rarity = i.value("rarity", "common");
            item.level_req = i.value("level_req", 1);
            if (i.contains("classes")) {
                for (const auto& c : i["classes"]) {
                    item.classes.push_back(c.get<std::string>());
                }
            }
            if (i.contains("stats")) {
                const auto& s = i["stats"];
                item.stats.damage_bonus = s.value("damage_bonus", 0.0f);
                item.stats.attack_speed_bonus = s.value("attack_speed_bonus", 0.0f);
                item.stats.health_bonus = s.value("health_bonus", 0.0f);
                item.stats.defense = s.value("defense", 0.0f);
                item.stats.speed_bonus = s.value("speed_bonus", 0.0f);
                item.stats.heal_amount = s.value("heal_amount", 0.0f);
                item.stats.buff_duration = s.value("buff_duration", 0.0f);
                item.stats.buff_multiplier = s.value("buff_multiplier", 0.0f);
            }
            item.description = i.value("description", "");
            item.sell_value = i.value("sell_value", 0);
            item.stack_size = i.value("stack_size", 1);
            items_.push_back(std::move(item));
        }
        std::cout << "[GameConfig] Loaded " << items_.size() << " items\n";
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << "\n";
        return false;
    }
}

bool GameConfig::load_loot_tables(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        loot_tables_.clear();
        for (const auto& lt : j["loot_tables"]) {
            LootTableConfig table;
            table.id = lt.value("id", "");
            table.monster_type = lt.value("monster_type", "");
            table.gold_min = lt.value("gold_min", 0);
            table.gold_max = lt.value("gold_max", 10);
            if (lt.contains("drops")) {
                for (const auto& d : lt["drops"]) {
                    LootDropConfig drop;
                    drop.item_id = d.value("item_id", "");
                    drop.chance = d.value("chance", 0.1f);
                    drop.count_min = d.value("count_min", 1);
                    drop.count_max = d.value("count_max", 1);
                    table.drops.push_back(std::move(drop));
                }
            }
            loot_tables_.push_back(std::move(table));
        }
        std::cout << "[GameConfig] Loaded " << loot_tables_.size() << " loot tables\n";
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << "\n";
        return false;
    }
}

bool GameConfig::load_leveling(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        leveling_.max_level = j.value("max_level", 20);
        leveling_.xp_curve.clear();
        if (j.contains("xp_curve")) {
            for (const auto& xp : j["xp_curve"]) {
                leveling_.xp_curve.push_back(xp.get<int>());
            }
        }
        if (j.contains("death_penalty")) {
            leveling_.death_xp_loss_percent = j["death_penalty"].value("xp_loss_percent", 5.0f);
        }
        // Load class growth - dynamically sized to match the number of loaded classes
        leveling_.class_growth.resize(classes_.size());
        if (j.contains("stat_growth_per_level")) {
            const auto& sg = j["stat_growth_per_level"];
            for (size_t i = 0; i < classes_.size(); ++i) {
                // Match class config name (uppercase) to growth key (lowercase)
                std::string lower_name = classes_[i].name;
                for (auto& c : lower_name) c = static_cast<char>(std::tolower(c));
                if (sg.contains(lower_name)) {
                    const auto& g = sg[lower_name];
                    leveling_.class_growth[i].health = g.value("health", 0.0f);
                    leveling_.class_growth[i].damage = g.value("damage", 0.0f);
                    leveling_.class_growth[i].speed = g.value("speed", 0.0f);
                    leveling_.class_growth[i].attack_range = g.value("attack_range", 0.0f);
                    leveling_.class_growth[i].attack_cooldown_reduction = g.value("attack_cooldown_reduction", 0.0f);
                }
            }
        }
        std::cout << "[GameConfig] Loaded leveling config: " << leveling_.max_level << " levels\n";
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << "\n";
        return false;
    }
}

bool GameConfig::load_skills(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        skills_.clear();
        for (const auto& s : j["skills"]) {
            SkillConfig sk;
            sk.id = s.value("id", "");
            sk.name = s.value("name", "Skill");
            sk.class_name = s.value("class", "");
            sk.unlock_level = s.value("unlock_level", 1);
            sk.cooldown = s.value("cooldown", 10.0f);
            sk.description = s.value("description", "");
            sk.effect_type = s.value("effect_type", "");
            sk.damage_multiplier = s.value("damage_multiplier", 1.0f);
            sk.range = s.value("range", 100.0f);
            sk.mana_cost = s.value("mana_cost", 20.0f);
            sk.cone_angle = s.value("cone_angle", 0.5f);
            sk.heal_percent = s.value("heal_percent", 0.0f);
            sk.duration = s.value("duration", 0.0f);

            // Status effect fields
            sk.stun_duration = s.value("stun_duration", 0.0f);
            sk.slow_percent = s.value("slow_percent", 0.0f);
            sk.slow_duration = s.value("slow_duration", 0.0f);
            sk.freeze_duration = s.value("freeze_duration", 0.0f);
            sk.burn_duration = s.value("burn_duration", 0.0f);
            sk.burn_damage = s.value("burn_damage", 0.0f);
            sk.root_duration = s.value("root_duration", 0.0f);
            sk.buff_duration = s.value("buff_duration", 0.0f);
            sk.damage_reduction = s.value("damage_reduction", 0.0f);
            sk.invulnerable_duration = s.value("invulnerable_duration", 0.0f);
            sk.speed_boost = s.value("speed_boost", 0.0f);
            sk.speed_boost_duration = s.value("speed_boost_duration", 0.0f);
            sk.lifesteal_percent = s.value("lifesteal_percent", 0.0f);
            sk.enemy_damage_reduction = s.value("enemy_damage_reduction", 0.0f);
            sk.debuff_duration = s.value("debuff_duration", 0.0f);
            skills_.push_back(std::move(sk));
        }
        // Load mana_system section (per-class mana values)
        if (j.contains("mana_system")) {
            const auto& ms = j["mana_system"];
            if (ms.contains("base_mana") && ms.contains("mana_regen_per_second")) {
                for (auto& [class_name, mana_val] : ms["base_mana"].items()) {
                    auto& entry = mana_system_[class_name];
                    entry.base_mana = mana_val.get<float>();
                }
                for (auto& [class_name, regen_val] : ms["mana_regen_per_second"].items()) {
                    mana_system_[class_name].mana_regen = regen_val.get<float>();
                }
                if (ms.contains("mana_per_level")) {
                    for (auto& [class_name, mpl_val] : ms["mana_per_level"].items()) {
                        mana_system_[class_name].mana_per_level = mpl_val.get<float>();
                    }
                }
                // Apply mana values to class configs
                apply_mana_system();
            }
        }

        std::cout << "[GameConfig] Loaded " << skills_.size() << " skills\n";
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << "\n";
        return false;
    }
}

void GameConfig::apply_mana_system() {
    for (auto& cls : classes_) {
        // Match class name (case-insensitive: config uses "WARRIOR", mana_system uses "warrior")
        std::string lower_name = cls.name;
        for (auto& c : lower_name) c = static_cast<char>(std::tolower(c));
        auto it = mana_system_.find(lower_name);
        if (it != mana_system_.end()) {
            cls.base_mana = it->second.base_mana;
            cls.mana_regen = it->second.mana_regen;
        }
    }
}

bool GameConfig::load_talents(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        talent_config_.first_talent_point_level = j.value("first_talent_point_level", 3);
        talent_config_.max_talent_points = j.value("max_talent_points", 28);

        talent_trees_.clear();
        for (const auto& tree : j["talent_trees"]) {
            TalentTreeConfig ttc;
            ttc.class_name = tree.value("class", "");
            if (tree.contains("branches")) {
                for (const auto& branch : tree["branches"]) {
                    TalentBranch tb;
                    tb.name = branch.value("name", "");
                    tb.description = branch.value("description", "");
                    if (branch.contains("talents")) {
                        for (const auto& t : branch["talents"]) {
                            TalentConfig tc;
                            tc.id = t.value("id", "");
                            tc.name = t.value("name", "");
                            tc.tier = t.value("tier", 1);
                            tc.description = t.value("description", "");
                            tc.prerequisite = t.value("requires", "");
                            if (t.contains("effect")) {
                                const auto& e = t["effect"];
                                // Base stats
                                tc.effect.damage_mult = e.value("damage_mult", 1.0f);
                                tc.effect.speed_mult = e.value("speed_mult", 1.0f);
                                tc.effect.health_mult = e.value("health_mult", 1.0f);
                                tc.effect.crit_chance = e.value("crit_chance", 0.0f);
                                tc.effect.kill_heal_pct = e.value("kill_heal_percent", 0.0f);
                                tc.effect.defense_mult = e.value("defense_mult", 1.0f);
                                tc.effect.mana_mult = e.value("mana_mult", 1.0f);
                                tc.effect.cooldown_mult = e.value("cooldown_mult", 1.0f);
                                tc.effect.attack_speed_mult = e.value("attack_speed_mult", 1.0f);

                                // Additional stat modifiers
                                tc.effect.crit_damage_mult = e.value("crit_damage_mult", 1.0f);
                                tc.effect.mana_cost_mult = e.value("mana_cost_mult", 1.0f);
                                tc.effect.skill_damage_mult = e.value("skill_damage_mult", 1.0f);
                                tc.effect.attack_range_bonus = e.value("attack_range_bonus", 0.0f);
                                tc.effect.attack_range_mult = e.value("attack_range_mult", 1.0f);
                                tc.effect.healing_received_mult = e.value("healing_received_mult", 1.0f);
                                tc.effect.global_cdr = e.value("global_cdr_percent", 0.0f);
                                tc.effect.cc_immunity = e.value("cc_immunity", false);

                                // Cheat death
                                if (e.contains("cheat_death_cooldown") || e.contains("angel_cooldown")) {
                                    tc.effect.has_cheat_death = true;
                                    tc.effect.cheat_death_cooldown = e.contains("angel_cooldown")
                                        ? e.value("angel_cooldown", 120.0f)
                                        : e.value("cheat_death_cooldown", 60.0f);
                                    tc.effect.cheat_death_hp = e.value("cheat_death_health", 0.1f);
                                }

                                // Avenge
                                if (e.contains("avenge_damage_mult")) {
                                    tc.effect.has_avenge = true;
                                    tc.effect.avenge_damage_mult = e.value("avenge_damage_mult", 1.0f);
                                    tc.effect.avenge_attack_speed_mult = e.value("avenge_attack_speed", 1.0f);
                                    tc.effect.avenge_duration = e.value("avenge_duration", 0.0f);
                                }

                                // On-hit procs
                                tc.effect.slow_on_hit_chance = e.value("slow_chance", 0.0f);
                                tc.effect.slow_on_hit_value = e.value("slow_percent",
                                    e.value("attack_slow_percent", 0.0f));
                                tc.effect.slow_on_hit_dur = e.value("slow_duration",
                                    e.value("attack_slow_duration", 0.0f));
                                tc.effect.burn_on_hit_pct = e.value("burn_damage_percent", 0.0f);
                                tc.effect.burn_on_hit_dur = e.value("burn_duration", 0.0f);
                                tc.effect.poison_on_hit_pct = e.value("poison_damage_percent", 0.0f);
                                tc.effect.poison_on_hit_dur = e.value("poison_duration", 0.0f);
                                tc.effect.mana_on_hit_pct = e.value("attack_mana_restore_percent", 0.0f);
                                tc.effect.hit_speed_bonus = e.value("hit_speed_bonus", 0.0f);
                                tc.effect.hit_speed_dur = e.value("hit_speed_duration", 0.0f);

                                // Kill effects
                                tc.effect.kill_explosion_pct = e.value("kill_explosion_percent", 0.0f);
                                tc.effect.kill_explosion_radius = e.value("kill_explosion_radius", 0.0f);
                                {
                                    float kdm = e.value("kill_damage_mult", 1.0f);
                                    tc.effect.kill_damage_bonus = (kdm > 1.0f) ? kdm - 1.0f : 0.0f;
                                }
                                tc.effect.kill_damage_dur = e.value("kill_buff_duration", 0.0f);
                                {
                                    float ksm = e.value("kill_speed_mult", 1.0f);
                                    tc.effect.kill_speed_bonus = (ksm > 1.0f) ? ksm - 1.0f : 0.0f;
                                }
                                tc.effect.kill_speed_dur = e.value("kill_buff_duration", 0.0f);
                                tc.effect.burn_spread_radius = e.value("burn_spread_radius", 0.0f);

                                // Damage reflect
                                tc.effect.reflect_percent = e.value("reflect_percent",
                                    e.value("reflect_damage_percent", 0.0f));

                                // Stationary
                                tc.effect.stationary_damage_mult = e.value("stationary_damage_mult", 1.0f);
                                tc.effect.stationary_damage_reduction = e.value("stationary_damage_reduction", 0.0f);
                                tc.effect.stationary_heal_pct = e.value("stationary_heal_percent", 0.0f);
                                if (e.contains("stationary_delay") || e.contains("stationary_damage_mult") ||
                                    e.contains("stationary_damage_reduction") || e.contains("stationary_heal_percent")) {
                                    tc.effect.stationary_delay = e.value("stationary_delay", 1.0f);
                                }

                                // Low HP
                                tc.effect.low_health_regen_pct = e.value("low_health_regen_percent", 0.0f);
                                tc.effect.low_health_threshold = e.value("low_health_threshold", 0.0f);

                                // Fury
                                tc.effect.fury_threshold = e.value("fury_threshold", 0.0f);
                                tc.effect.fury_damage_mult = e.value("fury_damage_mult", 1.0f);
                                tc.effect.fury_attack_speed_mult = e.value("fury_attack_speed_mult", 1.0f);

                                // Combo stacks
                                tc.effect.combo_damage_bonus = e.value("combo_damage_bonus", 0.0f);
                                tc.effect.combo_max_stacks = e.value("combo_max_stacks", 0);
                                tc.effect.combo_window = e.value("combo_window", 3.0f);

                                // Empowered attacks
                                tc.effect.empowered_every = e.value("empowered_attack_every", 0);
                                tc.effect.empowered_damage_mult = e.value("empowered_damage_mult", 1.0f);
                                tc.effect.empowered_stun_dur = e.value("empowered_stun_duration", 0.0f);

                                // Passive aura
                                tc.effect.aura_damage_pct = e.value("aura_damage_percent", 0.0f);
                                tc.effect.aura_range = e.value("aura_range", 0.0f);

                                // Nearby debuff
                                tc.effect.nearby_debuff_range = e.value("presence_range", 0.0f);
                                tc.effect.nearby_damage_reduction = e.value("nearby_enemy_damage_reduction", 0.0f);

                                // Panic freeze
                                tc.effect.panic_freeze_radius = e.value("panic_freeze_radius", 0.0f);
                                tc.effect.panic_freeze_duration = e.value("panic_freeze_duration", 0.0f);
                                tc.effect.panic_freeze_threshold = e.value("panic_freeze_threshold", 0.0f);
                                tc.effect.panic_freeze_cooldown = e.value("panic_freeze_cooldown", 60.0f);

                                // Periodic shield
                                tc.effect.shield_regen_pct = e.value("shield_percent", 0.0f);
                                tc.effect.shield_regen_cooldown = e.value("shield_cooldown", 15.0f);

                                // Spell echo
                                tc.effect.spell_echo_chance = e.value("spell_echo_chance", 0.0f);

                                // Frozen vulnerability
                                tc.effect.frozen_vulnerability = e.value("frozen_vulnerability", 0.0f);

                                // Mana conditionals
                                tc.effect.high_mana_damage_mult = e.value("high_mana_damage_mult", 1.0f);
                                tc.effect.high_mana_threshold = e.value("high_mana_threshold", 0.0f);
                                tc.effect.low_mana_regen_mult = e.value("low_mana_regen_mult", 1.0f);
                                tc.effect.low_mana_threshold = e.value("low_mana_threshold", 0.0f);

                                // Conditional damage
                                tc.effect.high_hp_bonus_damage = e.value("high_hp_bonus_damage", 0.0f);
                                tc.effect.high_hp_threshold = e.value("high_hp_threshold", 0.0f);
                                tc.effect.max_range_damage_bonus = e.value("max_range_damage_bonus", 0.0f);

                                // Damage sharing
                                tc.effect.damage_share_percent = e.value("damage_share_percent", 0.0f);
                                tc.effect.share_radius = e.value("share_radius", 0.0f);

                                // Dodge
                                tc.effect.moving_dodge_chance = e.value("moving_dodge_chance", 0.0f);

                                // Trap modifications
                                tc.effect.max_traps = e.value("max_traps", 1);
                                tc.effect.trap_lifetime_mult = e.value("trap_lifetime_mult", 1.0f);
                                tc.effect.trap_radius_mult = e.value("trap_radius_mult", 1.0f);
                                tc.effect.trap_vulnerability = e.value("trap_vulnerability", 0.0f);
                                tc.effect.trap_vulnerability_dur = e.value("trap_vulnerability_duration", 0.0f);
                                tc.effect.trap_cdr = e.value("trap_cdr", 0.0f);
                                tc.effect.trap_cloud_damage = e.value("trap_cloud_damage", 0.0f);
                                tc.effect.trap_cloud_duration = e.value("trap_cloud_duration", 0.0f);
                                tc.effect.trap_cloud_radius = e.value("trap_cloud_radius", 0.0f);
                                tc.effect.poison_death_explosion_pct = e.value("poison_death_explosion_percent", 0.0f);
                                tc.effect.poison_explosion_radius = e.value("explosion_radius", 0.0f);
                            }
                            tb.talents.push_back(std::move(tc));
                        }
                    }
                    ttc.branches.push_back(std::move(tb));
                }
            }
            talent_trees_.push_back(std::move(ttc));
        }
        std::cout << "[GameConfig] Loaded " << talent_trees_.size() << " talent trees\n";
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << "\n";
        return false;
    }
}

bool GameConfig::load_quests(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        quests_.clear();
        for (const auto& q : j["quests"]) {
            QuestConfig qc;
            qc.id = q.value("id", "");
            qc.name = q.value("name", "Quest");
            qc.description = q.value("description", "");
            qc.giver_npc = q.value("giver_npc", "");
            qc.giver_type = q.value("giver_type", "");
            qc.type = q.value("type", "kill");
            if (q.contains("requirements")) {
                const auto& r = q["requirements"];
                qc.min_level = r.value("min_level", 1);
                if (r.contains("prerequisite_quest") && !r["prerequisite_quest"].is_null()) {
                    qc.prerequisite_quest = r["prerequisite_quest"].get<std::string>();
                }
            }
            if (q.contains("objectives")) {
                for (const auto& o : q["objectives"]) {
                    QuestObjectiveConfig obj;
                    obj.type = o.value("type", "kill");
                    obj.target = o.value("target", "");
                    obj.count = o.value("count", 1);
                    obj.description = o.value("description", "");
                    if (o.contains("location") && o["location"].is_array() && o["location"].size() >= 2) {
                        obj.location_x = o["location"][0].get<float>();
                        obj.location_z = o["location"][1].get<float>();
                    }
                    obj.radius = o.value("radius", 100.0f);
                    qc.objectives.push_back(std::move(obj));
                }
            }
            if (q.contains("rewards")) {
                const auto& r = q["rewards"];
                qc.rewards.xp = r.value("xp", 0);
                qc.rewards.gold = r.value("gold", 0);
                qc.rewards.item_reward = r.value("item_reward", "");
            }
            if (q.contains("dialogue")) {
                const auto& d = q["dialogue"];
                qc.dialogue.offer = d.value("offer", "");
                qc.dialogue.progress = d.value("progress", "");
                qc.dialogue.complete = d.value("complete", "");
            }
            qc.repeatable = q.value("repeatable", false);
            quests_.push_back(std::move(qc));
        }
        std::cout << "[GameConfig] Loaded " << quests_.size() << " quests\n";
        return true;
    } catch (const json::exception& e) {
        std::cerr << "[GameConfig] Error parsing " << path << ": " << e.what() << "\n";
        return false;
    }
}

// ============================================================================
// Lookup helpers
// ============================================================================

void GameConfig::build_indexes() {
    monster_type_index_.clear();
    for (const auto& mt : monster_types_) monster_type_index_[mt.id] = &mt;
    item_index_.clear();
    for (const auto& item : items_) item_index_[item.id] = &item;
    loot_table_index_.clear();
    for (const auto& lt : loot_tables_) loot_table_index_[lt.monster_type] = &lt;
    skill_index_.clear();
    for (const auto& sk : skills_) skill_index_[sk.id] = &sk;
    quest_index_.clear();
    for (const auto& q : quests_) quest_index_[q.id] = &q;
}

const MonsterTypeConfig* GameConfig::find_monster_type(const std::string& id) const {
    auto it = monster_type_index_.find(id);
    return it != monster_type_index_.end() ? it->second : nullptr;
}

const ZoneConfig* GameConfig::find_zone_at(float x, float z) const {
    const ZoneConfig* best = nullptr;
    float best_dist = 1e9f;
    for (const auto& zone : zones_) {
        float dx = x - zone.center_x;
        float dz = z - zone.center_z;
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist < zone.radius && dist < best_dist) {
            best_dist = dist;
            best = &zone;
        }
    }
    return best;
}

const ItemConfig* GameConfig::find_item(const std::string& id) const {
    auto it = item_index_.find(id);
    return it != item_index_.end() ? it->second : nullptr;
}

const LootTableConfig* GameConfig::find_loot_table(const std::string& monster_type) const {
    auto it = loot_table_index_.find(monster_type);
    return it != loot_table_index_.end() ? it->second : nullptr;
}

std::vector<const SkillConfig*> GameConfig::skills_for_class(const std::string& class_name) const {
    std::vector<const SkillConfig*> result;
    for (const auto& sk : skills_) {
        if (sk.class_name == class_name) result.push_back(&sk);
    }
    return result;
}

const SkillConfig* GameConfig::find_skill(const std::string& id) const {
    auto it = skill_index_.find(id);
    return it != skill_index_.end() ? it->second : nullptr;
}

const TalentConfig* GameConfig::find_talent(const std::string& id) const {
    for (const auto& tree : talent_trees_) {
        for (const auto& branch : tree.branches) {
            for (const auto& talent : branch.talents) {
                if (talent.id == id) return &talent;
            }
        }
    }
    return nullptr;
}

const QuestConfig* GameConfig::find_quest(const std::string& id) const {
    auto it = quest_index_.find(id);
    return it != quest_index_.end() ? it->second : nullptr;
}

std::vector<const QuestConfig*> GameConfig::quests_for_npc(const std::string& npc_type) const {
    std::vector<const QuestConfig*> result;
    for (const auto& q : quests_) {
        if (q.giver_type == npc_type) result.push_back(&q);
    }
    return result;
}

} // namespace mmo::server
