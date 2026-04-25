#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <SDL3/SDL_gpu.h>
#include <string>

namespace mmo::engine::render_graph {

enum class ResourceType : uint8_t {
    Texture,
    Buffer,
};

struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTextureUsageFlags usage = 0;
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
    uint32_t sample_count = 1;

    bool operator==(const TextureDesc& other) const noexcept {
        return width == other.width && height == other.height && format == other.format && usage == other.usage &&
               mip_levels == other.mip_levels && array_layers == other.array_layers &&
               sample_count == other.sample_count;
    }
};

struct BufferDesc {
    size_t size = 0;
    SDL_GPUBufferUsageFlags usage = 0;

    bool operator==(const BufferDesc& other) const noexcept { return size == other.size && usage == other.usage; }
};

class ResourceHandle {
public:
    constexpr ResourceHandle() noexcept = default;
    explicit constexpr ResourceHandle(uint32_t idx) noexcept : idx_(idx) {}

    constexpr bool valid() const noexcept { return idx_ != INVALID; }
    constexpr uint32_t index() const noexcept { return idx_; }

    static constexpr ResourceHandle invalid() noexcept { return ResourceHandle{}; }

    constexpr bool operator==(const ResourceHandle& o) const noexcept { return idx_ == o.idx_; }
    constexpr bool operator!=(const ResourceHandle& o) const noexcept { return idx_ != o.idx_; }

private:
    static constexpr uint32_t INVALID = std::numeric_limits<uint32_t>::max();
    uint32_t idx_ = INVALID;
};

struct ResourceNode {
    ResourceType type = ResourceType::Texture;
    std::string name;

    // For transient resources owned by the graph.
    bool transient = false;
    TextureDesc texture_desc{};
    BufferDesc buffer_desc{};

    // For imported resources (graph does not own).
    SDL_GPUTexture* imported_texture = nullptr;
    SDL_GPUBuffer* imported_buffer = nullptr;
    uint32_t imported_width = 0;
    uint32_t imported_height = 0;
    SDL_GPUTextureFormat imported_format = SDL_GPU_TEXTUREFORMAT_INVALID;

    // Resolved at compile-time (transient resources): the real GPU object.
    SDL_GPUTexture* resolved_texture = nullptr;
    SDL_GPUBuffer* resolved_buffer = nullptr;

    // Versioning: each write produces a new "version" (used for cycle detection
    // and so reads can target a specific producer).
    uint32_t version = 0;

    // Lifetime: indices into compiled pass list. UINT32_MAX = unset.
    uint32_t first_write_pass = std::numeric_limits<uint32_t>::max();
    uint32_t last_read_pass = std::numeric_limits<uint32_t>::max();
};

} // namespace mmo::engine::render_graph
