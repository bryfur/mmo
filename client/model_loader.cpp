#include "model_loader.hpp"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#include <SDL3_image/SDL_image.h>

#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

namespace mmo {

// Static vertex layouts
bgfx::VertexLayout Vertex3D::layout;
bgfx::VertexLayout SkinnedVertex::layout;

void Vertex3D::init_layout() {
    layout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float, true)
        .end();
}

void SkinnedVertex::init_layout() {
    layout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float, true)
        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8, false, true)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
        .end();
}

void ModelLoader::init_vertex_layouts() {
    Vertex3D::init_layout();
    SkinnedVertex::init_layout();
}

// Helper to interpolate between keyframes
template<typename T>
T interpolate_keyframes(const std::vector<float>& times, const std::vector<T>& values, float t) {
    if (times.empty() || values.empty()) return T();
    if (times.size() == 1) return values[0];
    
    // Clamp time
    if (t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();
    
    // Find keyframes to interpolate between
    for (size_t i = 0; i < times.size() - 1; i++) {
        if (t >= times[i] && t <= times[i + 1]) {
            float factor = (t - times[i]) / (times[i + 1] - times[i]);
            if constexpr (std::is_same_v<T, glm::quat>) {
                return glm::slerp(values[i], values[i + 1], factor);
            } else {
                return glm::mix(values[i], values[i + 1], factor);
            }
        }
    }
    return values.back();
}

bool ModelLoader::load_glb(const std::string& path, Model& model) {
    tinygltf::Model gltf;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    
    // Set custom SDL3_image loader
    loader.SetImageLoader(load_image_data, nullptr);
    loader.SetImageWriter(write_image_data, nullptr);
    
    bool ret = loader.LoadBinaryFromFile(&gltf, &err, &warn, path);
    
    if (!warn.empty()) {
        std::cerr << "Warning loading " << path << ": " << warn << std::endl;
    }
    if (!err.empty()) {
        std::cerr << "Error loading " << path << ": " << err << std::endl;
        return false;
    }
    if (!ret) {
        std::cerr << "Failed to load GLB: " << path << std::endl;
        return false;
    }
    
    model.meshes.clear();
    model.min_x = model.min_y = model.min_z = 1e10f;
    model.max_x = model.max_y = model.max_z = -1e10f;
    
    // Load textures from images (store in memory, upload later)
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
                }
                
                // Base color texture
                int tex_idx = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (tex_idx >= 0 && tex_idx < static_cast<int>(gltf.textures.size())) {
                    texture_image_idx = gltf.textures[tex_idx].source;
                    out_mesh.has_texture = true;
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
                v.x = positions[i * 3 + 0];
                v.y = positions[i * 3 + 1];
                v.z = positions[i * 3 + 2];
                
                model.min_x = std::min(model.min_x, v.x);
                model.min_y = std::min(model.min_y, v.y);
                model.min_z = std::min(model.min_z, v.z);
                model.max_x = std::max(model.max_x, v.x);
                model.max_y = std::max(model.max_y, v.y);
                model.max_z = std::max(model.max_z, v.z);
                
                if (normals) {
                    v.nx = normals[i * 3 + 0];
                    v.ny = normals[i * 3 + 1];
                    v.nz = normals[i * 3 + 2];
                } else {
                    v.nx = 0; v.ny = 1; v.nz = 0;
                }
                
                if (uvs) {
                    v.u = uvs[i * 2 + 0];
                    v.v = uvs[i * 2 + 1];
                } else {
                    v.u = 0; v.v = 0;
                }
                
                if (colors_f) {
                    v.r = colors_f[i * 4 + 0];
                    v.g = colors_f[i * 4 + 1];
                    v.b = colors_f[i * 4 + 2];
                    v.a = colors_f[i * 4 + 3];
                } else if (colors_u8) {
                    v.r = colors_u8[i * 4 + 0] / 255.0f;
                    v.g = colors_u8[i * 4 + 1] / 255.0f;
                    v.b = colors_u8[i * 4 + 2] / 255.0f;
                    v.a = colors_u8[i * 4 + 3] / 255.0f;
                } else {
                    v.r = (out_mesh.base_color & 0xFF) / 255.0f;
                    v.g = ((out_mesh.base_color >> 8) & 0xFF) / 255.0f;
                    v.b = ((out_mesh.base_color >> 16) & 0xFF) / 255.0f;
                    v.a = ((out_mesh.base_color >> 24) & 0xFF) / 255.0f;
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
                    sv.x = v.x; sv.y = v.y; sv.z = v.z;
                    sv.nx = v.nx; sv.ny = v.ny; sv.nz = v.nz;
                    sv.u = v.u; sv.v = v.v;
                    sv.r = v.r; sv.g = v.g; sv.b = v.b; sv.a = v.a;
                    
                    // Copy joint indices and weights
                    for (int j = 0; j < 4; j++) {
                        sv.joints[j] = joints_data[i * 4 + j];
                        sv.weights[j] = weights_data[i * 4 + j];
                    }
                    
                    out_mesh.skinned_vertices.push_back(sv);
                }
            }
            
            // Store texture index temporarily (for loading texture later)
            // We use a temporary marker since bgfx handle isn't valid yet
            if (texture_image_idx >= 0) {
                // Store index + 1 so 0 means no texture
                out_mesh.texture.idx = static_cast<uint16_t>(texture_image_idx + 1);
            }
            
            model.meshes.push_back(std::move(out_mesh));
        }
    }
    
    // Load skin data (skeleton) - same as before
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
            Joint& joint = model.skeleton.joints[i];
            
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
        
        std::cout << "  Loaded skeleton with " << model.skeleton.joints.size() << " joints" << std::endl;
    }
    
    // Load animations - same as before
    for (const auto& gltf_anim : gltf.animations) {
        AnimationClip clip;
        clip.name = gltf_anim.name;
        clip.duration = 0.0f;
        
        // Build a map from node index to joint index
        std::unordered_map<int, int> node_to_joint;
        for (size_t i = 0; i < model.skeleton.joint_node_indices.size(); i++) {
            node_to_joint[model.skeleton.joint_node_indices[i]] = static_cast<int>(i);
        }
        
        // Group samplers by target node
        std::unordered_map<int, AnimationChannel> channels_by_joint;
        
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
        std::cout << "  Loaded " << model.animations.size() << " animations:" << std::endl;
        for (const auto& anim : model.animations) {
            std::cout << "    - " << anim.name << " (" << anim.duration << "s)" << std::endl;
        }
    }
    
    // Create bgfx textures
    std::vector<bgfx::TextureHandle> texture_handles(loaded_textures.size());
    for (size_t i = 0; i < loaded_textures.size(); i++) {
        auto& [pixels, dims] = loaded_textures[i];
        auto [width, height] = dims;
        
        // Ensure we have RGBA data (4 bytes per pixel)
        size_t expected_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        if (pixels.size() != expected_size) {
            std::cerr << "  Warning: Texture " << i << " size mismatch: expected " 
                      << expected_size << " bytes, got " << pixels.size() << " bytes" << std::endl;
            // Skip this texture if size doesn't match
            texture_handles[i] = BGFX_INVALID_HANDLE;
            continue;
        }
        
        const bgfx::Memory* mem = bgfx::copy(pixels.data(), static_cast<uint32_t>(pixels.size()));
        texture_handles[i] = bgfx::createTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            false, 1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            mem
        );
        
        std::cout << "  Loaded texture " << i << ": " << width << "x" << height << std::endl;
    }
    
    // Update mesh texture handles
    for (auto& mesh : model.meshes) {
        if (mesh.has_texture && mesh.texture.idx > 0) {
            int tex_idx = mesh.texture.idx - 1;  // Undo the +1 offset
            if (tex_idx < static_cast<int>(texture_handles.size())) {
                mesh.texture = texture_handles[tex_idx];
            } else {
                mesh.texture = BGFX_INVALID_HANDLE;
                mesh.has_texture = false;
            }
        } else {
            mesh.texture = BGFX_INVALID_HANDLE;
        }
    }
    
    model.loaded = true;
    std::cout << "Loaded GLB model: " << path << " (" << model.meshes.size() << " meshes)"
              << " bounds: Y=[" << model.min_y << ", " << model.max_y << "]" << std::endl;
    return true;
}

