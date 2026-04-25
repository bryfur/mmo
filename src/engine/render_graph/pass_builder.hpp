#pragma once

#include "engine/render_graph/render_graph_resource.hpp"
#include "engine/render_graph/render_graph_pass.hpp"

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <string>

namespace mmo::engine::render_graph {

class RenderGraph;

// Used inside add_pass setup lambda to declare reads/writes and create transients.
class PassBuilder {
public:
    PassBuilder(RenderGraph& graph, PassNode& node) noexcept
        : graph_(&graph), node_(&node) {}

    // Create a transient color render-target attached to this pass as a write.
    ResourceHandle create_color_target(const std::string& name, const TextureDesc& desc,
                                       SDL_GPULoadOp loadop = SDL_GPU_LOADOP_CLEAR,
                                       glm::vec4 clear_color = glm::vec4(0.0f));

    // Create a transient depth target attached to this pass as a write.
    ResourceHandle create_depth_target(const std::string& name, const TextureDesc& desc,
                                       SDL_GPULoadOp loadop = SDL_GPU_LOADOP_CLEAR,
                                       float clear_depth = 1.0f);

    // Bring an externally-owned texture into the graph.
    ResourceHandle import_texture(const std::string& name, SDL_GPUTexture* tex,
                                  uint32_t width, uint32_t height,
                                  SDL_GPUTextureFormat format);

    // Look up an existing resource by name (must have been create_/import_'d earlier).
    ResourceHandle lookup(const std::string& name) const;

    // Declare reads/writes by name or handle.
    ResourceHandle read(const std::string& name);
    ResourceHandle write(const std::string& name);
    void read(ResourceHandle h);
    void write(ResourceHandle h);

    // Attach an existing resource as a color RT for this pass (write).
    void attach_color(ResourceHandle h, SDL_GPULoadOp loadop = SDL_GPU_LOADOP_LOAD,
                      glm::vec4 clear_color = glm::vec4(0.0f));

    // Attach an existing resource as the depth RT for this pass (write).
    void attach_depth(ResourceHandle h, SDL_GPULoadOp loadop = SDL_GPU_LOADOP_CLEAR,
                      float clear_depth = 1.0f);

    // If true, the graph will SDL_BeginGPURenderPass / SDL_EndGPURenderPass for you.
    void set_managed_render_pass(bool managed) noexcept;

    void set_pass_type(PassType t) noexcept;

private:
    RenderGraph* graph_;
    PassNode* node_;
};

} // namespace mmo::engine::render_graph
