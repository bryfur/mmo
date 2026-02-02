#include "render_scene.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace mmo::engine::scene {

void RenderScene::clear() {
    commands_.clear();
    billboards_.clear();
    // NOTE: particle_effect_spawns_ is NOT cleared here - it's cleared by the renderer
    // after consuming the commands, so they persist from update to render

    // Reset world element flags to defaults
    draw_skybox_ = true;
    draw_mountains_ = true;
    draw_rocks_ = true;
    draw_trees_ = true;
    draw_ground_ = true;
    draw_grass_ = true;
}

void RenderScene::add_model(const std::string& model_name, const glm::mat4& transform,
                            const glm::vec4& tint, float attack_tilt, bool no_fog) {
    RenderCommand cmd;
    ModelCommand model_cmd;
    model_cmd.model_name = model_name;
    model_cmd.transform = transform;
    model_cmd.tint = tint;
    model_cmd.attack_tilt = attack_tilt;
    model_cmd.no_fog = no_fog;
    cmd.data = std::move(model_cmd);
    commands_.push_back(std::move(cmd));
}

void RenderScene::add_skinned_model(const std::string& model_name, const glm::mat4& transform,
                                    const std::array<glm::mat4, 64>& bone_matrices,
                                    const glm::vec4& tint) {
    RenderCommand cmd;
    SkinnedModelCommand skinned_cmd;
    skinned_cmd.model_name = model_name;
    skinned_cmd.transform = transform;
    skinned_cmd.bone_matrices = bone_matrices;
    skinned_cmd.tint = tint;
    cmd.data = std::move(skinned_cmd);
    commands_.push_back(std::move(cmd));
}

void RenderScene::add_particle_effect_spawn(const ::engine::EffectDefinition* definition,
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

} // namespace mmo::engine::scene
