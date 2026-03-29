#include <gtest/gtest.h>
#include "engine/systems/effect_system.hpp"
#include <cmath>

using namespace mmo::engine::systems;
using namespace mmo::engine;

// ===== Particle default value tests =====

TEST(Particle, DefaultPosition) {
    Particle p;
    EXPECT_EQ(p.position, glm::vec3(0, 0, 0));
}

TEST(Particle, DefaultVelocity) {
    Particle p;
    EXPECT_EQ(p.velocity, glm::vec3(0, 0, 0));
}

TEST(Particle, DefaultRotation) {
    Particle p;
    EXPECT_EQ(p.rotation, glm::vec3(0, 0, 0));
    EXPECT_EQ(p.rotation_rate, glm::vec3(0, 0, 0));
}

TEST(Particle, DefaultAppearance) {
    Particle p;
    EXPECT_FLOAT_EQ(p.scale, 1.0f);
    EXPECT_FLOAT_EQ(p.opacity, 1.0f);
    EXPECT_EQ(p.color, glm::vec4(1, 1, 1, 1));
}

TEST(Particle, DefaultTiming) {
    Particle p;
    EXPECT_FLOAT_EQ(p.age, 0.0f);
    EXPECT_FLOAT_EQ(p.lifetime, 1.0f);
}

TEST(Particle, DefaultModelIsNull) {
    Particle p;
    EXPECT_EQ(p.model, nullptr);
}

// ===== EmitterInstance tests =====

class EmitterInstanceTest : public ::testing::Test {
protected:
    EmitterDefinition def;
    EmitterInstance emitter;

    void SetUp() override {
        def.particle_lifetime = 2.0f;
        def.duration = 3.0f;
        emitter.definition = &def;
    }
};

TEST_F(EmitterInstanceTest, IsActiveWhenAgeLessThanDuration) {
    emitter.age = 1.0f;
    EXPECT_TRUE(emitter.is_active());
}

TEST_F(EmitterInstanceTest, IsNotActiveWhenAgeEqualsDuration) {
    emitter.age = 3.0f;
    EXPECT_FALSE(emitter.is_active());
}

TEST_F(EmitterInstanceTest, IsNotActiveWhenAgeExceedsDuration) {
    emitter.age = 5.0f;
    EXPECT_FALSE(emitter.is_active());
}

TEST_F(EmitterInstanceTest, IsNotActiveWithNullDefinition) {
    EmitterInstance null_emitter;
    null_emitter.definition = nullptr;
    EXPECT_FALSE(null_emitter.is_active());
}

TEST_F(EmitterInstanceTest, DurationFallsBackToParticleLifetimeWhenNegative) {
    def.duration = -1.0f;
    def.particle_lifetime = 2.0f;

    emitter.age = 1.5f;
    EXPECT_TRUE(emitter.is_active());

    emitter.age = 2.5f;
    EXPECT_FALSE(emitter.is_active());
}

TEST_F(EmitterInstanceTest, IsCompleteWhenNotActiveAndNoParticles) {
    emitter.age = 10.0f;
    emitter.particles.clear();
    EXPECT_TRUE(emitter.is_complete());
}

TEST_F(EmitterInstanceTest, IsNotCompleteWhenNotActiveButParticlesRemain) {
    emitter.age = 10.0f;
    emitter.particles.push_back(Particle{});
    EXPECT_FALSE(emitter.is_complete());
}

TEST_F(EmitterInstanceTest, IsNotCompleteWhenStillActive) {
    emitter.age = 0.5f;
    emitter.particles.clear();
    EXPECT_FALSE(emitter.is_complete());
}

// ===== EffectInstance tests =====

TEST(EffectInstance, IsCompleteWithNoEmitters) {
    EffectInstance effect;
    EXPECT_TRUE(effect.is_complete());
}

TEST(EffectInstance, IsCompleteWhenAllEmittersComplete) {
    EmitterDefinition def;
    def.duration = 1.0f;

    EffectInstance effect;
    EmitterInstance e1;
    e1.definition = &def;
    e1.age = 5.0f; // past duration
    // no particles

    EmitterInstance e2;
    e2.definition = &def;
    e2.age = 5.0f;

    effect.emitters.push_back(e1);
    effect.emitters.push_back(e2);
    EXPECT_TRUE(effect.is_complete());
}

