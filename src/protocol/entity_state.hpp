#pragma once

#include "serializable.hpp"
#include "message_type.hpp"

namespace mmo::protocol {

struct NetEntityState : Serializable<NetEntityState> {
    uint32_t id = 0;
    EntityType type = EntityType::Player;
    uint8_t player_class = 0;
    uint8_t npc_type = 0;
    uint8_t building_type = 0;
    uint8_t environment_type = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;  // Terrain height/elevation (server-authoritative)
    float vx = 0.0f;
    float vy = 0.0f;
    float rotation = 0.0f;  // Rotation in radians (for buildings)
    float health = 100.0f;
    float max_health = 100.0f;
    uint32_t color = 0xFFFFFFFF;
    char name[32] = {0};
    bool is_attacking = false;
    float attack_cooldown = 0.0f;
    float attack_dir_x = 0.0f;  // Attack direction for visual effects
    float attack_dir_y = 1.0f;

    // Per-instance scale multiplier (1.0 = default size)
    float scale = 1.0f;

    // Rendering data (server-authoritative, client renders using these directly)
    char model_name[32] = {0};       // Model to render (e.g. "warrior", "building_tower")
    float target_size = 0.0f;        // Visual target size in world units
    char effect_type[16] = {0};      // Attack effect: "melee_swing", "projectile", "orbit", ""
    char animation[16] = {0};        // Animation config name (e.g. "humanoid"), empty = none
    float cone_angle = 0.0f;         // Attack cone angle for hit detection and visualization
    bool shows_reticle = false;      // Whether to show targeting reticle

    static constexpr size_t serialized_size() {
        return sizeof(uint32_t) + sizeof(EntityType) + sizeof(uint8_t) * 4 +
               sizeof(float) * 11 + sizeof(uint32_t) + 32 + sizeof(uint8_t) +
               32 + sizeof(float) + 16 + 16 + sizeof(float) + sizeof(uint8_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(id);
        w.write(type);
        w.write(player_class);
        w.write(npc_type);
        w.write(building_type);
        w.write(environment_type);
        w.write(x); w.write(y); w.write(z);
        w.write(vx); w.write(vy);
        w.write(rotation);
        w.write(health); w.write(max_health);
        w.write(color);
        w.write_bytes(name, 32);
        w.write<uint8_t>(is_attacking ? 1 : 0);
        w.write(attack_dir_x); w.write(attack_dir_y);
        w.write(scale);
        w.write_bytes(model_name, 32);
        w.write(target_size);
        w.write_bytes(effect_type, 16);
        w.write_bytes(animation, 16);
        w.write(cone_angle);
        w.write<uint8_t>(shows_reticle ? 1 : 0);
    }

    void deserialize_impl(BufferReader& r) {
        id = r.read<uint32_t>();
        type = r.read<EntityType>();
        player_class = r.read<uint8_t>();
        npc_type = r.read<uint8_t>();
        building_type = r.read<uint8_t>();
        environment_type = r.read<uint8_t>();
        x = r.read<float>(); y = r.read<float>(); z = r.read<float>();
        vx = r.read<float>(); vy = r.read<float>();
        rotation = r.read<float>();
        health = r.read<float>(); max_health = r.read<float>();
        color = r.read<uint32_t>();
        r.read_bytes(name, 32);
        is_attacking = r.read<uint8_t>() != 0;
        attack_dir_x = r.read<float>(); attack_dir_y = r.read<float>();
        scale = r.read<float>();
        r.read_bytes(model_name, 32);
        target_size = r.read<float>();
        r.read_bytes(effect_type, 16);
        r.read_bytes(animation, 16);
        cone_angle = r.read<float>();
        shows_reticle = r.read<uint8_t>() != 0;
    }
};

using EntityState = NetEntityState;
using PlayerState = NetEntityState;

} // namespace mmo::protocol
