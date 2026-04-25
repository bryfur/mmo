#pragma once

#include "serializable.hpp"

namespace mmo::protocol {

struct PlayerInput : Serializable<PlayerInput> {
    bool move_up = false;
    bool move_down = false;
    bool move_left = false;
    bool move_right = false;
    bool attacking = false;
    bool sprinting = false;
    float attack_dir_x = 0.0f; // Normalized attack direction from mouse
    float attack_dir_y = 1.0f;
    float move_dir_x = 0.0f; // Continuous movement direction (normalized)
    float move_dir_y = 0.0f; // For smooth camera-relative movement

    // Field-by-field equality. Don't memcmp this struct: the layout has
    // padding bytes between the bool block and the float block, and those
    // bytes are unspecified — memcmp would report spurious differences.
    bool operator==(const PlayerInput& o) const {
        return move_up == o.move_up && move_down == o.move_down && move_left == o.move_left &&
               move_right == o.move_right && attacking == o.attacking && sprinting == o.sprinting &&
               attack_dir_x == o.attack_dir_x && attack_dir_y == o.attack_dir_y &&
               move_dir_x == o.move_dir_x && move_dir_y == o.move_dir_y;
    }
    bool operator!=(const PlayerInput& o) const { return !(*this == o); }

    // Serialize movement + attack flag only (1 byte)
    uint8_t serialize_flags() const {
        uint8_t flags = 0;
        if (move_up) {
            flags |= 0x01;
        }
        if (move_down) {
            flags |= 0x02;
        }
        if (move_left) {
            flags |= 0x04;
        }
        if (move_right) {
            flags |= 0x08;
        }
        if (attacking) {
            flags |= 0x10;
        }
        if (sprinting) {
            flags |= 0x20;
        }
        return flags;
    }

    void deserialize_flags(uint8_t flags) {
        move_up = ((flags & 0x01) != 0);
        move_down = ((flags & 0x02) != 0);
        move_left = ((flags & 0x04) != 0);
        move_right = ((flags & 0x08) != 0);
        attacking = ((flags & 0x10) != 0);
        sprinting = ((flags & 0x20) != 0);
    }

    static constexpr size_t serialized_size() { return 17; }

    void serialize_impl(BufferWriter& w) const {
        w.write(serialize_flags());
        w.write(attack_dir_x);
        w.write(attack_dir_y);
        w.write(move_dir_x);
        w.write(move_dir_y);
    }

    void deserialize_impl(BufferReader& r) {
        deserialize_flags(r.read<uint8_t>());
        attack_dir_x = r.read<float>();
        attack_dir_y = r.read<float>();
        move_dir_x = r.read<float>();
        move_dir_y = r.read<float>();
    }

    // Bounds-checked deserialize via span (hides base to accept (ptr, size) pairs)
    void deserialize(std::span<const uint8_t> data) {
        if (data.size() >= serialized_size()) {
            BufferReader r(data);
            deserialize_impl(r);
        } else if (data.size() >= 9) {
            deserialize_flags(data[0]);
            BufferReader r(data.subspan(1));
            attack_dir_x = r.read<float>();
            attack_dir_y = r.read<float>();
        } else if (!data.empty()) {
            deserialize_flags(data[0]);
        }
    }
};

} // namespace mmo::protocol
