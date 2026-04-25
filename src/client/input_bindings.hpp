#pragma once

#include "engine/input_handler.hpp"
#include <array>
#include <SDL3/SDL_scancode.h>

namespace mmo::client {

// Game-bound named actions. Engine input remains generic; this layer
// translates raw key/mouse state into game semantics (attack, sprint,
// skills, panel toggles, interact).
enum class InputAction {
    Attack,
    Sprint,
    Interact,
    Inventory,
    QuestLog,
    TalentTree,
    Map,
    Skill1,
    Skill2,
    Skill3,
    Skill4,
    Skill5,
    Count
};

// Tracks per-frame action state on top of an engine InputHandler.
// Owns latched-attack semantics (a click sticks until consumed) and
// per-frame edge detection for panel toggles + skill keys.
class InputBindings {
public:
    explicit InputBindings(engine::InputHandler& input);

    // Update once per frame after engine InputHandler::process_events().
    // Reads raw input state and refreshes action edges + held flags.
    void update();

    // Clear per-frame edge flags; call after gameplay has consumed them.
    void clear_edges();

    // Held-state actions
    bool attacking() const { return attacking_; }
    bool sprinting() const { return sprinting_; }

    // Edge actions (true only the frame the key was first pressed)
    bool interact_pressed() const { return action_edges_[idx(InputAction::Interact)]; }
    bool inventory_pressed() const { return action_edges_[idx(InputAction::Inventory)]; }
    bool quest_log_pressed() const { return action_edges_[idx(InputAction::QuestLog)]; }
    bool talent_tree_pressed() const { return action_edges_[idx(InputAction::TalentTree)]; }
    bool map_pressed() const { return action_edges_[idx(InputAction::Map)]; }

    // Returns 0 = none, or 1..5 for which skill slot was pressed this frame.
    int skill_pressed() const;

    // Latched-attack semantics: a click is recorded until consume_attack().
    void consume_attack() { attack_latched_ = false; }

    // If UI consumed a left-click, drop the latched attack so it doesn't
    // turn into a world attack on the next network tick.
    void suppress_attack_for_ui_click();

    // Allow rebinding any action to a different scancode.
    void set_action_key(InputAction action, SDL_Scancode scancode);
    SDL_Scancode action_key(InputAction action) const { return key_bindings_[idx(action)]; }

    // Direct read of the held attack-direction (matches engine input state).
    float attack_dir_x() const;
    float attack_dir_y() const;

private:
    static constexpr int idx(InputAction a) { return static_cast<int>(a); }

    engine::InputHandler& input_;

    static constexpr size_t ACTION_COUNT = static_cast<size_t>(InputAction::Count);
    std::array<SDL_Scancode, ACTION_COUNT> key_bindings_{};
    std::array<bool, ACTION_COUNT> action_edges_{};

    bool attacking_ = false;
    bool sprinting_ = false;
    bool attack_latched_ = false;
};

} // namespace mmo::client
