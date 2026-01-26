#pragma once

#include "gpu_device.hpp"
#include "gpu_types.hpp"
#include <memory>
#include <string>

namespace mmo::gpu {

/**
 * @brief GPU Texture abstraction for 2D textures, render targets, and depth buffers
 * 
 * This class simplifies texture creation and data upload. It handles the transfer
 * buffer lifecycle for uploads automatically and supports common texture types.
 * 
 * Usage:
 *   // Load from file
 *   auto diffuse = GPUTexture::load_from_file(device, "assets/textures/grass.png");
 *   
 *   // Create render target
 *   auto color_target = GPUTexture::create_render_target(device, 1920, 1080, 
 *                                                          TextureFormat::RGBA8);
 *   
 *   // Create depth buffer
 *   auto depth = GPUTexture::create_depth(device, 1920, 1080);
 */
class GPUTexture {
public:
    ~GPUTexture();

    // Non-copyable, movable
    GPUTexture(const GPUTexture&) = delete;
    GPUTexture& operator=(const GPUTexture&) = delete;
    GPUTexture(GPUTexture&& other) noexcept;
    GPUTexture& operator=(GPUTexture&& other) noexcept;

    /**
     * @brief Load a texture from a file (PNG, JPG, etc. via SDL_image)
     * 
     * @param device The GPU device
     * @param path Path to the image file
     * @param generate_mipmaps Whether to generate mipmaps (default true)
     * @return Unique pointer to the texture, or nullptr on failure
     */
    static std::unique_ptr<GPUTexture> load_from_file(GPUDevice& device, 
                                                        const std::string& path,
                                                        bool generate_mipmaps = true);

    /**
     * @brief Create a 2D texture from raw pixel data
     * 
     * @param device The GPU device
     * @param width Texture width
     * @param height Texture height
     * @param format Pixel format
     * @param pixels Pointer to pixel data (can be nullptr for uninitialized)
     * @param generate_mipmaps Whether to generate mipmaps
     * @return Unique pointer to the texture, or nullptr on failure
     */
    static std::unique_ptr<GPUTexture> create_2d(GPUDevice& device,
                                                   int width, int height,
                                                   TextureFormat format,
                                                   const void* pixels = nullptr,
                                                   bool generate_mipmaps = false);

    /**
     * @brief Create a 2D texture with explicit SDL format and usage flags
     * 
     * This overload provides direct access to SDL GPU texture formats and usage flags
     * for advanced use cases that need more control over texture creation.
     * 
     * @param device The GPU device
     * @param width Texture width
     * @param height Texture height
     * @param format SDL GPU texture format
     * @param usage SDL GPU texture usage flags
     * @return Unique pointer to the texture, or nullptr on failure
     */
    static std::unique_ptr<GPUTexture> create_2d(GPUDevice& device,
                                                   int width, int height,
                                                   SDL_GPUTextureFormat format,
                                                   SDL_GPUTextureUsageFlags usage);

    /**
     * @brief Create a render target texture
     * 
     * @param device The GPU device
     * @param width Texture width
     * @param height Texture height
     * @param format Pixel format
     * @return Unique pointer to the texture, or nullptr on failure
     */
    static std::unique_ptr<GPUTexture> create_render_target(GPUDevice& device,
                                                              int width, int height,
                                                              TextureFormat format);

    /**
     * @brief Create a depth buffer texture
     * 
     * @param device The GPU device
     * @param width Texture width
     * @param height Texture height
     * @return Unique pointer to the texture, or nullptr on failure
     */
    static std::unique_ptr<GPUTexture> create_depth(GPUDevice& device, int width, int height);

    /**
     * @brief Create a depth-stencil buffer texture
     * 
     * @param device The GPU device
     * @param width Texture width
     * @param height Texture height
     * @return Unique pointer to the texture, or nullptr on failure
     */
    static std::unique_ptr<GPUTexture> create_depth_stencil(GPUDevice& device, 
                                                              int width, int height);

    /**
     * @brief Upload pixel data to the texture
     * 
     * @param cmd Command buffer for the copy operation
     * @param pixels Pointer to pixel data
     * @param width Width of the data (must match texture width)
     * @param height Height of the data (must match texture height)
     */
    void upload(SDL_GPUCommandBuffer* cmd, const void* pixels, int width, int height);

    /**
     * @brief Get the raw SDL GPU texture handle
     */
    SDL_GPUTexture* handle() const { return texture_; }

    /**
     * @brief Get the texture width
     */
    int width() const { return width_; }

    /**
     * @brief Get the texture height
     */
    int height() const { return height_; }

    /**
     * @brief Get the texture format
     */
    SDL_GPUTextureFormat format() const { return format_; }

    /**
     * @brief Check if this texture can be used as a render target
     */
    bool is_render_target() const { return is_render_target_; }

    /**
     * @brief Check if this is a depth texture
     */
    bool is_depth() const { return is_depth_; }

private:
    GPUTexture() = default;

    GPUDevice* device_ = nullptr;
    SDL_GPUTexture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    SDL_GPUTextureFormat format_ = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    bool is_render_target_ = false;
    bool is_depth_ = false;
    uint32_t mip_levels_ = 1;

    static uint32_t calculate_mip_levels(int width, int height);
    static size_t get_bytes_per_pixel(SDL_GPUTextureFormat format);
};

/**
 * @brief GPU Sampler configuration for texture filtering and addressing
 */
struct SamplerConfig {
    SDL_GPUFilter min_filter = SDL_GPU_FILTER_LINEAR;
    SDL_GPUFilter mag_filter = SDL_GPU_FILTER_LINEAR;
    SDL_GPUSamplerMipmapMode mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    SDL_GPUSamplerAddressMode address_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    SDL_GPUSamplerAddressMode address_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    SDL_GPUSamplerAddressMode address_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    float mip_lod_bias = 0.0f;
    float max_anisotropy = 1.0f;
    bool enable_anisotropy = false;

    // Preset configurations
    static SamplerConfig linear_repeat();
    static SamplerConfig linear_clamp();
    static SamplerConfig nearest_repeat();
    static SamplerConfig nearest_clamp();
    static SamplerConfig anisotropic(float max_aniso = 16.0f);
    static SamplerConfig shadow(); // For shadow map sampling
};

/**
 * @brief GPU Sampler wrapper
 */
class GPUSampler {
public:
    ~GPUSampler();

    GPUSampler(const GPUSampler&) = delete;
    GPUSampler& operator=(const GPUSampler&) = delete;
    GPUSampler(GPUSampler&& other) noexcept;
    GPUSampler& operator=(GPUSampler&& other) noexcept;

    /**
     * @brief Create a sampler with the given configuration
     */
    static std::unique_ptr<GPUSampler> create(GPUDevice& device, const SamplerConfig& config);

    SDL_GPUSampler* handle() const { return sampler_; }

private:
    GPUSampler() = default;

    GPUDevice* device_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
};

} // namespace mmo::gpu
