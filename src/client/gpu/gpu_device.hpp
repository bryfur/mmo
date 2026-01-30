#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <string>
#include <memory>

namespace mmo::gpu {

/**
 * @brief GPU Device wrapper for SDL3 GPU API
 * 
 * This class manages the GPU device lifecycle and provides a clean interface
 * for frame management and resource creation. It encapsulates the SDL3 GPU
 * device and handles the command buffer acquire/submit cycle.
 * 
 * Usage:
 *   GPUDevice device;
 *   device.init(window);
 *   
 *   // Each frame:
 *   auto* cmd = device.begin_frame();
 *   // ... render commands ...
 *   device.end_frame(cmd);
 *   
 *   device.shutdown();
 */
class GPUDevice {
public:
    GPUDevice() = default;
    ~GPUDevice();

    // Non-copyable, non-movable by design:
    // 1. GPUDevice owns the SDL_GPUDevice which manages global GPU state
    // 2. Other GPU resources (buffers, textures, etc.) hold raw pointers to GPUDevice
    // 3. Moving would invalidate those pointers and complicate lifetime management
    // 4. There should typically be only one GPUDevice per application
    GPUDevice(const GPUDevice&) = delete;
    GPUDevice& operator=(const GPUDevice&) = delete;
    GPUDevice(GPUDevice&&) = delete;
    GPUDevice& operator=(GPUDevice&&) = delete;

    /**
     * @brief Initialize the GPU device
     * @param window The SDL window to render to
     * @param prefer_low_power If true, prefer integrated GPU over discrete
     * @return true on success, false on failure
     */
    bool init(SDL_Window* window, bool prefer_low_power = false);

    /**
     * @brief Shutdown and release all GPU resources
     */
    void shutdown();

    /**
     * @brief Check if the device is initialized
     */
    bool is_initialized() const { return device_ != nullptr; }

    // =========================================================================
    // Frame Lifecycle
    // =========================================================================

    /**
     * @brief Begin a new frame and acquire a command buffer
     * @return Command buffer for recording commands, or nullptr on failure
     */
    SDL_GPUCommandBuffer* begin_frame();

    /**
     * @brief End the frame and submit the command buffer
     * @param cmd The command buffer to submit
     */
    void end_frame(SDL_GPUCommandBuffer* cmd);

    /**
     * @brief Acquire the swapchain texture for the current frame
     * @param cmd The command buffer
     * @param out_width Output width of the swapchain texture
     * @param out_height Output height of the swapchain texture
     * @return The swapchain texture, or nullptr if unavailable (e.g., window minimized)
     *
     * @warning Must be called from the thread that created the window.
     *          See: https://wiki.libsdl.org/SDL3/SDL_WaitAndAcquireGPUSwapchainTexture
     */
    SDL_GPUTexture* acquire_swapchain_texture(SDL_GPUCommandBuffer* cmd,
                                                uint32_t* out_width = nullptr,
                                                uint32_t* out_height = nullptr);

    // =========================================================================
    // Resource Creation (Factory Methods)
    // =========================================================================

    /**
     * @brief Create a GPU buffer
     */
    SDL_GPUBuffer* create_buffer(const SDL_GPUBufferCreateInfo& info);

    /**
     * @brief Create a transfer buffer for uploads/downloads
     */
    SDL_GPUTransferBuffer* create_transfer_buffer(const SDL_GPUTransferBufferCreateInfo& info);

    /**
     * @brief Create a GPU texture
     */
    SDL_GPUTexture* create_texture(const SDL_GPUTextureCreateInfo& info);

    /**
     * @brief Create a GPU sampler
     */
    SDL_GPUSampler* create_sampler(const SDL_GPUSamplerCreateInfo& info);

    /**
     * @brief Create a graphics pipeline
     */
    SDL_GPUGraphicsPipeline* create_graphics_pipeline(const SDL_GPUGraphicsPipelineCreateInfo& info);

    /**
     * @brief Create a shader from bytecode
     */
    SDL_GPUShader* create_shader(const SDL_GPUShaderCreateInfo& info);

    // =========================================================================
    // Resource Destruction
    // =========================================================================

    void release_buffer(SDL_GPUBuffer* buffer);
    void release_transfer_buffer(SDL_GPUTransferBuffer* buffer);
    void release_texture(SDL_GPUTexture* texture);
    void release_sampler(SDL_GPUSampler* sampler);
    void release_graphics_pipeline(SDL_GPUGraphicsPipeline* pipeline);
    void release_shader(SDL_GPUShader* shader);

    // =========================================================================
    // Transfer Operations
    // =========================================================================

    /**
     * @brief Map a transfer buffer for CPU access
     */
    void* map_transfer_buffer(SDL_GPUTransferBuffer* buffer, bool cycle);

    /**
     * @brief Unmap a transfer buffer
     */
    void unmap_transfer_buffer(SDL_GPUTransferBuffer* buffer);

    // =========================================================================
    // Accessors
    // =========================================================================

    SDL_GPUDevice* handle() const { return device_; }
    SDL_Window* window() const { return window_; }

    int width() const;
    int height() const;

    /**
     * @brief Get the swapchain texture format
     */
    SDL_GPUTextureFormat swapchain_format() const;

    /**
     * @brief Get the device driver name (e.g., "metal", "vulkan", "d3d12")
     */
    std::string driver_name() const;

    /**
     * @brief Check if a texture format is supported
     */
    bool supports_format(SDL_GPUTextureFormat format, SDL_GPUTextureType type, 
                         SDL_GPUTextureUsageFlags usage) const;

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_Window* window_ = nullptr;
};

} // namespace mmo::gpu
