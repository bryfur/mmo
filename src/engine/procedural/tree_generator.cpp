#define GLM_ENABLE_EXPERIMENTAL
#include "tree_generator.hpp"
#include "engine/gpu/gpu_types.hpp"
#include "engine/model_loader.hpp"
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <queue>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <vector>

namespace mmo::engine::procedural {

// ============================================================================
// Seeded RNG (same algorithm as ez-tree)
// ============================================================================

class TreeRNG {
public:
    explicit TreeRNG(uint32_t seed) : state_(seed == 0 ? 1 : seed) {}

    // Returns random float in [min, max]
    float random(float max = 1.0f, float min = 0.0f) {
        state_ = (state_ * 16807u) % 2147483647u;
        float t = static_cast<float>(state_) / 2147483647.0f;
        return min + t * (max - min);
    }

private:
    uint32_t state_;
};

// ============================================================================
// Internal branch representation
// ============================================================================

struct BranchData {
    glm::vec3 origin;
    glm::quat orientation;
    float length;
    float radius;
    int level;
    int section_count;
    int segment_count;
};

struct SectionInfo {
    glm::vec3 origin;
    glm::quat orientation;
    float radius;
};

// ============================================================================
// Tree generation implementation
// ============================================================================

struct TreeBuilder {
    const TreeParams& params;
    TreeRNG rng;

    // Accumulated geometry
    std::vector<gpu::Vertex3D> branch_verts;
    std::vector<uint32_t> branch_indices;
    std::vector<gpu::Vertex3D> leaf_verts;
    std::vector<uint32_t> leaf_indices;

    // Per-leaf-cluster centers for the canopy shadow proxy. One entry is added
    // per leaf-bearing branch in generate_leaves(). Enables placing small
    // shadow-only proxies at each cluster so the canopy shadow looks lumpy
    // and tree-shaped rather than a single smooth blob.
    struct LeafClump {
        glm::vec3 center;
        glm::vec3 half_extent;
    };
    std::vector<LeafClump> leaf_clumps;

    std::queue<BranchData> branch_queue;

    TreeBuilder(const TreeParams& p) : params(p), rng(p.seed) {}

    void build() {
        // Start with trunk
        BranchData trunk{};
        trunk.origin = glm::vec3(0.0f);
        trunk.orientation = glm::quat(1, 0, 0, 0); // identity
        trunk.length = params.length[0];
        trunk.radius = params.radius[0];
        trunk.level = 0;
        trunk.section_count = params.sections[0];
        trunk.segment_count = params.segments[0];
        branch_queue.push(trunk);

        while (!branch_queue.empty()) {
            BranchData branch = branch_queue.front();
            branch_queue.pop();
            generate_branch(branch);
        }
    }

