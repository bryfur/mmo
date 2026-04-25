#include "bloom.hpp"
#include "engine/gpu/gpu_pipeline.hpp"
#include <algorithm>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>

namespace mmo::engine::render {

Bloom::Bloom() = default;

Bloom::~Bloom() {
    shutdown();
}

bool Bloom::init(gpu::GPUDevice& device, int width, int height) {
    device_ = &device;
    width_ = width;
    height_ = height;

    create_textures(width, height);
    create_sampler();

    if (!is_ready()) {
        SDL_Log("Bloom: Failed to initialize");
        return false;
    }

    SDL_Log("Bloom: Initialized %dx%d (%d mip levels)", width, height, MIP_COUNT);
    return true;
}

void Bloom::shutdown() {
    for (int i = 0; i < MIP_COUNT; ++i) {
        mip_textures_[i].reset();
    }

    if (device_ && linear_sampler_) {
        device_->release_sampler(linear_sampler_);
        linear_sampler_ = nullptr;
    }

    device_ = nullptr;
}

void Bloom::resize(int width, int height) {
    if (width == width_ && height == height_) {
        return;
    }
    if (!device_) {
        return;
    }

    width_ = width;
    height_ = height;

    create_textures(width, height);
    SDL_Log("Bloom: Resized to %dx%d", width, height);
}

void Bloom::create_textures(int width, int height) {
    // Each mip is half the resolution of the previous
    int w = std::max(1, width / 2);
    int h = std::max(1, height / 2);

    for (int i = 0; i < MIP_COUNT; ++i) {
        mip_textures_[i] = gpu::GPUTexture::create_2d(*device_, w, h, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                                                      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);

        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
}

void Bloom::create_sampler() {
    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    linear_sampler_ = device_->create_sampler(info);
}

bool Bloom::is_ready() const {
    if (!linear_sampler_) {
        return false;
    }
    for (int i = 0; i < MIP_COUNT; ++i) {
        if (!mip_textures_[i]) {
            return false;
        }
    }
    return true;
}

SDL_GPUTexture* Bloom::bloom_texture() const {
    if (mip_textures_[0]) {
        return mip_textures_[0]->handle();
    }
    return nullptr;
}

void Bloom::render(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& registry, SDL_GPUTexture* scene_color,
                   float threshold) {
    if (!is_ready() || !cmd || !scene_color) {
        return;
    }

    auto* downsample_pipeline = registry.get_bloom_downsample_pipeline();
    auto* upsample_pipeline = registry.get_bloom_upsample_pipeline();
    if (!downsample_pipeline || !upsample_pipeline) {
        return;
    }

    // =========================================================================
    // Downsample chain: scene_color -> mip[0] -> mip[1] -> ... -> mip[N-1]
    // =========================================================================
    for (int i = 0; i < MIP_COUNT; ++i) {
        // Source is either the scene color (first pass) or previous mip
        SDL_GPUTexture* src_tex = (i == 0) ? scene_color : mip_textures_[i - 1]->handle();
        int src_w = (i == 0) ? width_ : mip_textures_[i - 1]->width();
        int src_h = (i == 0) ? height_ : mip_textures_[i - 1]->height();

        gpu::BloomDownsampleUniforms uniforms = {};
        uniforms.srcTexelSize = glm::vec2(1.0f / src_w, 1.0f / src_h);
        uniforms.threshold = threshold;
        uniforms.isFirstPass = (i == 0) ? 1.0f : 0.0f;

        SDL_GPUColorTargetInfo color_target = {};
        color_target.texture = mip_textures_[i]->handle();
        color_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        color_target.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
        if (!pass) {
            continue;
        }

        downsample_pipeline->bind(pass);
        SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

        SDL_GPUTextureSamplerBinding binding = {};
        binding.texture = src_tex;
        binding.sampler = linear_sampler_;
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }

    // =========================================================================
    // Upsample chain: mip[N-1] -> mip[N-2] -> ... -> mip[0]
    // Each pass reads from the smaller mip and writes to the larger one,
    // replacing its contents with the upsampled bloom.
    // =========================================================================
    for (int i = MIP_COUNT - 1; i > 0; --i) {
        // Source is the current (smaller) mip, destination is the next larger mip
        SDL_GPUTexture* src_tex = mip_textures_[i]->handle();
        int src_w = mip_textures_[i]->width();
        int src_h = mip_textures_[i]->height();

        gpu::BloomUpsampleUniforms uniforms = {};
        uniforms.srcTexelSize = glm::vec2(1.0f / src_w, 1.0f / src_h);
        uniforms.bloomRadius = 1.0f;

        SDL_GPUColorTargetInfo color_target = {};
        color_target.texture = mip_textures_[i - 1]->handle();
        color_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        color_target.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
        if (!pass) {
            continue;
        }

        upsample_pipeline->bind(pass);
        SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

        SDL_GPUTextureSamplerBinding binding = {};
        binding.texture = src_tex;
        binding.sampler = linear_sampler_;
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }
}

} // namespace mmo::engine::render
