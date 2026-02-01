#pragma once

#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_joystick.h"
#include "engine/input_state.hpp"
#include <SDL3/SDL.h>

namespace mmo::engine {

class InputHandler {
public:
    InputHandler();
    ~InputHandler();
    
    // Process SDL events, returns false if quit requested
    bool process_events();
    
    // Get current input state
    const engine::InputState& get_input() const { return current_input_; }
    
    // Check if input changed since last frame
    bool input_changed() const { return input_changed_; }
    
    // Reset input changed flag
    void reset_changed() { input_changed_ = false; }
    
    // Set player position for mouse direction calculation
    void set_player_screen_pos(float x, float y) { player_screen_x_ = x; player_screen_y_ = y; }
    
    // Get raw mouse position
    float mouse_x() const { return mouse_x_; }
    float mouse_y() const { return mouse_y_; }
    
    // Camera control - third person action cam style
    float get_camera_yaw() const { return camera_yaw_; }
    float get_camera_pitch() const { return camera_pitch_; }
    float get_camera_zoom_delta() const { return camera_zoom_delta_; }
    void reset_camera_deltas() { camera_zoom_delta_ = 0.0f; }
    
    // Set camera yaw externally (for syncing with renderer)
    void set_camera_yaw(float yaw) { camera_yaw_ = yaw; }
    
    // Raw movement input (before camera-relative transform)
    bool move_forward() const { return move_forward_; }
    bool move_backward() const { return move_backward_; }
    bool move_left() const { return move_left_; }
    bool move_right() const { return move_right_; }
    bool is_attacking() const { return attacking_; }
    bool is_sprinting() const { return sprinting_; }
    void consume_attack() { attack_latched_ = false; }
    
    // Set actual camera forward direction (accounting for shoulder offset)
    void set_camera_forward(float x, float z) { camera_forward_x_ = x; camera_forward_z_ = z; }
    
    // Menu controls
    bool menu_toggle_pressed() const { return menu_toggle_pressed_; }
    bool menu_up_pressed() const { return menu_up_pressed_; }
    bool menu_down_pressed() const { return menu_down_pressed_; }
    bool menu_left_pressed() const { return menu_left_pressed_; }
    bool menu_right_pressed() const { return menu_right_pressed_; }
    bool menu_select_pressed() const { return menu_select_pressed_; }
    void clear_menu_inputs() { 
        menu_toggle_pressed_ = false; 
        menu_up_pressed_ = false; 
        menu_down_pressed_ = false;
        menu_left_pressed_ = false;
        menu_right_pressed_ = false;
        menu_select_pressed_ = false;
    }
    
    // Enable/disable game input (for menu mode)
    void set_game_input_enabled(bool enabled) { game_input_enabled_ = enabled; }
    bool is_game_input_enabled() const { return game_input_enabled_; }
    
    // Controller support
    bool has_controller() const { return gamepad_ != nullptr; }
    const char* get_controller_name() const;
    
    // Camera inversion settings
    bool is_camera_x_inverted() const { return invert_camera_x_; }
    bool is_camera_y_inverted() const { return invert_camera_y_; }
    void set_camera_x_inverted(bool inverted) { invert_camera_x_ = inverted; }
    void set_camera_y_inverted(bool inverted) { invert_camera_y_ = inverted; }
    void toggle_camera_x_invert() { invert_camera_x_ = !invert_camera_x_; }
    void toggle_camera_y_invert() { invert_camera_y_ = !invert_camera_y_; }
    
    // Sensitivity settings
    float get_mouse_sensitivity() const { return mouse_sensitivity_; }
    float get_controller_sensitivity() const { return controller_sensitivity_; }
    void set_mouse_sensitivity(float sens) { mouse_sensitivity_ = sens; }
    void set_controller_sensitivity(float sens) { controller_sensitivity_ = sens; }
    
private:
    void update_input_from_keyboard();
    void update_camera_from_mouse();
    void update_input_from_controller();
    void handle_controller_added(SDL_JoystickID id);
    void handle_controller_removed(SDL_JoystickID id);
    
    engine::InputState current_input_;
    engine::InputState last_input_;
    bool input_changed_ = false;
    
    float mouse_x_ = 0.0f;
    float mouse_y_ = 0.0f;
    float last_mouse_x_ = 0.0f;
    float last_mouse_y_ = 0.0f;
    float player_screen_x_ = 640.0f;
    float player_screen_y_ = 360.0f;
    
    // Raw movement keys
    bool move_forward_ = false;
    bool move_backward_ = false;
    bool move_left_ = false;
    bool move_right_ = false;
    bool attacking_ = false;
    bool attack_latched_ = false;  // Latched on keydown, cleared after network send
    bool sprinting_ = false;
    
    // Camera orbit controls
    float camera_yaw_ = 0.0f;    // Horizontal rotation (degrees)
    float camera_pitch_ = 20.0f; // Vertical angle - lower for over-the-shoulder action view
    float camera_zoom_delta_ = 0.0f;
    bool right_mouse_down_ = false;
    
    // Actual camera forward direction (set by renderer, accounts for shoulder offset)
    float camera_forward_x_ = 0.0f;
    float camera_forward_z_ = -1.0f;
    
    // Menu input state
    bool menu_toggle_pressed_ = false;
    bool menu_up_pressed_ = false;
    bool menu_down_pressed_ = false;
    bool menu_left_pressed_ = false;
    bool menu_right_pressed_ = false;
    bool menu_select_pressed_ = false;
    bool game_input_enabled_ = true;
    
    // Sensitivity settings (configurable)
    float mouse_sensitivity_ = 0.35f;
    float controller_sensitivity_ = 2.5f;
    static constexpr float CONTROLLER_STICK_DEADZONE = 0.15f;
    static constexpr float CONTROLLER_TRIGGER_DEADZONE = 0.1f;
    
    // Camera inversion settings (default: not inverted)
    bool invert_camera_x_ = false;
    bool invert_camera_y_ = false;
    
    // Controller state
    SDL_Gamepad* gamepad_ = nullptr;
    SDL_JoystickID gamepad_id_ = 0;
    
    // Controller input state (for smooth analog input)
    float controller_move_x_ = 0.0f;
    float controller_move_y_ = 0.0f;
    float controller_camera_x_ = 0.0f;
    float controller_camera_y_ = 0.0f;
    bool controller_attack_ = false;
    bool controller_sprint_ = false;
};

} // namespace mmo::engine
