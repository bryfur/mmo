#include "engine/render_graph/render_graph.hpp"
#include "engine/core/logger.hpp"

#include <algorithm>
#include <cassert>
#include <ostream>
#include <queue>
#include <unordered_set>

namespace mmo::engine::render_graph {

// ---------- RenderPassContext ----------

SDL_GPUTexture* RenderPassContext::get_texture(ResourceHandle h) const noexcept {
    if (!graph_) {
        return nullptr;
    }
    if (!h.valid()) {
        return nullptr;
    }
    const auto& r = graph_->resource(h);
    return r.transient ? r.resolved_texture : r.imported_texture;
}

SDL_GPUBuffer* RenderPassContext::get_buffer(ResourceHandle h) const noexcept {
    if (!graph_) {
        return nullptr;
    }
    if (!h.valid()) {
        return nullptr;
    }
    const auto& r = graph_->resource(h);
    return r.transient ? r.resolved_buffer : r.imported_buffer;
}

// ---------- PassBuilder ----------

ResourceHandle PassBuilder::create_color_target(const std::string& name, const TextureDesc& desc, SDL_GPULoadOp loadop,
                                                glm::vec4 clear_color) {
    TextureDesc d = desc;
    if ((d.usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) == 0) {
        d.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    }
    ResourceHandle h = graph_->declare_transient_texture(name, d);
    write(h);
    SDL_GPUColorTargetInfo info{};
    info.load_op = loadop;
    info.store_op = SDL_GPU_STOREOP_STORE;
    info.clear_color.r = clear_color.r;
    info.clear_color.g = clear_color.g;
    info.clear_color.b = clear_color.b;
    info.clear_color.a = clear_color.a;
    node_->color_attachments.push_back(ColorAttachment{h, info});
    node_->managed_render_pass = true;
    return h;
}

ResourceHandle PassBuilder::create_depth_target(const std::string& name, const TextureDesc& desc, SDL_GPULoadOp loadop,
                                                float clear_depth) {
    TextureDesc d = desc;
    if ((d.usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) == 0) {
        d.usage |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    }
    ResourceHandle h = graph_->declare_transient_texture(name, d);
    write(h);
    SDL_GPUDepthStencilTargetInfo info{};
    info.load_op = loadop;
    info.store_op = SDL_GPU_STOREOP_STORE;
    info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    info.clear_depth = clear_depth;
    info.clear_stencil = 0;
    info.cycle = false;
    node_->depth_attachment = DepthAttachment{h, info};
    node_->has_depth = true;
    node_->managed_render_pass = true;
    return h;
}

ResourceHandle PassBuilder::import_texture(const std::string& name, SDL_GPUTexture* tex, uint32_t width,
                                           uint32_t height, SDL_GPUTextureFormat format) {
    return graph_->declare_imported_texture(name, tex, width, height, format);
}

ResourceHandle PassBuilder::lookup(const std::string& name) const {
    return graph_->find_resource(name);
}

ResourceHandle PassBuilder::read(const std::string& name) {
    ResourceHandle h = graph_->find_resource(name);
    if (h.valid()) {
        read(h);
    }
    return h;
}

ResourceHandle PassBuilder::write(const std::string& name) {
    ResourceHandle h = graph_->find_resource(name);
    if (h.valid()) {
        write(h);
    }
    return h;
}

void PassBuilder::read(ResourceHandle h) {
    if (!h.valid()) {
        return;
    }
    if (std::find(node_->reads.begin(), node_->reads.end(), h) == node_->reads.end()) {
        node_->reads.push_back(h);
    }
}

void PassBuilder::write(ResourceHandle h) {
    if (!h.valid()) {
        return;
    }
    if (std::find(node_->writes.begin(), node_->writes.end(), h) == node_->writes.end()) {
        node_->writes.push_back(h);
        graph_->resource(h).version += 1;
    }
}

void PassBuilder::attach_color(ResourceHandle h, SDL_GPULoadOp loadop, glm::vec4 clear_color) {
    write(h);
    SDL_GPUColorTargetInfo info{};
    info.load_op = loadop;
    info.store_op = SDL_GPU_STOREOP_STORE;
    info.clear_color.r = clear_color.r;
    info.clear_color.g = clear_color.g;
    info.clear_color.b = clear_color.b;
    info.clear_color.a = clear_color.a;
    node_->color_attachments.push_back(ColorAttachment{h, info});
    node_->managed_render_pass = true;
}

void PassBuilder::attach_depth(ResourceHandle h, SDL_GPULoadOp loadop, float clear_depth) {
    write(h);
    SDL_GPUDepthStencilTargetInfo info{};
    info.load_op = loadop;
    info.store_op = SDL_GPU_STOREOP_STORE;
    info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    info.clear_depth = clear_depth;
    info.clear_stencil = 0;
    node_->depth_attachment = DepthAttachment{h, info};
    node_->has_depth = true;
    node_->managed_render_pass = true;
}

void PassBuilder::set_managed_render_pass(bool managed) noexcept {
    node_->managed_render_pass = managed;
}
void PassBuilder::set_pass_type(PassType t) noexcept {
    node_->type = t;
}

// ---------- RenderGraph ----------

RenderGraph::~RenderGraph() {
    shutdown();
}

bool RenderGraph::init(gpu::GPUDevice& device) {
    device_ = &device;
    return allocator_.init(device);
}

void RenderGraph::shutdown() {
    passes_.clear();
    resources_.clear();
    name_to_resource_.clear();
    compiled_order_.clear();
    allocator_.shutdown();
    device_ = nullptr;
}

void RenderGraph::begin_frame(SDL_GPUCommandBuffer* cb) {
    cb_ = cb;
    reset();
    allocator_.begin_frame();
}

void RenderGraph::reset() {
    passes_.clear();
    resources_.clear();
    name_to_resource_.clear();
    compiled_order_.clear();
    compiled_ = false;
}

PassNode& RenderGraph::create_pass_node(const std::string& name) {
    passes_.emplace_back();
    PassNode& n = passes_.back();
    n.name = name;
    return n;
}

void RenderGraph::add_pass(const std::string& name, PassType type, std::function<void(PassBuilder&)> setup,
                           PassExecuteFn execute) {
    PassNode& node = create_pass_node(name);
    node.type = type;
    {
        PassBuilder b(*this, node);
        if (setup) {
            setup(b);
        }
    }
    node.execute = std::move(execute);
}

ResourceNode& RenderGraph::resource(ResourceHandle h) {
    assert(h.valid() && h.index() < resources_.size());
    return resources_[h.index()];
}
const ResourceNode& RenderGraph::resource(ResourceHandle h) const {
    assert(h.valid() && h.index() < resources_.size());
    return resources_[h.index()];
}

ResourceHandle RenderGraph::find_resource(const std::string& name) const {
    auto it = name_to_resource_.find(name);
    if (it == name_to_resource_.end()) {
        return ResourceHandle::invalid();
    }
    return ResourceHandle{it->second};
}

ResourceHandle RenderGraph::declare_transient_texture(const std::string& name, const TextureDesc& desc) {
    auto it = name_to_resource_.find(name);
    if (it != name_to_resource_.end()) {
        return ResourceHandle{it->second};
    }
    uint32_t idx = static_cast<uint32_t>(resources_.size());
    ResourceNode r;
    r.type = ResourceType::Texture;
    r.name = name;
    r.transient = true;
    r.texture_desc = desc;
    resources_.push_back(std::move(r));
    name_to_resource_[name] = idx;
    return ResourceHandle{idx};
}

ResourceHandle RenderGraph::declare_imported_texture(const std::string& name, SDL_GPUTexture* tex, uint32_t w,
                                                     uint32_t h, SDL_GPUTextureFormat fmt) {
    auto it = name_to_resource_.find(name);
    if (it != name_to_resource_.end()) {
        auto& existing = resources_[it->second];
        existing.imported_texture = tex;
        existing.imported_width = w;
        existing.imported_height = h;
        existing.imported_format = fmt;
        existing.transient = false;
        return ResourceHandle{it->second};
    }
    uint32_t idx = static_cast<uint32_t>(resources_.size());
    ResourceNode r;
    r.type = ResourceType::Texture;
    r.name = name;
    r.transient = false;
    r.imported_texture = tex;
    r.imported_width = w;
    r.imported_height = h;
    r.imported_format = fmt;
    resources_.push_back(std::move(r));
    name_to_resource_[name] = idx;
    return ResourceHandle{idx};
}

bool RenderGraph::topo_sort(std::vector<size_t>& out_order, bool& out_cycle) const {
    out_cycle = false;
    out_order.clear();
    const size_t n = passes_.size();
    if (n == 0) {
        return true;
    }

    // For each resource, find the producing pass(es).
    // A pass that reads R has an edge from every pass that wrote R.
    std::vector<std::vector<size_t>> producers(resources_.size());
    for (size_t i = 0; i < n; ++i) {
        for (auto h : passes_[i].writes) {
            if (h.valid()) {
                producers[h.index()].push_back(i);
            }
        }
    }

    std::vector<std::vector<size_t>> adj(n);
    std::vector<int> indegree(n, 0);
    std::vector<std::unordered_set<size_t>> seen_edges(n);
    for (size_t i = 0; i < n; ++i) {
        for (auto h : passes_[i].reads) {
            if (!h.valid()) {
                continue;
            }
            for (size_t p : producers[h.index()]) {
                if (p == i) {
                    continue;
                }
                if (seen_edges[p].insert(i).second) {
                    adj[p].push_back(i);
                    ++indegree[i];
                }
            }
        }
    }

    // Kahn topo with FIFO so passes added earlier come first when independent.
    std::queue<size_t> q;
    for (size_t i = 0; i < n; ++i) {
        if (indegree[i] == 0) {
            q.push(i);
        }
    }
    while (!q.empty()) {
        size_t i = q.front();
        q.pop();
        out_order.push_back(i);
        for (size_t j : adj[i]) {
            if (--indegree[j] == 0) {
                q.push(j);
            }
        }
    }
    if (out_order.size() != n) {
        out_cycle = true;
        return false;
    }
    return true;
}

void RenderGraph::compute_lifetimes() {
    for (auto& r : resources_) {
        r.first_write_pass = std::numeric_limits<uint32_t>::max();
        r.last_read_pass = std::numeric_limits<uint32_t>::max();
    }
    for (size_t step = 0; step < compiled_order_.size(); ++step) {
        size_t pi = compiled_order_[step];
        for (auto h : passes_[pi].writes) {
            auto& r = resources_[h.index()];
            if (r.first_write_pass == std::numeric_limits<uint32_t>::max()) {
                r.first_write_pass = static_cast<uint32_t>(step);
            }
            r.last_read_pass =
                std::max<uint32_t>(r.last_read_pass == std::numeric_limits<uint32_t>::max() ? 0u : r.last_read_pass,
                                   static_cast<uint32_t>(step));
        }
        for (auto h : passes_[pi].reads) {
            auto& r = resources_[h.index()];
            r.last_read_pass = static_cast<uint32_t>(step);
        }
    }
}

void RenderGraph::allocate_transients() {
    // For each compiled step, acquire transients whose first_write_pass == step,
    // and release ones whose last_read_pass == step. This implements interval-based reuse.
    last_transient_count_ = 0;
    last_reuse_count_ = 0;
    uint32_t reuses_before = allocator_.reuses_this_frame();

    for (size_t step = 0; step < compiled_order_.size(); ++step) {
        // Acquire new transients.
        for (auto& r : resources_) {
            if (!r.transient) {
                continue;
            }
            if (r.first_write_pass == static_cast<uint32_t>(step) && r.resolved_texture == nullptr) {
                r.resolved_texture = allocator_.acquire_texture(r.texture_desc, r.name.c_str());
                ++last_transient_count_;
            }
        }
        // Release transients no longer needed.
        for (auto& r : resources_) {
            if (!r.transient) {
                continue;
            }
            if (r.last_read_pass == static_cast<uint32_t>(step) && r.resolved_texture != nullptr) {
                allocator_.release_texture(r.texture_desc, r.resolved_texture);
                // Don't null out resolved_texture: pass might run after lifetime in same frame
                // because we release for *next* acquires within the same frame; but the texture
                // is still valid for use by this same pass. Mark for skipping reacquire.
            }
        }
    }

    last_reuse_count_ = allocator_.reuses_this_frame() - reuses_before;
}

bool RenderGraph::compile() {
    bool cycle = false;
    bool ok = topo_sort(compiled_order_, cycle);
    if (!ok) {
        ENGINE_LOG_ERROR("rg", "compile: cycle detected in render graph (passes={}); skipping execute", passes_.size());
        compiled_order_.clear();
        compiled_ = false;
        return false;
    }
    compute_lifetimes();
    allocate_transients();
    last_compiled_passes_ = static_cast<uint32_t>(compiled_order_.size());
    compiled_ = true;
    return true;
}

void RenderGraph::execute() {
    if (!compiled_) {
        return;
    }
    for (size_t step = 0; step < compiled_order_.size(); ++step) {
        PassNode& pass = passes_[compiled_order_[step]];

        SDL_GPURenderPass* sdl_pass = nullptr;
        if (cb_ && pass.managed_render_pass && (!pass.color_attachments.empty() || pass.has_depth)) {

            std::vector<SDL_GPUColorTargetInfo> color_infos;
            color_infos.reserve(pass.color_attachments.size());
            for (auto& ca : pass.color_attachments) {
                SDL_GPUColorTargetInfo info = ca.info;
                info.texture = nullptr;
                if (ca.handle.valid()) {
                    auto& r = resources_[ca.handle.index()];
                    info.texture = r.transient ? r.resolved_texture : r.imported_texture;
                }
                color_infos.push_back(info);
            }
            SDL_GPUDepthStencilTargetInfo depth_info{};
            SDL_GPUDepthStencilTargetInfo* depth_ptr = nullptr;
            if (pass.has_depth && pass.depth_attachment.handle.valid()) {
                depth_info = pass.depth_attachment.info;
                auto& r = resources_[pass.depth_attachment.handle.index()];
                depth_info.texture = r.transient ? r.resolved_texture : r.imported_texture;
                depth_ptr = &depth_info;
            }
            sdl_pass = SDL_BeginGPURenderPass(cb_, color_infos.empty() ? nullptr : color_infos.data(),
                                              static_cast<Uint32>(color_infos.size()), depth_ptr);
        }

        RenderPassContext ctx(*this, cb_, sdl_pass);
        if (pass.execute) {
            pass.execute(ctx);
        }

        if (sdl_pass) {
            SDL_EndGPURenderPass(sdl_pass);
        }
    }
}

void RenderGraph::end_frame() {
    // Return all transients to the pool.
    allocator_.release_all();
    for (auto& r : resources_) {
        if (r.transient) {
            r.resolved_texture = nullptr;
            r.resolved_buffer = nullptr;
        }
    }
    cb_ = nullptr;
}

void RenderGraph::evict_pool() {
    allocator_.evict_all();
}

void RenderGraph::dump_dot(std::ostream& os) const {
    os << "digraph RenderGraph {\n";
    os << "  rankdir=LR;\n";
    for (size_t i = 0; i < passes_.size(); ++i) {
        os << "  pass" << i << " [label=\"" << passes_[i].name << "\", shape=box];\n";
    }
    for (size_t i = 0; i < resources_.size(); ++i) {
        os << "  res" << i << " [label=\"" << resources_[i].name << (resources_[i].transient ? " (T)" : " (I)")
           << "\", shape=ellipse];\n";
    }
    for (size_t i = 0; i < passes_.size(); ++i) {
        for (auto h : passes_[i].writes) {
            if (h.valid()) {
                os << "  pass" << i << " -> res" << h.index() << ";\n";
            }
        }
        for (auto h : passes_[i].reads) {
            if (h.valid()) {
                os << "  res" << h.index() << " -> pass" << i << ";\n";
            }
        }
    }
    os << "}\n";
}

} // namespace mmo::engine::render_graph
