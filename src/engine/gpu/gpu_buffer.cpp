#include "gpu_buffer.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_stdinc.h"
#include "engine/gpu/gpu_device.hpp"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <memory>

namespace mmo::engine::gpu {

GPUBuffer::~GPUBuffer() {
    if (device_) {
        if (transfer_buffer_) {
            device_->release_transfer_buffer(transfer_buffer_);
        }
        if (buffer_) {
            device_->release_buffer(buffer_);
        }
    }
}

GPUBuffer::GPUBuffer(GPUBuffer&& other) noexcept
    : device_(other.device_)
    , buffer_(other.buffer_)
    , transfer_buffer_(other.transfer_buffer_)
    , size_(other.size_)
    , type_(other.type_) {
    other.device_ = nullptr;
    other.buffer_ = nullptr;
    other.transfer_buffer_ = nullptr;
    other.size_ = 0;
}

GPUBuffer& GPUBuffer::operator=(GPUBuffer&& other) noexcept {
    if (this != &other) {
        // Release current resources
        if (device_) {
            if (transfer_buffer_) {
                device_->release_transfer_buffer(transfer_buffer_);
            }
            if (buffer_) {
                device_->release_buffer(buffer_);
            }
        }

        // Move from other
        device_ = other.device_;
        buffer_ = other.buffer_;
        transfer_buffer_ = other.transfer_buffer_;
        size_ = other.size_;
        type_ = other.type_;

        // Clear other
        other.device_ = nullptr;
        other.buffer_ = nullptr;
        other.transfer_buffer_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

SDL_GPUBufferUsageFlags GPUBuffer::get_usage_flags(Type type) {
    switch (type) {
        case Type::Vertex:
            return SDL_GPU_BUFFERUSAGE_VERTEX;
        case Type::Index:
            return SDL_GPU_BUFFERUSAGE_INDEX;
        case Type::Uniform:
            // SDL3 GPU uses push constants for small uniform data.
            // For larger uniform data, we use read-only storage buffers which
            // provide similar functionality with more flexibility.
            return SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        case Type::Storage:
            return SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    }
    return SDL_GPU_BUFFERUSAGE_VERTEX;
}

std::unique_ptr<GPUBuffer> GPUBuffer::create_static(GPUDevice& device, Type type,
                                                       const void* data, size_t size) {
    if (!data || size == 0) {
        SDL_Log("GPUBuffer::create_static: Invalid parameters");
        return nullptr;
    }

    auto buffer = std::unique_ptr<GPUBuffer>(new GPUBuffer());
    buffer->device_ = &device;
    buffer->type_ = type;
    buffer->size_ = size;

    // Create the GPU buffer
    SDL_GPUBufferCreateInfo buf_info = {};
    buf_info.usage = get_usage_flags(type);
    buf_info.size = static_cast<Uint32>(size);

    buffer->buffer_ = device.create_buffer(buf_info);
    if (!buffer->buffer_) {
        SDL_Log("GPUBuffer::create_static: Failed to create buffer: %s", SDL_GetError());
        return nullptr;
    }

    // Create a temporary transfer buffer for upload
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(size);

    SDL_GPUTransferBuffer* transfer = device.create_transfer_buffer(transfer_info);
    if (!transfer) {
        SDL_Log("GPUBuffer::create_static: Failed to create transfer buffer: %s", SDL_GetError());
        device.release_buffer(buffer->buffer_);
        return nullptr;
    }

    // Map, copy, unmap
    void* mapped = device.map_transfer_buffer(transfer, false);
    if (!mapped) {
        SDL_Log("GPUBuffer::create_static: Failed to map transfer buffer: %s", SDL_GetError());
        device.release_transfer_buffer(transfer);
        device.release_buffer(buffer->buffer_);
        return nullptr;
    }
    std::memcpy(mapped, data, size);
    device.unmap_transfer_buffer(transfer);

    // Upload to GPU via copy pass
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device.handle());
    if (!cmd) {
        SDL_Log("GPUBuffer::create_static: Failed to acquire command buffer: %s", SDL_GetError());
        device.release_transfer_buffer(transfer);
        device.release_buffer(buffer->buffer_);
        return nullptr;
    }

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_Log("GPUBuffer::create_static: Failed to begin copy pass: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        device.release_transfer_buffer(transfer);
        device.release_buffer(buffer->buffer_);
        return nullptr;
    }

    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = transfer;
    src.offset = 0;

    SDL_GPUBufferRegion dst = {};
    dst.buffer = buffer->buffer_;
    dst.offset = 0;
    dst.size = static_cast<Uint32>(size);

    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    // Release the temporary transfer buffer
    device.release_transfer_buffer(transfer);

    return buffer;
}

std::unique_ptr<GPUBuffer> GPUBuffer::create_dynamic(GPUDevice& device, Type type, size_t size) {
    if (size == 0) {
        SDL_Log("GPUBuffer::create_dynamic: Invalid size");
        return nullptr;
    }

    auto buffer = std::unique_ptr<GPUBuffer>(new GPUBuffer());
    buffer->device_ = &device;
    buffer->type_ = type;
    buffer->size_ = size;

    // Create the GPU buffer
    SDL_GPUBufferCreateInfo buf_info = {};
    buf_info.usage = get_usage_flags(type);
    buf_info.size = static_cast<Uint32>(size);

    buffer->buffer_ = device.create_buffer(buf_info);
    if (!buffer->buffer_) {
        SDL_Log("GPUBuffer::create_dynamic: Failed to create buffer: %s", SDL_GetError());
        return nullptr;
    }

    // Create a persistent transfer buffer for updates
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(size);

    buffer->transfer_buffer_ = device.create_transfer_buffer(transfer_info);
    if (!buffer->transfer_buffer_) {
        SDL_Log("GPUBuffer::create_dynamic: Failed to create transfer buffer: %s", SDL_GetError());
        device.release_buffer(buffer->buffer_);
        return nullptr;
    }

    return buffer;
}

void GPUBuffer::update(SDL_GPUCommandBuffer* cmd, const void* data, size_t size, size_t offset) {
    if (!transfer_buffer_) {
        SDL_Log("GPUBuffer::update: Not a dynamic buffer");
        return;
    }

    if (!cmd || !data) {
        SDL_Log("GPUBuffer::update: Invalid parameters");
        return;
    }

    if (offset + size > size_) {
        SDL_Log("GPUBuffer::update: Data exceeds buffer size");
        return;
    }

    // Map, copy, unmap (with cycle=true for double-buffering)
    void* mapped = device_->map_transfer_buffer(transfer_buffer_, true);
    if (!mapped) {
        SDL_Log("GPUBuffer::update: Failed to map transfer buffer: %s", SDL_GetError());
        return;
    }
    std::memcpy(static_cast<char*>(mapped) + offset, data, size);
    device_->unmap_transfer_buffer(transfer_buffer_);

    // Upload via copy pass
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_Log("GPUBuffer::update: Failed to begin copy pass: %s", SDL_GetError());
        return;
    }

    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = transfer_buffer_;
    src.offset = static_cast<Uint32>(offset);

    SDL_GPUBufferRegion dst = {};
    dst.buffer = buffer_;
    dst.offset = static_cast<Uint32>(offset);
    dst.size = static_cast<Uint32>(size);

    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, true); // cycle=true
    SDL_EndGPUCopyPass(copy_pass);
}

} // namespace mmo::engine::gpu
