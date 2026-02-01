#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace mmo::protocol {

// Lightweight buffer reader with bounds checking via std::span
class BufferReader {
    std::span<const uint8_t> data_;
    size_t offset_ = 0;

    void check_bounds(size_t n) const {
        if (offset_ + n > data_.size()) {
            throw std::out_of_range("BufferReader: read past end of buffer");
        }
    }

public:
    BufferReader(std::span<const uint8_t> data) : data_(data) {}

    template<typename T>
    T read() {
        check_bounds(sizeof(T));
        T val;
        std::memcpy(&val, data_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return val;
    }

    void read_bytes(void* dst, size_t len) {
        check_bounds(len);
        std::memcpy(dst, data_.data() + offset_, len);
        offset_ += len;
    }

    // Length-prefixed string (uint16_t length + raw bytes)
    std::string read_string() {
        uint16_t len = read<uint16_t>();
        check_bounds(len);
        std::string str(reinterpret_cast<const char*>(data_.data() + offset_), len);
        offset_ += len;
        return str;
    }

    // Fixed-size null-padded string (for legacy wire formats)
    std::string read_fixed_string(size_t max_len) {
        check_bounds(max_len);
        const char* ptr = reinterpret_cast<const char*>(data_.data() + offset_);
        std::string str(ptr, strnlen(ptr, max_len));
        offset_ += max_len;
        return str;
    }

    // Get array size (reads and consumes the count prefix)
    uint16_t get_array_size() {
        return read<uint16_t>();
    }

    // Read array elements into a span (caller must ensure span is large enough)
    // Call get_array_size() first to know how many elements to expect
    template<typename T>
    void read_array_into(std::span<T> output, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            output[i].deserialize(remaining());
            offset_ += output[i].serialized_size();
        }
    }

    // Legacy: Read a length-prefixed array of Serializable items (allocates)
    template<typename T>
    std::vector<T> read_array() {
        uint16_t count = read<uint16_t>();
        std::vector<T> items(count);
        for (auto& item : items) {
            item.deserialize(remaining());
            offset_ += item.serialized_size();
        }
        return items;
    }

    size_t offset() const { return offset_; }
    size_t remaining_size() const { return data_.size() - offset_; }
    std::span<const uint8_t> remaining() const { return data_.subspan(offset_); }
};

} // namespace mmo::protocol
