#include "animation_state_machine.hpp"

#include <algorithm>
#include <cmath>

namespace mmo::engine::animation {

bool TransitionCondition::evaluate(const std::vector<ParamValue>& params) const {
    if (param_index < 0 || param_index >= static_cast<int>(params.size())) return false;

    const auto& val = params[param_index];

    switch (op) {
        case Op::IS_TRUE:
            return val.type == ParamType::Bool && val.b;
        case Op::IS_FALSE:
            return val.type == ParamType::Bool && !val.b;
        case Op::GT:
            return val.type == ParamType::Float && val.f > threshold;
        case Op::LT:
            return val.type == ParamType::Float && val.f < threshold;
        case Op::EQ:
            return val.type == ParamType::Float && std::abs(val.f - threshold) < 0.001f;
        case Op::NE:
            return val.type == ParamType::Float && std::abs(val.f - threshold) >= 0.001f;
    }
    return false;
}

void AnimationStateMachine::add_state(AnimState state) {
    std::string name = state.name;
    std::sort(state.transitions.begin(), state.transitions.end(),
              [](const StateTransition& a, const StateTransition& b) {
                  return a.priority > b.priority;
              });

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
    params_.emplace_back(0.0f);
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

    for (auto& state : states_) {
        for (auto& transition : state.transitions) {
            auto sit = state_name_to_index_.find(transition.target_state);
            transition.target_index = (sit != state_name_to_index_.end()) ? sit->second : -1;

            for (auto& cond : transition.conditions) {
                cond.param_index = ensure_param(cond.param_name);
            }
            for (auto& cond : transition.any_of) {
                cond.param_index = ensure_param(cond.param_name);
            }
        }
    }

    bound_ = true;

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
    params_[idx] = ParamValue{v};
}

void AnimationStateMachine::set_bool(const std::string& name, bool v) {
    int idx = ensure_param(name);
    params_[idx] = ParamValue{v};
}

float AnimationStateMachine::get_float(const std::string& name) const {
    int idx = find_param(name);
    if (idx >= 0 && params_[idx].type == ParamType::Float)
        return params_[idx].f;
    return 0.0f;
}

bool AnimationStateMachine::get_bool(const std::string& name) const {
    int idx = find_param(name);
    if (idx >= 0 && params_[idx].type == ParamType::Bool)
        return params_[idx].b;
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
    player.set_playing(true);
    player.set_loop(state.loop);
    player.set_speed(state.speed);
}

void AnimationStateMachine::update(AnimationPlayer& player) {
    if (!bound_ || current_state_ < 0) return;
    if (current_state_ >= static_cast<int>(states_.size())) return;

    const auto& state = states_[current_state_];

    bool clip_ended = !state.loop && !player.is_playing();

    for (const auto& transition : state.transitions) {
        bool passes = clip_ended;
        if (!passes) {
            passes = true;
            for (const auto& cond : transition.conditions) {
                if (!cond.evaluate(params_)) {
                    passes = false;
                    break;
                }
            }
            if (passes && !transition.any_of.empty()) {
                bool any = false;
                for (const auto& cond : transition.any_of) {
                    if (cond.evaluate(params_)) { any = true; break; }
                }
                passes = any;
            }
        }

        if (passes) {
            enter_state(transition.target_index, player, transition.crossfade_duration);
            return;
        }
    }
}

} // namespace mmo::engine::animation
