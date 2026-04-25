#include "panels.hpp"
#include "client/ui_colors.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace mmo::client::hud {

using engine::scene::UIScene;
using namespace engine::ui_colors;

namespace {

// Walk the talent list once to find the (total height, selected y-offset) the
// renderer needs in order to pick a scroll value that keeps the cursor in view.
struct TalentLayoutMetrics {
    float total_h = 0.0f;
    float selected_y_start = 0.0f;
};

TalentLayoutMetrics measure_talent_tree(const PanelState& panel) {
    TalentLayoutMetrics m{};
    std::string prev_branch;
    for (int i = 0; i < static_cast<int>(panel.talent_tree.size()); ++i) {
        const auto& t = panel.talent_tree[i];
        if (t.branch_name != prev_branch) {
            if (!prev_branch.empty()) m.total_h += 6.0f;
            m.total_h += 18.0f;
            prev_branch = t.branch_name;
        }
        if (i == panel.talent_cursor) m.selected_y_start = m.total_h;
        m.total_h += (i == panel.talent_cursor) ? 48.0f : 32.0f;
    }
    return m;
}

void update_scroll(PanelState& panel, const TalentLayoutMetrics& m, float view_h) {
    float& scroll = panel.talent_scroll_offset;
    const float selected_end = m.selected_y_start + 48.0f;
    if (m.selected_y_start - scroll < 0.0f) scroll = m.selected_y_start;
    if (selected_end - scroll > view_h)     scroll = selected_end - view_h;
    if (scroll < 0.0f)                      scroll = 0.0f;
    if (m.total_h <= view_h)                scroll = 0.0f;
}

bool talent_unlocked(const PanelState& panel, const std::string& id) {
    for (const auto& ut : panel.unlocked_talents) {
        if (ut == id) return true;
    }
    return false;
}

} // namespace

void build_talent_panel(UIScene& ui, PanelState& panel, MouseUI& mui,
                        float screen_w, float screen_h) {
    constexpr float w = 420.0f;
    float h = std::clamp(screen_h * 0.8f, 400.0f, 700.0f);
    auto pos = mui.default_pos(WidgetId::TitleTalents,
                               (screen_w - w) * 0.5f, (screen_h - h) * 0.5f);
    const float px = pos.x;
    const float py = pos.y;

    ui.add_filled_rect(px, py, w, h, 0xE0222222);
    ui.add_rect_outline(px, py, w, h, 0xFF888888, 2.0f);

    // Title bar + close
    ui.add_filled_rect(px, py, w, 28.0f, 0xFF553322);
    ui.add_text("TALENT TREE", px + 10.0f, py + 5.0f, 1.0f, WHITE);
    constexpr float close_size = 22.0f;
    const float close_x = px + w - close_size - 4.0f;
    const float close_y = py + 3.0f;
    ui.add_filled_rect(close_x, close_y, close_size, close_size, 0xFF553344);
    ui.add_text("X", close_x + 7.0f, close_y + 4.0f, 0.9f, 0xFFFFFFFF);
    mui.push_region(WidgetId::TitleTalents, WidgetId::TitleTalents,
                    px, py, w - close_size - 8.0f - 110.0f, 28.0f);
    mui.push_region(WidgetId::CloseTalents, WidgetId::TitleTalents,
                    close_x, close_y, close_size, close_size);

    char pts[32];
    std::snprintf(pts, sizeof(pts), "Points: %d", panel.talent_points);
    ui.add_text(pts, px + w - 100.0f, py + 8.0f, 0.7f,
                panel.talent_points > 0 ? 0xFF00FF00 : 0xFFFFCC00);

    // Footer hint
    ui.add_filled_rect(px, py + h - 28.0f, w, 28.0f, 0xE0222222);
    ui.add_text("[W/S] Navigate  [Enter] Unlock  [T/ESC] Close",
                px + 10.0f, py + h - 22.0f, 0.55f, TEXT_HINT);

    if (panel.talent_tree.empty()) {
        ui.add_text("No talents available", px + 20.0f, py + 50.0f, 0.8f, TEXT_HINT);
        return;
    }

    const float content_top = py + 34.0f;
    const float content_bottom = py + h - 32.0f;
    const float view_h = content_bottom - content_top;

    const auto metrics = measure_talent_tree(panel);
    update_scroll(panel, metrics, view_h);
    const float scroll = panel.talent_scroll_offset;

    std::string prev_branch;
    float slot_y = content_top - scroll;

    for (int i = 0; i < static_cast<int>(panel.talent_tree.size()); ++i) {
        const auto& talent = panel.talent_tree[i];
        const bool selected = (i == panel.talent_cursor);

        if (talent.branch_name != prev_branch) {
            if (!prev_branch.empty()) slot_y += 6.0f;
            if (slot_y >= content_top - 18.0f && slot_y < content_bottom) {
                ui.add_text(talent.branch_name, px + 15.0f, slot_y + 1.0f, 0.7f, 0xFF00DDFF);
            }
            slot_y += 18.0f;
            prev_branch = talent.branch_name;
        }

        const float row_h = selected ? 48.0f : 32.0f;
        if (slot_y + row_h > content_top && slot_y < content_bottom) {
            const bool unlocked = talent_unlocked(panel, talent.id);
            const bool prereq_met = talent.prerequisite.empty()
                || talent_unlocked(panel, talent.prerequisite);

            ui.add_filled_rect(px + 10.0f, slot_y, w - 20.0f, row_h,
                               selected ? 0x40FFFFFF : 0x15FFFFFF);

            uint32_t status_color = 0xFF666666;
            const char* status_text = "[--]";
            if (unlocked) {
                status_color = 0xFF00FF00;
                status_text = "[OK]";
            } else if (panel.talent_points > 0 && prereq_met) {
                status_color = 0xFF00AAFF;
                status_text = "[  ]";
            }
            ui.add_text(status_text, px + 15.0f, slot_y + 3.0f, 0.65f, status_color);

            ui.add_text(talent.name, px + 50.0f, slot_y + 3.0f, 0.75f,
                        unlocked ? 0xFF00FF00 : (selected ? WHITE : 0xFFCCCCCC));

            char tier_buf[8];
            std::snprintf(tier_buf, sizeof(tier_buf), "T%d", talent.tier);
            ui.add_text(tier_buf, px + w - 45.0f, slot_y + 4.0f, 0.55f, 0xFF888888);

            if (selected) {
                ui.add_text_wrapped(talent.description, px + 50.0f, slot_y + 18.0f,
                                    w - 100.0f, 0.5f, 0xFFAAAAAA);
            }

            // Click selects the row; Enter (or a 2nd click) actually unlocks —
            // keeps destructive actions deliberate.
            mui.push_region(talent_row_id(i), WidgetId::TitleTalents,
                            px + 10.0f, slot_y, w - 20.0f, row_h);
        }

        slot_y += row_h;
    }

    if (metrics.total_h > view_h) {
        const float bar_h = view_h * (view_h / metrics.total_h);
        const float bar_y = content_top + (scroll / metrics.total_h) * view_h;
        ui.add_filled_rect(px + w - 6.0f, bar_y, 3.0f, bar_h, 0x60FFFFFF);
    }
}

} // namespace mmo::client::hud