void ModelLoader::upload_to_gpu(Model& model) {
    for (auto& mesh : model.meshes) {
        if (mesh.uploaded) continue;
        if (mesh.vertices.empty() && mesh.skinned_vertices.empty()) continue;
        
        if (mesh.is_skinned && !mesh.skinned_vertices.empty()) {
            // Upload skinned vertices
            const bgfx::Memory* vb_mem = bgfx::copy(
                mesh.skinned_vertices.data(),
                static_cast<uint32_t>(mesh.skinned_vertices.size() * sizeof(SkinnedVertex))
            );
            mesh.vbh = bgfx::createVertexBuffer(vb_mem, SkinnedVertex::layout);
        } else {
            // Upload regular vertices
            const bgfx::Memory* vb_mem = bgfx::copy(
                mesh.vertices.data(),
                static_cast<uint32_t>(mesh.vertices.size() * sizeof(Vertex3D))
            );
            mesh.vbh = bgfx::createVertexBuffer(vb_mem, Vertex3D::layout);
        }
        
        if (!mesh.indices.empty()) {
            const bgfx::Memory* ib_mem = bgfx::copy(
                mesh.indices.data(),
                static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t))
            );
            mesh.ibh = bgfx::createIndexBuffer(ib_mem, BGFX_BUFFER_INDEX32);
        }
        
        mesh.uploaded = true;
    }
}

