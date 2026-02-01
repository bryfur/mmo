#include "effect_loader.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace mmo {

namespace {

// Helper functions to parse enums from strings
engine::CurveType parse_curve_type(const std::string& str) {
    if (str == "constant") return engine::CurveType::CONSTANT;
    if (str == "linear") return engine::CurveType::LINEAR;
    if (str == "ease_in") return engine::CurveType::EASE_IN;
    if (str == "ease_out") return engine::CurveType::EASE_OUT;
    if (str == "ease_in_out") return engine::CurveType::EASE_IN_OUT;
    if (str == "fade_out_late") return engine::CurveType::FADE_OUT_LATE;
    return engine::CurveType::CONSTANT;
}

engine::SpawnMode parse_spawn_mode(const std::string& str) {
    if (str == "burst") return engine::SpawnMode::BURST;
    if (str == "continuous") return engine::SpawnMode::CONTINUOUS;
    return engine::SpawnMode::BURST;
}

engine::VelocityType parse_velocity_type(const std::string& str) {
    if (str == "directional") return engine::VelocityType::DIRECTIONAL;
    if (str == "radial") return engine::VelocityType::RADIAL;
    if (str == "orbital") return engine::VelocityType::ORBITAL;
    if (str == "custom") return engine::VelocityType::CUSTOM;
    if (str == "arc") return engine::VelocityType::ARC;
    return engine::VelocityType::DIRECTIONAL;
}

// Parse a curve from JSON
engine::Curve parse_curve(const json& j) {
    engine::Curve curve;

    if (j.is_string()) {
        // Simple string format: just curve type with default values
        curve.type = parse_curve_type(j.get<std::string>());
    } else if (j.is_object()) {
        // Full object format with all properties
        if (j.contains("type")) {
            curve.type = parse_curve_type(j["type"].get<std::string>());
        }
        if (j.contains("start")) curve.start_value = j["start"].get<float>();
        if (j.contains("end")) curve.end_value = j["end"].get<float>();
        if (j.contains("fade_start")) curve.fade_start = j["fade_start"].get<float>();
    } else if (j.is_number()) {
        // Single number = constant value
        curve.type = engine::CurveType::CONSTANT;
        curve.start_value = j.get<float>();
        curve.end_value = j.get<float>();
    }

    return curve;
}

// Parse glm::vec3 from JSON array
glm::vec3 parse_vec3(const json& j, const glm::vec3& default_val = {0, 0, 0}) {
    if (j.is_array() && j.size() >= 3) {
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    }
    return default_val;
}

// Parse glm::vec4 from JSON array
glm::vec4 parse_vec4(const json& j, const glm::vec4& default_val = {1, 1, 1, 1}) {
    if (j.is_array() && j.size() >= 4) {
        return glm::vec4(j[0].get<float>(), j[1].get<float>(),
                         j[2].get<float>(), j[3].get<float>());
    }
    return default_val;
}

// Parse velocity definition
engine::VelocityDefinition parse_velocity(const json& j) {
    engine::VelocityDefinition vel;

    if (j.contains("type")) {
        vel.type = parse_velocity_type(j["type"].get<std::string>());
    }
    if (j.contains("speed")) vel.speed = j["speed"].get<float>();
    if (j.contains("direction")) vel.direction = parse_vec3(j["direction"], {1, 0, 0});
    if (j.contains("spread_angle")) vel.spread_angle = j["spread_angle"].get<float>();
    if (j.contains("gravity")) vel.gravity = parse_vec3(j["gravity"]);
    if (j.contains("drag")) vel.drag = j["drag"].get<float>();
    if (j.contains("orbit_radius")) vel.orbit_radius = j["orbit_radius"].get<float>();
    if (j.contains("orbit_speed")) vel.orbit_speed = j["orbit_speed"].get<float>();
    if (j.contains("orbit_height_base")) vel.orbit_height_base = j["orbit_height_base"].get<float>();
    if (j.contains("height_variation")) vel.height_variation = j["height_variation"].get<float>();
    if (j.contains("arc_radius")) vel.arc_radius = j["arc_radius"].get<float>();
    if (j.contains("arc_height_base")) vel.arc_height_base = j["arc_height_base"].get<float>();
    if (j.contains("arc_height_amplitude")) vel.arc_height_amplitude = j["arc_height_amplitude"].get<float>();
    if (j.contains("arc_tilt_amplitude")) vel.arc_tilt_amplitude = j["arc_tilt_amplitude"].get<float>();

    return vel;
}

// Parse rotation definition
engine::RotationDefinition parse_rotation(const json& j) {
    engine::RotationDefinition rot;

    if (j.contains("initial")) rot.initial_rotation = parse_vec3(j["initial"]);
    if (j.contains("rate")) rot.rotation_rate = parse_vec3(j["rate"]);
    if (j.contains("face_velocity")) rot.face_velocity = j["face_velocity"].get<bool>();

    return rot;
}

// Parse appearance definition
engine::AppearanceDefinition parse_appearance(const json& j) {
    engine::AppearanceDefinition app;

    if (j.contains("scale_over_lifetime")) {
        app.scale_over_lifetime = parse_curve(j["scale_over_lifetime"]);
    }
    if (j.contains("opacity_over_lifetime")) {
        app.opacity_over_lifetime = parse_curve(j["opacity_over_lifetime"]);
    }
    if (j.contains("color_tint")) {
        app.color_tint = parse_vec4(j["color_tint"], {1, 1, 1, 1});
    }
    if (j.contains("color_end")) {
        app.color_end = parse_vec4(j["color_end"], {1, 1, 1, 1});
        app.use_color_gradient = true;
    }

    return app;
}

// Parse emitter definition
engine::EmitterDefinition parse_emitter(const json& j) {
    engine::EmitterDefinition emitter;

    if (j.contains("name")) emitter.name = j["name"].get<std::string>();
    if (j.contains("particle_type")) emitter.particle_type = j["particle_type"].get<std::string>();
    if (j.contains("model")) emitter.model = j["model"].get<std::string>();

    if (j.contains("spawn_mode")) {
        emitter.spawn_mode = parse_spawn_mode(j["spawn_mode"].get<std::string>());
    }
    if (j.contains("spawn_count")) emitter.spawn_count = j["spawn_count"].get<int>();
    if (j.contains("spawn_rate")) emitter.spawn_rate = j["spawn_rate"].get<float>();

    if (j.contains("lifetime")) emitter.particle_lifetime = j["lifetime"].get<float>();

    if (j.contains("velocity")) {
        emitter.velocity = parse_velocity(j["velocity"]);
    }
    if (j.contains("rotation")) {
        emitter.rotation = parse_rotation(j["rotation"]);
    }
    if (j.contains("appearance")) {
        emitter.appearance = parse_appearance(j["appearance"]);
    }

    if (j.contains("delay")) emitter.delay = j["delay"].get<float>();
    if (j.contains("duration")) emitter.duration = j["duration"].get<float>();

    return emitter;
}

// Parse effect definition from JSON
engine::EffectDefinition parse_effect(const json& j) {
    engine::EffectDefinition effect;

    if (j.contains("name")) {
        effect.name = j["name"].get<std::string>();
    }

    if (j.contains("duration")) {
        effect.duration = j["duration"].get<float>();
    }

    if (j.contains("loop")) {
        effect.loop = j["loop"].get<bool>();
    }

    if (j.contains("default_range")) {
        effect.default_range = j["default_range"].get<float>();
    }

    if (j.contains("emitters") && j["emitters"].is_array()) {
        for (const auto& emitter_json : j["emitters"]) {
            effect.emitters.push_back(parse_emitter(emitter_json));
        }
    }

    return effect;
}

} // anonymous namespace

