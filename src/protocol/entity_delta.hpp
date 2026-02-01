#pragma once

#include "serializable.hpp"

namespace mmo::protocol {

// Compact delta update for frequently-changing fields
struct EntityDeltaUpdate : Serializable<EntityDeltaUpdate> {
    uint32_t id = 0;
    uint8_t flags = 0;  // Bit flags for which fields are present

    // Optional fields (only included if flag is set)
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float health = 0.0f;
    uint8_t is_attacking = 0;
    float attack_dir_x = 0.0f, attack_dir_y = 0.0f;
    float rotation = 0.0f;

    static constexpr uint8_t FLAG_POSITION = 0x01;
    static constexpr uint8_t FLAG_VELOCITY = 0x02;
    static constexpr uint8_t FLAG_HEALTH = 0x04;
    static constexpr uint8_t FLAG_ATTACKING = 0x08;
    static constexpr uint8_t FLAG_ATTACK_DIR = 0x10;
    static constexpr uint8_t FLAG_ROTATION = 0x20;

    // Variable size based on flags
    static size_t serialized_size(uint8_t flags) {
        size_t size = sizeof(uint32_t) + sizeof(uint8_t);  // id + flags
        if (flags & FLAG_POSITION) size += sizeof(float) * 3;
        if (flags & FLAG_VELOCITY) size += sizeof(float) * 2;
        if (flags & FLAG_HEALTH) size += sizeof(float);
        if (flags & FLAG_ATTACKING) size += sizeof(uint8_t);
        if (flags & FLAG_ATTACK_DIR) size += sizeof(float) * 2;
        if (flags & FLAG_ROTATION) size += sizeof(float);
        return size;
    }

    size_t serialized_size() const { return serialized_size(flags); }

    void serialize_impl(BufferWriter& w) const {
        w.write(id);
        w.write(flags);

        if (flags & FLAG_POSITION) { w.write(x); w.write(y); w.write(z); }
        if (flags & FLAG_VELOCITY) { w.write(vx); w.write(vy); }
        if (flags & FLAG_HEALTH) { w.write(health); }
        if (flags & FLAG_ATTACKING) { w.write(is_attacking); }
        if (flags & FLAG_ATTACK_DIR) { w.write(attack_dir_x); w.write(attack_dir_y); }
        if (flags & FLAG_ROTATION) { w.write(rotation); }
    }

    void deserialize_impl(BufferReader& r) {
        id = r.read<uint32_t>();
        flags = r.read<uint8_t>();

        if (flags & FLAG_POSITION) { x = r.read<float>(); y = r.read<float>(); z = r.read<float>(); }
        if (flags & FLAG_VELOCITY) { vx = r.read<float>(); vy = r.read<float>(); }
        if (flags & FLAG_HEALTH) { health = r.read<float>(); }
        if (flags & FLAG_ATTACKING) { is_attacking = r.read<uint8_t>(); }
        if (flags & FLAG_ATTACK_DIR) { attack_dir_x = r.read<float>(); attack_dir_y = r.read<float>(); }
        if (flags & FLAG_ROTATION) { rotation = r.read<float>(); }
    }

    // Bounds-checked deserialize via span — reader throws on overrun
    void deserialize(std::span<const uint8_t> data) {
        BufferReader r(data);
        deserialize_impl(r);
    }
};

} // namespace mmo::protocol
