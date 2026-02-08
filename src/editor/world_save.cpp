#include "world_save.hpp"
#include "client/ecs/components.hpp"
#include "server/entity_config.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace mmo::editor {

using namespace mmo::client::ecs;
using namespace mmo::protocol;

bool WorldSave::save(const std::string& save_dir,
                      const engine::Heightmap& heightmap,
                      const entt::registry& registry) {
    std::filesystem::create_directories(save_dir);

    // Save heightmap binary
    {
        std::ofstream f(save_dir + "/heightmap.bin", std::ios::binary);
        if (!f) {
            std::cerr << "WorldSave: failed to open heightmap.bin for writing" << std::endl;
            return false;
        }

        f.write(reinterpret_cast<const char*>(&heightmap.resolution), sizeof(uint32_t));
        f.write(reinterpret_cast<const char*>(&heightmap.world_origin_x), sizeof(float));
        f.write(reinterpret_cast<const char*>(&heightmap.world_origin_z), sizeof(float));
        f.write(reinterpret_cast<const char*>(&heightmap.world_size), sizeof(float));
        f.write(reinterpret_cast<const char*>(&heightmap.min_height), sizeof(float));
        f.write(reinterpret_cast<const char*>(&heightmap.max_height), sizeof(float));

        f.write(reinterpret_cast<const char*>(heightmap.height_data.data()),
                heightmap.height_data.size() * sizeof(uint16_t));
    }

    // Save entities as JSON
    {
        nlohmann::json j = nlohmann::json::array();

        auto view = registry.view<Transform, EntityInfo>();
        for (auto entity : view) {
            auto& t = view.get<Transform>(entity);
            auto& info = view.get<EntityInfo>(entity);

            nlohmann::json ej;
            ej["entity_type"] = server::config::entity_type_to_string(info.type);
            ej["model"] = info.model_name;
            ej["target_size"] = info.target_size;
            ej["color"] = info.color;
            ej["position"] = {t.x, t.y, t.z};
            ej["rotation"] = t.rotation;

            if (registry.all_of<Name>(entity)) {
                ej["name"] = registry.get<Name>(entity).value;
            }

            j.push_back(ej);
        }

        std::ofstream f(save_dir + "/world_entities.json");
        if (!f) {
            std::cerr << "WorldSave: failed to open world_entities.json for writing" << std::endl;
            return false;
        }
        f << j.dump(2);
    }

    std::cout << "World saved to " << save_dir << std::endl;
    return true;
}

bool WorldSave::load(const std::string& save_dir,
                      engine::Heightmap& heightmap,
                      entt::registry& registry) {
    // Load heightmap binary
    {
        std::ifstream f(save_dir + "/heightmap.bin", std::ios::binary);
        if (!f) {
            std::cerr << "WorldSave: failed to open heightmap.bin for reading" << std::endl;
            return false;
        }

        f.read(reinterpret_cast<char*>(&heightmap.resolution), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&heightmap.world_origin_x), sizeof(float));
        f.read(reinterpret_cast<char*>(&heightmap.world_origin_z), sizeof(float));
        f.read(reinterpret_cast<char*>(&heightmap.world_size), sizeof(float));
        f.read(reinterpret_cast<char*>(&heightmap.min_height), sizeof(float));
        f.read(reinterpret_cast<char*>(&heightmap.max_height), sizeof(float));

        size_t data_size = static_cast<size_t>(heightmap.resolution) * heightmap.resolution;
        heightmap.height_data.resize(data_size);
        f.read(reinterpret_cast<char*>(heightmap.height_data.data()),
               data_size * sizeof(uint16_t));
    }

    // Load entities from JSON
    {
        std::ifstream f(save_dir + "/world_entities.json");
        if (!f) {
            std::cerr << "WorldSave: failed to open world_entities.json for reading" << std::endl;
            return false;
        }

        nlohmann::json j;
        f >> j;

        // Clear existing entities
        registry.clear();

        for (auto& ej : j) {
            auto entity = registry.create();

            float px = ej["position"][0].get<float>();
            float py = ej["position"][1].get<float>();
            float pz = ej["position"][2].get<float>();
            float rot = ej.value("rotation", 0.0f);
            registry.emplace<Transform>(entity, px, py, pz, rot);

            EntityInfo info;
            info.model_name = ej["model"].get<std::string>();
            if (ej.contains("entity_type")) {
                info.type = server::config::entity_type_from_string(ej["entity_type"].get<std::string>());
            } else {
                info.type = static_cast<EntityType>(ej.value("type", 0));
            }
            info.target_size = ej.value("target_size", 30.0f);
            info.color = ej.value("color", (uint32_t)0xFFFFFFFF);
            registry.emplace<EntityInfo>(entity, info);

            if (ej.contains("name")) {
                registry.emplace<Name>(entity, ej["name"].get<std::string>());
            }
        }

        std::cout << "Loaded " << j.size() << " entities from " << save_dir << std::endl;
    }

    return true;
}

bool WorldSave::exists(const std::string& save_dir) {
    return std::filesystem::exists(save_dir + "/heightmap.bin") &&
           std::filesystem::exists(save_dir + "/world_entities.json");
}

} // namespace mmo::editor
