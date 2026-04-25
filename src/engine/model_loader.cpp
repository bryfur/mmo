#include "model_loader.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_iostream.h"
#include "SDL3/SDL_pixels.h"
#include "SDL3/SDL_surface.h"
#include "engine/gpu/gpu_types.hpp"
#include "engine/model_utils.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "glm/gtc/quaternion.hpp"
#include "gpu/gpu_device.hpp"
#include "gpu/gpu_buffer.hpp"
#include "gpu/gpu_texture.hpp"

#include <SDL3/SDL_gpu.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#include <SDL3_image/SDL_image.h>

#include "engine/core/asset/file_watcher.hpp"
#include "engine/core/logger.hpp"

namespace mmo::engine {

// Implementation of Mesh::bind_buffers
void Mesh::bind_buffers(SDL_GPURenderPass* pass) const {
    if (vertex_buffer) {
        SDL_GPUBufferBinding vbuf = { vertex_buffer->handle(), 0 };
        SDL_BindGPUVertexBuffers(pass, 0, &vbuf, 1);
    }
    if (index_buffer) {
        SDL_GPUBufferBinding ibuf = { index_buffer->handle(), 0 };
        SDL_BindGPUIndexBuffer(pass, &ibuf, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    }
}

} // namespace mmo::engine

namespace {

// Custom image loader for tinygltf using SDL3_image
bool load_image_data(tinygltf::Image* image, const int image_idx, std::string* err,
                     std::string* warn, int req_width, int req_height,
                     const unsigned char* bytes, int size, void* user_data) {
    (void)image_idx;
    (void)warn;
    (void)req_width;
    (void)req_height;
    (void)user_data;

    SDL_IOStream* io = SDL_IOFromConstMem(bytes, size);
    if (!io) {
        if (err) *err = "Failed to create SDL IOStream from memory";
        return false;
    }

    SDL_Surface* surface = IMG_Load_IO(io, true);  // true = close IOStream when done
    if (!surface) {
        if (err) *err = std::string("SDL_image failed to load: ") + SDL_GetError();
        return false;
    }

    // Convert to RGBA format
    SDL_Surface* rgba_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);

    if (!rgba_surface) {
        if (err) *err = std::string("Failed to convert to RGBA: ") + SDL_GetError();
        return false;
    }

    image->width = rgba_surface->w;
    image->height = rgba_surface->h;
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;

    size_t data_size = static_cast<size_t>(rgba_surface->w * rgba_surface->h * 4);
    image->image.resize(data_size);
    memcpy(image->image.data(), rgba_surface->pixels, data_size);

    SDL_DestroySurface(rgba_surface);
    return true;
}

// Dummy write function (not used but required by tinygltf)
bool write_image_data(const std::string* basepath, const std::string* filename,
                      const tinygltf::Image* image, bool embedImages,
                      const tinygltf::FsCallbacks* fs_cb,
                      const tinygltf::URICallbacks* uri_cb, std::string* out_uri,
                      void* user_pointer) {
    (void)basepath;
    (void)filename;
    (void)image;
    (void)embedImages;
    (void)fs_cb;
    (void)uri_cb;
    (void)out_uri;
    (void)user_pointer;
    return false;  // Writing not supported
}

}  // anonymous namespace

