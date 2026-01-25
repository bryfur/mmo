#include "gpu_texture.hpp"
#include <SDL3/SDL_log.h>
#include <SDL3_image/SDL_image.h>
#include <cstring>
#include <cmath>

namespace mmo::gpu {

// =============================================================================
// GPUTexture Implementation
// =============================================================================

GPUTexture::~GPUTexture() {
    if (device_ && texture_) {
        device_->release_texture(texture_);
    }
}

GPUTexture::GPUTexture(GPUTexture&& other) noexcept
    : device_(other.device_)
    , texture_(other.texture_)
    , width_(other.width_)
    , height_(other.height_)
    , format_(other.format_)
    , is_render_target_(other.is_render_target_)
    , is_depth_(other.is_depth_)
    , mip_levels_(other.mip_levels_) {
    other.device_ = nullptr;
    other.texture_ = nullptr;
}

GPUTexture& GPUTexture::operator=(GPUTexture&& other) noexcept {
    if (this != &other) {
        if (device_ && texture_) {
            device_->release_texture(texture_);
        }

        device_ = other.device_;
        texture_ = other.texture_;
        width_ = other.width_;
        height_ = other.height_;
        format_ = other.format_;
        is_render_target_ = other.is_render_target_;
        is_depth_ = other.is_depth_;
        mip_levels_ = other.mip_levels_;

        other.device_ = nullptr;
        other.texture_ = nullptr;
    }
    return *this;
}

uint32_t GPUTexture::calculate_mip_levels(int width, int height) {
    return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

size_t GPUTexture::get_bytes_per_pixel(SDL_GPUTextureFormat format) {
    switch (format) {
        case SDL_GPU_TEXTUREFORMAT_R8_UNORM:
            return 1;
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
            return 4;
        default:
            return 4;
    }
}

std::unique_ptr<GPUTexture> GPUTexture::load_from_file(GPUDevice& device,
                                                         const std::string& path,
                                                         bool generate_mipmaps) {
    // Load image via SDL_image
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        SDL_Log("GPUTexture::load_from_file: Failed to load '%s': %s", 
                path.c_str(), SDL_GetError());
        return nullptr;
    }

    // Convert to RGBA8 format if needed
    SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    
    if (!converted) {
        SDL_Log("GPUTexture::load_from_file: Failed to convert surface: %s", SDL_GetError());
        return nullptr;
    }

    // Create texture from pixel data
    auto texture = create_2d(device, converted->w, converted->h, TextureFormat::RGBA8,
                              converted->pixels, generate_mipmaps);
    
    SDL_DestroySurface(converted);
    return texture;
}

std::unique_ptr<GPUTexture> GPUTexture::create_2d(GPUDevice& device,
                                                    int width, int height,
                                                    TextureFormat format,
                                                    const void* pixels,
                                                    bool generate_mipmaps) {
    auto texture = std::unique_ptr<GPUTexture>(new GPUTexture());
    texture->device_ = &device;
    texture->width_ = width;
    texture->height_ = height;
    texture->format_ = to_sdl_format(format);
    texture->mip_levels_ = generate_mipmaps ? calculate_mip_levels(width, height) : 1;

    SDL_GPUTextureCreateInfo tex_info = {};
    tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tex_info.format = texture->format_;
    tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_info.width = static_cast<Uint32>(width);
    tex_info.height = static_cast<Uint32>(height);
    tex_info.layer_count_or_depth = 1;
    tex_info.num_levels = texture->mip_levels_;

    texture->texture_ = device.create_texture(tex_info);
    if (!texture->texture_) {
        SDL_Log("GPUTexture::create_2d: Failed to create texture: %s", SDL_GetError());
        return nullptr;
    }

    // Upload pixel data if provided
    if (pixels) {
        // Create transfer buffer
        size_t data_size = width * height * get_bytes_per_pixel(texture->format_);
        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = static_cast<Uint32>(data_size);

        SDL_GPUTransferBuffer* transfer = device.create_transfer_buffer(transfer_info);
        if (!transfer) {
            SDL_Log("GPUTexture::create_2d: Failed to create transfer buffer: %s", SDL_GetError());
            device.release_texture(texture->texture_);
            return nullptr;
        }

        // Map and copy
        void* mapped = device.map_transfer_buffer(transfer, false);
        if (!mapped) {
            SDL_Log("GPUTexture::create_2d: Failed to map transfer buffer: %s", SDL_GetError());
            device.release_transfer_buffer(transfer);
            device.release_texture(texture->texture_);
            return nullptr;
        }
        std::memcpy(mapped, pixels, data_size);
        device.unmap_transfer_buffer(transfer);

        // Upload via copy pass
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device.handle());
        if (!cmd) {
            SDL_Log("GPUTexture::create_2d: Failed to acquire command buffer: %s", SDL_GetError());
            device.release_transfer_buffer(transfer);
            device.release_texture(texture->texture_);
            return nullptr;
        }

        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
        if (!copy_pass) {
            SDL_Log("GPUTexture::create_2d: Failed to begin copy pass: %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(cmd);
            device.release_transfer_buffer(transfer);
            device.release_texture(texture->texture_);
            return nullptr;
        }

        SDL_GPUTextureTransferInfo src = {};
        src.transfer_buffer = transfer;
        src.offset = 0;

        SDL_GPUTextureRegion dst = {};
        dst.texture = texture->texture_;
        dst.w = static_cast<Uint32>(width);
        dst.h = static_cast<Uint32>(height);
        dst.d = 1;

        SDL_UploadToGPUTexture(copy_pass, &src, &dst, false);
        SDL_EndGPUCopyPass(copy_pass);

        // TODO: Generate mipmaps if requested
        // SDL3 GPU API may require manual mipmap generation via blit passes

        SDL_SubmitGPUCommandBuffer(cmd);
        device.release_transfer_buffer(transfer);
    }

    return texture;
}

std::unique_ptr<GPUTexture> GPUTexture::create_render_target(GPUDevice& device,
                                                               int width, int height,
                                                               TextureFormat format) {
    auto texture = std::unique_ptr<GPUTexture>(new GPUTexture());
    texture->device_ = &device;
    texture->width_ = width;
    texture->height_ = height;
    texture->format_ = to_sdl_format(format);
    texture->is_render_target_ = true;
    texture->mip_levels_ = 1;

    SDL_GPUTextureCreateInfo tex_info = {};
    tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tex_info.format = texture->format_;
    tex_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_info.width = static_cast<Uint32>(width);
    tex_info.height = static_cast<Uint32>(height);
    tex_info.layer_count_or_depth = 1;
    tex_info.num_levels = 1;

    texture->texture_ = device.create_texture(tex_info);
    if (!texture->texture_) {
        SDL_Log("GPUTexture::create_render_target: Failed to create texture: %s", SDL_GetError());
        return nullptr;
    }

    return texture;
}

std::unique_ptr<GPUTexture> GPUTexture::create_depth(GPUDevice& device, int width, int height) {
    auto texture = std::unique_ptr<GPUTexture>(new GPUTexture());
    texture->device_ = &device;
    texture->width_ = width;
    texture->height_ = height;
    texture->format_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    texture->is_depth_ = true;
    texture->mip_levels_ = 1;

    SDL_GPUTextureCreateInfo tex_info = {};
    tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tex_info.format = texture->format_;
    tex_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_info.width = static_cast<Uint32>(width);
    tex_info.height = static_cast<Uint32>(height);
    tex_info.layer_count_or_depth = 1;
    tex_info.num_levels = 1;

    texture->texture_ = device.create_texture(tex_info);
    if (!texture->texture_) {
        SDL_Log("GPUTexture::create_depth: Failed to create texture: %s", SDL_GetError());
        return nullptr;
    }

    return texture;
}

std::unique_ptr<GPUTexture> GPUTexture::create_depth_stencil(GPUDevice& device,
                                                               int width, int height) {
    auto texture = std::unique_ptr<GPUTexture>(new GPUTexture());
    texture->device_ = &device;
    texture->width_ = width;
    texture->height_ = height;
    texture->format_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    texture->is_depth_ = true;
    texture->mip_levels_ = 1;

    SDL_GPUTextureCreateInfo tex_info = {};
    tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tex_info.format = texture->format_;
    tex_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    tex_info.width = static_cast<Uint32>(width);
    tex_info.height = static_cast<Uint32>(height);
    tex_info.layer_count_or_depth = 1;
    tex_info.num_levels = 1;

    texture->texture_ = device.create_texture(tex_info);
    if (!texture->texture_) {
        SDL_Log("GPUTexture::create_depth_stencil: Failed to create texture: %s", SDL_GetError());
        return nullptr;
    }

    return texture;
}

void GPUTexture::upload(SDL_GPUCommandBuffer* cmd, const void* pixels, int width, int height) {
    if (!cmd || !pixels || !device_ || !texture_) {
        SDL_Log("GPUTexture::upload: Invalid parameters");
        return;
    }

    if (width != width_ || height != height_) {
        SDL_Log("GPUTexture::upload: Dimension mismatch");
        return;
    }

    size_t data_size = width * height * get_bytes_per_pixel(format_);
    
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(data_size);

    SDL_GPUTransferBuffer* transfer = device_->create_transfer_buffer(transfer_info);
    if (!transfer) {
        SDL_Log("GPUTexture::upload: Failed to create transfer buffer: %s", SDL_GetError());
        return;
    }

    void* mapped = device_->map_transfer_buffer(transfer, false);
    if (!mapped) {
        SDL_Log("GPUTexture::upload: Failed to map transfer buffer: %s", SDL_GetError());
        device_->release_transfer_buffer(transfer);
        return;
    }
    std::memcpy(mapped, pixels, data_size);
    device_->unmap_transfer_buffer(transfer);

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_Log("GPUTexture::upload: Failed to begin copy pass: %s", SDL_GetError());
        device_->release_transfer_buffer(transfer);
        return;
    }

