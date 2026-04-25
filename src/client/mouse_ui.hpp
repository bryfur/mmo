#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mmo::client {

// Per-window persistent screen position + visibility. A window tagged
// `has_position=false` will be centered (or otherwise default-placed) by
// its builder; as soon as the user drags it, `has_position=true` sticks.
struct WindowPos {
    float x = 0.0f;
    float y = 0.0f;
    bool has_position = false;

    void set(float nx, float ny) { x = nx; y = ny; has_position = true; }
};

// Identifier for every interactive region registered this frame.
enum class WidgetId : uint16_t {
    None = 0,

    // Title bars (drag handles)
    TitleInventory = 10,
    TitleQuestLog,
    TitleTalents,
    TitleWorldMap,
    TitleVendor,
    TitleCrafting,
    TitleChat,
    TitleDialogue,

    // Close buttons
    CloseInventory = 30,
    CloseQuestLog,
    CloseTalents,
    CloseWorldMap,
    CloseVendor,
    CloseCrafting,
    CloseDialogue,

    // Vendor rows (32 slots max)
    VendorRowFirst = 100,
    VendorRowLast  = 131,

    // Vendor tab switch (buy / sell)
    VendorTab = 140,

    // Sell tab source: click inventory slot to list in sell mode (reuses inv rows)

    // Inventory slots (MAX_INVENTORY_SLOTS)
    InventorySlotFirst = 200,
    InventorySlotLast  = 219,

    // Inventory action buttons
    InventoryEquipBtn = 230,
    InventoryUseBtn   = 231,

    // Craft rows
    CraftRowFirst = 300,
    CraftRowLast  = 331,
    CraftButton   = 340,

    // Quest log rows
    QuestRowFirst = 400,
    QuestRowLast  = 431,
    QuestAbandonBtn = 440,

    // Talent tree rows
    TalentRowFirst = 500,
    TalentRowLast  = 599,

    // Party frame right-click kick targets (5 slots)
    PartyKickFirst = 600,
    PartyKickLast  = 604,

    // Skill bar buttons (5 slots)
    SkillSlotFirst = 700,
    SkillSlotLast  = 707,

    // Chat channel buttons (5 channels)
    ChatChannelFirst = 800,
    ChatChannelLast  = 804,
};

inline WidgetId vendor_row_id(int i) {
    return static_cast<WidgetId>(static_cast<int>(WidgetId::VendorRowFirst) + i);
}
inline WidgetId inventory_slot_id(int i) {
    return static_cast<WidgetId>(static_cast<int>(WidgetId::InventorySlotFirst) + i);
}
inline WidgetId craft_row_id(int i) {
    return static_cast<WidgetId>(static_cast<int>(WidgetId::CraftRowFirst) + i);
}
inline WidgetId quest_row_id(int i) {
    return static_cast<WidgetId>(static_cast<int>(WidgetId::QuestRowFirst) + i);
}
inline WidgetId talent_row_id(int i) {
    return static_cast<WidgetId>(static_cast<int>(WidgetId::TalentRowFirst) + i);
}
inline WidgetId party_kick_id(int i) {
    return static_cast<WidgetId>(static_cast<int>(WidgetId::PartyKickFirst) + i);
}
inline WidgetId skill_slot_id(int i) {
    return static_cast<WidgetId>(static_cast<int>(WidgetId::SkillSlotFirst) + i);
}
inline WidgetId chat_channel_id(int i) {
    return static_cast<WidgetId>(static_cast<int>(WidgetId::ChatChannelFirst) + i);
}

// Rectangular hit region registered by a panel builder. `window_id` ties
// a region to a draggable window (for z-order / front-to-back lookups).
struct HitRegion {
    WidgetId id = WidgetId::None;
    WidgetId window = WidgetId::None;   // The title bar / window this region belongs to
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

// Stores ordered list of hit regions (later entries drawn on top).
// Cleared each frame before rendering the HUD.
struct MouseUI {
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    bool  left_pressed = false;
    bool  left_held = false;
    bool  left_released = false;
    bool  right_pressed = false;

