#include "input_handler.hpp"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_joystick.h"
#include "SDL3/SDL_keyboard.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_scancode.h"
#include "SDL3/SDL_stdinc.h"
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace mmo::engine {

InputHandler::InputHandler() {
    // Get initial mouse position
    SDL_GetMouseState(&mouse_x_, &mouse_y_);
    last_mouse_x_ = mouse_x_;
    last_mouse_y_ = mouse_y_;
    
    // Initialize controller subsystem
    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
        SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    }
    
    // Check for any already-connected controllers
    int num_joysticks = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&num_joysticks);
    if (joysticks) {
        for (int i = 0; i < num_joysticks && !gamepad_; i++) {
            if (SDL_IsGamepad(joysticks[i])) {
                handle_controller_added(joysticks[i]);
            }
        }
        SDL_free(joysticks);
    }
}

InputHandler::~InputHandler() {
    if (gamepad_) {
        SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
    }
}

const char* InputHandler::get_controller_name() const {
    if (gamepad_) {
        return SDL_GetGamepadName(gamepad_);
    }
    return "No Controller";
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
        
        // Handle controller connection/disconnection
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            handle_controller_added(event.gdevice.which);
        }
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            handle_controller_removed(event.gdevice.which);
        }
        
        // Handle controller button events for menu
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && gamepad_) {
            switch (event.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_START:
                    menu_toggle_pressed_ = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    if (!game_input_enabled_) menu_up_pressed_ = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    if (!game_input_enabled_) menu_down_pressed_ = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    if (!game_input_enabled_) menu_left_pressed_ = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    if (!game_input_enabled_) menu_right_pressed_ = true;
                    break;
                case SDL_GAMEPAD_BUTTON_SOUTH: // A button
                    if (!game_input_enabled_) menu_select_pressed_ = true;
                    break;
            }
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
                    if (!game_input_enabled_) {
                        menu_select_pressed_ = true;
                    } else {
                        attack_latched_ = true;
                    }
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
                    attack_latched_ = true;
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
                float x_mult = invert_camera_x_ ? 1.0f : -1.0f;
                float y_mult = invert_camera_y_ ? -1.0f : 1.0f;
                
                camera_yaw_ += event.motion.xrel * mouse_sensitivity_ * x_mult;
                camera_pitch_ += event.motion.yrel * mouse_sensitivity_ * y_mult;
                
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
    
    // Update controller state
    if (gamepad_ && game_input_enabled_) {
        update_input_from_controller();
    }
    
    // Only update game input if enabled
    if (game_input_enabled_) {
        // Update keyboard state
        update_input_from_keyboard();
        
        // Update camera-relative movement direction
        update_camera_from_mouse();
        
        // Apply controller camera input (smooth analog)
        if (gamepad_) {
            float x_mult = invert_camera_x_ ? 1.0f : -1.0f;
            float y_mult = invert_camera_y_ ? -1.0f : 1.0f;
            
            camera_yaw_ += controller_camera_x_ * controller_sensitivity_ * x_mult;
            camera_pitch_ += controller_camera_y_ * controller_sensitivity_ * y_mult;
            
            // Clamp pitch
            camera_pitch_ = std::clamp(camera_pitch_, -70.0f, 70.0f);
            
            // Normalize yaw
            while (camera_yaw_ < 0.0f) camera_yaw_ += 360.0f;
            while (camera_yaw_ >= 360.0f) camera_yaw_ -= 360.0f;
        }
    } else {
        // Clear movement when in menu
        move_forward_ = false;
        move_backward_ = false;
        move_left_ = false;
        move_right_ = false;
        attacking_ = false;
        attack_latched_ = false;
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
    
    // Keyboard input
    bool kb_forward = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    bool kb_backward = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
    bool kb_left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    bool kb_right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
    bool kb_sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    
    // Merge with controller digital input (left stick beyond deadzone)
    move_forward_ = kb_forward || (controller_move_y_ < -0.5f);
    move_backward_ = kb_backward || (controller_move_y_ > 0.5f);
    move_left_ = kb_left || (controller_move_x_ < -0.5f);
    move_right_ = kb_right || (controller_move_x_ > 0.5f);
    sprinting_ = kb_sprint || controller_sprint_;
    
    // Space or left mouse button for attack (or controller)
    bool space_attack = keys[SDL_SCANCODE_SPACE];
    uint32_t mouse_state = SDL_GetMouseState(nullptr, nullptr);
    bool mouse_attack = (mouse_state & SDL_BUTTON_LMASK) != 0;
    attacking_ = space_attack || mouse_attack || controller_attack_ || attack_latched_;
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
    
    // Check if controller has analog movement input
    bool has_controller_analog = (std::abs(controller_move_x_) > 0.01f || 
                                   std::abs(controller_move_y_) > 0.01f);
    
    if (has_controller_analog) {
        // Use smooth analog controller input
        // Controller Y is inverted (up is negative), map to forward
        move_x = controller_move_x_ * right_x + (-controller_move_y_) * forward_x;
        move_z = controller_move_x_ * right_z + (-controller_move_y_) * forward_z;
    } else {
        // Use digital keyboard input
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
    }
    
    // Normalize movement direction
    float move_len = std::sqrt(move_x * move_x + move_z * move_z);
    if (move_len > 1.0f) {
        // Only normalize if magnitude > 1 (allow controller to have partial movement)
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

void InputHandler::update_input_from_controller() {
    if (!gamepad_) return;
    
    // Read left stick for movement (raw axis values are -32768 to 32767)
    float raw_lx = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float raw_ly = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    
    // Apply deadzone to left stick
    float left_magnitude = std::sqrt(raw_lx * raw_lx + raw_ly * raw_ly);
    if (left_magnitude > CONTROLLER_STICK_DEADZONE) {
        // Rescale to 0-1 range outside deadzone
        float scale = (left_magnitude - CONTROLLER_STICK_DEADZONE) / (1.0f - CONTROLLER_STICK_DEADZONE);
        scale = std::min(scale, 1.0f);
        controller_move_x_ = (raw_lx / left_magnitude) * scale;
        controller_move_y_ = (raw_ly / left_magnitude) * scale;
    } else {
        controller_move_x_ = 0.0f;
        controller_move_y_ = 0.0f;
    }
    
    // Read right stick for camera (raw axis values are -32768 to 32767)
    float raw_rx = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
    float raw_ry = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
    
    // Apply deadzone to right stick
    float right_magnitude = std::sqrt(raw_rx * raw_rx + raw_ry * raw_ry);
    if (right_magnitude > CONTROLLER_STICK_DEADZONE) {
        float scale = (right_magnitude - CONTROLLER_STICK_DEADZONE) / (1.0f - CONTROLLER_STICK_DEADZONE);
        scale = std::min(scale, 1.0f);
        controller_camera_x_ = (raw_rx / right_magnitude) * scale;
        controller_camera_y_ = (raw_ry / right_magnitude) * scale;
    } else {
        controller_camera_x_ = 0.0f;
        controller_camera_y_ = 0.0f;
    }
    
    // Read triggers for attack (right trigger) and sprint (left trigger)
    float right_trigger = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;
    float left_trigger = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
    
    controller_attack_ = right_trigger > CONTROLLER_TRIGGER_DEADZONE || 
                         SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_SOUTH); // A button
    controller_sprint_ = left_trigger > CONTROLLER_TRIGGER_DEADZONE ||
                         SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER); // LB button
    
    // Bumpers for zoom
    if (SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        camera_zoom_delta_ += 2.0f; // Zoom out
    }
}

void InputHandler::handle_controller_added(SDL_JoystickID id) {
    // Only connect one controller at a time
    if (gamepad_) return;
    
    if (SDL_IsGamepad(id)) {
        gamepad_ = SDL_OpenGamepad(id);
        if (gamepad_) {
            gamepad_id_ = id;
            printf("Controller connected: %s\n", SDL_GetGamepadName(gamepad_));
        }
    }
}

void InputHandler::handle_controller_removed(SDL_JoystickID id) {
    if (gamepad_ && gamepad_id_ == id) {
        printf("Controller disconnected: %s\n", SDL_GetGamepadName(gamepad_));
        SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
        gamepad_id_ = 0;
        
        // Reset controller input state
        controller_move_x_ = 0.0f;
        controller_move_y_ = 0.0f;
        controller_camera_x_ = 0.0f;
        controller_camera_y_ = 0.0f;
        controller_attack_ = false;
        controller_sprint_ = false;
        
        // Try to find another controller
        int num_joysticks = 0;
        SDL_JoystickID* joysticks = SDL_GetJoysticks(&num_joysticks);
        if (joysticks) {
            for (int i = 0; i < num_joysticks && !gamepad_; i++) {
                if (SDL_IsGamepad(joysticks[i])) {
                    handle_controller_added(joysticks[i]);
                }
            }
            SDL_free(joysticks);
        }
    }
}

} // namespace mmo::engine