TEST(EffectInstance, IsNotCompleteWhenAnyEmitterStillActive) {
    EmitterDefinition def;
    def.duration = 10.0f;

    EffectInstance effect;
    EmitterInstance e1;
    e1.definition = &def;
    e1.age = 5.0f; // still active (5 < 10)

    effect.emitters.push_back(e1);
    EXPECT_FALSE(effect.is_complete());
}

TEST(EffectInstance, IsNotCompleteWhenEmitterHasLiveParticles) {
    EmitterDefinition def;
    def.duration = 1.0f;

    EffectInstance effect;
    EmitterInstance e1;
    e1.definition = &def;
    e1.age = 5.0f; // past duration
    e1.particles.push_back(Particle{}); // but still has particles

    effect.emitters.push_back(e1);
    EXPECT_FALSE(effect.is_complete());
}

// ===== EffectSystem tests =====

class EffectSystemTest : public ::testing::Test {
protected:
    EffectSystem system;

    // Terrain height callback that returns flat ground at y=0
    std::function<float(float, float)> flat_terrain = [](float, float) { return 0.0f; };

    // A basic burst emitter definition
    EmitterDefinition make_burst_emitter(int count = 3, float lifetime = 1.0f) {
        EmitterDefinition def;
        def.spawn_mode = SpawnMode::BURST;
        def.spawn_count = count;
        def.particle_lifetime = lifetime;
        def.duration = -1.0f; // use particle_lifetime
        def.model = "test_model";
        def.velocity.type = VelocityType::CUSTOM;
        def.velocity.speed = 0.0f;
        def.velocity.direction = {0, 0, 0};
        return def;
    }

    // A basic effect definition with one burst emitter
    EffectDefinition make_effect(int particle_count = 3, float lifetime = 1.0f) {
        EffectDefinition effect_def;
        effect_def.name = "test_effect";
        effect_def.emitters.push_back(make_burst_emitter(particle_count, lifetime));
        return effect_def;
    }
};

TEST_F(EffectSystemTest, StartsEmpty) {
    EXPECT_EQ(system.effect_count(), 0u);
    EXPECT_EQ(system.particle_count(), 0u);
    EXPECT_TRUE(system.get_effects().empty());
}

TEST_F(EffectSystemTest, SpawnEffectReturnsIndex) {
    auto def = make_effect();
    int idx = system.spawn_effect(&def, {0, 0, 0});
    EXPECT_EQ(idx, 0);
}

TEST_F(EffectSystemTest, SpawnEffectWithNullDefinitionReturnsFail) {
    int idx = system.spawn_effect(nullptr, {0, 0, 0});
    EXPECT_EQ(idx, -1);
    EXPECT_EQ(system.effect_count(), 0u);
}

TEST_F(EffectSystemTest, SpawnedEffectIsInList) {
    auto def = make_effect();
    system.spawn_effect(&def, {0, 0, 0});
    EXPECT_EQ(system.effect_count(), 1u);
}

TEST_F(EffectSystemTest, MultipleEffectsGetIncrementingIndices) {
    auto def = make_effect();
    int idx0 = system.spawn_effect(&def, {0, 0, 0});
    int idx1 = system.spawn_effect(&def, {10, 0, 0});
    EXPECT_EQ(idx0, 0);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(system.effect_count(), 2u);
}

TEST_F(EffectSystemTest, BurstSpawnsParticlesOnFirstUpdate) {
    auto def = make_effect(5, 2.0f);
    system.spawn_effect(&def, {0, 0, 0});

    // First update should trigger burst spawn
    system.update(0.016f, flat_terrain);

    EXPECT_EQ(system.particle_count(), 5u);
}

