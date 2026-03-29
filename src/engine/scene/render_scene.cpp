#include "render_scene.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace mmo::engine::scene {

void RenderScene::clear() {
    model_commands_.clear();
    skinned_commands_.clear();
    billboards_.clear();
    debug_lines_.clear();
    // NOTE: particle_effect_spawns_ is NOT cleared here - it's cleared by the renderer
    // after consuming the commands, so they persist from update to render

    // Reset world element flags to defaults
    draw_skybox_ = true;
    draw_rocks_ = true;
    draw_trees_ = true;
    draw_ground_ = true;
    draw_grass_ = true;
}

void RenderScene::add_model(mmo::engine::ModelHandle handle, const glm::mat4& transform,
                            const glm::vec4& tint, bool force_non_instanced, bool no_fog) {
    ModelCommand cmd;
    cmd.model_handle = handle;
    cmd.transform = transform;
    cmd.tint = tint;
    cmd.force_non_instanced = force_non_instanced;
    cmd.no_fog = no_fog;
    model_commands_.push_back(std::move(cmd));
}

void RenderScene::add_model(std::string model_name, const glm::mat4& transform,
                            const glm::vec4& tint, bool force_non_instanced, bool no_fog) {
    ModelCommand cmd;
    cmd.model_name = std::move(model_name);
    cmd.transform = transform;
    cmd.tint = tint;
    cmd.force_non_instanced = force_non_instanced;
    cmd.no_fog = no_fog;
    model_commands_.push_back(std::move(cmd));
}

void RenderScene::add_skinned_model(mmo::engine::ModelHandle handle, const glm::mat4& transform,
                                    const std::array<glm::mat4, 64>& bone_matrices,
                                    const glm::vec4& tint) {
    SkinnedModelCommand cmd;
    cmd.model_handle = handle;
    cmd.transform = transform;
    cmd.bone_matrices = &bone_matrices;
    cmd.tint = tint;
    skinned_commands_.push_back(std::move(cmd));
}

void RenderScene::add_skinned_model(std::string model_name, const glm::mat4& transform,
                                    const std::array<glm::mat4, 64>& bone_matrices,
                                    const glm::vec4& tint) {
    SkinnedModelCommand cmd;
    cmd.model_name = std::move(model_name);
    cmd.transform = transform;
    cmd.bone_matrices = &bone_matrices;
    cmd.tint = tint;
    skinned_commands_.push_back(std::move(cmd));
}

void RenderScene::add_particle_effect_spawn(const mmo::engine::EffectDefinition* definition,
                                             const glm::vec3& position,
                                             const glm::vec3& direction,
                                             float range) {
    ParticleEffectSpawnCommand cmd;
    cmd.definition = definition;
    cmd.position = position;
    cmd.direction = direction;
    cmd.range = range;
    particle_effect_spawns_.push_back(cmd);
}

void RenderScene::add_billboard_3d(float world_x, float world_y, float world_z,
                                    float width, float fill_ratio,
                                    uint32_t fill_color, uint32_t bg_color, uint32_t frame_color) {
    billboards_.push_back({world_x, world_y, world_z, width, fill_ratio,
                           fill_color, bg_color, frame_color});
}

// ============================================================================
// Debug Drawing
// ============================================================================

void RenderScene::add_debug_line(const glm::vec3& start, const glm::vec3& end, uint32_t color) {
    debug_lines_.push_back({start, end, color});
}

void RenderScene::add_debug_sphere(const glm::vec3& center, float radius, uint32_t color, int segments) {
    const float step = 2.0f * 3.14159265358979f / static_cast<float>(segments);

    // XZ plane (horizontal circle)
    for (int i = 0; i < segments; ++i) {
        float a0 = step * static_cast<float>(i);
        float a1 = step * static_cast<float>(i + 1);
        glm::vec3 p0 = center + glm::vec3(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
        glm::vec3 p1 = center + glm::vec3(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
        debug_lines_.push_back({p0, p1, color});
    }

    // XY plane (vertical circle facing Z)
    for (int i = 0; i < segments; ++i) {
        float a0 = step * static_cast<float>(i);
        float a1 = step * static_cast<float>(i + 1);
        glm::vec3 p0 = center + glm::vec3(std::cos(a0) * radius, std::sin(a0) * radius, 0.0f);
        glm::vec3 p1 = center + glm::vec3(std::cos(a1) * radius, std::sin(a1) * radius, 0.0f);
        debug_lines_.push_back({p0, p1, color});
    }

    // YZ plane (vertical circle facing X)
    for (int i = 0; i < segments; ++i) {
        float a0 = step * static_cast<float>(i);
        float a1 = step * static_cast<float>(i + 1);
        glm::vec3 p0 = center + glm::vec3(0.0f, std::cos(a0) * radius, std::sin(a0) * radius);
        glm::vec3 p1 = center + glm::vec3(0.0f, std::cos(a1) * radius, std::sin(a1) * radius);
        debug_lines_.push_back({p0, p1, color});
    }
}

void RenderScene::add_debug_box(const glm::vec3& min, const glm::vec3& max, uint32_t color) {
    // 8 corners of the AABB
    glm::vec3 c[8] = {
        {min.x, min.y, min.z},  // 0: ---
        {max.x, min.y, min.z},  // 1: +--
        {max.x, min.y, max.z},  // 2: +-+
        {min.x, min.y, max.z},  // 3: --+
        {min.x, max.y, min.z},  // 4: -+-
        {max.x, max.y, min.z},  // 5: ++-
        {max.x, max.y, max.z},  // 6: +++
        {min.x, max.y, max.z},  // 7: -++
    };

    // 12 edges
    // Bottom face
    debug_lines_.push_back({c[0], c[1], color});
    debug_lines_.push_back({c[1], c[2], color});
    debug_lines_.push_back({c[2], c[3], color});
    debug_lines_.push_back({c[3], c[0], color});
    // Top face
    debug_lines_.push_back({c[4], c[5], color});
    debug_lines_.push_back({c[5], c[6], color});
    debug_lines_.push_back({c[6], c[7], color});
    debug_lines_.push_back({c[7], c[4], color});
    // Vertical edges
    debug_lines_.push_back({c[0], c[4], color});
    debug_lines_.push_back({c[1], c[5], color});
    debug_lines_.push_back({c[2], c[6], color});
    debug_lines_.push_back({c[3], c[7], color});
}

} // namespace mmo::engine::scene
