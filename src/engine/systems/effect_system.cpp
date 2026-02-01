#include "effect_system.hpp"
#include <glm/gtc/constants.hpp>
#include <random>
#include <algorithm>

namespace mmo::engine::systems {

// Import types from engine namespace
using ::engine::SpawnMode;
using ::engine::VelocityType;

namespace {

// Random number generator for particle spread
std::random_device rd;
std::mt19937 gen(rd());

float random_float(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}

} // anonymous namespace

int EffectSystem::spawn_effect(
    const EffectDefinition* definition,
    const glm::vec3& position,
    const glm::vec3& direction,
    float range
) {
    if (!definition) {
        return -1;
    }

    EffectInstance effect;
    effect.definition = definition;
    effect.age = 0.0f;

    float actual_range = range < 0 ? definition->default_range : range;

    // Create emitter instances
    for (const auto& emitter_def : definition->emitters) {
        EmitterInstance emitter;
        emitter.definition = &emitter_def;
        emitter.origin = position;
        emitter.direction = direction;
        emitter.range = actual_range;
        emitter.age = 0.0f;
        emitter.next_spawn_time = emitter_def.delay;
        emitter.has_spawned_burst = false;

        effect.emitters.push_back(std::move(emitter));
    }

    effects_.push_back(std::move(effect));
    return static_cast<int>(effects_.size() - 1);
}

void EffectSystem::update(float dt, std::function<float(float, float)> get_terrain_height) {
    // Update all effects
    for (auto& effect : effects_) {
        effect.age += dt;

        // Update each emitter in the effect
        for (auto& emitter : effect.emitters) {
            update_emitter(emitter, dt, get_terrain_height);
        }
    }

    // Remove completed effects
    effects_.erase(
        std::remove_if(effects_.begin(), effects_.end(),
            [](const EffectInstance& e) { return e.is_complete(); }),
        effects_.end()
    );
}

void EffectSystem::update_emitter(
    EmitterInstance& emitter,
    float dt,
    std::function<float(float, float)> get_terrain_height
) {
    if (!emitter.definition) return;

    const auto& def = *emitter.definition;
    emitter.age += dt;

    // Check if emitter should spawn particles
    if (emitter.is_active()) {
        // Handle burst spawning
        if (def.spawn_mode == SpawnMode::BURST && !emitter.has_spawned_burst) {
            if (emitter.age >= def.delay) {
                spawn_particles(emitter, def.spawn_count);
                emitter.has_spawned_burst = true;
            }
        }
        // Handle continuous spawning
        else if (def.spawn_mode == SpawnMode::CONTINUOUS) {
            while (emitter.age >= emitter.next_spawn_time) {
                float spawn_interval = 1.0f / def.spawn_rate;
                spawn_particles(emitter, 1);
                emitter.next_spawn_time += spawn_interval;
            }
        }
    }

    // Update all particles
    for (auto& particle : emitter.particles) {
        update_particle(particle, def, dt);

        // Apply terrain height if callback provided
        if (get_terrain_height) {
            float terrain_h = get_terrain_height(particle.position.x, particle.position.z);
            if (particle.position.y < terrain_h) {
                particle.position.y = terrain_h;
            }
        }
    }

    // Remove dead particles
    emitter.particles.erase(
        std::remove_if(emitter.particles.begin(), emitter.particles.end(),
            [](const Particle& p) { return p.age >= p.lifetime; }),
        emitter.particles.end()
    );
}

void EffectSystem::spawn_particles(EmitterInstance& emitter, int count) {
    if (!emitter.definition) return;

    const auto& def = *emitter.definition;

    for (int i = 0; i < count; ++i) {
        Particle particle;

        // Position
        particle.position = emitter.origin;

        // Model
        particle.model = def.model;

        // Lifetime
        particle.lifetime = def.particle_lifetime;
        particle.age = 0.0f;

        // Initial velocity
        particle.velocity = calculate_initial_velocity(def.velocity, emitter.direction, i);

        // Initial rotation
        particle.rotation = glm::radians(def.rotation.initial_rotation);
        particle.rotation_rate = glm::radians(def.rotation.rotation_rate);

        // Initial appearance
        particle.scale = def.appearance.scale_over_lifetime.evaluate(0.0f);
        particle.opacity = def.appearance.opacity_over_lifetime.evaluate(0.0f);
        particle.color = def.appearance.color_tint;

        // For orbital particles, store index and initial angle
        if (def.velocity.type == VelocityType::ORBITAL) {
            particle.orbit_index = static_cast<int>(emitter.particles.size());
            particle.orbit_angle = (glm::two_pi<float>() / def.spawn_count) * particle.orbit_index;
            particle.orbit_origin = emitter.origin;
        }

        // For arc particles, store origin and direction
        if (def.velocity.type == VelocityType::ARC) {
            particle.arc_origin = emitter.origin;
            particle.arc_direction = emitter.direction;
        }

        emitter.particles.push_back(std::move(particle));
    }
}

glm::vec3 EffectSystem::calculate_initial_velocity(
    const VelocityDefinition& vel_def,
    const glm::vec3& direction,
    int particle_index
) {
    glm::vec3 velocity = {0, 0, 0};

    switch (vel_def.type) {
        case VelocityType::DIRECTIONAL: {
            velocity = glm::normalize(direction) * vel_def.speed;

            // Apply spread angle
            if (vel_def.spread_angle > 0) {
                float spread_rad = glm::radians(vel_def.spread_angle);
                float angle_offset = random_float(-spread_rad, spread_rad);

                // Rotate velocity by spread angle (simplified 2D rotation in XZ plane)
                float cos_a = std::cos(angle_offset);
                float sin_a = std::sin(angle_offset);
                velocity = glm::vec3(
                    velocity.x * cos_a - velocity.z * sin_a,
                    velocity.y,
                    velocity.x * sin_a + velocity.z * cos_a
                );
            }
            break;
        }

        case VelocityType::RADIAL: {
            // Random direction outward
            float angle = random_float(0, glm::two_pi<float>());
            velocity = glm::vec3(
                std::cos(angle) * vel_def.speed,
                0,
                std::sin(angle) * vel_def.speed
            );
            break;
        }

        case VelocityType::ORBITAL: {
            // Orbital particles don't use initial velocity, they calculate position each frame
            velocity = {0, 0, 0};
            break;
        }

        case VelocityType::ARC: {
            // Arc particles don't use velocity, they calculate position based on progress
            velocity = {0, 0, 0};
            break;
        }

        case VelocityType::CUSTOM: {
            velocity = vel_def.direction * vel_def.speed;
            break;
        }
    }

    return velocity;
}

void EffectSystem::update_particle(Particle& particle, const EmitterDefinition& emitter_def, float dt) {
    particle.age += dt;
    float t = particle.age / particle.lifetime; // Normalized lifetime (0 to 1)

    const auto& vel_def = emitter_def.velocity;

    // Update position based on velocity type
    if (vel_def.type == VelocityType::ARC) {
        // Arc motion - replicate old draw_melee_slash behavior
        // Calculate base angle from direction
        float base_angle = std::atan2(particle.arc_direction.x, particle.arc_direction.z);

        // Swing angle goes from -1 to +1 radians over the lifetime
        float swing_angle = -1.0f + t * 2.0f;
        float rotation = base_angle + swing_angle;

        // Calculate position along the arc
        float arc_radius = vel_def.arc_radius;
        particle.position.x = particle.arc_origin.x + std::sin(rotation) * arc_radius;
        particle.position.z = particle.arc_origin.z + std::cos(rotation) * arc_radius;

        // Height oscillates with a sine wave
        particle.position.y = particle.arc_origin.y + vel_def.arc_height_base
                            + std::sin(t * glm::pi<float>()) * vel_def.arc_height_amplitude;

        // Tilt rotates with progress
        float tilt = std::sin(t * glm::pi<float>()) * vel_def.arc_tilt_amplitude;
        particle.rotation.x = tilt;

        // Face the direction of the arc
        particle.rotation.y = rotation + glm::half_pi<float>(); // +90 degrees
        particle.rotation.z = -0.5f; // Fixed roll

    } else if (vel_def.type == VelocityType::ORBITAL) {
        // Orbital motion - calculate position from orbit parameters
        particle.orbit_angle += vel_def.orbit_speed * glm::two_pi<float>() * dt;

        // Calculate position relative to orbit origin
        float radius = vel_def.orbit_radius;
        particle.position.x = particle.orbit_origin.x + std::cos(particle.orbit_angle) * radius;
        particle.position.z = particle.orbit_origin.z + std::sin(particle.orbit_angle) * radius;

        // Height with base offset and variation
        particle.position.y = particle.orbit_origin.y + vel_def.orbit_height_base;
        if (vel_def.height_variation > 0) {
            particle.position.y += std::sin(particle.orbit_angle * 3.0f) * vel_def.height_variation;
        }
    } else {
        // Standard velocity-based motion
        particle.position += particle.velocity * dt;

        // Apply gravity
        if (glm::length(vel_def.gravity) > 0) {
            particle.velocity += vel_def.gravity * dt;
        }

        // Apply drag
        if (vel_def.drag > 0) {
            particle.velocity *= (1.0f - vel_def.drag * dt);
        }
    }

    // Update rotation
    if (emitter_def.rotation.face_velocity && glm::length(particle.velocity) > 0.01f) {
        // Face the direction of movement
        glm::vec3 vel_norm = glm::normalize(particle.velocity);
        particle.rotation.y = std::atan2(vel_norm.x, vel_norm.z);
    } else {
        // Apply rotation rate
        particle.rotation += particle.rotation_rate * dt;
    }

    // Update appearance based on lifetime curves
    particle.scale = emitter_def.appearance.scale_over_lifetime.evaluate(t);
    particle.opacity = emitter_def.appearance.opacity_over_lifetime.evaluate(t);

    // Color gradient
    if (emitter_def.appearance.use_color_gradient) {
        particle.color = glm::mix(
            emitter_def.appearance.color_tint,
            emitter_def.appearance.color_end,
            t
        );
    }
}

size_t EffectSystem::particle_count() const {
    size_t count = 0;
    for (const auto& effect : effects_) {
        for (const auto& emitter : effect.emitters) {
            count += emitter.particles.size();
        }
    }
    return count;
}

} // namespace mmo::engine::systems
