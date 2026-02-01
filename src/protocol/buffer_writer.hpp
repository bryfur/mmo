#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace mmo::protocol {

// Lightweight buffer writer with three modes:
//   BufferWriter(span)        - fixed buffer, bounds-checked, no allocations
//   BufferWriter(vec, offset) - write into pre-sized vector at offset
//   BufferWriter(vec)         - append mode, grows the vector on each write
class BufferWriter {
    uint8_t* data_ = nullptr;
    size_t capacity_ = 0;
    size_t offset_ = 0;
    std::vector<uint8_t>* vec_ = nullptr;  // null for span mode

    void ensure(size_t n) {
        if (vec_) {
            if (vec_->size() < offset_ + n) {
                vec_->resize(offset_ + n);
            }
            data_ = vec_->data();
            capacity_ = vec_->size();
        } else if (offset_ + n > capacity_) {
            throw std::out_of_range("BufferWriter: write past end of buffer");
        }
    }

public:
    // Span mode: fixed buffer, bounds-checked
    explicit BufferWriter(std::span<uint8_t> buf)
        : data_(buf.data()), capacity_(buf.size()), offset_(0), vec_(nullptr) {}

    // Offset mode: write into pre-sized vector at offset
    BufferWriter(std::vector<uint8_t>& buf, size_t offset)
        : data_(buf.data()), capacity_(buf.size()), offset_(offset), vec_(&buf) {}

    // Append mode: grows the vector on each write
    explicit BufferWriter(std::vector<uint8_t>& buf)
        : data_(buf.data()), capacity_(buf.size()), offset_(buf.size()), vec_(&buf) {}

    template<typename T>
    void write(const T& val) {
        ensure(sizeof(T));
        std::memcpy(data_ + offset_, &val, sizeof(T));
        offset_ += sizeof(T);
    }

    void write_bytes(std::span<const uint8_t> bytes) {
        ensure(bytes.size());
        std::memcpy(data_ + offset_, bytes.data(), bytes.size());
        offset_ += bytes.size();
    }

    void write_bytes(const void* src, size_t len) {
        write_bytes(std::span<const uint8_t>(static_cast<const uint8_t*>(src), len));
    }

    // Length-prefixed string (uint16_t length + raw bytes, no null terminator)
    void write_string(const std::string& str) {
        write<uint16_t>(static_cast<uint16_t>(str.size()));
        write_bytes(str.data(), str.size());
    }

    // Fixed-size null-padded string
    void write_fixed_string(const std::string& str, size_t max_len) {
        ensure(max_len);
        std::memset(data_ + offset_, 0, max_len);
        size_t len = std::min(str.size(), max_len - 1);
        std::memcpy(data_ + offset_, str.data(), len);
        offset_ += max_len;
    }

    // Write a length-prefixed array of Serializable items
    template<typename T>
    void write_array(const std::vector<T>& items) {
        write<uint16_t>(static_cast<uint16_t>(items.size()));
        for (const auto& item : items) {
            item.serialize(*this);
        }
    }

    size_t offset() const { return offset_; }
};

} // namespace mmo::protocol
