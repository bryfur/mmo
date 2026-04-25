#pragma once

#include "engine/render_graph/render_graph_resource.hpp"
#include <cstdint>
#include <functional>
#include <SDL3/SDL_gpu.h>
#include <string>
#include <vector>

namespace mmo::engine::render_graph {

class RenderPassContext;

enum class PassType : uint8_t {
    Graphics,
    Compute,
    Generic, // raw command buffer use (e.g. CPU upload, blit, custom)
};

struct ColorAttachment {
    ResourceHandle handle;
    SDL_GPUColorTargetInfo info{};
};

struct DepthAttachment {
    ResourceHandle handle;
    SDL_GPUDepthStencilTargetInfo info{};
};

using PassExecuteFn = std::function<void(RenderPassContext&)>;

struct PassNode {
    std::string name;
    PassType type = PassType::Generic;

    std::vector<ResourceHandle> reads;
    std::vector<ResourceHandle> writes;

    // Render-target outputs (for graphics passes the graph manages).
    std::vector<ColorAttachment> color_attachments;
    DepthAttachment depth_attachment{};
    bool has_depth = false;

    // If true, the graph will SDL_BeginGPURenderPass / SDL_EndGPURenderPass
    // around the execute callback using color_attachments + depth_attachment.
    bool managed_render_pass = false;

    PassExecuteFn execute;
};

} // namespace mmo::engine::render_graph
