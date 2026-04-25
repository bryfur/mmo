#pragma once

#include "engine/effect_definition.hpp"
#include <algorithm>
#include <glm/glm.hpp>
#include <string>
#include <type_traits>
#include <vector>

namespace mmo::engine::systems {

// Types from mmo::engine (parent namespace) are directly accessible
using mmo::engine::AppearanceDefinition;
using mmo::engine::EffectDefinition;
using mmo::engine::EmitterDefinition;
using mmo::engine::RotationDefinition;
using mmo::engine::VelocityDefinition;

// Runtime particle instance
struct Particle {
    // Transform
    glm::vec3 position = {0, 0, 0};
    glm::vec3 velocity = {0, 0, 0};
    glm::vec3 rotation = {0, 0, 0};      // Current euler angles (radians)
    glm::vec3 rotation_rate = {0, 0, 0}; // Angular velocity (radians/sec)

    // Appearance
    float scale = 1.0f;
    float opacity = 1.0f;
    glm::vec4 color = {1, 1, 1, 1};

    // Timing
    float age = 0.0f;      // How long this particle has been alive
    float lifetime = 1.0f; // Total lifetime before death

    // Model reference (points to the stable string in EmitterDefinition)
    const std::string* model = nullptr;

    // For orbital particles
    float orbit_angle = 0.0f;
    int orbit_index = 0;                // Which object in the orbit (0, 1, 2, etc.)
    glm::vec3 orbit_origin = {0, 0, 0}; // Center point of orbit

    // For arc particles (melee slash)
    glm::vec3 arc_origin = {0, 0, 0};    // Center point of the arc
    glm::vec3 arc_direction = {1, 0, 0}; // Base direction for the arc
};

// Runtime emitter instance (spawns and manages particles)
struct EmitterInstance {
    const EmitterDefinition* definition = nullptr;

    // Particles spawned by this emitter
    std::vector<Particle> particles;

    // Emitter state
    float age = 0.0f;
    float next_spawn_time = 0.0f;   // For continuous spawning
    bool has_spawned_burst = false; // For burst mode

    // Spawn location and direction
    glm::vec3 origin = {0, 0, 0};
    glm::vec3 direction = {1, 0, 0};
    float range = 100.0f; // Effect range/scale

    // Check if emitter is still active
    bool is_active() const {
        if (!definition) {
            return false;
        }
        float duration = definition->duration < 0 ? definition->particle_lifetime : definition->duration;
        return age < duration;
    }

    // Check if emitter has finished and all particles are dead
    bool is_complete() const { return !is_active() && particles.empty(); }
};

// Runtime effect instance (collection of emitters)
struct EffectInstance {
    const EffectDefinition* definition = nullptr;

    // Emitters in this effect
    std::vector<EmitterInstance> emitters;

    // Effect state
    float age = 0.0f;

    // Check if effect is complete
    bool is_complete() const {
        for (const auto& emitter : emitters) {
            if (!emitter.is_complete()) {
                return false;
            }
        }
        return true;
    }
};

// Effect system - manages active effects and updates particles
class EffectSystem {
public:
    // Spawn a new effect at a location
    // Returns index of the spawned effect, or -1 if failed
    int spawn_effect(const EffectDefinition* definition, const glm::vec3& position,
                     const glm::vec3& direction = {1, 0, 0},
                     float range = -1.0f // -1 = use definition's default_range
    );

    // Update all active effects and particles.
    // TerrainHeightFn is invoked per particle; templating avoids std::function
    // indirection in the inner particle loop.
    template<typename TerrainHeightFn> void update(float dt, TerrainHeightFn&& get_terrain_height);

    // Update with no terrain sampling (skips the per-particle terrain clamp).
    void update(float dt);
    void update(float dt, std::nullptr_t) { update(dt); }

    // Get all active effects (for rendering)
    const std::vector<EffectInstance>& get_effects() const { return effects_; }

    // Clear all active effects
    void clear() { effects_.clear(); }

    // Get number of active effects
    size_t effect_count() const { return effects_.size(); }

    // Get total number of active particles across all effects
    size_t particle_count() const;

private:
    std::vector<EffectInstance> effects_;

    // Stateless sentinel used by the no-terrain update overload.
    struct NoTerrainFn {
        float operator()(float, float) const { return 0.0f; }
    };

    // Emitter update is templated on terrain sampler so the particle-level
    // check folds away when sampling is disabled. HasTerrain=false path
    // compiles out the per-particle callable invocation entirely.
    template<typename TerrainHeightFn, bool HasTerrain>
    void update_emitter_t(EmitterInstance& emitter, float dt, TerrainHeightFn& get_terrain_height);

    void spawn_particles(EmitterInstance& emitter, int count);
    void update_particle(Particle& particle, const EmitterDefinition& emitter_def, float dt);
    glm::vec3 calculate_initial_velocity(const VelocityDefinition& vel_def, const glm::vec3& direction,
                                         int particle_index);

    // Compacts dead effects after updating - shared between overloads.
    void compact_effects();
};

// ============================================================================
// Template definitions (header-only so any caller can instantiate them)
// ============================================================================

template<typename TerrainHeightFn, bool HasTerrain>
void EffectSystem::update_emitter_t(EmitterInstance& emitter, float dt, TerrainHeightFn& get_terrain_height) {
    if (!emitter.definition) {
        return;
    }

    const auto& def = *emitter.definition;
    emitter.age += dt;

    if (emitter.is_active()) {
        if (def.spawn_mode == mmo::engine::SpawnMode::BURST && !emitter.has_spawned_burst) {
            if (emitter.age >= def.delay) {
                spawn_particles(emitter, def.spawn_count);
                emitter.has_spawned_burst = true;
            }
        } else if (def.spawn_mode == mmo::engine::SpawnMode::CONTINUOUS) {
            while (emitter.age >= emitter.next_spawn_time) {
                float spawn_interval = 1.0f / def.spawn_rate;
                spawn_particles(emitter, 1);
                emitter.next_spawn_time += spawn_interval;
            }
        }
    }

    for (auto& particle : emitter.particles) {
        update_particle(particle, def, dt);
        if constexpr (HasTerrain) {
            float terrain_h = get_terrain_height(particle.position.x, particle.position.z);
            if (particle.position.y < terrain_h) {
                particle.position.y = terrain_h;
            }
        }
    }

    emitter.particles.erase(std::remove_if(emitter.particles.begin(), emitter.particles.end(),
                                           [](const Particle& p) { return p.age >= p.lifetime; }),
                            emitter.particles.end());
}

template<typename TerrainHeightFn> void EffectSystem::update(float dt, TerrainHeightFn&& get_terrain_height) {
    auto sampler = std::forward<TerrainHeightFn>(get_terrain_height);
    for (auto& effect : effects_) {
        effect.age += dt;
        for (auto& emitter : effect.emitters) {
            update_emitter_t<decltype(sampler), true>(emitter, dt, sampler);
        }
    }
    compact_effects();
}

inline void EffectSystem::update(float dt) {
    NoTerrainFn sampler;
    for (auto& effect : effects_) {
        effect.age += dt;
        for (auto& emitter : effect.emitters) {
            update_emitter_t<NoTerrainFn, false>(emitter, dt, sampler);
        }
    }
    compact_effects();
}

} // namespace mmo::engine::systems