    SDL_GPUTextureTransferInfo src = {};
    src.transfer_buffer = transfer;
    src.offset = 0;

    SDL_GPUTextureRegion dst = {};
    dst.texture = texture_;
    dst.w = static_cast<Uint32>(width);
    dst.h = static_cast<Uint32>(height);
    dst.d = 1;

    SDL_UploadToGPUTexture(copy_pass, &src, &dst, false);
    SDL_EndGPUCopyPass(copy_pass);

    // Note: Transfer buffer needs to stay alive until command buffer completes
    // For simplicity, we release it here which works for synchronized submissions
    device_->release_transfer_buffer(transfer);
}

// =============================================================================
// SamplerConfig Implementation
// =============================================================================

SamplerConfig SamplerConfig::linear_repeat() {
    return SamplerConfig{
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    };
}

SamplerConfig SamplerConfig::linear_clamp() {
    return SamplerConfig{
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
}

SamplerConfig SamplerConfig::nearest_repeat() {
    return SamplerConfig{
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    };
}

SamplerConfig SamplerConfig::nearest_clamp() {
    return SamplerConfig{
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
}

SamplerConfig SamplerConfig::anisotropic(float max_aniso) {
    return SamplerConfig{
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .max_anisotropy = max_aniso,
        .enable_anisotropy = true,
    };
}

SamplerConfig SamplerConfig::shadow() {
    return SamplerConfig{
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
}

// =============================================================================
// GPUSampler Implementation
// =============================================================================

GPUSampler::~GPUSampler() {
    if (device_ && sampler_) {
        device_->release_sampler(sampler_);
    }
}

GPUSampler::GPUSampler(GPUSampler&& other) noexcept
    : device_(other.device_)
    , sampler_(other.sampler_) {
    other.device_ = nullptr;
    other.sampler_ = nullptr;
}

GPUSampler& GPUSampler::operator=(GPUSampler&& other) noexcept {
    if (this != &other) {
        if (device_ && sampler_) {
            device_->release_sampler(sampler_);
        }

        device_ = other.device_;
        sampler_ = other.sampler_;

        other.device_ = nullptr;
        other.sampler_ = nullptr;
    }
    return *this;
}

std::unique_ptr<GPUSampler> GPUSampler::create(GPUDevice& device, const SamplerConfig& config) {
    auto sampler = std::unique_ptr<GPUSampler>(new GPUSampler());
    sampler->device_ = &device;

    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = config.min_filter;
    sampler_info.mag_filter = config.mag_filter;
    sampler_info.mipmap_mode = config.mipmap_mode;
    sampler_info.address_mode_u = config.address_u;
    sampler_info.address_mode_v = config.address_v;
    sampler_info.address_mode_w = config.address_w;
    sampler_info.mip_lod_bias = config.mip_lod_bias;
    sampler_info.enable_anisotropy = config.enable_anisotropy;
    sampler_info.max_anisotropy = config.max_anisotropy;
    sampler_info.enable_compare = false;

    sampler->sampler_ = device.create_sampler(sampler_info);
    if (!sampler->sampler_) {
        SDL_Log("GPUSampler::create: Failed to create sampler: %s", SDL_GetError());
        return nullptr;
    }

    return sampler;
}

} // namespace mmo::gpu
