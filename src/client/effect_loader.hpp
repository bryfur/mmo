#pragma once

#include "../engine/effect_definition.hpp"
#include <string>
#include <memory>
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
    const ::engine::EffectDefinition* get_effect(const std::string& name) const;

    // Check if an effect exists
    bool has_effect(const std::string& name) const;

    // Clear all loaded effects
    void clear();

private:
    std::unordered_map<std::string, ::engine::EffectDefinition> effects_;
};

} // namespace mmo