TEST_F(EffectSystemTest, ParticlesAgeOverTime) {
    auto def = make_effect(1, 10.0f);
    system.spawn_effect(&def, {0, 0, 0});

    system.update(0.1f, flat_terrain);

    // After update, particle should have aged
    const auto& effects = system.get_effects();
    ASSERT_EQ(effects.size(), 1u);
    ASSERT_EQ(effects[0].emitters.size(), 1u);
    ASSERT_EQ(effects[0].emitters[0].particles.size(), 1u);

    const auto& particle = effects[0].emitters[0].particles[0];
    // Particle spawned at age 0, then update_particle adds dt to age
    EXPECT_GT(particle.age, 0.0f);
}

TEST_F(EffectSystemTest, DeadParticlesAreRemoved) {
    auto def = make_effect(3, 0.5f);
    system.spawn_effect(&def, {0, 0, 0});

    // Spawn particles
    system.update(0.016f, flat_terrain);
    EXPECT_EQ(system.particle_count(), 3u);

    // Advance past particle lifetime
    system.update(0.6f, flat_terrain);
    EXPECT_EQ(system.particle_count(), 0u);
}

TEST_F(EffectSystemTest, CompletedEffectsAreRemoved) {
    auto def = make_effect(1, 0.5f);
    system.spawn_effect(&def, {0, 0, 0});

    // Spawn and kill particles
    system.update(0.016f, flat_terrain);
    EXPECT_EQ(system.effect_count(), 1u);

    // Advance past both emitter duration and particle lifetime
    system.update(1.0f, flat_terrain);
    EXPECT_EQ(system.effect_count(), 0u);
}

TEST_F(EffectSystemTest, ClearRemovesAllEffects) {
    auto def = make_effect(5, 10.0f);
    system.spawn_effect(&def, {0, 0, 0});
    system.spawn_effect(&def, {10, 0, 0});
    system.update(0.016f, flat_terrain);

    EXPECT_EQ(system.effect_count(), 2u);
    system.clear();
    EXPECT_EQ(system.effect_count(), 0u);
    EXPECT_EQ(system.particle_count(), 0u);
}

TEST_F(EffectSystemTest, EffectUsesDefinitionDefaultRange) {
    EffectDefinition def;
    def.name = "ranged";
    def.default_range = 42.0f;
    EmitterDefinition emitter_def = make_burst_emitter(1, 5.0f);
    def.emitters.push_back(emitter_def);

    system.spawn_effect(&def, {0, 0, 0}, {1, 0, 0}, -1.0f);
    system.update(0.016f, flat_terrain);

    const auto& effects = system.get_effects();
    ASSERT_EQ(effects.size(), 1u);
    EXPECT_FLOAT_EQ(effects[0].emitters[0].range, 42.0f);
}

TEST_F(EffectSystemTest, EffectUsesOverriddenRange) {
    auto def = make_effect(1, 5.0f);
    def.default_range = 42.0f;

    system.spawn_effect(&def, {0, 0, 0}, {1, 0, 0}, 99.0f);

    const auto& effects = system.get_effects();
    ASSERT_EQ(effects.size(), 1u);
    EXPECT_FLOAT_EQ(effects[0].emitters[0].range, 99.0f);
}

TEST_F(EffectSystemTest, ContinuousEmitterSpawnsOverTime) {
    EffectDefinition def;
    def.name = "continuous";
    EmitterDefinition emitter_def;
    emitter_def.spawn_mode = SpawnMode::CONTINUOUS;
    emitter_def.spawn_rate = 10.0f; // 10 particles per second
    emitter_def.particle_lifetime = 5.0f;
    emitter_def.duration = 5.0f;
    emitter_def.model = "test";
    emitter_def.velocity.type = VelocityType::CUSTOM;
    emitter_def.velocity.speed = 0.0f;
    def.emitters.push_back(emitter_def);

    system.spawn_effect(&def, {0, 0, 0});

    // After 1 second, should have spawned ~10 particles
    // Update in small steps to let continuous spawning work
    for (int i = 0; i < 100; ++i) {
        system.update(0.01f, flat_terrain);
    }

    // At 10/sec for 1 second, expect ~10 particles (all still alive, lifetime=5)
    size_t count = system.particle_count();
    EXPECT_GE(count, 8u);
    EXPECT_LE(count, 12u);
}