namespace mmo::engine {

bool ModelLoader::load_glb(const std::string& path, Model& model) {
    tinygltf::Model gltf;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    // Set custom SDL3_image loader
    loader.SetImageLoader(load_image_data, nullptr);
    loader.SetImageWriter(write_image_data, nullptr);

    bool ret = loader.LoadBinaryFromFile(&gltf, &err, &warn, path);

    if (!warn.empty()) {
        ENGINE_LOG_WARN("model", "Warning loading {}: {}", path, warn);
    }
    if (!err.empty()) {
        ENGINE_LOG_ERROR("model", "Error loading {}: {}", path, err);
        return false;
    }
    if (!ret) {
        ENGINE_LOG_ERROR("model", "Failed to load GLB: {}", path);
        return false;
    }

    model.meshes.clear();
    model.min_x = model.min_y = model.min_z = 1e10f;
    model.max_x = model.max_y = model.max_z = -1e10f;

    // Load textures from images
    std::vector<std::pair<std::vector<uint8_t>, std::pair<int, int>>> loaded_textures;
    for (const auto& image : gltf.images) {
        if (!image.image.empty()) {
            loaded_textures.push_back({image.image, {image.width, image.height}});
        }
    }

    // Process all meshes
    for (const auto& mesh : gltf.meshes) {
        for (const auto& primitive : mesh.primitives) {
            Mesh out_mesh;

            // Get material info
            int texture_image_idx = -1;
            if (primitive.material >= 0 && primitive.material < static_cast<int>(gltf.materials.size())) {
                const auto& mat = gltf.materials[primitive.material];

                // Base color factor
                if (mat.pbrMetallicRoughness.baseColorFactor.size() == 4) {
                    uint8_t r = static_cast<uint8_t>(mat.pbrMetallicRoughness.baseColorFactor[0] * 255);
                    uint8_t g = static_cast<uint8_t>(mat.pbrMetallicRoughness.baseColorFactor[1] * 255);
                    uint8_t b = static_cast<uint8_t>(mat.pbrMetallicRoughness.baseColorFactor[2] * 255);
                    uint8_t a = static_cast<uint8_t>(mat.pbrMetallicRoughness.baseColorFactor[3] * 255);
                    out_mesh.base_color = (a << 24) | (b << 16) | (g << 8) | r;

                    out_mesh.material.base_color_factor = {
                        static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[0]),
                        static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[1]),
                        static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[2]),
                        static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[3]),
                    };
                }

                out_mesh.material.metallic_factor  = static_cast<float>(mat.pbrMetallicRoughness.metallicFactor);
                out_mesh.material.roughness_factor = static_cast<float>(mat.pbrMetallicRoughness.roughnessFactor);
                out_mesh.material.normal_scale     = static_cast<float>(mat.normalTexture.scale);
                out_mesh.material.occlusion_strength = static_cast<float>(mat.occlusionTexture.strength);

                // Base color texture
                int tex_idx = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (tex_idx >= 0 && tex_idx < static_cast<int>(gltf.textures.size())) {
                    texture_image_idx = gltf.textures[tex_idx].source;
                    out_mesh.has_texture = true;
                }

                // Metallic-roughness texture (glTF packs: G = roughness, B = metallic)
                int mr_idx = mat.pbrMetallicRoughness.metallicRoughnessTexture.index;
                if (mr_idx >= 0 && mr_idx < static_cast<int>(gltf.textures.size())) {
                    int src = gltf.textures[mr_idx].source;
                    if (src >= 0 && src < static_cast<int>(gltf.images.size())) {
                        const auto& img = gltf.images[src];
                        out_mesh.material.metallic_roughness_pixels = img.image;
                        out_mesh.material.metallic_roughness_width  = img.width;
                        out_mesh.material.metallic_roughness_height = img.height;
                    }
                }

                // Normal map (tangent-space, UNORM)
                int n_idx = mat.normalTexture.index;
                if (n_idx >= 0 && n_idx < static_cast<int>(gltf.textures.size())) {
                    int src = gltf.textures[n_idx].source;
                    if (src >= 0 && src < static_cast<int>(gltf.images.size())) {
                        const auto& img = gltf.images[src];
                        out_mesh.material.normal_pixels = img.image;
                        out_mesh.material.normal_width  = img.width;
                        out_mesh.material.normal_height = img.height;
                    }
                }

