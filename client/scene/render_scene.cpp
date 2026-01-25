#include "render_scene.hpp"

namespace mmo {

void RenderScene::clear() {
    commands_.clear();
    entities_.clear();
    entity_shadows_.clear();
    effects_.clear();
    
    // Reset world element flags to defaults
    draw_skybox_ = true;
    draw_mountains_ = true;
    draw_rocks_ = true;
    draw_trees_ = true;
    draw_ground_ = true;
    draw_grass_ = true;
    draw_mountain_shadows_ = true;
    draw_tree_shadows_ = true;
}

void RenderScene::add_model(const std::string& model_name, const glm::mat4& transform,
                            const glm::vec4& tint, float attack_tilt, bool no_fog) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::Model;
    cmd.model.model_name = model_name;
    cmd.model.transform = transform;
    cmd.model.tint = tint;
    cmd.model.attack_tilt = attack_tilt;
    cmd.model.no_fog = no_fog;
    commands_.push_back(std::move(cmd));
}

void RenderScene::add_skinned_model(const std::string& model_name, const glm::mat4& transform,
                                    const std::array<glm::mat4, 64>& bone_matrices,
                                    const glm::vec4& tint) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::SkinnedModel;
    cmd.skinned_model.model_name = model_name;
    cmd.skinned_model.transform = transform;
    cmd.skinned_model.bone_matrices = bone_matrices;
    cmd.skinned_model.tint = tint;
    commands_.push_back(std::move(cmd));
}

void RenderScene::add_entity(const EntityState& state, bool is_local) {
    EntityCommand cmd;
    cmd.state = state;
    cmd.is_local = is_local;
    entities_.push_back(std::move(cmd));
}

void RenderScene::add_entity_shadow(const EntityState& state) {
    EntityShadowCommand cmd;
    cmd.state = state;
    entity_shadows_.push_back(std::move(cmd));
}

void RenderScene::add_effect(const ecs::AttackEffect& effect) {
    EffectCommand cmd;
    cmd.effect = effect;
    effects_.push_back(std::move(cmd));
}

} // namespace mmo