    std::vector<HitRegion> regions;

    // Active drag state
    WidgetId dragging_window = WidgetId::None;
    float drag_offset_x = 0.0f;
    float drag_offset_y = 0.0f;

    // Click results consumed by application logic (set during process()).
    WidgetId clicked = WidgetId::None;
    WidgetId right_clicked = WidgetId::None;

    // Per-window positions keyed by the title WidgetId.
    std::unordered_map<uint16_t, WindowPos> window_positions;

    void begin_frame(float mx, float my, bool pressed, bool held, bool released,
                     bool right_pressed_edge = false) {
        mouse_x = mx; mouse_y = my;
        left_pressed = pressed; left_held = held; left_released = released;
        right_pressed = right_pressed_edge;
        regions.clear();
        clicked = WidgetId::None;
        right_clicked = WidgetId::None;
    }

    void push_region(WidgetId id, WidgetId window,
                     float x, float y, float w, float h) {
        regions.push_back({id, window, x, y, w, h});
    }

    // Find the topmost region under (mx, my). Iterates in reverse so the
    // region drawn last wins (matches visual z-order).
    const HitRegion* hit_test(float mx, float my) const {
        for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
            if (it->contains(mx, my)) return &*it;
        }
        return nullptr;
    }

    // Returns true if the mouse is currently over any registered region
    // (used to suppress click-to-attack in the world).
    bool over_any_ui() const {
        for (const auto& r : regions) if (r.contains(mouse_x, mouse_y)) return true;
        return false;
    }

    WindowPos& pos_for(WidgetId title_id) {
        return window_positions[static_cast<uint16_t>(title_id)];
    }
    WindowPos default_pos(WidgetId title_id, float def_x, float def_y) {
        auto& p = window_positions[static_cast<uint16_t>(title_id)];
        if (!p.has_position) { p.x = def_x; p.y = def_y; p.has_position = true; }
        return p;
    }

    // Viewport size (set by client each frame via begin_frame). Used to
    // clamp dragged windows to visible area so a user can't lose a panel
    // by dragging it off-screen.
    float viewport_w = 1280.0f;
    float viewport_h = 720.0f;

    // Called once per frame after the HUD has been rendered and all regions
    // pushed: resolves drag state and fires clicked events.
    void process() {
        // Continue an existing drag.
        if (dragging_window != WidgetId::None) {
            if (!left_held) {
                dragging_window = WidgetId::None;
            } else {
                auto& p = pos_for(dragging_window);
                p.x = mouse_x - drag_offset_x;
                p.y = mouse_y - drag_offset_y;
                // Keep at least a 40x24 handle on-screen so the user can
                // always grab the window back even if they drag to a corner.
                const float min_visible_w = 40.0f;
                const float min_visible_h = 24.0f;
                if (p.x + min_visible_w > viewport_w) p.x = viewport_w - min_visible_w;
                if (p.x < -0.0f) p.x = 0.0f;
                if (p.y + min_visible_h > viewport_h) p.y = viewport_h - min_visible_h;
                if (p.y < 0.0f) p.y = 0.0f;
                p.has_position = true;
            }
            return;
        }

        // Start a new drag or click when left button first pressed this frame.
        if (left_pressed) {
            const HitRegion* hit = hit_test(mouse_x, mouse_y);
            if (hit) {
                int raw = static_cast<int>(hit->id);
                bool is_title = (raw >= static_cast<int>(WidgetId::TitleInventory)
                              && raw <  static_cast<int>(WidgetId::CloseInventory));
                if (is_title) {
                    dragging_window = hit->id;
                    auto& p = pos_for(hit->id);
                    drag_offset_x = mouse_x - p.x;
                    drag_offset_y = mouse_y - p.y;
                } else {
                    clicked = hit->id;
                }
            }
        }

        // Right-click events (context menus: party kick, inventory drop, etc.)
        if (right_pressed) {
            const HitRegion* hit = hit_test(mouse_x, mouse_y);
            if (hit) right_clicked = hit->id;
        }
    }
};

} // namespace mmo::client