                // Ambient occlusion (R channel)
                int ao_idx = mat.occlusionTexture.index;
                if (ao_idx >= 0 && ao_idx < static_cast<int>(gltf.textures.size())) {
                    int src = gltf.textures[ao_idx].source;
                    if (src >= 0 && src < static_cast<int>(gltf.images.size())) {
                        const auto& img = gltf.images[src];
                        out_mesh.material.occlusion_pixels = img.image;
                        out_mesh.material.occlusion_width  = img.width;
                        out_mesh.material.occlusion_height = img.height;
                    }
                }
            }

            // Helper to get accessor data
            auto get_buffer_data = [&](int accessor_idx) -> const uint8_t* {
                if (accessor_idx < 0) return nullptr;
                const auto& accessor = gltf.accessors[accessor_idx];
                const auto& bufferView = gltf.bufferViews[accessor.bufferView];
                const auto& buffer = gltf.buffers[bufferView.buffer];
                return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
            };

            // Position
            auto pos_it = primitive.attributes.find("POSITION");
            if (pos_it == primitive.attributes.end()) continue;

            const auto& pos_accessor = gltf.accessors[pos_it->second];
            const float* positions = reinterpret_cast<const float*>(get_buffer_data(pos_it->second));
            int vertex_count = static_cast<int>(pos_accessor.count);

            // Normal
            const float* normals = nullptr;
            auto norm_it = primitive.attributes.find("NORMAL");
            if (norm_it != primitive.attributes.end()) {
                normals = reinterpret_cast<const float*>(get_buffer_data(norm_it->second));
            }

            // Texcoord
            const float* uvs = nullptr;
            auto uv_it = primitive.attributes.find("TEXCOORD_0");
            if (uv_it != primitive.attributes.end()) {
                uvs = reinterpret_cast<const float*>(get_buffer_data(uv_it->second));
            }

            // Tangent (glTF: vec4 with bitangent sign in .w)
            const float* tangents = nullptr;
            auto tan_it = primitive.attributes.find("TANGENT");
            if (tan_it != primitive.attributes.end()) {
                tangents = reinterpret_cast<const float*>(get_buffer_data(tan_it->second));
            }

            // Color
            const float* colors_f = nullptr;
            const uint8_t* colors_u8 = nullptr;
            auto color_it = primitive.attributes.find("COLOR_0");
            if (color_it != primitive.attributes.end()) {
                const auto& color_accessor = gltf.accessors[color_it->second];
                if (color_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                    colors_f = reinterpret_cast<const float*>(get_buffer_data(color_it->second));
                } else {
                    colors_u8 = get_buffer_data(color_it->second);
                }
            }

            // Build vertices
            out_mesh.vertices.reserve(vertex_count);
            for (int i = 0; i < vertex_count; i++) {
                Vertex3D v;
                v.position.x = positions[i * 3 + 0];
                v.position.y = positions[i * 3 + 1];
                v.position.z = positions[i * 3 + 2];

                model.min_x = std::min(model.min_x, v.position.x);
                model.min_y = std::min(model.min_y, v.position.y);
                model.min_z = std::min(model.min_z, v.position.z);
                model.max_x = std::max(model.max_x, v.position.x);
                model.max_y = std::max(model.max_y, v.position.y);
                model.max_z = std::max(model.max_z, v.position.z);

                if (normals) {
                    v.normal.x = normals[i * 3 + 0];
                    v.normal.y = normals[i * 3 + 1];
                    v.normal.z = normals[i * 3 + 2];
                } else {
                    v.normal = {0, 1, 0};
                }

                if (uvs) {
                    v.texcoord.x = uvs[i * 2 + 0];
                    v.texcoord.y = uvs[i * 2 + 1];
                } else {
                    v.texcoord = {0, 0};
                }

                if (colors_f) {
                    v.color.r = colors_f[i * 4 + 0];
                    v.color.g = colors_f[i * 4 + 1];
                    v.color.b = colors_f[i * 4 + 2];
                    v.color.a = colors_f[i * 4 + 3];
                } else if (colors_u8) {
                    v.color.r = colors_u8[i * 4 + 0] / 255.0f;
                    v.color.g = colors_u8[i * 4 + 1] / 255.0f;
                    v.color.b = colors_u8[i * 4 + 2] / 255.0f;
                    v.color.a = colors_u8[i * 4 + 3] / 255.0f;
                } else {
                    v.color.r = (out_mesh.base_color & 0xFF) / 255.0f;
                    v.color.g = ((out_mesh.base_color >> 8) & 0xFF) / 255.0f;
                    v.color.b = ((out_mesh.base_color >> 16) & 0xFF) / 255.0f;
                    v.color.a = ((out_mesh.base_color >> 24) & 0xFF) / 255.0f;
                }

                if (tangents) {
                    v.tangent.x = tangents[i * 4 + 0];
                    v.tangent.y = tangents[i * 4 + 1];
                    v.tangent.z = tangents[i * 4 + 2];
                    v.tangent.w = tangents[i * 4 + 3];
                } else {
                    // Filled in below after indices are known. Initialise to a
                    // safe value so unindexed/edge-case meshes still render.
                    v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                }

                out_mesh.vertices.push_back(v);
            }

            // Indices
            if (primitive.indices >= 0) {
                const auto& idx_accessor = gltf.accessors[primitive.indices];
                const uint8_t* idx_data = get_buffer_data(primitive.indices);
                int idx_count = static_cast<int>(idx_accessor.count);

                out_mesh.indices.reserve(idx_count);
                for (int i = 0; i < idx_count; i++) {
                    uint32_t index;
                    switch (idx_accessor.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            index = reinterpret_cast<const uint16_t*>(idx_data)[i];
                            break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                            index = reinterpret_cast<const uint32_t*>(idx_data)[i];
                            break;
                        default:
                            index = idx_data[i];
                            break;
                    }
                    out_mesh.indices.push_back(index);
                }
            }

            // Fallback tangent generation when the glTF asset omits TANGENT.
            if (!tangents) {
                compute_tangents_from_uvs(out_mesh.vertices, out_mesh.indices);
            }

            // Store texture data directly in mesh for deferred GPU upload
            if (texture_image_idx >= 0 && texture_image_idx < static_cast<int>(loaded_textures.size())) {
                const auto& [pixels, dims] = loaded_textures[texture_image_idx];
                const auto width = dims.first;
                const auto height = dims.second;
                out_mesh.texture_pixels = pixels;
                out_mesh.texture_width = width;
                out_mesh.texture_height = height;
            }

            // Check for skinning data (JOINTS_0 and WEIGHTS_0)
            auto joints_it = primitive.attributes.find("JOINTS_0");
            auto weights_it = primitive.attributes.find("WEIGHTS_0");

            if (joints_it != primitive.attributes.end() && weights_it != primitive.attributes.end()) {
                out_mesh.is_skinned = true;

                const uint8_t* joints_data = get_buffer_data(joints_it->second);
                const float* weights_data = reinterpret_cast<const float*>(get_buffer_data(weights_it->second));

                // Convert to skinned vertices
                out_mesh.skinned_vertices.reserve(vertex_count);
                for (int i = 0; i < vertex_count; i++) {
                    SkinnedVertex sv;
                    // Copy base vertex data
                    const auto& v = out_mesh.vertices[i];
                    sv.position = v.position;
                    sv.normal = v.normal;
                    sv.texcoord = v.texcoord;
                    sv.color = v.color;
                    sv.tangent = v.tangent;

                    // Copy joint indices and weights
                    for (int j = 0; j < 4; j++) {
                        sv.joints[j] = joints_data[i * 4 + j];
                        sv.weights[j] = weights_data[i * 4 + j];
                    }

                    out_mesh.skinned_vertices.push_back(sv);
                }
            }

            model.meshes.push_back(std::move(out_mesh));
        }
    }

    // Load skin data (skeleton)
    if (!gltf.skins.empty()) {
        const auto& skin = gltf.skins[0];
        model.has_skeleton = true;

        // Load inverse bind matrices
        std::vector<glm::mat4> inverse_bind_matrices;
        if (skin.inverseBindMatrices >= 0) {
            const auto& accessor = gltf.accessors[skin.inverseBindMatrices];
            const auto& bufferView = gltf.bufferViews[accessor.bufferView];
            const auto& buffer = gltf.buffers[bufferView.buffer];
            const float* data = reinterpret_cast<const float*>(
                buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);

            for (size_t i = 0; i < accessor.count; i++) {
                glm::mat4 mat;
                std::memcpy(&mat, data + i * 16, sizeof(glm::mat4));
                inverse_bind_matrices.push_back(mat);
            }
        }

        // Create joints from skin.joints (node indices)
        model.skeleton.joint_node_indices = skin.joints;
        model.skeleton.joints.resize(skin.joints.size());

        // Build a map from node index to joint index
        std::unordered_map<int, int> node_to_joint;
        for (size_t i = 0; i < skin.joints.size(); i++) {
            node_to_joint[skin.joints[i]] = static_cast<int>(i);
        }

        // Load joint data
        for (size_t i = 0; i < skin.joints.size(); i++) {
            int node_idx = skin.joints[i];
            const auto& node = gltf.nodes[node_idx];
            animation::Joint& joint = model.skeleton.joints[i];

            joint.name = node.name;
            joint.parent_index = -1;

            // Find parent joint
            for (size_t j = 0; j < gltf.nodes.size(); j++) {
                const auto& potential_parent = gltf.nodes[j];
                for (int child : potential_parent.children) {
                    if (child == node_idx) {
                        auto parent_it = node_to_joint.find(static_cast<int>(j));
                        if (parent_it != node_to_joint.end()) {
                            joint.parent_index = parent_it->second;
                        }
                        break;
                    }
                }
            }

            // Get local transform
            if (node.translation.size() == 3) {
                joint.local_translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
            } else {
                joint.local_translation = glm::vec3(0.0f);
            }

            if (node.rotation.size() == 4) {
                joint.local_rotation = glm::quat(
                    static_cast<float>(node.rotation[3]),  // w
                    static_cast<float>(node.rotation[0]),  // x
                    static_cast<float>(node.rotation[1]),  // y
                    static_cast<float>(node.rotation[2])   // z
                );
            } else {
                joint.local_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }

            if (node.scale.size() == 3) {
                joint.local_scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
            } else {
                joint.local_scale = glm::vec3(1.0f);
            }

            // Inverse bind matrix
            if (i < inverse_bind_matrices.size()) {
                joint.inverse_bind_matrix = inverse_bind_matrices[i];
            } else {
                joint.inverse_bind_matrix = glm::mat4(1.0f);
            }
        }

        ENGINE_LOG_DEBUG("model", "  Loaded skeleton with {} joints", model.skeleton.joints.size());

        // Cache foot IK bone indices
        model.foot_ik.init(model.skeleton);
        if (model.foot_ik.valid) {
            ENGINE_LOG_DEBUG("model", "  Foot IK bones found");
        }
    }

    // Load animations
    for (const auto& gltf_anim : gltf.animations) {
        animation::AnimationClip clip;
        clip.name = gltf_anim.name;
        clip.duration = 0.0f;

        // Build a map from node index to joint index
        std::unordered_map<int, int> node_to_joint;
        for (size_t i = 0; i < model.skeleton.joint_node_indices.size(); i++) {
            node_to_joint[model.skeleton.joint_node_indices[i]] = static_cast<int>(i);
        }

        // Group samplers by target node
        std::unordered_map<int, animation::AnimationChannel> channels_by_joint;

        for (const auto& channel : gltf_anim.channels) {
            int node_idx = channel.target_node;
            auto joint_it = node_to_joint.find(node_idx);
            if (joint_it == node_to_joint.end()) continue;

            int joint_idx = joint_it->second;
            auto& anim_channel = channels_by_joint[joint_idx];
            anim_channel.bone_index = joint_idx;

            const auto& sampler = gltf_anim.samplers[channel.sampler];

            // Get input (time) data
            const auto& input_accessor = gltf.accessors[sampler.input];
            const auto& input_bufferView = gltf.bufferViews[input_accessor.bufferView];
            const auto& input_buffer = gltf.buffers[input_bufferView.buffer];
            const float* times = reinterpret_cast<const float*>(
                input_buffer.data.data() + input_bufferView.byteOffset + input_accessor.byteOffset);

            // Get output (value) data
            const auto& output_accessor = gltf.accessors[sampler.output];
            const auto& output_bufferView = gltf.bufferViews[output_accessor.bufferView];
            const auto& output_buffer = gltf.buffers[output_bufferView.buffer];
            const float* values = reinterpret_cast<const float*>(
                output_buffer.data.data() + output_bufferView.byteOffset + output_accessor.byteOffset);

            size_t count = input_accessor.count;

            if (channel.target_path == "translation") {
                for (size_t i = 0; i < count; i++) {
                    anim_channel.position_times.push_back(times[i]);
                    anim_channel.positions.push_back(glm::vec3(values[i*3], values[i*3+1], values[i*3+2]));
                    clip.duration = std::max(clip.duration, times[i]);
                }
            } else if (channel.target_path == "rotation") {
                for (size_t i = 0; i < count; i++) {
                    anim_channel.rotation_times.push_back(times[i]);
                    anim_channel.rotations.push_back(glm::quat(values[i*4+3], values[i*4], values[i*4+1], values[i*4+2]));
                    clip.duration = std::max(clip.duration, times[i]);
                }
            } else if (channel.target_path == "scale") {
                for (size_t i = 0; i < count; i++) {
                    anim_channel.scale_times.push_back(times[i]);
                    anim_channel.scales.push_back(glm::vec3(values[i*3], values[i*3+1], values[i*3+2]));
                    clip.duration = std::max(clip.duration, times[i]);
                }
            }
        }

        // Add channels to clip
        for (auto& [joint_idx, channel] : channels_by_joint) {
            clip.channels.push_back(std::move(channel));
        }

        if (!clip.channels.empty()) {
            model.animations.push_back(std::move(clip));
        }
    }

    if (!model.animations.empty()) {
        ENGINE_LOG_DEBUG("model", "  Loaded {} animations:", model.animations.size());
        for (const auto& anim : model.animations) {
            ENGINE_LOG_DEBUG("model", "    - {} ({}s)", anim.name, anim.duration);
        }
    }

    model.loaded = true;
    model.compute_bounding_sphere();
    ENGINE_LOG_INFO("model", "Loaded GLB model: {} ({} meshes) bounds: Y=[{}, {}]",
                    path, model.meshes.size(), model.min_y, model.max_y);
    return true;
}

