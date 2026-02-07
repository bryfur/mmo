#include "animation_loader.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace mmo {

namespace {

using namespace mmo::engine::animation;

TransitionCondition::Op parse_op(const std::string& str) {
    if (str == "gt") return TransitionCondition::Op::GT;
    if (str == "lt") return TransitionCondition::Op::LT;
    if (str == "eq") return TransitionCondition::Op::EQ;
    if (str == "ne") return TransitionCondition::Op::NE;
    if (str == "is_true") return TransitionCondition::Op::IS_TRUE;
    if (str == "is_false") return TransitionCondition::Op::IS_FALSE;
    return TransitionCondition::Op::GT;
}

TransitionCondition parse_condition(const json& j) {
    TransitionCondition cond;
    cond.param_name = j.value("param", "");
    cond.op = parse_op(j.value("op", "gt"));
    cond.threshold = j.value("value", 0.0f);
    return cond;
}

StateTransition parse_transition(const json& j) {
    StateTransition t;
    t.target_state = j.value("to", "");
    t.crossfade_duration = j.value("crossfade", 0.2f);
    t.priority = j.value("priority", 0);

    if (j.contains("conditions") && j["conditions"].is_array()) {
        for (const auto& cj : j["conditions"]) {
            t.conditions.push_back(parse_condition(cj));
        }
    }
    return t;
}

AnimState parse_state(const std::string& name, const json& j) {
    AnimState state;
    state.name = name;
    state.clip_name = j.value("clip", name);
    state.loop = j.value("loop", true);
    state.speed = j.value("speed", 1.0f);

    if (j.contains("transitions") && j["transitions"].is_array()) {
        for (const auto& tj : j["transitions"]) {
            state.transitions.push_back(parse_transition(tj));
        }
    }
    return state;
}

ProceduralConfig parse_procedural(const json& j) {
    ProceduralConfig cfg;
    cfg.foot_ik = j.value("foot_ik", true);
    cfg.lean = j.value("lean", true);
    cfg.forward_lean_factor = j.value("forward_lean_factor", 0.015f);
    cfg.forward_lean_max = j.value("forward_lean_max", 0.18f);
    cfg.lateral_lean_factor = j.value("lateral_lean_factor", 0.06f);
    cfg.lateral_lean_max = j.value("lateral_lean_max", 0.15f);
    cfg.attack_tilt_max = j.value("attack_tilt_max", 0.4f);
    cfg.attack_tilt_cooldown = j.value("attack_tilt_cooldown", 0.5f);
    return cfg;
}

AnimationConfig parse_config(const json& j) {
    AnimationConfig config;

    if (j.contains("name")) {
        config.name = j["name"].get<std::string>();
    }

    // Parse states
    if (j.contains("states") && j["states"].is_object()) {
        for (auto& [name, state_json] : j["states"].items()) {
            config.state_machine.add_state(parse_state(name, state_json));
        }
    }

    // Default state
    if (j.contains("default_state")) {
        config.state_machine.set_default_state(j["default_state"].get<std::string>());
    }

    // Procedural tuning
    if (j.contains("procedural")) {
        config.procedural = parse_procedural(j["procedural"]);
    }

    return config;
}

} // anonymous namespace

bool AnimationRegistry::load_config(const std::string& file_path) {
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            std::cerr << "Failed to open animation config: " << file_path << std::endl;
            return false;
        }

        json j;
        file >> j;

        AnimationConfig config = parse_config(j);

        if (config.name.empty()) {
            config.name = fs::path(file_path).stem().string();
        }

        std::string config_name = config.name;
        configs_[config_name] = std::move(config);

        std::cout << "Loaded animation config: " << config_name << " from " << file_path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error loading animation config from " << file_path << ": " << e.what() << std::endl;
        return false;
    }
}

bool AnimationRegistry::load_directory(const std::string& directory_path) {
    if (!fs::exists(directory_path) || !fs::is_directory(directory_path)) {
        std::cerr << "Animation config directory does not exist: " << directory_path << std::endl;
        return false;
    }

    bool success = true;
    for (const auto& entry : fs::directory_iterator(directory_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            if (!load_config(entry.path().string())) {
                success = false;
            }
        }
    }

    return success;
}

const AnimationConfig* AnimationRegistry::get_config(const std::string& name) const {
    auto it = configs_.find(name);
    if (it != configs_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool AnimationRegistry::has_config(const std::string& name) const {
    return configs_.find(name) != configs_.end();
}

void AnimationRegistry::clear() {
    configs_.clear();
}

} // namespace mmo
