#include <gtest/gtest.h>
#include "physics_system.hpp"
#include "server/ecs/game_components.hpp"

#include <entt/entt.hpp>

using namespace mmo::server;
using namespace mmo::server::systems;

namespace {

// Add a "character" entity (player-style: Dynamic + lock_rotation, capsule)
// — the kind that becomes a Jolt CharacterVirtual with an inner body.
// shutdown() must not double-destroy that inner body.
entt::entity make_character(entt::registry& r, uint32_t net_id) {
    auto e = r.create();
    r.emplace<ecs::NetworkId>(e, net_id);
    r.emplace<ecs::Transform>(e, ecs::Transform{0.0f, 0.0f, 0.0f, 0.0f});
    r.emplace<ecs::PlayerTag>(e);

    ecs::Collider col;
    col.type = ecs::ColliderType::Capsule;
    col.radius = 0.4f;
    col.half_height = 0.8f;
    col.offset_y = col.half_height + col.radius;
    r.emplace<ecs::Collider>(e, col);

    ecs::RigidBody rb;
    rb.motion_type = ecs::PhysicsMotionType::Dynamic;
    rb.lock_rotation = true;  // -> CharacterVirtual path
    rb.mass = 70.0f;
    r.emplace<ecs::RigidBody>(e, rb);
    return e;
}

// Add a "static" entity (building-style: Static box). Goes through the
// regular CreateBody path, NOT CharacterVirtual.
entt::entity make_static_box(entt::registry& r, uint32_t net_id) {
    auto e = r.create();
    r.emplace<ecs::NetworkId>(e, net_id);
    r.emplace<ecs::Transform>(e, ecs::Transform{10.0f, 0.0f, 10.0f, 0.0f});

    ecs::Collider col;
    col.type = ecs::ColliderType::Box;
    col.half_extents_x = col.half_extents_y = col.half_extents_z = 1.0f;
    col.offset_y = 1.0f;
    r.emplace<ecs::Collider>(e, col);

    ecs::RigidBody rb;
    rb.motion_type = ecs::PhysicsMotionType::Static;
    r.emplace<ecs::RigidBody>(e, rb);
    return e;
}

} // namespace

// Regression: shutdown() used to double-free CharacterVirtual inner bodies
// because impl_->characters.clear() destroys the inner body via the
// CharacterVirtual destructor, then the network_to_body loop tried to
// DestroyBody the same (now-stale) ID. Manifested as SIGSEGV inside
// JPH::BodyManager::sDeleteBody on Ctrl+C.
TEST(PhysicsSystem, ShutdownDoesNotDoubleDestroyCharacterBodies) {
    entt::registry registry;
    make_character(registry, 1);
    make_character(registry, 2);
    make_static_box(registry, 100);

    PhysicsSystem physics;
    physics.initialize();
    physics.create_bodies(registry);

    // shutdown() must not crash. Test passes simply by surviving.
    EXPECT_NO_THROW(physics.shutdown());
}

TEST(PhysicsSystem, InitializeShutdownIsIdempotent) {
    PhysicsSystem physics;
    physics.initialize();
    physics.shutdown();
    physics.shutdown();  // second call is a no-op
    physics.initialize();
    physics.shutdown();
}

TEST(PhysicsSystem, ShutdownWithNoEntitiesIsSafe) {
    PhysicsSystem physics;
    physics.initialize();
    EXPECT_NO_THROW(physics.shutdown());
}

TEST(PhysicsSystem, ShutdownOnlyStaticBodiesIsSafe) {
    entt::registry registry;
    make_static_box(registry, 1);
    make_static_box(registry, 2);

    PhysicsSystem physics;
    physics.initialize();
    physics.create_bodies(registry);

    EXPECT_NO_THROW(physics.shutdown());
}

TEST(PhysicsSystem, ShutdownOnlyCharactersIsSafe) {
    entt::registry registry;
    make_character(registry, 1);
    make_character(registry, 2);
    make_character(registry, 3);

    PhysicsSystem physics;
    physics.initialize();
    physics.create_bodies(registry);

    EXPECT_NO_THROW(physics.shutdown());
}
