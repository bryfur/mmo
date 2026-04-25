#include "client/hud/widgets.hpp"
#include "client/hud/hud_layout.hpp"

#include <cstdio>
#include <string>

namespace mmo::client {

using engine::scene::UIScene;

void build_party_frames(UIScene& ui, const HUDState& hud, MouseUI& mui,
                        float /*screen_w*/, float /*screen_h*/) {
    const auto& party = hud.party;
    if (!party.has_party()) return;

    const float x = 20.0f;
    float y = 80.0f;
    const float w = 200.0f;
    const float frame_h = 44.0f;
    const float gap = 6.0f;

    int idx = 0;
    for (const auto& m : party.members) {
        ui.add_filled_rect(x, y, w, frame_h, 0xCC1A1A22);
        ui.add_rect_outline(x, y, w, frame_h, 0xFF555566, 1.0f);
        // Hit-region prevents click-throughs onto world entities and gives a
        // future right-click-to-kick handle when the local player is leader.
        mui.push_region(party_kick_id(idx), WidgetId::None, x, y, w, frame_h);

        const bool is_leader = (m.player_id == party.leader_id);
        const uint32_t title_color = is_leader ? 0xFF00DDFF : 0xFFCCCCCC;
        char nbuf[48];
        std::snprintf(nbuf, sizeof(nbuf), "%s%s  Lv %d",
                      is_leader ? "* " : "  ", m.name.c_str(), m.level);
        ui.add_text(nbuf, x + 6, y + 4, 0.75f, title_color);

        // HP bar
        const float hp_ratio = hud_layout::bar_ratio(m.health, m.max_health);
        ui.add_filled_rect(x + 6, y + 22, w - 12, 7, 0xFF1A0000);
        ui.add_filled_rect(x + 6, y + 22, (w - 12) * hp_ratio, 7,
                           hud_layout::health_bar_color(hp_ratio));

        // Mana bar
        const float mp_ratio = hud_layout::bar_ratio(m.mana, m.max_mana);
        ui.add_filled_rect(x + 6, y + 32, w - 12, 5, 0xFF000033);
        ui.add_filled_rect(x + 6, y + 32, (w - 12) * mp_ratio, 5, 0xFFFF3300);

        y += frame_h + gap;
        ++idx;
    }
}

void build_party_invite_popup(UIScene& ui, const HUDState& hud,
                              float screen_w, float screen_h) {
    const auto& party = hud.party;
    if (party.pending_inviter_id == 0) return;

    const float w = 360.0f;
    const float h = 110.0f;
    const float x = (screen_w - w) * 0.5f;
    const float y = (screen_h - h) * 0.5f - 100.0f;

    ui.add_filled_rect(x, y, w, h, 0xEE111122);
    ui.add_rect_outline(x, y, w, h, 0xFF00DDFF, 2.0f);

    ui.add_text("Party Invite", x + 12, y + 8, 0.95f, 0xFF00DDFF);
    std::string msg = party.pending_inviter_name + " invited you to join their party.";
    if (msg.size() > 48) msg = msg.substr(0, 48) + "...";
    ui.add_text(msg, x + 12, y + 34, 0.78f, 0xFFEEEEEE);
    ui.add_text("[Y] Accept    [N] Decline", x + 12, y + h - 24, 0.8f, 0xFFCCCCCC);
}

} // namespace mmo::client
