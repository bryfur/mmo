#pragma once

#include "engine/render_graph/render_graph_resource.hpp"
#include "engine/render_graph/render_graph_pass.hpp"
#include "engine/render_graph/render_graph_context.hpp"
#include "engine/render_graph/pass_builder.hpp"
#include "engine/render_graph/transient_allocator.hpp"

#include <SDL3/SDL_gpu.h>
#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mmo::engine::gpu { class GPUDevice; }

namespace mmo::engine::render_graph {

class RenderGraph {
public:
    RenderGraph() = default;
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    bool init(gpu::GPUDevice& device);
    void shutdown();

    // Begin a new frame. cb may be null for tests; execute() is then a no-op for managed render passes.
    void begin_frame(SDL_GPUCommandBuffer* cb);

    // Add a pass with a typed data struct that lives across setup -> execute.
    template <typename Data, typename SetupFn, typename ExecuteFn>
    void add_pass(const std::string& name, SetupFn&& setup, ExecuteFn&& execute) {
        auto data = std::make_shared<Data>();
        PassNode& node = create_pass_node(name);
        {
            PassBuilder b(*this, node);
            setup(b, *data);
        }
        node.execute = [data, exec = std::forward<ExecuteFn>(execute)](RenderPassContext& ctx) mutable {
            exec(ctx, *data);
        };
    }

    // Untyped overload: setup builds, execute uses captured locals.
    void add_pass(const std::string& name, PassType type,
                  std::function<void(PassBuilder&)> setup,
                  PassExecuteFn execute);

    // Compile: topo-sort, resolve transient resources from the pool, compute lifetimes.
    // Returns false on cycle (which is logged and skipped).
    bool compile();

    // Execute compiled order. begin_frame() must have set the command buffer first.
    void execute();

    // End frame: returns transient textures to pool for reuse next frame.
    void end_frame();

    // Reset graph between frames (clears passes & resources but keeps the pool).
    void reset();

    // Diagnostics.
    size_t pass_count() const noexcept { return passes_.size(); }
    size_t resource_count() const noexcept { return resources_.size(); }
    const std::vector<size_t>& compiled_order() const noexcept { return compiled_order_; }

    // Emit Graphviz DOT description of the compiled graph for inspection.
    void dump_dot(std::ostream& os) const;

    // Access methods used by PassBuilder + RenderPassContext.
    ResourceNode& resource(ResourceHandle h);
    const ResourceNode& resource(ResourceHandle h) const;
    ResourceHandle find_resource(const std::string& name) const;

    // Used by PassBuilder.
    ResourceHandle declare_transient_texture(const std::string& name, const TextureDesc& desc);
    ResourceHandle declare_imported_texture(const std::string& name, SDL_GPUTexture* tex,
                                            uint32_t w, uint32_t h, SDL_GPUTextureFormat fmt);

    // Stats.
    uint32_t last_compiled_passes() const noexcept { return last_compiled_passes_; }
    uint32_t last_transient_count() const noexcept { return last_transient_count_; }
    uint32_t last_reuse_count() const noexcept { return last_reuse_count_; }

    // For tests.
    PassNode& pass_at(size_t i) { return passes_[i]; }
    const PassNode& pass_at(size_t i) const { return passes_[i]; }

    // Force-evict pooled transient textures (call on resize).
    void evict_pool();

private:
    PassNode& create_pass_node(const std::string& name);
    bool topo_sort(std::vector<size_t>& out_order, bool& out_cycle) const;
    void compute_lifetimes();
    void allocate_transients();

    gpu::GPUDevice* device_ = nullptr;
    SDL_GPUCommandBuffer* cb_ = nullptr;
    TransientAllocator allocator_;

    std::vector<PassNode> passes_;
    std::vector<ResourceNode> resources_;
    std::unordered_map<std::string, uint32_t> name_to_resource_;

    std::vector<size_t> compiled_order_;
    bool compiled_ = false;

    uint32_t last_compiled_passes_ = 0;
    uint32_t last_transient_count_ = 0;
    uint32_t last_reuse_count_ = 0;
};

} // namespace mmo::engine::render_graph