void ModelLoader::upload_to_gpu(gpu::GPUDevice& device, Model& model) {
    for (auto& mesh : model.meshes) {
        if (mesh.uploaded) continue;
        if (mesh.vertices.empty() && mesh.skinned_vertices.empty()) continue;

        // Create vertex buffer
        if (mesh.is_skinned && !mesh.skinned_vertices.empty()) {
            mesh.vertex_buffer = gpu::GPUBuffer::create_static(
                device, gpu::GPUBuffer::Type::Vertex,
                mesh.skinned_vertices.data(),
                mesh.skinned_vertices.size() * sizeof(SkinnedVertex));
        } else if (!mesh.vertices.empty()) {
            mesh.vertex_buffer = gpu::GPUBuffer::create_static(
                device, gpu::GPUBuffer::Type::Vertex,
                mesh.vertices.data(),
                mesh.vertices.size() * sizeof(Vertex3D));
        }

        if (!mesh.vertex_buffer) {
            ENGINE_LOG_ERROR("model", "Failed to create vertex buffer for mesh");
            continue;
        }

        // Create index buffer
        if (!mesh.indices.empty()) {
            mesh.index_buffer = gpu::GPUBuffer::create_static(
                device, gpu::GPUBuffer::Type::Index,
                mesh.indices.data(),
                mesh.indices.size() * sizeof(uint32_t));

            if (!mesh.index_buffer) {
                ENGINE_LOG_ERROR("model", "Failed to create index buffer for mesh");
            }
        }

        // Create texture if available. Base-color textures are sRGB-encoded per
        // glTF; sampler linearizes on read so lighting math stays in linear.
        if (mesh.has_texture && !mesh.texture_pixels.empty()) {
            mesh.texture = gpu::GPUTexture::create_2d(
                device,
                mesh.texture_width, mesh.texture_height,
                gpu::TextureFormat::RGBA8_SRGB,
                mesh.texture_pixels.data(),
                true);  // Generate mipmaps

            if (!mesh.texture) {
                ENGINE_LOG_ERROR("model", "Failed to create texture for mesh");
                mesh.has_texture = false;
            } else {
                // Clear CPU-side texture data to save memory
                mesh.texture_pixels.clear();
                mesh.texture_pixels.shrink_to_fit();
            }
        }

        // Normal map: stored UNORM (NOT sRGB) per glTF spec. Generate mipmaps so
        // distant fragments don't sample raw aliased high-frequency detail.
        if (!mesh.material.normal_pixels.empty()) {
            mesh.material.normal_texture = gpu::GPUTexture::create_2d(
                device,
                mesh.material.normal_width, mesh.material.normal_height,
                gpu::TextureFormat::RGBA8,
                mesh.material.normal_pixels.data(),
                true);
            if (mesh.material.normal_texture) {
                mesh.material.normal_pixels.clear();
                mesh.material.normal_pixels.shrink_to_fit();
            }
        }

        mesh.uploaded = true;

        // Cache index count before freeing CPU-side geometry data
        mesh.cached_index_count = static_cast<uint32_t>(mesh.indices.size());

        // Free CPU-side vertex/index data - it's on the GPU now
        mesh.vertices.clear();
        mesh.vertices.shrink_to_fit();
        mesh.skinned_vertices.clear();
        mesh.skinned_vertices.shrink_to_fit();
        mesh.indices.clear();
        mesh.indices.shrink_to_fit();
    }
}

