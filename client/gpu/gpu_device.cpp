#include "gpu_device.hpp"
#include <SDL3/SDL_log.h>

namespace mmo::gpu {

GPUDevice::~GPUDevice() {
    shutdown();
}

bool GPUDevice::init(SDL_Window* window, bool prefer_low_power) {
    if (device_) {
        SDL_Log("GPUDevice::init: Already initialized");
        return false;
    }

    window_ = window;

    // Create the GPU device with automatic backend selection
    // SDL3 will choose the best available backend (Metal on macOS, Vulkan/D3D12 on others)
    device_ = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_METALLIB | SDL_GPU_SHADERFORMAT_DXIL,
        true,  // debug mode
        nullptr // prefer no specific driver
    );

    if (!device_) {
        SDL_Log("GPUDevice::init: Failed to create GPU device: %s", SDL_GetError());
        return false;
    }

    // Claim the window for the GPU device
    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        SDL_Log("GPUDevice::init: Failed to claim window: %s", SDL_GetError());
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
        return false;
    }

    SDL_Log("GPUDevice::init: Initialized with driver '%s'", driver_name().c_str());
    return true;
}

void GPUDevice::shutdown() {
    if (device_) {
        // Wait for all GPU operations to complete
        SDL_WaitForGPUIdle(device_);
        
        if (window_) {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
        }
        
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
        window_ = nullptr;
        
        SDL_Log("GPUDevice::shutdown: Device destroyed");
    }
}

SDL_GPUCommandBuffer* GPUDevice::begin_frame() {
    if (!device_) {
        return nullptr;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        SDL_Log("GPUDevice::begin_frame: Failed to acquire command buffer: %s", SDL_GetError());
        return nullptr;
    }

    return cmd;
}

void GPUDevice::end_frame(SDL_GPUCommandBuffer* cmd) {
    if (!cmd) {
        return;
    }

    SDL_SubmitGPUCommandBuffer(cmd);
}

SDL_GPUTexture* GPUDevice::acquire_swapchain_texture(SDL_GPUCommandBuffer* cmd,
                                                       uint32_t* out_width,
                                                       uint32_t* out_height) {
    if (!cmd || !window_) {
        SDL_Log("GPUDevice::acquire_swapchain_texture: cmd=%p window=%p", (void*)cmd, (void*)window_);
        return nullptr;
    }

    SDL_GPUTexture* swapchain_texture = nullptr;
    uint32_t w = 0, h = 0;

    if (!SDL_AcquireGPUSwapchainTexture(cmd, window_, &swapchain_texture, &w, &h)) {
        SDL_Log("GPUDevice::acquire_swapchain_texture: SDL call failed: %s", SDL_GetError());
        return nullptr;
    }

    // NULL texture with success means window is minimized or not ready
    if (!swapchain_texture) {
        static int null_count = 0;
        if (++null_count <= 3) {
            SDL_Log("GPUDevice::acquire_swapchain_texture: SDL returned success but NULL texture (attempt %d, w=%u h=%u)",
                    null_count, w, h);
        }
        return nullptr;
    }

    if (out_width) *out_width = w;
    if (out_height) *out_height = h;

    return swapchain_texture;
}

// =============================================================================
// Resource Creation
// =============================================================================

SDL_GPUBuffer* GPUDevice::create_buffer(const SDL_GPUBufferCreateInfo& info) {
    if (!device_) return nullptr;
    return SDL_CreateGPUBuffer(device_, &info);
}

SDL_GPUTransferBuffer* GPUDevice::create_transfer_buffer(const SDL_GPUTransferBufferCreateInfo& info) {
    if (!device_) return nullptr;
    return SDL_CreateGPUTransferBuffer(device_, &info);
}

SDL_GPUTexture* GPUDevice::create_texture(const SDL_GPUTextureCreateInfo& info) {
    if (!device_) return nullptr;
    return SDL_CreateGPUTexture(device_, &info);
}

SDL_GPUSampler* GPUDevice::create_sampler(const SDL_GPUSamplerCreateInfo& info) {
    if (!device_) return nullptr;
    return SDL_CreateGPUSampler(device_, &info);
}

SDL_GPUGraphicsPipeline* GPUDevice::create_graphics_pipeline(const SDL_GPUGraphicsPipelineCreateInfo& info) {
    if (!device_) return nullptr;
    return SDL_CreateGPUGraphicsPipeline(device_, &info);
}

SDL_GPUShader* GPUDevice::create_shader(const SDL_GPUShaderCreateInfo& info) {
    if (!device_) return nullptr;
    return SDL_CreateGPUShader(device_, &info);
}

// =============================================================================
// Resource Destruction
// =============================================================================

void GPUDevice::release_buffer(SDL_GPUBuffer* buffer) {
    if (device_ && buffer) {
        SDL_ReleaseGPUBuffer(device_, buffer);
    }
}

void GPUDevice::release_transfer_buffer(SDL_GPUTransferBuffer* buffer) {
    if (device_ && buffer) {
        SDL_ReleaseGPUTransferBuffer(device_, buffer);
    }
}

void GPUDevice::release_texture(SDL_GPUTexture* texture) {
    if (device_ && texture) {
        SDL_ReleaseGPUTexture(device_, texture);
    }
}

void GPUDevice::release_sampler(SDL_GPUSampler* sampler) {
    if (device_ && sampler) {
        SDL_ReleaseGPUSampler(device_, sampler);
    }
}

void GPUDevice::release_graphics_pipeline(SDL_GPUGraphicsPipeline* pipeline) {
    if (device_ && pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
    }
}

void GPUDevice::release_shader(SDL_GPUShader* shader) {
    if (device_ && shader) {
        SDL_ReleaseGPUShader(device_, shader);
    }
}

// =============================================================================
// Transfer Operations
// =============================================================================

void* GPUDevice::map_transfer_buffer(SDL_GPUTransferBuffer* buffer, bool cycle) {
    if (!device_ || !buffer) return nullptr;
    return SDL_MapGPUTransferBuffer(device_, buffer, cycle);
}

void GPUDevice::unmap_transfer_buffer(SDL_GPUTransferBuffer* buffer) {
    if (device_ && buffer) {
        SDL_UnmapGPUTransferBuffer(device_, buffer);
    }
}

// =============================================================================
// Accessors
// =============================================================================

int GPUDevice::width() const {
    if (!window_) return 0;
    int w = 0;
    SDL_GetWindowSize(window_, &w, nullptr);
    return w;
}

int GPUDevice::height() const {
    if (!window_) return 0;
    int h = 0;
    SDL_GetWindowSize(window_, nullptr, &h);
    return h;
}

SDL_GPUTextureFormat GPUDevice::swapchain_format() const {
    if (!device_ || !window_) {
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; // Default fallback
    }
    return SDL_GetGPUSwapchainTextureFormat(device_, window_);
}

std::string GPUDevice::driver_name() const {
    if (!device_) return "none";
    const char* name = SDL_GetGPUDeviceDriver(device_);
    return name ? name : "unknown";
}

bool GPUDevice::supports_format(SDL_GPUTextureFormat format, SDL_GPUTextureType type,
                                 SDL_GPUTextureUsageFlags usage) const {
    if (!device_) return false;
    return SDL_GPUTextureSupportsFormat(device_, format, type, usage);
}

} // namespace mmo::gpu
