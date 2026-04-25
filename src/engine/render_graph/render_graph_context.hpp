#pragma once

#include "engine/render_graph/render_graph_resource.hpp"
#include <SDL3/SDL_gpu.h>

namespace mmo::engine::render_graph {

class RenderGraph;

class RenderPassContext {
public:
    RenderPassContext(RenderGraph& graph, SDL_GPUCommandBuffer* cb,
                      SDL_GPURenderPass* render_pass) noexcept
        : graph_(&graph), cb_(cb), render_pass_(render_pass) {}

    SDL_GPUCommandBuffer* command_buffer() const noexcept { return cb_; }

    // Only non-null inside graphics passes that declared color/depth outputs and
    // requested the graph begin/end the SDL render pass for them.
    SDL_GPURenderPass* render_pass() const noexcept { return render_pass_; }

    SDL_GPUTexture* get_texture(ResourceHandle h) const noexcept;
    SDL_GPUBuffer*  get_buffer(ResourceHandle h) const noexcept;

private:
    RenderGraph* graph_;
    SDL_GPUCommandBuffer* cb_;
    SDL_GPURenderPass* render_pass_;
};

} // namespace mmo::engine::render_graph