bool EffectRegistry::load_effect(const std::string& file_path) {
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            std::cerr << "Failed to open effect file: " << file_path << std::endl;
            return false;
        }

        json j;
        file >> j;

        engine::EffectDefinition effect = parse_effect(j);

        if (effect.name.empty()) {
            // Use filename as effect name if not specified
            effect.name = fs::path(file_path).stem().string();
        }

        std::string effect_name = effect.name;  // Save name before move
        effects_[effect_name] = std::move(effect);

        std::cout << "Loaded effect: " << effect_name << " from " << file_path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error loading effect from " << file_path << ": " << e.what() << std::endl;
        return false;
    }
}

bool EffectRegistry::load_effects_directory(const std::string& directory_path) {
    if (!fs::exists(directory_path) || !fs::is_directory(directory_path)) {
        std::cerr << "Effects directory does not exist: " << directory_path << std::endl;
        return false;
    }

    bool success = true;
    for (const auto& entry : fs::directory_iterator(directory_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            if (!load_effect(entry.path().string())) {
                success = false;
            }
        }
    }

    return success;
}

const engine::EffectDefinition* EffectRegistry::get_effect(const std::string& name) const {
    auto it = effects_.find(name);
    if (it != effects_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool EffectRegistry::has_effect(const std::string& name) const {
    return effects_.find(name) != effects_.end();
}

void EffectRegistry::clear() {
    effects_.clear();
}

} // namespace mmo
