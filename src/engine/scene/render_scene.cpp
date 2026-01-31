#include "render_scene.hpp"

namespace mmo {

void RenderScene::clear() {
    commands_.clear();
    effects_.clear();

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

void RenderScene::add_effect(const engine::EffectInstance& effect) {
    effects_.push_back(effect);
}

} // namespace mmo