TEST_F(EffectSystemTest, TerrainHeightPreventsParticlesBelowGround) {
    // Create particles with downward velocity so they would go below ground
    EffectDefinition def;
    def.name = "falling";
    EmitterDefinition emitter_def;
    emitter_def.spawn_mode = SpawnMode::BURST;
    emitter_def.spawn_count = 1;
    emitter_def.particle_lifetime = 5.0f;
    emitter_def.model = "test";
    emitter_def.velocity.type = VelocityType::CUSTOM;
    emitter_def.velocity.speed = 100.0f;
    emitter_def.velocity.direction = {0, -1, 0}; // straight down
    def.emitters.push_back(emitter_def);

    auto elevated_terrain = [](float, float) { return 5.0f; };

    system.spawn_effect(&def, {0, 10, 0});

    // Update enough for particle to fall below terrain
    for (int i = 0; i < 100; ++i) {
        system.update(0.016f, elevated_terrain);
    }

    const auto& effects = system.get_effects();
    if (!effects.empty() && !effects[0].emitters.empty()) {
        for (const auto& p : effects[0].emitters[0].particles) {
            EXPECT_GE(p.position.y, 5.0f);
        }
    }
}

TEST_F(EffectSystemTest, UpdateWithNullTerrainCallbackDoesNotCrash) {
    auto def = make_effect(3, 1.0f);
    system.spawn_effect(&def, {0, 0, 0});

    // Should not crash with null terrain callback
    EXPECT_NO_THROW(system.update(0.1f, nullptr));
    EXPECT_NO_THROW(system.update(0.1f));
}

TEST_F(EffectSystemTest, ParticleCountSumsAcrossEffectsAndEmitters) {
    auto def = make_effect(4, 5.0f);
    system.spawn_effect(&def, {0, 0, 0});
    system.spawn_effect(&def, {10, 0, 0});

    system.update(0.016f, flat_terrain);

    EXPECT_EQ(system.particle_count(), 8u);
}

TEST_F(EffectSystemTest, EmitterDelayDefersSpawning) {
    EffectDefinition def;
    def.name = "delayed";
    EmitterDefinition emitter_def;
    emitter_def.spawn_mode = SpawnMode::BURST;
    emitter_def.spawn_count = 3;
    emitter_def.particle_lifetime = 5.0f;
    emitter_def.delay = 1.0f; // 1 second delay
    emitter_def.model = "test";
    emitter_def.velocity.type = VelocityType::CUSTOM;
    emitter_def.velocity.speed = 0.0f;
    def.emitters.push_back(emitter_def);

    system.spawn_effect(&def, {0, 0, 0});

    // Update for less than the delay
    system.update(0.5f, flat_terrain);
    EXPECT_EQ(system.particle_count(), 0u);

    // Update past the delay
    system.update(0.6f, flat_terrain);
    EXPECT_EQ(system.particle_count(), 3u);
}

TEST_F(EffectSystemTest, ParticleScaleFollowsCurve) {
    EffectDefinition def;
    def.name = "scaling";
    EmitterDefinition emitter_def;
    emitter_def.spawn_mode = SpawnMode::BURST;
    emitter_def.spawn_count = 1;
    emitter_def.particle_lifetime = 1.0f;
    emitter_def.model = "test";
    emitter_def.velocity.type = VelocityType::CUSTOM;
    emitter_def.velocity.speed = 0.0f;
    emitter_def.appearance.scale_over_lifetime.type = CurveType::LINEAR;
    emitter_def.appearance.scale_over_lifetime.start_value = 2.0f;
    emitter_def.appearance.scale_over_lifetime.end_value = 0.0f;
    def.emitters.push_back(emitter_def);

    system.spawn_effect(&def, {0, 0, 0});

    // Update to ~50% lifetime
    system.update(0.016f, flat_terrain); // spawn
    system.update(0.484f, flat_terrain); // advance to ~0.5s

    const auto& effects = system.get_effects();
    ASSERT_FALSE(effects.empty());
    ASSERT_FALSE(effects[0].emitters[0].particles.empty());

    const auto& p = effects[0].emitters[0].particles[0];
    // At t~0.5, scale should be ~1.0 (linear from 2 to 0)
    EXPECT_NEAR(p.scale, 1.0f, 0.15f);
}

