#pragma once

#include "gpu_device.hpp"
#include <memory>
#include <cstddef>

namespace mmo::gpu {

/**
 * @brief GPU Buffer abstraction for vertex, index, uniform, and storage buffers
 * 
 * This class simplifies buffer creation and data upload by handling the transfer
 * buffer lifecycle automatically. It supports both static (upload once) and
 * dynamic (update frequently) buffer patterns.
 * 
 * Usage:
 *   // Static buffer (geometry that doesn't change)
 *   auto vbo = GPUBuffer::create_static(device, GPUBuffer::Type::Vertex, 
 *                                        vertices.data(), vertices.size() * sizeof(Vertex));
 *   
 *   // Dynamic buffer (UI, particles, uniforms that change every frame)
 *   auto ubo = GPUBuffer::create_dynamic(device, GPUBuffer::Type::Uniform, sizeof(Uniforms));
 *   ubo->update(cmd, &uniforms, sizeof(uniforms));
 */
class GPUBuffer {
public:
    enum class Type {
        Vertex,     ///< Vertex buffer for geometry data
        Index,      ///< Index buffer for indexed drawing
        Uniform,    ///< Uniform buffer for shader constants
        Storage,    ///< Storage buffer for compute or large data
    };

    ~GPUBuffer();

    // Non-copyable, movable
    GPUBuffer(const GPUBuffer&) = delete;
    GPUBuffer& operator=(const GPUBuffer&) = delete;
    GPUBuffer(GPUBuffer&& other) noexcept;
    GPUBuffer& operator=(GPUBuffer&& other) noexcept;

    /**
     * @brief Create a static buffer with initial data
     * 
     * Static buffers are optimized for data that is uploaded once and used many times.
     * The data is uploaded immediately via a transfer buffer.
     * 
     * @param device The GPU device
     * @param type Buffer type (Vertex, Index, Uniform, Storage)
     * @param data Pointer to the initial data
     * @param size Size of the data in bytes
     * @return Unique pointer to the buffer, or nullptr on failure
     */
    static std::unique_ptr<GPUBuffer> create_static(GPUDevice& device, Type type,
                                                      const void* data, size_t size);

    /**
     * @brief Create a dynamic buffer for frequent updates
     * 
     * Dynamic buffers maintain an internal transfer buffer for efficient updates.
     * Use update() to upload new data each frame.
     * 
     * @param device The GPU device
     * @param type Buffer type
     * @param size Maximum size of the buffer in bytes
     * @return Unique pointer to the buffer, or nullptr on failure
     */
    static std::unique_ptr<GPUBuffer> create_dynamic(GPUDevice& device, Type type, size_t size);

    /**
     * @brief Update buffer contents (for dynamic buffers)
     * 
     * @param cmd Command buffer for the copy operation
     * @param data Pointer to the new data
     * @param size Size of the data in bytes
     * @param offset Offset into the buffer (default 0)
     */
    void update(SDL_GPUCommandBuffer* cmd, const void* data, size_t size, size_t offset = 0);

    /**
     * @brief Get the raw SDL GPU buffer handle
     */
    SDL_GPUBuffer* handle() const { return buffer_; }

    /**
     * @brief Get the buffer size in bytes
     */
    size_t size() const { return size_; }

    /**
     * @brief Get the buffer type
     */
    Type type() const { return type_; }

    /**
     * @brief Check if this is a dynamic buffer
     */
    bool is_dynamic() const { return transfer_buffer_ != nullptr; }

private:
    GPUBuffer() = default;

    GPUDevice* device_ = nullptr;
    SDL_GPUBuffer* buffer_ = nullptr;
    SDL_GPUTransferBuffer* transfer_buffer_ = nullptr; // For dynamic buffers
    size_t size_ = 0;
    Type type_ = Type::Vertex;

    static SDL_GPUBufferUsageFlags get_usage_flags(Type type);
};

} // namespace mmo::gpu
