#pragma once

#include "serializable.hpp"

namespace mmo::protocol {

// Class information sent from server to client for class selection UI
struct ClassInfo : Serializable<ClassInfo> {
    char name[32] = {0};             // Display name (e.g. "WARRIOR")
    char short_desc[32] = {0};       // Short description (e.g. "High HP, Melee")
    char desc_line1[64] = {0};       // Full description line 1
    char desc_line2[64] = {0};       // Full description line 2
    char model_name[32] = {0};       // Model for preview
    uint32_t color = 0xFFFFFFFF;     // Class color (ARGB)
    uint32_t select_color = 0xFFFFFFFF; // Background color for select screen
    uint32_t ui_color = 0xFFFFFFFF;  // UI accent color
    bool shows_reticle = false;      // Whether class shows targeting reticle

    static constexpr size_t serialized_size() {
        return 32 + 32 + 64 + 64 + 32 + sizeof(uint32_t) * 3 + sizeof(uint8_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(name, 32);
        w.write_bytes(short_desc, 32);
        w.write_bytes(desc_line1, 64);
        w.write_bytes(desc_line2, 64);
        w.write_bytes(model_name, 32);
        w.write(color);
        w.write(select_color);
        w.write(ui_color);
        w.write<uint8_t>(shows_reticle ? 1 : 0);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(name, 32);
        r.read_bytes(short_desc, 32);
        r.read_bytes(desc_line1, 64);
        r.read_bytes(desc_line2, 64);
        r.read_bytes(model_name, 32);
        color = r.read<uint32_t>();
        select_color = r.read<uint32_t>();
        ui_color = r.read<uint32_t>();
        shows_reticle = r.read<uint8_t>() != 0;
    }
};

} // namespace mmo::protocol
