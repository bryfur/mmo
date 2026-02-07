#include "animation_state_machine.hpp"

#include <algorithm>
#include <cmath>

namespace mmo::engine::animation {

bool TransitionCondition::evaluate(const std::unordered_map<std::string, ParamValue>& params) const {
    auto it = params.find(param_name);
    if (it == params.end()) return false;

    const auto& val = it->second;

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
    states_[name] = std::move(state);
}

void AnimationStateMachine::set_default_state(const std::string& name) {
    default_state_ = name;
}

bool AnimationStateMachine::bind_clips(const std::vector<AnimationClip>& clips) {
    bool all_found = true;
    for (auto& [name, state] : states_) {
        state.clip_index = -1;
        for (size_t i = 0; i < clips.size(); i++) {
            if (clips[i].name == state.clip_name) {
                state.clip_index = static_cast<int>(i);
                break;
            }
        }
        if (state.clip_index < 0) all_found = false;
    }
    bound_ = true;

    // Enter default state
    if (!default_state_.empty()) {
        current_state_ = default_state_;
    }

    return all_found;
}

void AnimationStateMachine::set_float(const std::string& name, float v) {
    params_[name] = v;
}

void AnimationStateMachine::set_bool(const std::string& name, bool v) {
    params_[name] = v;
}

float AnimationStateMachine::get_float(const std::string& name) const {
    auto it = params_.find(name);
    if (it != params_.end() && std::holds_alternative<float>(it->second))
        return std::get<float>(it->second);
    return 0.0f;
}

bool AnimationStateMachine::get_bool(const std::string& name) const {
    auto it = params_.find(name);
    if (it != params_.end() && std::holds_alternative<bool>(it->second))
        return std::get<bool>(it->second);
    return false;
}

void AnimationStateMachine::enter_state(const std::string& name,
                                        AnimationPlayer& player,
                                        float crossfade) {
    auto it = states_.find(name);
    if (it == states_.end()) return;

    const auto& state = it->second;
    if (state.clip_index < 0) return;

    current_state_ = name;
    player.crossfade_to(state.clip_index, crossfade);
    player.playing = true;
    player.loop = state.loop;
    player.speed = state.speed;
}

void AnimationStateMachine::update(AnimationPlayer& player) {
    if (!bound_ || current_state_.empty()) return;

    auto it = states_.find(current_state_);
    if (it == states_.end()) return;

    const auto& state = it->second;

    // For non-looping states that have finished playing, force transition out
    bool clip_ended = !state.loop && !player.playing;

    // Collect and sort transitions by priority (highest first)
    std::vector<const StateTransition*> sorted;
    sorted.reserve(state.transitions.size());
    for (const auto& t : state.transitions) {
        sorted.push_back(&t);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const StateTransition* a, const StateTransition* b) {
                  return a->priority > b->priority;
              });

    for (const auto* transition : sorted) {
        // If clip ended, accept any transition (ignore conditions)
        bool all_pass = clip_ended;
        if (!all_pass) {
            all_pass = true;
            for (const auto& cond : transition->conditions) {
                if (!cond.evaluate(params_)) {
                    all_pass = false;
                    break;
                }
            }
        }

        if (all_pass) {
            enter_state(transition->target_state, player, transition->crossfade_duration);
            return;
        }
    }
}

} // namespace mmo::engine::animation
