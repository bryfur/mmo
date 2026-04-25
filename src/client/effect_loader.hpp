#pragma once

#include "../engine/effect_definition.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace mmo {

// Registry for loaded effect definitions (client-side)
class EffectRegistry {
public:
    // Load an effect definition from JSON file
    bool load_effect(const std::string& file_path);

    // Load multiple effects from a directory
    bool load_effects_directory(const std::string& directory_path);

    // Get an effect definition by name
    const mmo::engine::EffectDefinition* get_effect(const std::string& name) const;

    // Check if an effect exists
    bool has_effect(const std::string& name) const;

    // Clear all loaded effects
    void clear();

private:
    std::unordered_map<std::string, mmo::engine::EffectDefinition> effects_;
};

} // namespace mmo
