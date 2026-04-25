#include "input_bindings.hpp"
#include <SDL3/SDL.h>

namespace mmo::client {

InputBindings::InputBindings(engine::InputHandler& input) : input_(input) {
    key_bindings_[idx(InputAction::Attack)]     = SDL_SCANCODE_SPACE;
    key_bindings_[idx(InputAction::Sprint)]     = SDL_SCANCODE_LSHIFT;
    key_bindings_[idx(InputAction::Interact)]   = SDL_SCANCODE_E;
    key_bindings_[idx(InputAction::Inventory)]  = SDL_SCANCODE_I;
    key_bindings_[idx(InputAction::QuestLog)]   = SDL_SCANCODE_L;
    key_bindings_[idx(InputAction::TalentTree)] = SDL_SCANCODE_T;
    key_bindings_[idx(InputAction::Map)]        = SDL_SCANCODE_M;
    key_bindings_[idx(InputAction::Skill1)]     = SDL_SCANCODE_1;
    key_bindings_[idx(InputAction::Skill2)]     = SDL_SCANCODE_2;
    key_bindings_[idx(InputAction::Skill3)]     = SDL_SCANCODE_3;
    key_bindings_[idx(InputAction::Skill4)]     = SDL_SCANCODE_4;
    key_bindings_[idx(InputAction::Skill5)]     = SDL_SCANCODE_5;
    action_edges_.fill(false);
}

void InputBindings::set_action_key(InputAction action, SDL_Scancode scancode) {
    if (action == InputAction::Count) return;
    key_bindings_[idx(action)] = scancode;
}

void InputBindings::update() {
    // Skip game actions while menus or text input are active.
    bool game_active = input_.is_game_input_enabled() && !input_.is_text_input_enabled();

    // Edge detection for panel/skill keys uses the engine's per-frame
    // just-pressed state. Pressed once -> action_edges_ holds it until
    // clear_edges() is called.
    if (game_active) {
        for (int i = 0; i < idx(InputAction::Count); ++i) {
            if (input_.was_key_just_pressed(key_bindings_[i])) {
                action_edges_[i] = true;
            }
        }

        // Latch attack on left-mouse press (raw edge from engine).
        if (input_.mouse_left_pressed()) {
            attack_latched_ = true;
        }
        // Space + Enter latch attack as well, matching prior behavior.
        if (input_.was_key_just_pressed(SDL_SCANCODE_SPACE) ||
            input_.was_key_just_pressed(SDL_SCANCODE_RETURN)) {
            attack_latched_ = true;
        }

        const bool* keys = SDL_GetKeyboardState(nullptr);
        bool space_attack = keys[SDL_SCANCODE_SPACE];
        bool mouse_attack = input_.mouse_left_held();
        bool kb_sprint = keys[key_bindings_[idx(InputAction::Sprint)]] ||
                         keys[SDL_SCANCODE_RSHIFT];

        attacking_ = space_attack || mouse_attack || attack_latched_ ||
                     input_.controller_attack();
        sprinting_ = kb_sprint || input_.controller_sprint();
    } else {
        attacking_ = false;
        sprinting_ = false;
        attack_latched_ = false;
    }
}

void InputBindings::clear_edges() {
    action_edges_.fill(false);
}

int InputBindings::skill_pressed() const {
    for (int slot = 1; slot <= 5; ++slot) {
        InputAction a = static_cast<InputAction>(idx(InputAction::Skill1) + (slot - 1));
        if (action_edges_[idx(a)]) return slot;
    }
    return 0;
}

void InputBindings::suppress_attack_for_ui_click() {
    attacking_ = false;
    attack_latched_ = false;
}

float InputBindings::attack_dir_x() const {
    return input_.get_input().attack_dir_x;
}

float InputBindings::attack_dir_y() const {
    return input_.get_input().attack_dir_y;
}

} // namespace mmo::client
