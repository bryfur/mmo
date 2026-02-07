#pragma once

#include "engine/animation/animation_state_machine.hpp"
#include "engine/animation/animation_types.hpp"
#include <string>
#include <unordered_map>

namespace mmo {

namespace animation = mmo::engine::animation;

struct AnimationConfig {
    animation::AnimationStateMachine state_machine;
    animation::ProceduralConfig procedural;
    std::string name;
};

class AnimationRegistry {
public:
    bool load_config(const std::string& file_path);
    bool load_directory(const std::string& directory_path);
    const AnimationConfig* get_config(const std::string& name) const;
    bool has_config(const std::string& name) const;
    void clear();

private:
    std::unordered_map<std::string, AnimationConfig> configs_;
};

} // namespace mmo
