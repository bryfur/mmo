#pragma once

#include "animation_player.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

namespace mmo::engine::animation {

using ParamValue = std::variant<float, bool>;

struct TransitionCondition {
    std::string param_name;
    enum class Op { GT, LT, EQ, NE, IS_TRUE, IS_FALSE } op;
    float threshold = 0.0f;

    bool evaluate(const std::unordered_map<std::string, ParamValue>& params) const;
};

struct StateTransition {
    std::string target_state;
    std::vector<TransitionCondition> conditions; // ALL must pass (AND logic)
    float crossfade_duration = 0.2f;
    int priority = 0; // higher = checked first
};

struct AnimState {
    std::string name;
    std::string clip_name;     // animation clip name in model
    int clip_index = -1;       // resolved at bind time
    bool loop = true;
    float speed = 1.0f;
    std::vector<StateTransition> transitions;
};

class AnimationStateMachine {
public:
    AnimationStateMachine() = default;

    // Define states
    void add_state(AnimState state);
    void set_default_state(const std::string& name);

    // Bind clip names to indices from a model's animation list.
    // Call after model is loaded.
    bool bind_clips(const std::vector<AnimationClip>& clips);

    // Set parameters from game logic
    void set_float(const std::string& name, float v);
    void set_bool(const std::string& name, bool v);
    float get_float(const std::string& name) const;
    bool get_bool(const std::string& name) const;

    // Evaluate transitions and drive the AnimationPlayer.
    // Call once per frame before AnimationPlayer::update.
    void update(AnimationPlayer& player);

    const std::string& current_state() const { return current_state_; }
    bool is_bound() const { return bound_; }

private:
    std::unordered_map<std::string, AnimState> states_;
    std::unordered_map<std::string, ParamValue> params_;
    std::string current_state_;
    std::string default_state_;
    bool bound_ = false;

    void enter_state(const std::string& name, AnimationPlayer& player, float crossfade);
};

} // namespace mmo::engine::animation
