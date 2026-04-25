#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mmo::engine::memory {

class Arena {
public:
    static constexpr std::size_t k_default_chunk_size = 64 * 1024;

    Arena() = default;
    explicit Arena(std::size_t initial_chunk_size);
    Arena(void* buffer, std::size_t size);

    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;

    void* allocate(std::size_t size, std::size_t align);

    template<typename T, typename... Args> T* create(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        if (!mem) {
            return nullptr;
        }
        T* obj = new (mem) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            register_destructor(obj, [](void* p) { static_cast<T*>(p)->~T(); });
        }
        return obj;
    }

    void reset();

    std::size_t bytes_used() const noexcept;
    std::size_t bytes_capacity() const noexcept;

    bool owns_memory() const noexcept { return owns_memory_; }

private:
    struct Chunk {
        std::byte* base = nullptr;
        std::size_t size = 0;
        std::size_t used = 0;
    };

    struct Destructor {
        void* obj;
        void (*fn)(void*);
    };

    void destroy_all() noexcept;
    void free_chunks() noexcept;
    bool grow(std::size_t min_bytes);

    void register_destructor(void* obj, void (*fn)(void*));

    std::vector<Chunk> chunks_;
    std::vector<Destructor> destructors_;
    std::size_t default_chunk_size_ = k_default_chunk_size;
    bool owns_memory_ = true; // false when constructed from caller-supplied buffer
    bool fixed_buffer_ = false;
};

} // namespace mmo::engine::memory
