#include "floating_text.hpp"
#include "world_projection.hpp"

#include <algorithm>
#include <cstdio>

namespace mmo::client::hud {

using engine::scene::UIScene;

void build_damage_numbers(UIScene& ui,
                          const HUDState& state,
                          const glm::mat4& view_projection,
                          float screen_w,
                          float screen_h) {
    for (const auto& dn : state.damage_numbers) {
        auto sp = world_to_screen(view_projection,
                                  glm::vec3(dn.x, dn.y, dn.z),
                                  screen_w, screen_h);
        if (!sp) continue;

        const float alpha_ratio = dn.alpha();
        const uint8_t alpha = static_cast<uint8_t>(255.0f * alpha_ratio);
        const uint32_t base_color = dn.is_heal ? 0x0000FF00u : 0x000000FFu;
        const uint32_t color = base_color | (static_cast<uint32_t>(alpha) << 24);

        char text[16];
        const int val = static_cast<int>(dn.damage);
        if (dn.is_heal) {
            std::snprintf(text, sizeof(text), "+%d", val);
        } else {
            std::snprintf(text, sizeof(text), "%d", val);
        }

        // Grow slightly as the number fades for visual emphasis.
        const float scale = 1.0f + (1.0f - alpha_ratio) * 0.3f;
        ui.add_text(text, sp->x - 15.0f, sp->y, scale, color);
    }
}

void build_notifications(UIScene& ui, const HUDState& state, float screen_w) {
    const float cx = screen_w * 0.5f;
    float y = 160.0f;  // Below zone name (~y=60+30) and debug HUD top.

    for (const auto& notif : state.notifications) {
        const float alpha_ratio = std::min(notif.timer / 0.5f, 1.0f);
        const uint8_t alpha = static_cast<uint8_t>(255.0f * alpha_ratio);
        const uint32_t color = (notif.color & 0x00FFFFFFu) |
                               (static_cast<uint32_t>(alpha) << 24);
        const uint32_t bg_alpha = static_cast<uint32_t>(alpha * 0.6f) << 24;

        const float text_w = static_cast<float>(notif.text.size()) * 8.0f;
        ui.add_filled_rect(cx - text_w * 0.5f - 10.0f, y - 5.0f,
                           text_w + 20.0f, 28.0f, bg_alpha);
        ui.add_text(notif.text, cx - text_w * 0.5f, y, 1.2f, color);
        y += 35.0f;
    }
}

} // namespace mmo::client::hud
