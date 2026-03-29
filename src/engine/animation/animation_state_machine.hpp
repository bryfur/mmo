#pragma once

#include "animation_player.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

namespace mmo::engine::animation {

using ParamValue = std::variant<float, bool>;

struct TransitionCondition {
    std::string param_name;  // used during setup, resolved to param_index at bind time
    int param_index = -1;    // resolved index into params_ vector (hot path)
    enum class Op { GT, LT, EQ, NE, IS_TRUE, IS_FALSE } op;
    float threshold = 0.0f;

    bool evaluate(const std::vector<ParamValue>& params) const;
};

struct StateTransition {
    std::string target_state;   // used during setup, resolved to target_index at bind time
    int target_index = -1;      // resolved index into states_ vector (hot path)
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
    // Also resolves all string references (state names, param names) to integer indices.
    // Call after model is loaded.
    bool bind_clips(const std::vector<AnimationClip>& clips);

    // Set parameters from game logic (string API resolves to index internally)
    void set_float(const std::string& name, float v);
    void set_bool(const std::string& name, bool v);
    float get_float(const std::string& name) const;
    bool get_bool(const std::string& name) const;

    // Evaluate transitions and drive the AnimationPlayer.
    // Call once per frame before AnimationPlayer::update.
    void update(AnimationPlayer& player);

    const std::string& current_state() const;
    bool is_bound() const { return bound_; }

private:
    // States indexed by integer ID
    std::vector<AnimState> states_;
    std::unordered_map<std::string, int> state_name_to_index_;

    // Parameters indexed by integer ID
    std::vector<ParamValue> params_;
    std::unordered_map<std::string, int> param_name_to_index_;

    int current_state_ = -1;
    std::string default_state_;
    bool bound_ = false;

    // Ensure a parameter exists and return its index
    int ensure_param(const std::string& name);
    int find_param(const std::string& name) const;

    void enter_state(int state_index, AnimationPlayer& player, float crossfade);
};

} // namespace mmo::engine::animation
