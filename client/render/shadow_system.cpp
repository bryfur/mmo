#include "shadow_system.hpp"
#include "bgfx_utils.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace mmo {

// ============================================================================
// ShadowSystem Implementation
// ============================================================================

ShadowSystem::ShadowSystem()
    : enabled_(true)
    , shadow_map_size_(DEFAULT_SHADOW_MAP_SIZE)
    , light_space_matrix_(1.0f)
{
}

ShadowSystem::~ShadowSystem() {
    shutdown();
}

bool ShadowSystem::init(int shadow_map_size) {
    shadow_map_size_ = shadow_map_size;
    
    std::cout << "Initializing shadow mapping (bgfx)..." << std::endl;
    
    // Create shadow depth texture (render target)
    shadow_depth_texture_ = bgfx::createTexture2D(
        uint16_t(shadow_map_size_),
        uint16_t(shadow_map_size_),
        false, 1,
        bgfx::TextureFormat::D32F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL
    );
    
    if (!bgfx::isValid(shadow_depth_texture_)) {
        std::cerr << "Failed to create shadow depth texture" << std::endl;
        return false;
    }
    
    // Create shadow framebuffer with depth only
    bgfx::Attachment attachment;
    attachment.init(shadow_depth_texture_);
    
    shadow_fbo_ = bgfx::createFrameBuffer(1, &attachment, true);
    
    if (!bgfx::isValid(shadow_fbo_)) {
        std::cerr << "Failed to create shadow framebuffer" << std::endl;
        return false;
    }
    
    // Load shadow shaders
    shadow_program_ = bgfx_utils::load_program("shadow_vs", "shadow_fs");
    if (!bgfx::isValid(shadow_program_)) {
        std::cerr << "Failed to load shadow program" << std::endl;
        return false;
    }
    
    skinned_shadow_program_ = bgfx_utils::load_program("skinned_shadow_vs", "shadow_fs");
    if (!bgfx::isValid(skinned_shadow_program_)) {
        std::cerr << "Warning: Failed to load skinned shadow program" << std::endl;
        // Non-fatal - skinned shadows just won't work
    }
    
    // Note: u_model is a bgfx predefined uniform - use setTransform
    // Create only custom uniforms
    u_lightSpaceMatrix_ = bgfx::createUniform("u_lightSpaceMatrix", bgfx::UniformType::Mat4);
    
    std::cout << "Shadow mapping initialized with " << shadow_map_size_ << "x" << shadow_map_size_ << " shadow map" << std::endl;
    return true;
}

void ShadowSystem::shutdown() {
    if (bgfx::isValid(shadow_fbo_)) {
        bgfx::destroy(shadow_fbo_);
        shadow_fbo_ = BGFX_INVALID_HANDLE;
    }
    
    // Note: shadow_depth_texture_ is destroyed with the framebuffer (destroyTexture=true)
    shadow_depth_texture_ = BGFX_INVALID_HANDLE;
    
    if (bgfx::isValid(shadow_program_)) {
        bgfx::destroy(shadow_program_);
        shadow_program_ = BGFX_INVALID_HANDLE;
    }
    
    if (bgfx::isValid(skinned_shadow_program_)) {
        bgfx::destroy(skinned_shadow_program_);
        skinned_shadow_program_ = BGFX_INVALID_HANDLE;
    }
    
    if (bgfx::isValid(u_lightSpaceMatrix_)) {
        bgfx::destroy(u_lightSpaceMatrix_);
        u_lightSpaceMatrix_ = BGFX_INVALID_HANDLE;
    }
    
    // Note: u_model is bgfx predefined, so we don't destroy it
}

void ShadowSystem::update_light_space_matrix(float camera_x, float camera_z, const glm::vec3& light_dir) {
    float shadow_distance = 1500.0f;
    
    glm::vec3 light_pos = glm::vec3(camera_x, 0, camera_z) - light_dir * 12000.0f;
    glm::vec3 light_target = glm::vec3(camera_x, 0, camera_z);
    glm::mat4 light_view = glm::lookAt(light_pos, light_target, glm::vec3(0.0f, 1.0f, 0.0f));
    
    glm::mat4 light_projection = glm::ortho(
        -shadow_distance, shadow_distance,
        -shadow_distance, shadow_distance,
        1.0f, 25000.0f
    );
    
    light_space_matrix_ = light_projection * light_view;
}

void ShadowSystem::begin_shadow_pass() {
    if (!enabled_) return;
    
    // Set up shadow view
    bgfx::setViewFrameBuffer(ViewId::Shadow, shadow_fbo_);
    bgfx::setViewRect(ViewId::Shadow, 0, 0, uint16_t(shadow_map_size_), uint16_t(shadow_map_size_));
    bgfx::setViewClear(ViewId::Shadow, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    
    // Set view transform (identity view, light space as projection)
    float view[16];
    float proj[16];
    
    glm::mat4 identity(1.0f);
    memcpy(view, glm::value_ptr(identity), sizeof(view));
    memcpy(proj, glm::value_ptr(light_space_matrix_), sizeof(proj));
    
    bgfx::setViewTransform(ViewId::Shadow, view, proj);
}

void ShadowSystem::end_shadow_pass() {
    // Nothing special needed for bgfx
    // The framebuffer will be unbound automatically when switching views
}

// ============================================================================
// SSAOSystem Implementation (Disabled for now)
// ============================================================================

SSAOSystem::SSAOSystem()
    : enabled_(false)  // Disabled by default in bgfx migration
    , width_(0)
    , height_(0)
{
}

SSAOSystem::~SSAOSystem() {
    shutdown();
}

bool SSAOSystem::init(int width, int height) {
    width_ = width;
    height_ = height;
    
    std::cout << "SSAO disabled in bgfx migration (can be added later)" << std::endl;
    
    // Create a dummy 1x1 white texture for SSAO
    // This allows the main shader to still sample it without issues
    uint32_t white = 0xFFFFFFFF;
    ssao_blur_texture_ = bgfx::createTexture2D(
        1, 1, false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE,
        bgfx::copy(&white, sizeof(white))
    );
    
    return true;
}

void SSAOSystem::shutdown() {
    if (bgfx::isValid(ssao_blur_texture_)) {
        bgfx::destroy(ssao_blur_texture_);
        ssao_blur_texture_ = BGFX_INVALID_HANDLE;
    }
}

void SSAOSystem::resize(int width, int height) {
    width_ = width;
    height_ = height;
    // No-op for now since SSAO is disabled
}

} // namespace mmo