void ModelLoader::free_gpu_resources(Model& model) {
    for (auto& mesh : model.meshes) {
        if (bgfx::isValid(mesh.vbh)) {
            bgfx::destroy(mesh.vbh);
            mesh.vbh = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(mesh.ibh)) {
            bgfx::destroy(mesh.ibh);
            mesh.ibh = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(mesh.texture)) {
            bgfx::destroy(mesh.texture);
            mesh.texture = BGFX_INVALID_HANDLE;
        }
        mesh.uploaded = false;
        mesh.has_texture = false;
    }
}

void ModelLoader::update_animation(Model& model, AnimationState& state, float dt) {
    if (!model.has_skeleton || model.animations.empty() || !state.playing) return;
    
    // Clamp clip index
    if (state.current_clip < 0 || state.current_clip >= static_cast<int>(model.animations.size())) {
        state.current_clip = 0;
    }
    
    const auto& clip = model.animations[state.current_clip];
    
    // Update time
    state.time += dt * state.speed;
    
    // Handle looping
    if (state.time > clip.duration) {
        if (state.loop) {
            state.time = std::fmod(state.time, clip.duration);
        } else {
            state.time = clip.duration;
            state.playing = false;
        }
    }
    
    // Compute bone matrices
    compute_bone_matrices(model, state);
}

void ModelLoader::compute_bone_matrices(Model& model, AnimationState& state) {
    if (!model.has_skeleton) return;
    
    const auto& skeleton = model.skeleton;
    size_t num_joints = skeleton.joints.size();
    
    if (num_joints == 0) return;
    
    // Get animation clip
    const AnimationClip* clip = nullptr;
    if (state.current_clip >= 0 && state.current_clip < static_cast<int>(model.animations.size())) {
        clip = &model.animations[state.current_clip];
    }
    
    // Build a map from joint index to animation channel
    std::unordered_map<int, const AnimationChannel*> joint_channels;
    if (clip) {
        for (const auto& channel : clip->channels) {
            joint_channels[channel.bone_index] = &channel;
        }
    }
    
    // Compute local transforms for each joint
    std::vector<glm::mat4> local_transforms(num_joints);
    for (size_t i = 0; i < num_joints; i++) {
        const auto& joint = skeleton.joints[i];
        
        // Start with bind pose
        glm::vec3 translation = joint.local_translation;
        glm::quat rotation = joint.local_rotation;
        glm::vec3 scale = joint.local_scale;
        
        // Override with animation if available
        auto channel_it = joint_channels.find(static_cast<int>(i));
        if (channel_it != joint_channels.end()) {
            const auto& channel = *channel_it->second;
            
            if (!channel.position_times.empty()) {
                translation = interpolate_keyframes(channel.position_times, channel.positions, state.time);
            }
            if (!channel.rotation_times.empty()) {
                rotation = interpolate_keyframes(channel.rotation_times, channel.rotations, state.time);
            }
            if (!channel.scale_times.empty()) {
                scale = interpolate_keyframes(channel.scale_times, channel.scales, state.time);
            }
        }
        
        // Build local transform matrix: T * R * S
        glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        local_transforms[i] = T * R * S;
    }
    
    // Compute world transforms by walking hierarchy
    std::vector<glm::mat4> world_transforms(num_joints);
    for (size_t i = 0; i < num_joints; i++) {
        const auto& joint = skeleton.joints[i];
        if (joint.parent_index >= 0 && joint.parent_index < static_cast<int>(num_joints)) {
            world_transforms[i] = world_transforms[joint.parent_index] * local_transforms[i];
        } else {
            world_transforms[i] = local_transforms[i];
        }
    }
    
    // Compute final bone matrices: world_transform * inverse_bind_matrix
    for (size_t i = 0; i < num_joints && i < MAX_BONES; i++) {
        state.bone_matrices[i] = world_transforms[i] * skeleton.joints[i].inverse_bind_matrix;
    }
    
    // Fill remaining slots with identity
    for (size_t i = num_joints; i < MAX_BONES; i++) {
        state.bone_matrices[i] = glm::mat4(1.0f);
    }
}

ModelManager::~ModelManager() {
    unload_all();
}

bool ModelManager::load_model(const std::string& name, const std::string& path) {
    Model model;
    if (ModelLoader::load_glb(path, model)) {
        ModelLoader::upload_to_gpu(model);
        
        // Create animation state if model has animations
        if (model.has_skeleton && !model.animations.empty()) {
            AnimationState state;
            state.reset();
            // Initialize with identity matrices
            for (auto& m : state.bone_matrices) m = glm::mat4(1.0f);
            animation_states_[name] = state;
        }
        
        models_[name] = std::move(model);
        return true;
    }
    return false;
}

Model* ModelManager::get_model(const std::string& name) {
    auto it = models_.find(name);
    return it != models_.end() ? &it->second : nullptr;
}

AnimationState* ModelManager::get_animation_state(const std::string& name) {
    auto it = animation_states_.find(name);
    return it != animation_states_.end() ? &it->second : nullptr;
}

void ModelManager::unload_all() {
    for (auto& [name, model] : models_) {
        ModelLoader::free_gpu_resources(model);
    }
    models_.clear();
    animation_states_.clear();
}

} // namespace mmo