void ModelLoader::free_gpu_resources(Model& model) {
    for (auto& mesh : model.meshes) {
        // Free SDL3 GPU resources
        mesh.vertex_buffer.reset();
        mesh.index_buffer.reset();
        mesh.texture.reset();
        mesh.material.normal_texture.reset();
        mesh.material.metallic_roughness_texture.reset();
        mesh.material.occlusion_texture.reset();
        mesh.uploaded = false;
        mesh.has_texture = false;
    }
}

ModelManager::ModelManager() {
    // Reserve slot 0 as INVALID_MODEL_HANDLE
    models_.push_back(nullptr);
    model_paths_.emplace_back();
}

ModelManager::~ModelManager() {
    unload_all();
}

ModelHandle ModelManager::load_model(const std::string& name, const std::string& path) {
    auto model = std::make_unique<Model>();
    if (ModelLoader::load_glb(path, *model)) {
        // Upload to GPU if device is available
        if (device_) {
            ModelLoader::upload_to_gpu(*device_, *model);
        }

        ModelHandle handle = static_cast<ModelHandle>(models_.size());
        models_.push_back(std::move(model));
        model_paths_.push_back(path);
        name_to_handle_[name] = handle;

        if (watcher_) {
            watcher_->watch_file(path,
                [this, handle](const std::filesystem::path&) {
                    reload_model(handle);
                });
        }
        return handle;
    }
    return INVALID_MODEL_HANDLE;
}