    void generate_branch(const BranchData& branch) {
        uint32_t index_offset = static_cast<uint32_t>(branch_verts.size());

        glm::quat section_orient = branch.orientation;
        glm::vec3 section_origin = branch.origin;
        float section_length = branch.length / static_cast<float>(branch.section_count);
        if (!params.evergreen) {
            section_length /= static_cast<float>(std::max(1, params.levels - 1));
            section_length *= static_cast<float>(std::max(1, params.levels - 1));
            // Actually ez-tree divides by (levels-1) for deciduous. Let's keep it simple:
            section_length = branch.length / static_cast<float>(branch.section_count);
        }

        std::vector<SectionInfo> sections;
        sections.reserve(branch.section_count + 1);

        for (int i = 0; i <= branch.section_count; i++) {
            float section_radius = branch.radius;

            // Taper
            if (i == branch.section_count && branch.level == params.levels) {
                section_radius = 0.001f;
            } else if (params.evergreen) {
                section_radius *= 1.0f - (static_cast<float>(i) / static_cast<float>(branch.section_count));
            } else {
                section_radius *= 1.0f - params.taper[branch.level] *
                                             (static_cast<float>(i) / static_cast<float>(branch.section_count));
            }

            // Generate ring of vertices for this section
            int seg_count = branch.segment_count;
            for (int j = 0; j < seg_count; j++) {
                float angle = (2.0f * glm::pi<float>() * static_cast<float>(j)) / static_cast<float>(seg_count);

                glm::vec3 local_pos(std::cos(angle) * section_radius, 0.0f, std::sin(angle) * section_radius);
                glm::vec3 local_normal(std::cos(angle), 0.0f, std::sin(angle));

                glm::vec3 world_pos = section_origin + section_orient * local_pos;
                glm::vec3 world_normal = glm::normalize(section_orient * local_normal);

                gpu::Vertex3D vert;
                vert.position = world_pos;
                vert.normal = world_normal;
                vert.texcoord =
                    glm::vec2(static_cast<float>(j) / static_cast<float>(seg_count), (i % 2 == 0) ? 0.0f : 1.0f);
                vert.color = params.bark_color;
                branch_verts.push_back(vert);
            }

            // Duplicate first vertex for UV seam
            {
                float angle = 0.0f;
                glm::vec3 local_pos(std::cos(angle) * section_radius, 0.0f, std::sin(angle) * section_radius);
                glm::vec3 local_normal(std::cos(angle), 0.0f, std::sin(angle));
                glm::vec3 world_pos = section_origin + section_orient * local_pos;
                glm::vec3 world_normal = glm::normalize(section_orient * local_normal);

                gpu::Vertex3D vert;
                vert.position = world_pos;
                vert.normal = world_normal;
                vert.texcoord = glm::vec2(1.0f, (i % 2 == 0) ? 0.0f : 1.0f);
                vert.color = params.bark_color;
                branch_verts.push_back(vert);
            }

            sections.push_back({section_origin, section_orient, section_radius});

            // Advance origin along the branch direction
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            section_origin += section_orient * (up * section_length);

            // Apply gnarliness (random perturbation)
            float gnarl =
                std::max(1.0f, 1.0f / std::sqrt(std::max(section_radius, 0.001f))) * params.gnarliness[branch.level];

            // Convert to euler-like perturbation via small angle quaternions
            float perturb_x = rng.random(gnarl, -gnarl);
            float perturb_z = rng.random(gnarl, -gnarl);
            glm::quat q_perturb = glm::quat(glm::vec3(perturb_x, 0.0f, perturb_z));
            section_orient = section_orient * q_perturb;

            // Apply twist
            glm::quat q_twist = glm::angleAxis(params.twist[branch.level], glm::vec3(0, 1, 0));
            section_orient = section_orient * q_twist;

            // Apply growth force
            glm::vec3 current_up = section_orient * glm::vec3(0, 1, 0);
            glm::vec3 force_dir = glm::normalize(params.force_direction);
            float force_amount = params.force_strength / std::max(section_radius, 0.01f);
            // Slerp toward force direction
            glm::quat q_target = glm::rotation(current_up, force_dir);
            section_orient = glm::slerp(section_orient, q_target * section_orient, std::min(force_amount, 1.0f));
        }

        // Generate indices for branch cylinder
        int N = branch.segment_count + 1; // +1 for UV seam duplicate
        for (int i = 0; i < branch.section_count; i++) {
            for (int j = 0; j < branch.segment_count; j++) {
                uint32_t v1 = index_offset + static_cast<uint32_t>(i * N + j);
                uint32_t v2 = index_offset + static_cast<uint32_t>(i * N + (j + 1));
                uint32_t v3 = v1 + static_cast<uint32_t>(N);
                uint32_t v4 = v2 + static_cast<uint32_t>(N);
                branch_indices.push_back(v1);
                branch_indices.push_back(v3);
                branch_indices.push_back(v2);
                branch_indices.push_back(v2);
                branch_indices.push_back(v3);
                branch_indices.push_back(v4);
            }
        }

        // Generate child branches (if not at max level)
        if (branch.level < params.levels) {
            generate_child_branches(params.children[branch.level], branch.level + 1, sections);
        }

        // Generate leaves at final level
        if (branch.level == params.levels) {
            generate_leaves(sections);
        }
    }

    void generate_child_branches(int count, int level, const std::vector<SectionInfo>& sections) {
        float radial_offset = rng.random();

        for (int i = 0; i < count; i++) {
            float child_start = rng.random(1.0f, params.start[level]);

            // Find which parent sections bracket this position
            int sec_idx = static_cast<int>(std::floor(child_start * static_cast<float>(sections.size() - 1)));
            sec_idx = std::min(sec_idx, static_cast<int>(sections.size()) - 1);
            const auto& sec_a = sections[sec_idx];
            const auto& sec_b = (sec_idx < static_cast<int>(sections.size()) - 1) ? sections[sec_idx + 1] : sec_a;

            // Interpolation factor between sections
            float denom = 1.0f / static_cast<float>(sections.size() - 1);
            float alpha = (child_start - static_cast<float>(sec_idx) * denom) / denom;
            alpha = glm::clamp(alpha, 0.0f, 1.0f);

            // Interpolate origin, radius, orientation
            glm::vec3 child_origin = glm::mix(sec_a.origin, sec_b.origin, alpha);
            float child_radius = params.radius[level] * glm::mix(sec_a.radius, sec_b.radius, alpha);
            glm::quat parent_orient = glm::slerp(sec_a.orientation, sec_b.orientation, alpha);

            // Apply branch angle + radial rotation
            float radial_angle =
                2.0f * glm::pi<float>() * (radial_offset + static_cast<float>(i) / static_cast<float>(count));
            glm::quat q_angle = glm::angleAxis(glm::radians(params.angle[level]), glm::vec3(1, 0, 0));
            glm::quat q_radial = glm::angleAxis(radial_angle, glm::vec3(0, 1, 0));
            glm::quat child_orient = parent_orient * q_radial * q_angle;

            float child_length = params.length[level];
            if (params.evergreen) {
                child_length *= (1.0f - child_start); // shorter at top
            }

            BranchData child{};
            child.origin = child_origin;
            child.orientation = child_orient;
            child.length = child_length;
            child.radius = child_radius;
            child.level = level;
            child.section_count = params.sections[level];
            child.segment_count = params.segments[level];
            branch_queue.push(child);
        }
    }

