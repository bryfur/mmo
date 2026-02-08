#include "game_config.hpp"
#include "nlohmann/json_fwd.hpp"
#include "protocol/protocol.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
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
        world_.width = j.value("width", 8000.0f);
        world_.height = j.value("height", 8000.0f);
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

} // namespace mmo::server
