#pragma once

#include "animation_player.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mmo::engine::animation {

struct TransitionCondition {
    enum class Op : uint8_t { GT, LT, EQ, NE, IS_TRUE, IS_FALSE };

    std::string param_name;
    int param_index = -1;
    Op op = Op::GT;
    float threshold = 0.0f;

    bool evaluate(const std::vector<ParamValue>& params) const;
};

struct StateTransition {
    std::string target_state;
    int target_index = -1;
    // Transition fires when every `conditions` (AND) AND at least one of
    // `any_of` (OR, when non-empty) evaluates true.
    std::vector<TransitionCondition> conditions;
    std::vector<TransitionCondition> any_of;
    float crossfade_duration = 0.2f;
    int priority = 0;
};

struct AnimState {
    std::string name;
    std::string clip_name;
    int clip_index = -1;
    bool loop = true;
    float speed = 1.0f;
    std::vector<StateTransition> transitions;
};

class AnimationStateMachine {
public:
    AnimationStateMachine() = default;

    void add_state(AnimState state);
    void set_default_state(const std::string& name);

    bool bind_clips(const std::vector<AnimationClip>& clips);

    void set_float(const std::string& name, float v);
    void set_bool(const std::string& name, bool v);
    float get_float(const std::string& name) const;
    bool get_bool(const std::string& name) const;

    void update(AnimationPlayer& player);

    const std::string& current_state() const;
    bool is_bound() const { return bound_; }

private:
    std::vector<AnimState> states_;
    std::unordered_map<std::string, int> state_name_to_index_;

    std::vector<ParamValue> params_;
    std::unordered_map<std::string, int> param_name_to_index_;

    int current_state_ = -1;
    std::string default_state_;
    bool bound_ = false;

    int ensure_param(const std::string& name);
    int find_param(const std::string& name) const;

    void enter_state(int state_index, AnimationPlayer& player, float crossfade);
};

} // namespace mmo::engine::animation