    void generate_leaves(const std::vector<SectionInfo>& sections) {
        float radial_offset = rng.random();

        // Remember where this clump's leaves start so we can compute its AABB
        // afterward and add a small shadow proxy at this cluster.
        const size_t leaf_vert_start = leaf_verts.size();

        for (int i = 0; i < params.leaf_count; i++) {
            float leaf_start = rng.random(1.0f, params.leaf_start);

            int sec_idx = static_cast<int>(std::floor(leaf_start * static_cast<float>(sections.size() - 1)));
            sec_idx = std::min(sec_idx, static_cast<int>(sections.size()) - 1);
            const auto& sec_a = sections[sec_idx];
            const auto& sec_b = (sec_idx < static_cast<int>(sections.size()) - 1) ? sections[sec_idx + 1] : sec_a;

            float denom = 1.0f / static_cast<float>(sections.size() - 1);
            float alpha = (leaf_start - static_cast<float>(sec_idx) * denom) / denom;
            alpha = glm::clamp(alpha, 0.0f, 1.0f);

            glm::vec3 leaf_origin = glm::mix(sec_a.origin, sec_b.origin, alpha);
            glm::quat parent_orient = glm::slerp(sec_a.orientation, sec_b.orientation, alpha);

            float radial_angle = 2.0f * glm::pi<float>() *
                                 (radial_offset + static_cast<float>(i) / static_cast<float>(params.leaf_count));
            glm::quat q_angle = glm::angleAxis(glm::radians(params.leaf_angle), glm::vec3(1, 0, 0));
            glm::quat q_radial = glm::angleAxis(radial_angle, glm::vec3(0, 1, 0));
            glm::quat leaf_orient = parent_orient * q_radial * q_angle;

            generate_leaf_quad(leaf_origin, leaf_orient);
        }

        // Record this cluster's AABB for the canopy shadow proxy.
        if (leaf_verts.size() > leaf_vert_start) {
            glm::vec3 cmin(std::numeric_limits<float>::max());
            glm::vec3 cmax(-std::numeric_limits<float>::max());
            for (size_t i = leaf_vert_start; i < leaf_verts.size(); ++i) {
                cmin = glm::min(cmin, leaf_verts[i].position);
                cmax = glm::max(cmax, leaf_verts[i].position);
            }
            LeafClump c{};
            c.center = 0.5f * (cmin + cmax);
            c.half_extent = 0.5f * (cmax - cmin);
            leaf_clumps.push_back(c);
        }
    }

    void generate_leaf_quad(const glm::vec3& origin, const glm::quat& orientation) {
        float size = params.leaf_size * (1.0f + rng.random(params.leaf_size_variance, -params.leaf_size_variance));
        float W = size;
        float L = size;

        auto make_quad = [&](float y_rotation) {
            glm::quat q_rot = glm::angleAxis(y_rotation, glm::vec3(0, 1, 0));
            glm::quat full_orient = orientation * q_rot;

            glm::vec3 v[4] = {
                origin + full_orient * glm::vec3(-W / 2, L, 0),
                origin + full_orient * glm::vec3(-W / 2, 0, 0),
                origin + full_orient * glm::vec3(W / 2, 0, 0),
                origin + full_orient * glm::vec3(W / 2, L, 0),
            };

            glm::vec3 normal = glm::normalize(full_orient * glm::vec3(0, 0, 1));

            uint32_t base = static_cast<uint32_t>(leaf_verts.size());

            glm::vec2 uvs[4] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
            for (int i = 0; i < 4; i++) {
                gpu::Vertex3D vert;
                vert.position = v[i];
                vert.normal = normal;
                vert.texcoord = uvs[i];
                vert.color = params.leaf_color;
                leaf_verts.push_back(vert);
            }

            leaf_indices.push_back(base);
            leaf_indices.push_back(base + 1);
            leaf_indices.push_back(base + 2);
            leaf_indices.push_back(base);
            leaf_indices.push_back(base + 2);
            leaf_indices.push_back(base + 3);
        };

        make_quad(0.0f);
        if (params.double_billboard) {
            make_quad(glm::half_pi<float>());
        }
    }
};

// ============================================================================
// Texture loading helper
// ============================================================================

static bool load_texture_from_file(const std::string& path, Mesh& mesh) {
    if (path.empty()) {
        return false;
    }

    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        SDL_Log("TreeGenerator: Failed to load texture %s: %s", path.c_str(), SDL_GetError());
        return false;
    }

    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!rgba) {
        SDL_Log("TreeGenerator: Failed to convert texture to RGBA: %s", SDL_GetError());
        return false;
    }

    size_t data_size = static_cast<size_t>(rgba->w * rgba->h * 4);
    mesh.texture_pixels.resize(data_size);
    std::memcpy(mesh.texture_pixels.data(), rgba->pixels, data_size);
    mesh.texture_width = rgba->w;
    mesh.texture_height = rgba->h;
    mesh.has_texture = true;

    SDL_DestroySurface(rgba);
    return true;
}

