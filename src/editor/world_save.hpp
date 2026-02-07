#pragma once

#include "engine/heightmap.hpp"
#include <entt/entt.hpp>
#include <string>

namespace mmo::editor {

class WorldSave {
public:
    static bool save(const std::string& save_dir,
                     const engine::Heightmap& heightmap,
                     const entt::registry& registry);

    static bool load(const std::string& save_dir,
                     engine::Heightmap& heightmap,
                     entt::registry& registry);

    static bool exists(const std::string& save_dir);
};

} // namespace mmo::editor
