// Game asset loading — model registration for the runtime ModelManager.
// Method of mmo::client::Game (declared in game.hpp).

#include "engine/model_loader.hpp"
#include "engine/procedural/rock_generator.hpp"
#include "engine/procedural/tree_generator.hpp"
#include "game.hpp"
#include <future>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace mmo::client {

using namespace mmo::engine;
using namespace mmo::engine::procedural;

bool Game::load_models(const std::string& assets_path) {
    auto& mdl = models();
    std::string models_path = assets_path + "/models/";

    bool success = true;

    using mmo::engine::INVALID_MODEL_HANDLE;
    if (mdl.load_model("warrior", models_path + "warrior_rigged.glb") == INVALID_MODEL_HANDLE) {
        if (mdl.load_model("warrior", models_path + "warrior.glb") == INVALID_MODEL_HANDLE) {
            success = false;
        }
    }
    if (mdl.load_model("mage", models_path + "mage_rigged.glb") == INVALID_MODEL_HANDLE) {
        if (mdl.load_model("mage", models_path + "mage.glb") == INVALID_MODEL_HANDLE) {
            success = false;
        }
    }
    if (mdl.load_model("paladin", models_path + "paladin_rigged.glb") == INVALID_MODEL_HANDLE) {
        if (mdl.load_model("paladin", models_path + "paladin.glb") == INVALID_MODEL_HANDLE) {
            success = false;
        }
    }
    if (mdl.load_model("archer", models_path + "archer_rigged.glb") == INVALID_MODEL_HANDLE) {
        if (mdl.load_model("archer", models_path + "archer.glb") == INVALID_MODEL_HANDLE) {
            success = false;
        }
    }
    if (mdl.load_model("npc_enemy", models_path + "npc_enemy.glb") == INVALID_MODEL_HANDLE) {
        success = false;
    }

    mdl.load_model("ground_grass", models_path + "ground_grass.glb");
    mdl.load_model("ground_stone", models_path + "ground_stone.glb");

    mdl.load_model("mountain_small", models_path + "mountain_small.glb");
    mdl.load_model("mountain_medium", models_path + "mountain_medium.glb");
    mdl.load_model("mountain_large", models_path + "mountain_large.glb");

    mdl.load_model("building_tavern", models_path + "building_tavern.glb");
    mdl.load_model("building_blacksmith", models_path + "building_blacksmith.glb");
    mdl.load_model("building_tower", models_path + "building_tower.glb");
    mdl.load_model("building_shop", models_path + "building_shop.glb");
    mdl.load_model("building_well", models_path + "building_well.glb");
    mdl.load_model("building_house", models_path + "building_house.glb");
    mdl.load_model("building_inn", models_path + "inn.glb");
    mdl.load_model("wooden_log", models_path + "wooden_log.glb");
    mdl.load_model("log_tower", models_path + "log_tower.glb");

    mdl.load_model("npc_merchant", models_path + "npc_merchant.glb");
    mdl.load_model("npc_guard", models_path + "npc_guard.glb");
    mdl.load_model("npc_blacksmith", models_path + "npc_blacksmith.glb");
    mdl.load_model("npc_innkeeper", models_path + "npc_innkeeper.glb");
    mdl.load_model("npc_villager", models_path + "npc_villager.glb");

    mdl.load_model("weapon_sword", models_path + "weapon_sword.glb");
    mdl.load_model("spell_fireball", models_path + "spell_fireball.glb");
    mdl.load_model("spell_bible", models_path + "spell_bible.glb");

    // Procedural rocks + trees: CPU-side generation is heavy (recursive branching,
    // SDF marching cubes, leaf placement) and independent per asset. Run all
    // generations in parallel via std::async; do the GPU upload (register_model)
    // sequentially on the main thread because the GPU command queue is serial.
    using ModelTask = std::future<std::pair<std::string, std::unique_ptr<Model>>>;
    std::vector<ModelTask> tasks;
    tasks.reserve(26);

    using RockGen = std::unique_ptr<Model> (*)(uint32_t, const std::string&);
    auto launch_rock = [&](const char* name, RockGen gen_fn) {
        tasks.push_back(std::async(std::launch::async, [name, gen_fn]() {
            return std::make_pair(std::string(name), gen_fn(42, std::string{}));
        }));
    };
    launch_rock("rock_boulder", &procedural::RockGenerator::generate_boulder);
    launch_rock("rock_slate", &procedural::RockGenerator::generate_slate);
    launch_rock("rock_spire", &procedural::RockGenerator::generate_spire);
    launch_rock("rock_cluster", &procedural::RockGenerator::generate_cluster);
    launch_rock("rock_mossy", &procedural::RockGenerator::generate_mossy);

    std::string tex_path = assets_path + "/textures";
    struct TreePreset {
        const char* name;
        std::unique_ptr<Model> (*gen)(uint32_t, const std::string&);
    };
    TreePreset presets[] = {
        {"tree_oak", procedural::TreeGenerator::generate_oak},
        {"tree_pine", procedural::TreeGenerator::generate_pine},
        {"tree_dead", procedural::TreeGenerator::generate_dead},
        {"tree_willow", procedural::TreeGenerator::generate_willow},
        {"tree_birch", procedural::TreeGenerator::generate_birch},
        {"tree_maple", procedural::TreeGenerator::generate_maple},
        {"tree_aspen", procedural::TreeGenerator::generate_aspen},
    };
    for (auto& preset : presets) {
        for (int v = 0; v < 3; v++) {
            std::string variant_name = std::string(preset.name) + "_" + std::to_string(v);
            uint32_t seed = static_cast<uint32_t>(v * 1000 + 42);
            tasks.push_back(
                std::async(std::launch::async, [name = std::move(variant_name), seed, tex_path, gen = preset.gen]() {
                    return std::make_pair(name, gen(seed, tex_path));
                }));
        }
    }

    for (auto& t : tasks) {
        auto [name, model] = t.get();
        if (model) {
            mdl.register_model(name, std::move(model));
        }
    }

    if (success) {
        std::cout << "All 3D models loaded successfully" << '\n';
    } else {
        std::cerr << "Warning: Some models failed to load" << '\n';
    }

    return success;
}

} // namespace mmo::client
