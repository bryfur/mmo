#include "input_handler.hpp"
#include <cmath>
#include <algorithm>

namespace mmo {

InputHandler::InputHandler() {
    // Get initial mouse position
    SDL_GetMouseState(&mouse_x_, &mouse_y_);
    last_mouse_x_ = mouse_x_;
    last_mouse_y_ = mouse_y_;
}

bool InputHandler::process_events() {
    // Reset per-frame deltas
    camera_zoom_delta_ = 0.0f;
    menu_toggle_pressed_ = false;
    menu_up_pressed_ = false;
    menu_down_pressed_ = false;
    menu_left_pressed_ = false;
    menu_right_pressed_ = false;
    menu_select_pressed_ = false;
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        }
        
        // Handle key events for menu
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            switch (event.key.key) {
                case SDLK_ESCAPE:
                    menu_toggle_pressed_ = true;
                    break;
                case SDLK_UP:
                case SDLK_W:
                    if (!game_input_enabled_) menu_up_pressed_ = true;
                    break;
                case SDLK_DOWN:
                case SDLK_S:
                    if (!game_input_enabled_) menu_down_pressed_ = true;
                    break;
                case SDLK_LEFT:
                case SDLK_A:
                    if (!game_input_enabled_) menu_left_pressed_ = true;
                    break;
                case SDLK_RIGHT:
                case SDLK_D:
                    if (!game_input_enabled_) menu_right_pressed_ = true;
                    break;
                case SDLK_RETURN:
                case SDLK_SPACE:
                    if (!game_input_enabled_) menu_select_pressed_ = true;
                    break;
            }
        }
        
        // Only process game input if enabled
        if (game_input_enabled_) {
            // Track mouse button state for camera orbit
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    right_mouse_down_ = true;
                    // Capture mouse for smooth orbiting
                    SDL_SetWindowRelativeMouseMode(SDL_GetWindowFromEvent(&event), true);
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    attacking_ = true;
                }
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    right_mouse_down_ = false;
                    SDL_SetWindowRelativeMouseMode(SDL_GetWindowFromEvent(&event), false);
                }
            }
            
            // Mouse wheel for zoom
            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                camera_zoom_delta_ -= event.wheel.y * 50.0f;
            }
            
            // Mouse motion for camera orbit (only when right button held)
            if (event.type == SDL_EVENT_MOUSE_MOTION && right_mouse_down_) {
                camera_yaw_ += event.motion.xrel * MOUSE_SENSITIVITY;
                camera_pitch_ -= event.motion.yrel * MOUSE_SENSITIVITY;
                
                // Clamp pitch for over-the-shoulder action cam
                // Allow looking up high into sky (-70°) and down toward ground (70°)
                // Terrain collision in renderer prevents ground clipping
                camera_pitch_ = std::clamp(camera_pitch_, -70.0f, 70.0f);
                
                // Normalize yaw
                while (camera_yaw_ < 0.0f) camera_yaw_ += 360.0f;
                while (camera_yaw_ >= 360.0f) camera_yaw_ -= 360.0f;
            }
        }
    }
    
    // Save previous input for change detection
    last_input_ = current_input_;
    
    // Only update game input if enabled
    if (game_input_enabled_) {
        // Update keyboard state
        update_input_from_keyboard();
        
        // Update camera-relative movement direction
        update_camera_from_mouse();
    } else {
        // Clear movement when in menu
        move_forward_ = false;
        move_backward_ = false;
        move_left_ = false;
        move_right_ = false;
        attacking_ = false;
        current_input_.move_dir_x = 0.0f;
        current_input_.move_dir_y = 0.0f;
        current_input_.attacking = false;
    }
    
    // Check if input changed
    input_changed_ = (current_input_.move_up != last_input_.move_up ||
                      current_input_.move_down != last_input_.move_down ||
                      current_input_.move_left != last_input_.move_left ||
                      current_input_.move_right != last_input_.move_right ||
                      current_input_.attacking != last_input_.attacking ||
                      std::abs(current_input_.attack_dir_x - last_input_.attack_dir_x) > 0.01f ||
                      std::abs(current_input_.attack_dir_y - last_input_.attack_dir_y) > 0.01f);
    
    return true;
}

void InputHandler::update_input_from_keyboard() {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    
    move_forward_ = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    move_backward_ = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
    move_left_ = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    move_right_ = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
    sprinting_ = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    
    // Space or left mouse button for attack
    bool space_attack = keys[SDL_SCANCODE_SPACE];
    uint32_t mouse_state = SDL_GetMouseState(nullptr, nullptr);
    bool mouse_attack = (mouse_state & SDL_BUTTON_LMASK) != 0;
    attacking_ = space_attack || mouse_attack;
}

void InputHandler::update_camera_from_mouse() {
    // Use actual camera forward direction (set by renderer, accounts for shoulder offset)
    // This ensures W moves exactly "into the screen" regardless of camera offset
    float forward_x = camera_forward_x_;
    float forward_z = camera_forward_z_;
    
    // Normalize forward (should already be normalized, but be safe)
    float forward_len = std::sqrt(forward_x * forward_x + forward_z * forward_z);
    if (forward_len > 0.001f) {
        forward_x /= forward_len;
        forward_z /= forward_len;
    }
    
    // Right vector (perpendicular to forward)
    float right_x = -forward_z;
    float right_z = forward_x;
    
    // Transform WASD input to world-space movement direction
    float move_x = 0.0f;
    float move_z = 0.0f;
    
    if (move_forward_) {
        move_x += forward_x;
        move_z += forward_z;
    }
    if (move_backward_) {
        move_x -= forward_x;
        move_z -= forward_z;
    }
    if (move_left_) {
        move_x -= right_x;
        move_z -= right_z;
    }
    if (move_right_) {
        move_x += right_x;
        move_z += right_z;
    }
    
    // Normalize movement direction
    float move_len = std::sqrt(move_x * move_x + move_z * move_z);
    if (move_len > 0.001f) {
        move_x /= move_len;
        move_z /= move_len;
    }
    
    // Send continuous movement direction for perfectly smooth movement
    // In the 2D game world: X stays X, Z becomes Y
    current_input_.move_dir_x = move_x;
    current_input_.move_dir_y = move_z;
    
    // Legacy boolean flags (kept for compatibility, not used for movement)
    current_input_.move_up = move_len > 0.1f && move_z < -0.3f;
    current_input_.move_down = move_len > 0.1f && move_z > 0.3f;
    current_input_.move_left = move_len > 0.1f && move_x < -0.3f;
    current_input_.move_right = move_len > 0.1f && move_x > 0.3f;
    
    // Attack direction matches movement forward direction
    current_input_.attack_dir_x = forward_x;
    current_input_.attack_dir_y = forward_z;
    
    current_input_.attacking = attacking_;
}

} // namespace mmo