// ============================================================================
// Public API
// ============================================================================

std::unique_ptr<Model> TreeGenerator::generate(const TreeParams& params) {
    TreeBuilder builder(params);
    builder.build();

    auto model = std::make_unique<Model>();

    // Branches mesh
    if (!builder.branch_verts.empty()) {
        Mesh branch_mesh;
        branch_mesh.vertices = std::move(builder.branch_verts);
        branch_mesh.indices = std::move(builder.branch_indices);
        // Set white vertex color so texture isn't tinted when textured
        if (!params.bark_texture_path.empty()) {
            for (auto& v : branch_mesh.vertices) v.color = glm::vec4(1.0f);
            load_texture_from_file(params.bark_texture_path, branch_mesh);
        }
        if (!branch_mesh.has_texture) {
            branch_mesh.base_color = 0xFF000000 | (static_cast<uint32_t>(params.bark_color.r * 255) << 16) |
                                     (static_cast<uint32_t>(params.bark_color.g * 255) << 8) |
                                     (static_cast<uint32_t>(params.bark_color.b * 255));
        }
        model->meshes.push_back(std::move(branch_mesh));
    }

    // Leaves mesh - dominates triangle count (~60-70% of tree tris) but
    // contributes mostly dappled shadow coverage. Marked cast_shadows=false;
    // the per-cluster proxy mesh below carries the shadow silhouette at
    // drastically lower triangle count and no alpha-test overdraw.
    if (!builder.leaf_verts.empty()) {
        Mesh leaf_mesh;
        leaf_mesh.vertices = std::move(builder.leaf_verts);
        leaf_mesh.indices = std::move(builder.leaf_indices);
        leaf_mesh.cast_shadows = false;
        if (!params.leaf_texture_path.empty()) {
            for (auto& v : leaf_mesh.vertices) v.color = glm::vec4(1.0f);
            load_texture_from_file(params.leaf_texture_path, leaf_mesh);
        }
        if (!leaf_mesh.has_texture) {
            leaf_mesh.base_color = 0xFF000000 | (static_cast<uint32_t>(params.leaf_color.r * 255) << 16) |
                                   (static_cast<uint32_t>(params.leaf_color.g * 255) << 8) |
                                   (static_cast<uint32_t>(params.leaf_color.b * 255));
        }
        model->meshes.push_back(std::move(leaf_mesh));
    }

    // Canopy shadow proxy — a triple cross-billboard of alpha-tested leaf
    // cards per leaf cluster. Each cluster becomes three mutually-perpendicular
    // textured quads at the cluster centroid, sized to enclose the cluster.
    //
    // The shadow depth fragment shader does alpha-test via hasTexture, so the
    // leaf texture punches a leaf-shaped silhouette into the shadow — we get
    // actual dappled leaf shadows rather than smooth blobs.
    //
    // Per cluster: 12 vertices + 6 triangles (3 quads).
    // Typical tree with ~60 clusters: ~360 tris per tree (vs ~2000+ for raw
    // leaves, 1200 for icosphere blobs). Fewer but bigger quads means ~10x
    // less alpha-test overdraw than rasterizing the real leaf mesh.
    if (!builder.leaf_clumps.empty()) {
        // Three orthogonal unit quads (local-space). Each spans [-1, +1] × [-1, +1]
        // in its own plane. Two triangles per quad.
        //   plane 0: XY (normal Z)
        //   plane 1: XZ (normal Y) - horizontal, captures top-down shadow
        //   plane 2: YZ (normal X)
        const glm::vec3 plane_verts[3][4] = {
            {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}, // XY
            {{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}}, // XZ
            {{0, -1, -1}, {0, 1, -1}, {0, 1, 1}, {0, -1, 1}}, // YZ
        };
        const glm::vec3 plane_normals[3] = {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
        const glm::vec2 quad_uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

        Mesh proxy_mesh;
        proxy_mesh.shadow_only = true;
        proxy_mesh.cast_shadows = true;

        // Share the leaf texture so the shadow pass can alpha-test using the
        // actual leaf shape. If no texture path is provided, the mesh renders
        // opaque which still gives a leaf-cluster-sized shadow (just solid).
        if (!params.leaf_texture_path.empty()) {
            load_texture_from_file(params.leaf_texture_path, proxy_mesh);
        }
        proxy_mesh.base_color = 0xFF000000 | (static_cast<uint32_t>(params.leaf_color.r * 255) << 16) |
                                (static_cast<uint32_t>(params.leaf_color.g * 255) << 8) |
                                (static_cast<uint32_t>(params.leaf_color.b * 255));

        proxy_mesh.vertices.reserve(builder.leaf_clumps.size() * 12);
        proxy_mesh.indices.reserve(builder.leaf_clumps.size() * 18);

        // Deterministic per-clump hash for size + rotation variation.
        uint32_t hash_state = params.seed * 0x9E3779B9u + 0x85EBCA6Bu;
        auto next_rand = [&hash_state]() -> float {
            hash_state ^= hash_state >> 16;
            hash_state *= 0x85ebca6bu;
            hash_state ^= hash_state >> 13;
            hash_state *= 0xc2b2ae35u;
            hash_state ^= hash_state >> 16;
            return (hash_state & 0x00FFFFFFu) / static_cast<float>(0x01000000);
        };

        uint32_t vert_offset = 0;
        for (const auto& clump : builder.leaf_clumps) {
            // Random rotation so adjacent clumps' quads don't align into
            // obvious flat sheets — creates organic dappled shadow edges.
            float yaw = next_rand() * 6.2831853f;
            float pitch = (next_rand() - 0.5f) * 1.5f;
            float roll = (next_rand() - 0.5f) * 1.5f;
            glm::quat q = glm::quat(glm::vec3(pitch, yaw, roll));

            // Size slightly larger than the clump bounding box so leaves read
            // as slightly plumper clusters. Per-clump variance so they don't
            // feel uniform.
            float size_var = 0.95f + next_rand() * 0.35f;
            glm::vec3 half = glm::max(clump.half_extent * 1.05f, glm::vec3(0.8f)) * size_var;

            for (int p = 0; p < 3; ++p) {
                for (int i = 0; i < 4; ++i) {
                    glm::vec3 local = plane_verts[p][i] * half;
                    glm::vec3 world = clump.center + q * local;
                    gpu::Vertex3D v{};
                    v.position = world;
                    v.normal = q * plane_normals[p];
                    v.texcoord = quad_uvs[i];
                    v.color = glm::vec4(1.0f);
                    proxy_mesh.vertices.push_back(v);
                }
                // Double-sided quad: shadow pipelines cull_front, so we need
                // both winding orders for leaves to cast from any light angle.
                uint32_t b = vert_offset + p * 4;
                // CCW
                proxy_mesh.indices.push_back(b + 0);
                proxy_mesh.indices.push_back(b + 1);
                proxy_mesh.indices.push_back(b + 2);
                proxy_mesh.indices.push_back(b + 0);
                proxy_mesh.indices.push_back(b + 2);
                proxy_mesh.indices.push_back(b + 3);
                // CW (reverse winding — makes the quad double-sided)
                proxy_mesh.indices.push_back(b + 0);
                proxy_mesh.indices.push_back(b + 2);
                proxy_mesh.indices.push_back(b + 1);
                proxy_mesh.indices.push_back(b + 0);
                proxy_mesh.indices.push_back(b + 3);
                proxy_mesh.indices.push_back(b + 2);
            }
            vert_offset += 12;
        }

        model->meshes.push_back(std::move(proxy_mesh));
    }

    // Compute AABB from all vertices
    model->min_x = model->min_y = model->min_z = 1e9f;
    model->max_x = model->max_y = model->max_z = -1e9f;
    for (const auto& mesh : model->meshes) {
        for (const auto& v : mesh.vertices) {
            model->min_x = std::min(model->min_x, v.position.x);
            model->min_y = std::min(model->min_y, v.position.y);
            model->min_z = std::min(model->min_z, v.position.z);
            model->max_x = std::max(model->max_x, v.position.x);
            model->max_y = std::max(model->max_y, v.position.y);
            model->max_z = std::max(model->max_z, v.position.z);
        }
    }
    // Trunk base is always at Y=0; clamp min_y so the centering transform
    // doesn't lift the tree off the ground when branches droop below origin
    model->min_y = 0.0f;

    model->compute_bounding_sphere();
    model->loaded = true;

    return model;
}

std::unique_ptr<Model> TreeGenerator::generate_oak(uint32_t seed, const std::string& texture_base_path) {
    TreeParams p;
    p.seed = seed;
    p.levels = 3;
    p.evergreen = false;

    // Thick trunk, wide crown
    p.length[0] = 10.0f;
    p.length[1] = 8.0f;
    p.length[2] = 5.0f;
    p.length[3] = 1.0f;
    p.radius[0] = 1.0f;
    p.radius[1] = 0.5f;
    p.radius[2] = 0.4f;
    p.radius[3] = 0.3f;
    p.taper[0] = 0.6f;
    p.taper[1] = 0.7f;
    p.taper[2] = 0.8f;
    p.taper[3] = 0.9f;
    p.children[0] = 5;
    p.children[1] = 4;
    p.children[2] = 3;
    p.children[3] = 0;
    p.angle[0] = 0.0f;
    p.angle[1] = 50.0f;
    p.angle[2] = 55.0f;
    p.angle[3] = 60.0f;
    p.gnarliness[0] = 0.1f;
    p.gnarliness[1] = 0.15f;
    p.gnarliness[2] = 0.2f;
    p.gnarliness[3] = 0.02f;
    // Reduced poly: trunk 8*8->6*6 segments, level 1 6*5->5*4, etc. Visual
    // impact negligible on instanced trees at typical viewing distance; saves
    // ~40% of branch triangles per tree.
    p.sections[0] = 6;
    p.sections[1] = 5;
    p.sections[2] = 3;
    p.sections[3] = 2;
    p.segments[0] = 6;
    p.segments[1] = 4;
    p.segments[2] = 3;
    p.segments[3] = 3;
    p.start[0] = 0.0f;
    p.start[1] = 0.5f;
    p.start[2] = 0.3f;
    p.start[3] = 0.2f;

    p.force_strength = 0.02f;

    p.leaf_count = 3;
    p.leaf_size = 3.0f;
    p.leaf_size_variance = 0.5f;
    p.leaf_angle = 30.0f;
    p.double_billboard = true;

    p.bark_color = {0.4f, 0.28f, 0.18f, 1.0f};
    p.leaf_color = {0.15f, 0.45f, 0.12f, 1.0f};

    if (!texture_base_path.empty()) {
        p.bark_texture_path = texture_base_path + "/bark/oak_color_1k.jpg";
        p.leaf_texture_path = texture_base_path + "/leaves/oak_color.png";
    }

    return generate(p);
}

std::unique_ptr<Model> TreeGenerator::generate_pine(uint32_t seed, const std::string& texture_base_path) {
    TreeParams p;
    p.seed = seed;
    p.levels = 3;
    p.evergreen = true;

    // Tall, conical
    p.length[0] = 15.0f;
    p.length[1] = 6.0f;
    p.length[2] = 3.0f;
    p.length[3] = 1.0f;
    p.radius[0] = 0.8f;
    p.radius[1] = 0.3f;
    p.radius[2] = 0.2f;
    p.radius[3] = 0.15f;
    p.taper[0] = 0.8f;
    p.taper[1] = 0.7f;
    p.taper[2] = 0.8f;
    p.taper[3] = 0.9f;
    p.children[0] = 8;
    p.children[1] = 4;
    p.children[2] = 2;
    p.children[3] = 0;
    p.angle[0] = 0.0f;
    p.angle[1] = 75.0f;
    p.angle[2] = 60.0f;
    p.angle[3] = 50.0f;
    p.gnarliness[0] = 0.05f;
    p.gnarliness[1] = 0.08f;
    p.gnarliness[2] = 0.1f;
    p.gnarliness[3] = 0.02f;
    p.sections[0] = 7;
    p.sections[1] = 4;
    p.sections[2] = 3;
    p.sections[3] = 2;
    p.segments[0] = 5;
    p.segments[1] = 3;
    p.segments[2] = 3;
    p.segments[3] = 3;
    p.start[0] = 0.0f;
    p.start[1] = 0.2f;
    p.start[2] = 0.2f;
    p.start[3] = 0.2f;

    p.force_strength = 0.03f;

    p.leaf_count = 4;
    p.leaf_size = 2.0f;
    p.leaf_size_variance = 0.3f;
    p.leaf_angle = 15.0f;
    p.double_billboard = true;

    p.bark_color = {0.35f, 0.22f, 0.12f, 1.0f};
    p.leaf_color = {0.08f, 0.3f, 0.06f, 1.0f};

    if (!texture_base_path.empty()) {
        p.bark_texture_path = texture_base_path + "/bark/pine_color_1k.jpg";
        p.leaf_texture_path = texture_base_path + "/leaves/pine_color.png";
    }

    return generate(p);
}

std::unique_ptr<Model> TreeGenerator::generate_dead(uint32_t seed, const std::string& texture_base_path) {
    TreeParams p;
    p.seed = seed;
    p.levels = 2;
    p.evergreen = false;

    // Sparse, gnarled, no leaves
    p.length[0] = 8.0f;
    p.length[1] = 6.0f;
    p.length[2] = 3.0f;
    p.length[3] = 1.0f;
    p.radius[0] = 0.7f;
    p.radius[1] = 0.35f;
    p.radius[2] = 0.2f;
    p.radius[3] = 0.1f;
    p.taper[0] = 0.5f;
    p.taper[1] = 0.6f;
    p.taper[2] = 0.8f;
    p.taper[3] = 0.9f;
    p.children[0] = 4;
    p.children[1] = 3;
    p.children[2] = 0;
    p.children[3] = 0;
    p.angle[0] = 0.0f;
    p.angle[1] = 45.0f;
    p.angle[2] = 50.0f;
    p.angle[3] = 0.0f;
    p.gnarliness[0] = 0.3f;
    p.gnarliness[1] = 0.4f;
    p.gnarliness[2] = 0.3f;
    p.gnarliness[3] = 0.0f;
    p.sections[0] = 6;
    p.sections[1] = 4;
    p.sections[2] = 3;
    p.sections[3] = 2;
    p.segments[0] = 5;
    p.segments[1] = 3;
    p.segments[2] = 3;
    p.segments[3] = 3;
    p.start[0] = 0.0f;
    p.start[1] = 0.4f;
    p.start[2] = 0.3f;
    p.start[3] = 0.3f;

    p.force_strength = 0.005f;

    // No leaves for dead tree
    p.leaf_count = 0;

    p.bark_color = {0.35f, 0.3f, 0.25f, 1.0f};
    p.leaf_color = {0.0f, 0.0f, 0.0f, 0.0f};

    if (!texture_base_path.empty()) {
        p.bark_texture_path = texture_base_path + "/bark/birch_color_1k.jpg";
    }

    return generate(p);
}

std::unique_ptr<Model> TreeGenerator::generate_willow(uint32_t seed, const std::string& texture_base_path) {
    TreeParams p;
    p.seed = seed;
    p.levels = 3;
    p.evergreen = false;

    // Drooping, wide canopy with long hanging branches
    p.length[0] = 8.0f;
    p.length[1] = 10.0f;
    p.length[2] = 8.0f;
    p.length[3] = 2.0f;
    p.radius[0] = 1.2f;
    p.radius[1] = 0.4f;
    p.radius[2] = 0.2f;
    p.radius[3] = 0.1f;
    p.taper[0] = 0.5f;
    p.taper[1] = 0.6f;
    p.taper[2] = 0.8f;
    p.taper[3] = 0.9f;
    p.children[0] = 6;
    p.children[1] = 5;
    p.children[2] = 3;
    p.children[3] = 0;
    p.angle[0] = 0.0f;
    p.angle[1] = 60.0f;
    p.angle[2] = 80.0f;
    p.angle[3] = 85.0f;
    p.gnarliness[0] = 0.08f;
    p.gnarliness[1] = 0.12f;
    p.gnarliness[2] = 0.15f;
    p.gnarliness[3] = 0.02f;
    p.sections[0] = 6;
    p.sections[1] = 5;
    p.sections[2] = 4;
    p.sections[3] = 3;
    p.segments[0] = 5;
    p.segments[1] = 4;
    p.segments[2] = 3;
    p.segments[3] = 3;
    p.start[0] = 0.0f;
    p.start[1] = 0.4f;
    p.start[2] = 0.2f;
    p.start[3] = 0.1f;

    // Downward force to make branches droop
    p.force_direction = {0.0f, -0.3f, 0.0f};
    p.force_strength = 0.015f;

    p.leaf_count = 4;
    p.leaf_size = 2.5f;
    p.leaf_size_variance = 0.6f;
    p.leaf_angle = 45.0f;
    p.double_billboard = true;

    p.bark_color = {0.35f, 0.3f, 0.2f, 1.0f};
    p.leaf_color = {0.2f, 0.5f, 0.15f, 1.0f};

    if (!texture_base_path.empty()) {
        p.bark_texture_path = texture_base_path + "/bark/willow_color_1k.jpg";
        p.leaf_texture_path = texture_base_path + "/leaves/ash_color.png";
    }

    return generate(p);
}

std::unique_ptr<Model> TreeGenerator::generate_birch(uint32_t seed, const std::string& texture_base_path) {
    TreeParams p;
    p.seed = seed;
    p.levels = 3;
    p.evergreen = false;

    // Tall, thin, elegant with white bark
    p.length[0] = 12.0f;
    p.length[1] = 6.0f;
    p.length[2] = 4.0f;
    p.length[3] = 1.0f;
    p.radius[0] = 0.5f;
    p.radius[1] = 0.25f;
    p.radius[2] = 0.15f;
    p.radius[3] = 0.1f;
    p.taper[0] = 0.7f;
    p.taper[1] = 0.75f;
    p.taper[2] = 0.8f;
    p.taper[3] = 0.9f;
    p.children[0] = 5;
    p.children[1] = 4;
    p.children[2] = 2;
    p.children[3] = 0;
    p.angle[0] = 0.0f;
    p.angle[1] = 40.0f;
    p.angle[2] = 50.0f;
    p.angle[3] = 55.0f;
    p.gnarliness[0] = 0.05f;
    p.gnarliness[1] = 0.1f;
    p.gnarliness[2] = 0.15f;
    p.gnarliness[3] = 0.02f;
    p.sections[0] = 7;
    p.sections[1] = 4;
    p.sections[2] = 3;
    p.sections[3] = 2;
    p.segments[0] = 5;
    p.segments[1] = 3;
    p.segments[2] = 3;
    p.segments[3] = 3;
    p.start[0] = 0.0f;
    p.start[1] = 0.5f;
    p.start[2] = 0.3f;
    p.start[3] = 0.2f;

    p.force_strength = 0.025f;

    p.leaf_count = 3;
    p.leaf_size = 2.0f;
    p.leaf_size_variance = 0.4f;
    p.leaf_angle = 25.0f;
    p.double_billboard = true;

    p.bark_color = {0.85f, 0.82f, 0.78f, 1.0f}; // White-ish bark
    p.leaf_color = {0.2f, 0.55f, 0.1f, 1.0f};

    if (!texture_base_path.empty()) {
        p.bark_texture_path = texture_base_path + "/bark/birch_color_1k.jpg";
        p.leaf_texture_path = texture_base_path + "/leaves/aspen_color.png";
    }

    return generate(p);
}

std::unique_ptr<Model> TreeGenerator::generate_maple(uint32_t seed, const std::string& texture_base_path) {
    TreeParams p;
    p.seed = seed;
    p.levels = 3;
    p.evergreen = false;

    // Round, dense canopy, medium height
    p.length[0] = 9.0f;
    p.length[1] = 7.0f;
    p.length[2] = 5.0f;
    p.length[3] = 1.5f;
    p.radius[0] = 0.9f;
    p.radius[1] = 0.45f;
    p.radius[2] = 0.3f;
    p.radius[3] = 0.2f;
    p.taper[0] = 0.6f;
    p.taper[1] = 0.65f;
    p.taper[2] = 0.75f;
    p.taper[3] = 0.85f;
    p.children[0] = 6;
    p.children[1] = 5;
    p.children[2] = 4;
    p.children[3] = 0;
    p.angle[0] = 0.0f;
    p.angle[1] = 55.0f;
    p.angle[2] = 50.0f;
    p.angle[3] = 55.0f;
    p.gnarliness[0] = 0.12f;
    p.gnarliness[1] = 0.18f;
    p.gnarliness[2] = 0.22f;
    p.gnarliness[3] = 0.02f;
    p.sections[0] = 6;
    p.sections[1] = 5;
    p.sections[2] = 4;
    p.sections[3] = 2;
    p.segments[0] = 5;
    p.segments[1] = 4;
    p.segments[2] = 3;
    p.segments[3] = 3;
    p.start[0] = 0.0f;
    p.start[1] = 0.35f;
    p.start[2] = 0.25f;
    p.start[3] = 0.2f;

    p.force_strength = 0.02f;

    p.leaf_count = 5;
    p.leaf_size = 2.8f;
    p.leaf_size_variance = 0.5f;
    p.leaf_angle = 35.0f;
    p.double_billboard = true;

    p.bark_color = {0.38f, 0.3f, 0.22f, 1.0f};
    p.leaf_color = {0.6f, 0.2f, 0.05f, 1.0f}; // Autumn orange-red

    if (!texture_base_path.empty()) {
        p.bark_texture_path = texture_base_path + "/bark/oak_color_1k.jpg";
        p.leaf_texture_path = texture_base_path + "/leaves/oak_color.png";
    }

    return generate(p);
}

std::unique_ptr<Model> TreeGenerator::generate_aspen(uint32_t seed, const std::string& texture_base_path) {
    TreeParams p;
    p.seed = seed;
    p.levels = 3;
    p.evergreen = false;

    // Tall, slender, columnar shape with small trembling leaves
    p.length[0] = 14.0f;
    p.length[1] = 5.0f;
    p.length[2] = 3.0f;
    p.length[3] = 1.0f;
    p.radius[0] = 0.4f;
    p.radius[1] = 0.2f;
    p.radius[2] = 0.12f;
    p.radius[3] = 0.08f;
    p.taper[0] = 0.75f;
    p.taper[1] = 0.7f;
    p.taper[2] = 0.8f;
    p.taper[3] = 0.9f;
    p.children[0] = 7;
    p.children[1] = 3;
    p.children[2] = 2;
    p.children[3] = 0;
    p.angle[0] = 0.0f;
    p.angle[1] = 35.0f;
    p.angle[2] = 40.0f;
    p.angle[3] = 45.0f;
    p.gnarliness[0] = 0.03f;
    p.gnarliness[1] = 0.06f;
    p.gnarliness[2] = 0.1f;
    p.gnarliness[3] = 0.02f;
    p.sections[0] = 7;
    p.sections[1] = 4;
    p.sections[2] = 3;
    p.sections[3] = 2;
    p.segments[0] = 4;
    p.segments[1] = 3;
    p.segments[2] = 3;
    p.segments[3] = 3;
    p.start[0] = 0.0f;
    p.start[1] = 0.3f;
    p.start[2] = 0.2f;
    p.start[3] = 0.2f;

    p.force_strength = 0.035f; // Strong upward growth

    p.leaf_count = 3;
    p.leaf_size = 1.5f;
    p.leaf_size_variance = 0.3f;
    p.leaf_angle = 20.0f;
    p.leaf_start = 0.2f;
    p.double_billboard = true;

    p.bark_color = {0.8f, 0.78f, 0.7f, 1.0f};  // Pale bark
    p.leaf_color = {0.55f, 0.65f, 0.1f, 1.0f}; // Yellow-green

    if (!texture_base_path.empty()) {
        p.bark_texture_path = texture_base_path + "/bark/birch_color_1k.jpg";
        p.leaf_texture_path = texture_base_path + "/leaves/aspen_color.png";
    }

    return generate(p);
}

} // namespace mmo::engine::procedural
