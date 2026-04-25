#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace mmo::engine {
struct Model;
}

namespace mmo::engine::procedural {

/**
 * Parameters for procedural tree generation.
 * Based on the ez-tree algorithm by dgreenheck.
 */
struct TreeParams {
    uint32_t seed = 0;
    int levels = 3; // Branch recursion depth (0 = trunk only)

    // Per-level branch parameters (index 0 = trunk, 1+ = child branches)
    static constexpr int MAX_LEVELS = 4;
    float length[MAX_LEVELS] = {20.0f, 20.0f, 10.0f, 1.0f};
    float radius[MAX_LEVELS] = {1.5f, 0.7f, 0.7f, 0.7f};
    float taper[MAX_LEVELS] = {0.7f, 0.7f, 0.7f, 0.7f};
    int children[MAX_LEVELS] = {7, 7, 5, 0};
    float angle[MAX_LEVELS] = {0.0f, 70.0f, 60.0f, 60.0f}; // degrees
    float gnarliness[MAX_LEVELS] = {0.15f, 0.2f, 0.3f, 0.02f};
    float twist[MAX_LEVELS] = {0.0f, 0.0f, 0.0f, 0.0f};
    int sections[MAX_LEVELS] = {12, 10, 8, 6};
    int segments[MAX_LEVELS] = {8, 6, 4, 3};
    float start[MAX_LEVELS] = {0.0f, 0.4f, 0.3f, 0.3f}; // where children begin on parent (0-1)

    // External growth force
    glm::vec3 force_direction = {0.0f, 1.0f, 0.0f};
    float force_strength = 0.01f;

    // Leaf parameters
    int leaf_count = 1;
    float leaf_size = 2.5f;
    float leaf_size_variance = 0.7f;
    float leaf_angle = 10.0f; // degrees from parent branch
    float leaf_start = 0.0f;  // where leaves begin on branch (0-1)
    bool double_billboard = true;

    // Colors (used when no texture is provided)
    glm::vec4 bark_color = {0.45f, 0.32f, 0.22f, 1.0f};
    glm::vec4 leaf_color = {0.2f, 0.5f, 0.15f, 1.0f};

    // Texture paths (empty = use vertex colors instead)
    std::string bark_texture_path;
    std::string leaf_texture_path;

    // Evergreen mode: branches shorten toward top, taper is always 1
    bool evergreen = false;
};

/**
 * Procedural tree mesh generator.
 *
 * Generates a Model with two meshes (branches + leaves) using a
 * breadth-first recursive branching algorithm. The output is compatible
 * with the engine's instanced rendering pipeline.
 */
class TreeGenerator {
public:
    static std::unique_ptr<Model> generate(const TreeParams& params);

    // Preset generators. texture_base_path = directory containing bark/ and leaves/ subdirs.
    static std::unique_ptr<Model> generate_oak(uint32_t seed = 0, const std::string& texture_base_path = "");
    static std::unique_ptr<Model> generate_pine(uint32_t seed = 0, const std::string& texture_base_path = "");
    static std::unique_ptr<Model> generate_dead(uint32_t seed = 0, const std::string& texture_base_path = "");
    static std::unique_ptr<Model> generate_willow(uint32_t seed = 0, const std::string& texture_base_path = "");
    static std::unique_ptr<Model> generate_birch(uint32_t seed = 0, const std::string& texture_base_path = "");
    static std::unique_ptr<Model> generate_maple(uint32_t seed = 0, const std::string& texture_base_path = "");
    static std::unique_ptr<Model> generate_aspen(uint32_t seed = 0, const std::string& texture_base_path = "");
};

} // namespace mmo::engine::procedural