TEST_F(EffectSystemTest, ColorGradientInterpolatesOverLifetime) {
    EffectDefinition def;
    def.name = "gradient";
    EmitterDefinition emitter_def;
    emitter_def.spawn_mode = SpawnMode::BURST;
    emitter_def.spawn_count = 1;
    emitter_def.particle_lifetime = 1.0f;
    emitter_def.model = "test";
    emitter_def.velocity.type = VelocityType::CUSTOM;
    emitter_def.velocity.speed = 0.0f;
    emitter_def.appearance.use_color_gradient = true;
    emitter_def.appearance.color_tint = {1, 0, 0, 1}; // red
    emitter_def.appearance.color_end = {0, 0, 1, 1};   // blue
    def.emitters.push_back(emitter_def);

    system.spawn_effect(&def, {0, 0, 0});

    // Spawn, then advance to roughly halfway
    system.update(0.016f, flat_terrain);
    system.update(0.484f, flat_terrain);

    const auto& effects = system.get_effects();
    ASSERT_FALSE(effects.empty());
    ASSERT_FALSE(effects[0].emitters[0].particles.empty());

    const auto& p = effects[0].emitters[0].particles[0];
    // At ~t=0.5, color should be a mix of red and blue
    EXPECT_NEAR(p.color.r, 0.5f, 0.15f);
    EXPECT_NEAR(p.color.b, 0.5f, 0.15f);
}

TEST_F(EffectSystemTest, DirectionalVelocityMovesParticle) {
    EffectDefinition def;
    def.name = "moving";
    EmitterDefinition emitter_def;
    emitter_def.spawn_mode = SpawnMode::BURST;
    emitter_def.spawn_count = 1;
    emitter_def.particle_lifetime = 5.0f;
    emitter_def.model = "test";
    emitter_def.velocity.type = VelocityType::CUSTOM;
    emitter_def.velocity.speed = 100.0f;
    emitter_def.velocity.direction = {1, 0, 0};
    def.emitters.push_back(emitter_def);

    system.spawn_effect(&def, {0, 0, 0});
    system.update(0.016f, flat_terrain); // spawn
    system.update(1.0f, flat_terrain);   // move for 1 second

    const auto& effects = system.get_effects();
    ASSERT_FALSE(effects.empty());
    ASSERT_FALSE(effects[0].emitters[0].particles.empty());

    const auto& p = effects[0].emitters[0].particles[0];
    // After ~1 second at 100 units/sec in +X, should have moved significantly
    EXPECT_GT(p.position.x, 50.0f);
}

TEST_F(EffectSystemTest, GravityAffectsParticleVelocity) {
    EffectDefinition def;
    def.name = "gravity";
    EmitterDefinition emitter_def;
    emitter_def.spawn_mode = SpawnMode::BURST;
    emitter_def.spawn_count = 1;
    emitter_def.particle_lifetime = 5.0f;
    emitter_def.model = "test";
    emitter_def.velocity.type = VelocityType::CUSTOM;
    emitter_def.velocity.speed = 0.0f;
    emitter_def.velocity.direction = {0, 0, 0};
    emitter_def.velocity.gravity = {0, -10.0f, 0};
    def.emitters.push_back(emitter_def);

    system.spawn_effect(&def, {0, 100, 0});
    system.update(0.016f); // spawn
    system.update(1.0f);   // fall for 1 second

    const auto& effects = system.get_effects();
    ASSERT_FALSE(effects.empty());
    ASSERT_FALSE(effects[0].emitters[0].particles.empty());

    const auto& p = effects[0].emitters[0].particles[0];
    // Particle should have fallen (y decreased)
    EXPECT_LT(p.position.y, 100.0f);
    // Velocity should be negative in y
    EXPECT_LT(p.velocity.y, 0.0f);
}
