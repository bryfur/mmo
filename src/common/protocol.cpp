#include "protocol.hpp"

namespace mmo {

void NetEntityState::serialize(std::vector<uint8_t>& buffer) const {
    size_t offset = buffer.size();
    buffer.resize(offset + serialized_size());
    
    std::memcpy(buffer.data() + offset, &id, sizeof(id));
    offset += sizeof(id);
    std::memcpy(buffer.data() + offset, &type, sizeof(type));
    offset += sizeof(type);
    std::memcpy(buffer.data() + offset, &player_class, sizeof(player_class));
    offset += sizeof(player_class);
    std::memcpy(buffer.data() + offset, &npc_type, sizeof(npc_type));
    offset += sizeof(npc_type);
    std::memcpy(buffer.data() + offset, &building_type, sizeof(building_type));
    offset += sizeof(building_type);
    std::memcpy(buffer.data() + offset, &environment_type, sizeof(environment_type));
    offset += sizeof(environment_type);
    std::memcpy(buffer.data() + offset, &x, sizeof(x));
    offset += sizeof(x);
    std::memcpy(buffer.data() + offset, &y, sizeof(y));
    offset += sizeof(y);
    std::memcpy(buffer.data() + offset, &z, sizeof(z));
    offset += sizeof(z);
    std::memcpy(buffer.data() + offset, &vx, sizeof(vx));
    offset += sizeof(vx);
    std::memcpy(buffer.data() + offset, &vy, sizeof(vy));
    offset += sizeof(vy);
    std::memcpy(buffer.data() + offset, &rotation, sizeof(rotation));
    offset += sizeof(rotation);
    std::memcpy(buffer.data() + offset, &health, sizeof(health));
    offset += sizeof(health);
    std::memcpy(buffer.data() + offset, &max_health, sizeof(max_health));
    offset += sizeof(max_health);
    std::memcpy(buffer.data() + offset, &color, sizeof(color));
    offset += sizeof(color);
    std::memcpy(buffer.data() + offset, name, 32);
    offset += 32;
    uint8_t attacking_byte = is_attacking ? 1 : 0;
    std::memcpy(buffer.data() + offset, &attacking_byte, sizeof(attacking_byte));
    offset += sizeof(attacking_byte);
    std::memcpy(buffer.data() + offset, &attack_dir_x, sizeof(attack_dir_x));
    offset += sizeof(attack_dir_x);
    std::memcpy(buffer.data() + offset, &attack_dir_y, sizeof(attack_dir_y));
    offset += sizeof(attack_dir_y);
    std::memcpy(buffer.data() + offset, &scale, sizeof(scale));
    offset += sizeof(scale);
    std::memcpy(buffer.data() + offset, model_name, 32);
    offset += 32;
    std::memcpy(buffer.data() + offset, &target_size, sizeof(target_size));
    offset += sizeof(target_size);
    std::memcpy(buffer.data() + offset, effect_type, 16);
    offset += 16;
    std::memcpy(buffer.data() + offset, effect_model, 32);
    offset += 32;
    std::memcpy(buffer.data() + offset, &effect_duration, sizeof(effect_duration));
    offset += sizeof(effect_duration);
    std::memcpy(buffer.data() + offset, &cone_angle, sizeof(cone_angle));
    offset += sizeof(cone_angle);
    uint8_t reticle_byte = shows_reticle ? 1 : 0;
    std::memcpy(buffer.data() + offset, &reticle_byte, sizeof(reticle_byte));
}

void NetEntityState::deserialize(const uint8_t* data) {
    size_t offset = 0;
    std::memcpy(&id, data + offset, sizeof(id));
    offset += sizeof(id);
    std::memcpy(&type, data + offset, sizeof(type));
    offset += sizeof(type);
    std::memcpy(&player_class, data + offset, sizeof(player_class));
    offset += sizeof(player_class);
    std::memcpy(&npc_type, data + offset, sizeof(npc_type));
    offset += sizeof(npc_type);
    std::memcpy(&building_type, data + offset, sizeof(building_type));
    offset += sizeof(building_type);
    std::memcpy(&environment_type, data + offset, sizeof(environment_type));
    offset += sizeof(environment_type);
    std::memcpy(&x, data + offset, sizeof(x));
    offset += sizeof(x);
    std::memcpy(&y, data + offset, sizeof(y));
    offset += sizeof(y);
    std::memcpy(&z, data + offset, sizeof(z));
    offset += sizeof(z);
    std::memcpy(&vx, data + offset, sizeof(vx));
    offset += sizeof(vx);
    std::memcpy(&vy, data + offset, sizeof(vy));
    offset += sizeof(vy);
    std::memcpy(&rotation, data + offset, sizeof(rotation));
    offset += sizeof(rotation);
    std::memcpy(&health, data + offset, sizeof(health));
    offset += sizeof(health);
    std::memcpy(&max_health, data + offset, sizeof(max_health));
    offset += sizeof(max_health);
    std::memcpy(&color, data + offset, sizeof(color));
    offset += sizeof(color);
    std::memcpy(name, data + offset, 32);
    offset += 32;
    uint8_t attacking_byte;
    std::memcpy(&attacking_byte, data + offset, sizeof(attacking_byte));
    is_attacking = attacking_byte != 0;
    offset += sizeof(attacking_byte);
    std::memcpy(&attack_dir_x, data + offset, sizeof(attack_dir_x));
    offset += sizeof(attack_dir_x);
    std::memcpy(&attack_dir_y, data + offset, sizeof(attack_dir_y));
    offset += sizeof(attack_dir_y);
    std::memcpy(&scale, data + offset, sizeof(scale));
    offset += sizeof(scale);
    std::memcpy(model_name, data + offset, 32);
    offset += 32;
    std::memcpy(&target_size, data + offset, sizeof(target_size));
    offset += sizeof(target_size);
    std::memcpy(effect_type, data + offset, 16);
    offset += 16;
    std::memcpy(effect_model, data + offset, 32);
    offset += 32;
    std::memcpy(&effect_duration, data + offset, sizeof(effect_duration));
    offset += sizeof(effect_duration);
    std::memcpy(&cone_angle, data + offset, sizeof(cone_angle));
    offset += sizeof(cone_angle);
    uint8_t reticle_byte;
    std::memcpy(&reticle_byte, data + offset, sizeof(reticle_byte));
    shows_reticle = reticle_byte != 0;
}

void Packet::write_uint8(uint8_t value) {
    payload_.push_back(value);
    header_.payload_size = static_cast<uint32_t>(payload_.size());
}

void Packet::write_uint16(uint16_t value) {
    size_t offset = payload_.size();
    payload_.resize(offset + sizeof(value));
    std::memcpy(payload_.data() + offset, &value, sizeof(value));
    header_.payload_size = static_cast<uint32_t>(payload_.size());
}

void Packet::write_uint32(uint32_t value) {
    size_t offset = payload_.size();
    payload_.resize(offset + sizeof(value));
    std::memcpy(payload_.data() + offset, &value, sizeof(value));
    header_.payload_size = static_cast<uint32_t>(payload_.size());
}

void Packet::write_float(float value) {
    size_t offset = payload_.size();
    payload_.resize(offset + sizeof(value));
    std::memcpy(payload_.data() + offset, &value, sizeof(value));
    header_.payload_size = static_cast<uint32_t>(payload_.size());
}

void Packet::write_string(const std::string& str, size_t max_len) {
    size_t len = std::min(str.size(), max_len - 1);
    size_t offset = payload_.size();
    payload_.resize(offset + max_len);
    std::memset(payload_.data() + offset, 0, max_len);
    std::memcpy(payload_.data() + offset, str.data(), len);
    header_.payload_size = static_cast<uint32_t>(payload_.size());
}

void Packet::write_entity_state(const NetEntityState& state) {
    state.serialize(payload_);
    header_.payload_size = static_cast<uint32_t>(payload_.size());
}

void Packet::write_class_info(const ClassInfo& info) {
    info.serialize(payload_);
    header_.payload_size = static_cast<uint32_t>(payload_.size());
}

std::vector<uint8_t> Packet::build() const {
    std::vector<uint8_t> data(PacketHeader::size() + payload_.size());
    header_.serialize(data.data());
    if (!payload_.empty()) {
        std::memcpy(data.data() + PacketHeader::size(), payload_.data(), payload_.size());
    }
    return data;
}

void ClassInfo::serialize(std::vector<uint8_t>& buffer) const {
    size_t offset = buffer.size();
    buffer.resize(offset + serialized_size());

    std::memcpy(buffer.data() + offset, name, 32);
    offset += 32;
    std::memcpy(buffer.data() + offset, short_desc, 32);
    offset += 32;
    std::memcpy(buffer.data() + offset, desc_line1, 64);
    offset += 64;
    std::memcpy(buffer.data() + offset, desc_line2, 64);
    offset += 64;
    std::memcpy(buffer.data() + offset, model_name, 32);
    offset += 32;
    std::memcpy(buffer.data() + offset, &color, sizeof(color));
    offset += sizeof(color);
    std::memcpy(buffer.data() + offset, &select_color, sizeof(select_color));
    offset += sizeof(select_color);
    std::memcpy(buffer.data() + offset, &ui_color, sizeof(ui_color));
    offset += sizeof(ui_color);
    uint8_t reticle_byte = shows_reticle ? 1 : 0;
    std::memcpy(buffer.data() + offset, &reticle_byte, sizeof(reticle_byte));
}

void ClassInfo::deserialize(const uint8_t* data) {
    size_t offset = 0;
    std::memcpy(name, data + offset, 32);
    offset += 32;
    std::memcpy(short_desc, data + offset, 32);
    offset += 32;
    std::memcpy(desc_line1, data + offset, 64);
    offset += 64;
    std::memcpy(desc_line2, data + offset, 64);
    offset += 64;
    std::memcpy(model_name, data + offset, 32);
    offset += 32;
    std::memcpy(&color, data + offset, sizeof(color));
    offset += sizeof(color);
    std::memcpy(&select_color, data + offset, sizeof(select_color));
    offset += sizeof(select_color);
    std::memcpy(&ui_color, data + offset, sizeof(ui_color));
    offset += sizeof(ui_color);
    uint8_t reticle_byte;
    std::memcpy(&reticle_byte, data + offset, sizeof(reticle_byte));
    shows_reticle = reticle_byte != 0;
}

} // namespace mmo
