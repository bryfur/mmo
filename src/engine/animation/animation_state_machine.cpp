#include "animation_state_machine.hpp"

#include <algorithm>
#include <cmath>

namespace mmo::engine::animation {

bool TransitionCondition::evaluate(const std::vector<ParamValue>& params) const {
    if (param_index < 0 || param_index >= static_cast<int>(params.size())) return false;

    const auto& val = params[param_index];

    switch (op) {
        case Op::IS_TRUE:
            return std::holds_alternative<bool>(val) && std::get<bool>(val);
        case Op::IS_FALSE:
            return std::holds_alternative<bool>(val) && !std::get<bool>(val);
        case Op::GT:
            return std::holds_alternative<float>(val) && std::get<float>(val) > threshold;
        case Op::LT:
            return std::holds_alternative<float>(val) && std::get<float>(val) < threshold;
        case Op::EQ:
            return std::holds_alternative<float>(val) && std::abs(std::get<float>(val) - threshold) < 0.001f;
        case Op::NE:
            return std::holds_alternative<float>(val) && std::abs(std::get<float>(val) - threshold) >= 0.001f;
    }
    return false;
}

void AnimationStateMachine::add_state(AnimState state) {
    std::string name = state.name;
    // Pre-sort transitions by priority (highest first) so update() doesn't need to sort
    std::sort(state.transitions.begin(), state.transitions.end(),
              [](const StateTransition& a, const StateTransition& b) {
                  return a.priority > b.priority;
              });

    // Check if state already exists (replace it)
    auto it = state_name_to_index_.find(name);
    if (it != state_name_to_index_.end()) {
        states_[it->second] = std::move(state);
    } else {
        int index = static_cast<int>(states_.size());
        state_name_to_index_[name] = index;
        states_.push_back(std::move(state));
    }
}

void AnimationStateMachine::set_default_state(const std::string& name) {
    default_state_ = name;
}

int AnimationStateMachine::ensure_param(const std::string& name) {
    auto it = param_name_to_index_.find(name);
    if (it != param_name_to_index_.end()) return it->second;
    int index = static_cast<int>(params_.size());
    param_name_to_index_[name] = index;
    params_.emplace_back(0.0f);  // default float 0
    return index;
}

int AnimationStateMachine::find_param(const std::string& name) const {
    auto it = param_name_to_index_.find(name);
    if (it != param_name_to_index_.end()) return it->second;
    return -1;
}

bool AnimationStateMachine::bind_clips(const std::vector<AnimationClip>& clips) {
    bool all_found = true;
    for (auto& state : states_) {
        state.clip_index = -1;
        for (size_t i = 0; i < clips.size(); i++) {
            if (clips[i].name == state.clip_name) {
                state.clip_index = static_cast<int>(i);
                break;
            }
        }
        if (state.clip_index < 0) all_found = false;
    }

    // Resolve all string references to integer indices
    // 1. Resolve transition target states and condition param names
    for (auto& state : states_) {
        for (auto& transition : state.transitions) {
            // Resolve target state name to index
            auto sit = state_name_to_index_.find(transition.target_state);
            transition.target_index = (sit != state_name_to_index_.end()) ? sit->second : -1;

            // Resolve condition param names to indices
            for (auto& cond : transition.conditions) {
                cond.param_index = ensure_param(cond.param_name);
            }
        }
    }

    bound_ = true;

    // Enter default state
    if (!default_state_.empty()) {
        auto it = state_name_to_index_.find(default_state_);
        if (it != state_name_to_index_.end()) {
            current_state_ = it->second;
        }
    }

    return all_found;
}

void AnimationStateMachine::set_float(const std::string& name, float v) {
    int idx = ensure_param(name);
    params_[idx] = v;
}

void AnimationStateMachine::set_bool(const std::string& name, bool v) {
    int idx = ensure_param(name);
    params_[idx] = v;
}

float AnimationStateMachine::get_float(const std::string& name) const {
    int idx = find_param(name);
    if (idx >= 0 && std::holds_alternative<float>(params_[idx]))
        return std::get<float>(params_[idx]);
    return 0.0f;
}

bool AnimationStateMachine::get_bool(const std::string& name) const {
    int idx = find_param(name);
    if (idx >= 0 && std::holds_alternative<bool>(params_[idx]))
        return std::get<bool>(params_[idx]);
    return false;
}

const std::string& AnimationStateMachine::current_state() const {
    static const std::string empty;
    if (current_state_ >= 0 && current_state_ < static_cast<int>(states_.size()))
        return states_[current_state_].name;
    return empty;
}

void AnimationStateMachine::enter_state(int state_index,
                                        AnimationPlayer& player,
                                        float crossfade) {
    if (state_index < 0 || state_index >= static_cast<int>(states_.size())) return;

    const auto& state = states_[state_index];
    if (state.clip_index < 0) return;

    current_state_ = state_index;
    player.crossfade_to(state.clip_index, crossfade);
    player.playing = true;
    player.loop = state.loop;
    player.speed = state.speed;
}

void AnimationStateMachine::update(AnimationPlayer& player) {
    if (!bound_ || current_state_ < 0) return;
    if (current_state_ >= static_cast<int>(states_.size())) return;

    const auto& state = states_[current_state_];

    // For non-looping states that have finished playing, force transition out
    bool clip_ended = !state.loop && !player.playing;

    // Transitions are pre-sorted by priority at add_state() time
    for (const auto& transition : state.transitions) {
        // If clip ended, accept any transition (ignore conditions)
        bool all_pass = clip_ended;
        if (!all_pass) {
            all_pass = true;
            for (const auto& cond : transition.conditions) {
                if (!cond.evaluate(params_)) {
                    all_pass = false;
                    break;
                }
            }
        }

        if (all_pass) {
            enter_state(transition.target_index, player, transition.crossfade_duration);
            return;
        }
    }
}

} // namespace mmo::engine::animation