Model* ModelManager::get_model(ModelHandle handle) const {
    if (handle == INVALID_MODEL_HANDLE || handle >= models_.size()) return nullptr;
    return models_[handle].get();
}

Model* ModelManager::get_model(const std::string& name) const {
    auto it = name_to_handle_.find(name);
    if (it == name_to_handle_.end()) return nullptr;
    return models_[it->second].get();
}

ModelHandle ModelManager::register_model(const std::string& name, std::unique_ptr<Model> model) {
    if (device_) {
        ModelLoader::upload_to_gpu(*device_, *model);
    }
    ModelHandle handle = static_cast<ModelHandle>(models_.size());
    models_.push_back(std::move(model));
    model_paths_.emplace_back();  // procedural -- no source path
    name_to_handle_[name] = handle;
    return handle;
}

bool ModelManager::reload_model(ModelHandle handle) {
    if (handle == INVALID_MODEL_HANDLE || handle >= models_.size()) return false;
    if (handle >= model_paths_.size()) return false;
    const auto& path = model_paths_[handle];
    if (path.empty()) return false;  // procedural model, nothing to reload from

    auto fresh = std::make_unique<Model>();
    if (!ModelLoader::load_glb(path, *fresh)) {
        ENGINE_LOG_WARN("hot_reload", "ModelManager: reload failed for '{}'", path);
        return false;
    }
    if (device_) {
        ModelLoader::upload_to_gpu(*device_, *fresh);
        if (models_[handle]) {
            ModelLoader::free_gpu_resources(*models_[handle]);
        }
    }
    models_[handle] = std::move(fresh);
    ENGINE_LOG_INFO("hot_reload", "ModelManager: reloaded '{}'", path);
    return true;
}

void ModelManager::enable_hot_reload(core::asset::FileWatcher& watcher) {
    watcher_ = &watcher;
    for (size_t i = 1; i < model_paths_.size(); ++i) {
        if (model_paths_[i].empty()) continue;
        ModelHandle h = static_cast<ModelHandle>(i);
        watcher_->watch_file(model_paths_[i],
            [this, h](const std::filesystem::path&) {
                reload_model(h);
            });
    }
}

ModelHandle ModelManager::get_handle(const std::string& name) const {
    auto it = name_to_handle_.find(name);
    return it != name_to_handle_.end() ? it->second : INVALID_MODEL_HANDLE;
}

void ModelManager::unload_all() {
    for (size_t i = 1; i < models_.size(); ++i) {
        if (models_[i]) {
            ModelLoader::free_gpu_resources(*models_[i]);
        }
    }
    models_.clear();
    models_.push_back(nullptr);  // Re-reserve slot 0
    model_paths_.clear();
    model_paths_.emplace_back();
    name_to_handle_.clear();
}

} // namespace mmo::engine
