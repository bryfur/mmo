// JoltPhysics integration for MMO server.
// Must include Jolt.h first before any other Jolt headers.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <math.h>

#include "physics_system.hpp"
#include "server/ecs/game_components.hpp"
#include "server/heightmap_generator.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

JPH_SUPPRESS_WARNINGS

namespace mmo::server::systems {

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        object_to_broad_phase_[CollisionLayers::NON_MOVING] = JPH::BroadPhaseLayer(0);
        object_to_broad_phase_[CollisionLayers::MOVING] = JPH::BroadPhaseLayer(1);
        object_to_broad_phase_[CollisionLayers::TRIGGER] = JPH::BroadPhaseLayer(1);
    }

    uint32_t GetNumBroadPhaseLayers() const override { return 2; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return object_to_broad_phase_[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case 0:
                return "NON_MOVING";
            case 1:
                return "MOVING";
            default:
                return "UNKNOWN";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer object_to_broad_phase_[CollisionLayers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case CollisionLayers::NON_MOVING:
                return inLayer2 == JPH::BroadPhaseLayer(1);
            case CollisionLayers::MOVING:
                return true;
            case CollisionLayers::TRIGGER:
                return inLayer2 == JPH::BroadPhaseLayer(1);
            default:
                return false;
        }
    }
};

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
            case CollisionLayers::NON_MOVING:
                return inObject2 == CollisionLayers::MOVING;
            case CollisionLayers::MOVING:
                return true;
            case CollisionLayers::TRIGGER:
                return inObject2 == CollisionLayers::MOVING;
            default:
                return false;
        }
    }
};

class BodyActivationListenerImpl : public JPH::BodyActivationListener {
public:
    void OnBodyActivated(const JPH::BodyID&, uint64_t) override {}
    void OnBodyDeactivated(const JPH::BodyID&, uint64_t) override {}
};

// Stores contacts keyed by (lo, hi) network_id pair so are_colliding can
// answer in O(1) instead of scanning a vector each call.
struct ContactKey {
    uint32_t lo;
    uint32_t hi;
    bool operator==(const ContactKey& o) const { return lo == o.lo && hi == o.hi; }
};
struct ContactKeyHash {
    size_t operator()(const ContactKey& k) const noexcept {
        return std::hash<uint64_t>{}((uint64_t(k.hi) << 32) | k.lo);
    }
};
static inline ContactKey make_contact_key(uint32_t a, uint32_t b) {
    return a < b ? ContactKey{a, b} : ContactKey{b, a};
}

class ContactListenerImpl : public JPH::ContactListener {
public:
    void SetCollisionCallback(CollisionCallback callback) { callback_ = std::move(callback); }

    void ClearEvents() {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
        active_pairs_.clear();
    }

    std::vector<ecs::CollisionEvent> GetEventsCopy() {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

    bool HasContact(uint32_t a_net, uint32_t b_net) {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_pairs_.find(make_contact_key(a_net, b_net)) != active_pairs_.end();
    }

    JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&, JPH::RVec3Arg,
                                          const JPH::CollideShapeResult&) override {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold,
                        JPH::ContactSettings&) override {
        RecordCollision(inBody1, inBody2, inManifold);
    }

    void OnContactPersisted(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&,
                            JPH::ContactSettings&) override {}

    void OnContactRemoved(const JPH::SubShapeIDPair&) override {}

private:
    void RecordCollision(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold) {
        ecs::CollisionEvent event;
        event.entity_a_network_id = static_cast<uint32_t>(body1.GetUserData());
        event.entity_b_network_id = static_cast<uint32_t>(body2.GetUserData());

        if (manifold.mRelativeContactPointsOn1.size() > 0) {
            JPH::Vec3 point = manifold.GetWorldSpaceContactPointOn1(0);
            event.contact_point_x = point.GetX();
            event.contact_point_y = point.GetY();
            event.contact_point_z = point.GetZ();
        }

        event.normal_x = manifold.mWorldSpaceNormal.GetX();
        event.normal_y = manifold.mWorldSpaceNormal.GetY();
        event.normal_z = manifold.mWorldSpaceNormal.GetZ();
        event.penetration_depth = manifold.mPenetrationDepth;

        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);
        active_pairs_.insert(make_contact_key(event.entity_a_network_id, event.entity_b_network_id));
    }

    CollisionCallback callback_;
    std::mutex mutex_;
    std::vector<ecs::CollisionEvent> events_;
    std::unordered_set<ContactKey, ContactKeyHash> active_pairs_;
};

struct CharacterState {
    JPH::Ref<JPH::CharacterVirtual> controller;
    float gravity_y = -1500.0f;
};

class PhysicsSystem::Impl {
public:
    bool initialized = false;

    std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
    std::unique_ptr<JPH::JobSystemThreadPool> job_system;
    std::unique_ptr<JPH::PhysicsSystem> physics_system;

    BPLayerInterfaceImpl broad_phase_layer_interface;
    ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_filter;
    ObjectLayerPairFilterImpl object_layer_pair_filter;
    BodyActivationListenerImpl body_activation_listener;
    ContactListenerImpl contact_listener;

    std::unordered_map<uint32_t, JPH::BodyID> network_to_body;
    std::unordered_map<uint32_t, uint32_t> body_to_network;

    std::unordered_map<entt::entity, uint32_t> entity_to_network;
    std::unordered_map<uint32_t, entt::entity> network_to_entity;

    std::unordered_map<entt::entity, CharacterState> characters;
    // Reused per-tick scratch for deterministic character iteration.
    // Iterating the unordered_map directly diverges client/server runs and
    // makes character-character resolution order non-reproducible.
    std::vector<entt::entity> sorted_character_buf;

    JPH::BodyID terrain_body{};
    JPH::RefConst<JPH::Shape> terrain_shape;
    bool terrain_ready = false;
    mmo::protocol::HeightmapChunk terrain_heightmap;

    // Cached sphere shape for overlap_sphere; rebuilt only when radius changes.
    JPH::Ref<JPH::Shape> overlap_sphere_shape;
    float overlap_sphere_cached_radius = -1.0f;

    float gravity_x = 0.0f;
    float gravity_y = -1500.0f;
    float gravity_z = 0.0f;

    CollisionCallback collision_callback;
};

PhysicsSystem::PhysicsSystem() : impl_(std::make_unique<Impl>()) {}

PhysicsSystem::~PhysicsSystem() {
    if (impl_->initialized) {
        shutdown();
    }
}

void PhysicsSystem::initialize(uint32_t max_bodies, uint32_t max_body_pairs, uint32_t max_contact_constraints) {
    if (impl_->initialized) {
        return;
    }

    JPH::RegisterDefaultAllocator();

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    impl_->temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);

    int num_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    impl_->job_system =
        std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, num_threads);

    impl_->physics_system = std::make_unique<JPH::PhysicsSystem>();
    impl_->physics_system->Init(max_bodies, 0, max_body_pairs, max_contact_constraints,
                                impl_->broad_phase_layer_interface, impl_->object_vs_broadphase_filter,
                                impl_->object_layer_pair_filter);

    impl_->physics_system->SetGravity(JPH::Vec3(impl_->gravity_x, impl_->gravity_y, impl_->gravity_z));

    impl_->physics_system->SetBodyActivationListener(&impl_->body_activation_listener);
    impl_->physics_system->SetContactListener(&impl_->contact_listener);

    impl_->initialized = true;
}

void PhysicsSystem::shutdown() {
    if (!impl_->initialized) {
        return;
    }

    auto& body_interface = impl_->physics_system->GetBodyInterface();

    // CharacterVirtual owns its inner body; ~CharacterVirtual destroys it.
    // We must drop those entries from network_to_body BEFORE clearing the
    // characters, otherwise the body-destroy loop below double-frees the
    // inner bodies (Jolt segfaults inside BodyManager::sDeleteBody).
    for (auto& [entity, state] : impl_->characters) {
        auto enit = impl_->entity_to_network.find(entity);
        if (enit == impl_->entity_to_network.end()) {
            continue;
        }
        auto bit = impl_->network_to_body.find(enit->second);
        if (bit == impl_->network_to_body.end()) {
            continue;
        }
        impl_->body_to_network.erase(bit->second.GetIndex());
        impl_->network_to_body.erase(bit);
    }
    impl_->characters.clear();

    for (auto& [network_id, body_id] : impl_->network_to_body) {
        body_interface.RemoveBody(body_id);
        body_interface.DestroyBody(body_id);
    }
    impl_->network_to_body.clear();
    impl_->body_to_network.clear();
    impl_->entity_to_network.clear();
    impl_->network_to_entity.clear();

    if (impl_->terrain_ready && !impl_->terrain_body.IsInvalid()) {
        body_interface.RemoveBody(impl_->terrain_body);
        body_interface.DestroyBody(impl_->terrain_body);
        impl_->terrain_body = JPH::BodyID{};
        impl_->terrain_shape = nullptr;
        impl_->terrain_ready = false;
    }

    impl_->physics_system.reset();
    impl_->job_system.reset();
    impl_->temp_allocator.reset();

    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    JPH::UnregisterTypes();

    impl_->initialized = false;
}

void PhysicsSystem::build_terrain(const mmo::protocol::HeightmapChunk& chunk) {
    if (!impl_->initialized || chunk.resolution == 0 || chunk.height_data.empty()) {
        return;
    }

    impl_->terrain_heightmap = chunk;

    // Jolt rounds the sample count up to the nearest multiple of mBlockSize
    // internally, so any block size in [2, 8] is safe. Bigger blocks = less
    // memory for the broadphase tree but worse query performance.
    const uint32_t sample_count = chunk.resolution;
    const float world_size = chunk.world_size;
    const float texel_size = world_size / static_cast<float>(sample_count - 1);

    // Decode the uint16 samples to real heights. Samples are stored row-major
    // as height_data[z * resolution + x]; HeightFieldShape expects the same
    // row-major layout (samples[y * n + x]), so no transpose is needed — we
    // just map hf.y to world.z.
    const float range = mmo::protocol::heightmap_config::MAX_HEIGHT - mmo::protocol::heightmap_config::MIN_HEIGHT;
    const float min_h = mmo::protocol::heightmap_config::MIN_HEIGHT;

    std::vector<float> samples(static_cast<size_t>(sample_count) * sample_count);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = (chunk.height_data[i] / 65535.0f) * range + min_h;
    }

    JPH::HeightFieldShapeSettings settings(samples.data(), JPH::Vec3(chunk.world_origin_x, 0.0f, chunk.world_origin_z),
                                           JPH::Vec3(texel_size, 1.0f, texel_size), sample_count);
    settings.mBlockSize = 4;
    settings.mBitsPerSample = 8;

    auto result = settings.Create();
    if (result.HasError()) {
        std::cerr << "[Physics] Failed to build HeightFieldShape: " << result.GetError().c_str() << '\n';
        return;
    }
    impl_->terrain_shape = result.Get();

    JPH::BodyCreationSettings body_settings(impl_->terrain_shape, JPH::RVec3(0.0f, 0.0f, 0.0f), JPH::Quat::sIdentity(),
                                            JPH::EMotionType::Static, CollisionLayers::NON_MOVING);
    body_settings.mFriction = 0.6f;
    body_settings.mRestitution = 0.0f;
    body_settings.mUserData = 0;

    auto& bi = impl_->physics_system->GetBodyInterface();
    JPH::Body* body = bi.CreateBody(body_settings);
    if (body == nullptr) {
        std::cerr << "[Physics] Failed to create terrain body" << '\n';
        return;
    }
    bi.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    impl_->terrain_body = body->GetID();
    impl_->terrain_ready = true;
}

float PhysicsSystem::terrain_height(float x, float z) const {
    // Prefer the O(1) CPU sampler. The previous narrowphase raycast cost
    // thousands of queries per startup (one per spawn/respawn) for a value
    // the heightmap already encodes directly.
    if (impl_->terrain_heightmap.resolution > 0) {
        return heightmap_get_world(impl_->terrain_heightmap, x, z);
    }
    if (impl_->initialized && impl_->terrain_ready) {
        JPH::RRayCast ray(JPH::RVec3(x, mmo::protocol::heightmap_config::MAX_HEIGHT + 10.0f, z),
                          JPH::Vec3(0.0f,
                                    -(mmo::protocol::heightmap_config::MAX_HEIGHT -
                                      mmo::protocol::heightmap_config::MIN_HEIGHT + 20.0f),
                                    0.0f));
        JPH::RayCastResult hit;
        if (impl_->physics_system->GetNarrowPhaseQuery().CastRay(ray, hit)) {
            JPH::RVec3 p = ray.GetPointOnRay(hit.mFraction);
            return static_cast<float>(p.GetY());
        }
    }
    return 0.0f;
}

static JPH::ShapeRefC make_collider_shape(const ecs::Collider& c) {
    switch (c.type) {
        case ecs::ColliderType::Sphere:
            return new JPH::SphereShape(c.radius);
        case ecs::ColliderType::Box:
            return new JPH::BoxShape(JPH::Vec3(c.half_extents_x, c.half_extents_y, c.half_extents_z));
        case ecs::ColliderType::Capsule:
            return new JPH::CapsuleShape(c.half_height, c.radius);
        case ecs::ColliderType::Cylinder:
            return new JPH::CylinderShape(c.half_height, c.radius);
    }
    return new JPH::SphereShape(c.radius);
}

void PhysicsSystem::create_bodies(entt::registry& registry) {
    if (!impl_->initialized) {
        return;
    }

    auto view =
        registry.view<ecs::Transform, ecs::Collider, ecs::RigidBody, ecs::NetworkId>(entt::exclude<ecs::PhysicsBody>);

    for (auto entity : view) {
        create_body_for_entity(registry, entity);
    }
}

void PhysicsSystem::create_body_for_entity(entt::registry& registry, entt::entity entity) {
    if (!impl_->initialized) {
        return;
    }
    if (registry.all_of<ecs::PhysicsBody>(entity)) {
        return;
    }
    if (!registry.all_of<ecs::Transform, ecs::Collider, ecs::RigidBody, ecs::NetworkId>(entity)) {
        return;
    }

    auto& body_interface = impl_->physics_system->GetBodyInterface();

    {
        const auto& transform = registry.get<ecs::Transform>(entity);
        const auto& collider = registry.get<ecs::Collider>(entity);
        const auto& rigid_body = registry.get<ecs::RigidBody>(entity);
        const auto& network_id = registry.get<ecs::NetworkId>(entity);

        // Only PLAYERS use CharacterVirtual — its sweep-and-slide cost is
        // proportional to surrounding geometry and prohibitive at hundreds of
        // entities. NPCs are AI-driven kinematic capsules (cheap collision,
        // no per-step sweep) and players get full step-up / slope handling.
        const bool is_character = registry.all_of<ecs::PlayerTag>(entity) &&
                                  rigid_body.motion_type == ecs::PhysicsMotionType::Dynamic &&
                                  collider.type == ecs::ColliderType::Capsule && !collider.is_trigger;

        if (is_character) {
            JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
            settings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
            settings->mShape = new JPH::CapsuleShape(collider.half_height, collider.radius);
            settings->mUp = JPH::Vec3::sAxisY();
            settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -collider.radius);
            settings->mMass = rigid_body.mass;
            settings->mMaxStrength = 100.0f;
            settings->mCharacterPadding = 0.02f;
            settings->mPredictiveContactDistance = 0.5f;
            settings->mShapeOffset = JPH::Vec3(0.0f, collider.half_height + collider.radius, 0.0f);
            settings->mInnerBodyShape = new JPH::CapsuleShape(collider.half_height, collider.radius);
            settings->mInnerBodyLayer = CollisionLayers::MOVING;

            JPH::RVec3 position(transform.x, transform.y, transform.z);

            JPH::Ref<JPH::CharacterVirtual> controller =
                new JPH::CharacterVirtual(settings, position, JPH::Quat::sIdentity(),
                                          static_cast<uint64_t>(network_id.id), impl_->physics_system.get());

            CharacterState state;
            state.controller = controller;
            state.gravity_y = impl_->gravity_y;
            impl_->characters[entity] = std::move(state);

            impl_->entity_to_network[entity] = network_id.id;
            impl_->network_to_entity[network_id.id] = entity;

            JPH::BodyID inner = controller->GetInnerBodyID();
            if (!inner.IsInvalid()) {
                impl_->network_to_body[network_id.id] = inner;
                impl_->body_to_network[inner.GetIndex()] = network_id.id;
            }

            registry.emplace<ecs::PhysicsBody>(
                entity, inner.IsInvalid() ? uint32_t{0xFFFFFFFFu} : inner.GetIndexAndSequenceNumber(), true);
            return;
        }

        JPH::ShapeRefC shape = make_collider_shape(collider);

        JPH::EMotionType motion_type;
        JPH::ObjectLayer layer = 0;
        switch (rigid_body.motion_type) {
            case ecs::PhysicsMotionType::Static:
                motion_type = JPH::EMotionType::Static;
                layer = CollisionLayers::NON_MOVING;
                break;
            case ecs::PhysicsMotionType::Kinematic:
                motion_type = JPH::EMotionType::Kinematic;
                layer = CollisionLayers::MOVING;
                break;
            case ecs::PhysicsMotionType::Dynamic:
            default:
                motion_type = JPH::EMotionType::Dynamic;
                layer = CollisionLayers::MOVING;
                break;
        }

        if (collider.is_trigger) {
            layer = CollisionLayers::TRIGGER;
        }

        JPH::RVec3 position(transform.x, transform.y + collider.offset_y, transform.z);

        JPH::BodyCreationSettings settings(shape, position, JPH::Quat::sIdentity(), motion_type, layer);
        settings.mFriction = rigid_body.friction;
        settings.mRestitution = rigid_body.restitution;
        settings.mLinearDamping = rigid_body.linear_damping;
        settings.mAngularDamping = rigid_body.angular_damping;

        if (rigid_body.motion_type == ecs::PhysicsMotionType::Dynamic) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = rigid_body.mass;
        }

        if (rigid_body.lock_rotation) {
            settings.mAllowedDOFs =
                JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        }

        settings.mUserData = network_id.id;

        JPH::Body* body = body_interface.CreateBody(settings);
        if (body == nullptr) {
            std::cerr << "[Physics] Failed to create body for entity " << network_id.id << '\n';
            return;
        }

        body_interface.AddBody(body->GetID(), motion_type == JPH::EMotionType::Static ? JPH::EActivation::DontActivate
                                                                                      : JPH::EActivation::Activate);

        impl_->network_to_body[network_id.id] = body->GetID();
        impl_->body_to_network[body->GetID().GetIndex()] = network_id.id;
        impl_->entity_to_network[entity] = network_id.id;
        impl_->network_to_entity[network_id.id] = entity;

        registry.emplace<ecs::PhysicsBody>(entity, body->GetID().GetIndexAndSequenceNumber(), true);
    }
}


void PhysicsSystem::destroy_body(entt::registry& registry, entt::entity entity) {
    if (!impl_->initialized) {
        return;
    }

    auto* network_id = registry.try_get<ecs::NetworkId>(entity);
    if (!network_id) {
        return;
    }

    auto char_it = impl_->characters.find(entity);
    if (char_it != impl_->characters.end()) {
        auto it = impl_->network_to_body.find(network_id->id);
        if (it != impl_->network_to_body.end()) {
            impl_->body_to_network.erase(it->second.GetIndex());
            impl_->network_to_body.erase(it);
        }
        impl_->characters.erase(char_it);
        impl_->network_to_entity.erase(network_id->id);
        impl_->entity_to_network.erase(entity);
        registry.remove<ecs::PhysicsBody>(entity);
        return;
    }

    auto it = impl_->network_to_body.find(network_id->id);
    if (it != impl_->network_to_body.end()) {
        auto& body_interface = impl_->physics_system->GetBodyInterface();
        body_interface.RemoveBody(it->second);
        body_interface.DestroyBody(it->second);

        impl_->body_to_network.erase(it->second.GetIndex());
        impl_->network_to_body.erase(it);
    }

    impl_->network_to_entity.erase(network_id->id);
    impl_->entity_to_network.erase(entity);
    registry.remove<ecs::PhysicsBody>(entity);
}

void PhysicsSystem::update(entt::registry& registry, float fixed_dt) {
    if (!impl_->initialized) {
        return;
    }

    impl_->contact_listener.ClearEvents();

    create_bodies(registry);

    auto& body_interface = impl_->physics_system->GetBodyInterface();

    // Teleport requests for non-character dynamic bodies.
    auto teleport_view = registry.view<ecs::Transform, ecs::PhysicsBody, ecs::NetworkId>();
    for (auto entity : teleport_view) {
        auto& physics_body = teleport_view.get<ecs::PhysicsBody>(entity);
        if (!physics_body.needs_teleport) {
            continue;
        }

        const auto& transform = teleport_view.get<ecs::Transform>(entity);
        const auto& network_id = teleport_view.get<ecs::NetworkId>(entity);

        auto char_it = impl_->characters.find(entity);
        if (char_it != impl_->characters.end()) {
            char_it->second.controller->SetPosition(JPH::RVec3(transform.x, transform.y, transform.z));
            char_it->second.controller->SetLinearVelocity(JPH::Vec3::sZero());
        } else {
            auto it = impl_->network_to_body.find(network_id.id);
            if (it != impl_->network_to_body.end()) {
                float offset_y = 0.0f;
                if (const auto* collider = registry.try_get<ecs::Collider>(entity)) {
                    offset_y = collider->offset_y;
                }
                body_interface.SetPosition(it->second, JPH::RVec3(transform.x, transform.y + offset_y, transform.z),
                                           JPH::EActivation::Activate);
                body_interface.SetLinearVelocity(it->second, JPH::Vec3::sZero());
            }
        }

        physics_body.needs_teleport = false;
    }

    // Drive non-character dynamic bodies from ECS velocity.
    auto dynamic_view =
        registry.view<ecs::Transform, ecs::PhysicsBody, ecs::RigidBody, ecs::Velocity, ecs::NetworkId>();
    for (auto entity : dynamic_view) {
        const auto& rigid_body = dynamic_view.get<ecs::RigidBody>(entity);
        if (rigid_body.motion_type != ecs::PhysicsMotionType::Dynamic) {
            continue;
        }
        if (impl_->characters.find(entity) != impl_->characters.end()) {
            continue;
        }

        const auto& velocity = dynamic_view.get<ecs::Velocity>(entity);
        const auto& network_id = dynamic_view.get<ecs::NetworkId>(entity);

        auto it = impl_->network_to_body.find(network_id.id);
        if (it == impl_->network_to_body.end()) {
            continue;
        }

        if (velocity.x == 0.0f && velocity.y == 0.0f && velocity.z == 0.0f) {
            if (!body_interface.IsActive(it->second)) {
                continue;
            }
            body_interface.SetLinearVelocity(it->second, JPH::Vec3::sZero());
            continue;
        }

        body_interface.SetLinearVelocity(it->second, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        body_interface.ActivateBody(it->second);
    }

    // Update kinematic bodies (AI-driven without velocity).
    auto kinematic_view = registry.view<ecs::Transform, ecs::PhysicsBody, ecs::RigidBody>();
    for (auto entity : kinematic_view) {
        const auto& rigid_body = kinematic_view.get<ecs::RigidBody>(entity);
        if (rigid_body.motion_type != ecs::PhysicsMotionType::Kinematic) {
            continue;
        }

        const auto& transform = kinematic_view.get<ecs::Transform>(entity);
        const auto* network_id = registry.try_get<ecs::NetworkId>(entity);
        if (!network_id) {
            continue;
        }

        auto it = impl_->network_to_body.find(network_id->id);
        if (it != impl_->network_to_body.end()) {
            body_interface.MoveKinematic(it->second, JPH::RVec3(transform.x, transform.y, transform.z),
                                         JPH::Quat::sIdentity(), fixed_dt);
        }
    }

    // Drive CharacterVirtuals from ECS velocity and step them.
    {
        JPH::DefaultBroadPhaseLayerFilter bp_filter(impl_->object_vs_broadphase_filter, CollisionLayers::MOVING);
        JPH::DefaultObjectLayerFilter obj_filter(impl_->object_layer_pair_filter, CollisionLayers::MOVING);
        JPH::BodyFilter body_filter;
        JPH::ShapeFilter shape_filter;

        // Sort characters by network_id to make resolution order deterministic
        // across runs and between client/server.
        auto& iter_buf = impl_->sorted_character_buf;
        iter_buf.clear();
        iter_buf.reserve(impl_->characters.size());
        for (const auto& kv : impl_->characters) iter_buf.push_back(kv.first);
        std::sort(iter_buf.begin(), iter_buf.end(), [&](entt::entity a, entt::entity b) {
            auto ai = impl_->entity_to_network.find(a);
            auto bi = impl_->entity_to_network.find(b);
            uint32_t av = (ai != impl_->entity_to_network.end()) ? ai->second : 0;
            uint32_t bv = (bi != impl_->entity_to_network.end()) ? bi->second : 0;
            return av < bv;
        });

        for (entt::entity entity : iter_buf) {
            auto state_it = impl_->characters.find(entity);
            if (state_it == impl_->characters.end()) {
                continue;
            }
            auto& state = state_it->second;
            const auto* vel = registry.try_get<ecs::Velocity>(entity);
            if (!vel) {
                continue;
            }

            JPH::Vec3 current = state.controller->GetLinearVelocity();
            JPH::Vec3 desired(vel->x, 0.0f, vel->z);

            // Vertical component: preserve accumulated fall/jump velocity,
            // but reset upward when grounded so we don't glue into the sky.
            float new_y = NAN;
            if (state.controller->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround) {
                new_y = std::max(0.0f, current.GetY());
                new_y += state.gravity_y * fixed_dt;
            } else {
                new_y = current.GetY() + state.gravity_y * fixed_dt;
            }
            desired.SetY(new_y);

            state.controller->SetLinearVelocity(desired);

            JPH::CharacterVirtual::ExtendedUpdateSettings upd;
            upd.mWalkStairsStepUp = JPH::Vec3(0.0f, 30.0f, 0.0f);
            upd.mStickToFloorStepDown = JPH::Vec3(0.0f, -30.0f, 0.0f);
            state.controller->ExtendedUpdate(fixed_dt, JPH::Vec3(0.0f, state.gravity_y, 0.0f), upd, bp_filter,
                                             obj_filter, body_filter, shape_filter, *impl_->temp_allocator);
        }
    }

    const int collision_steps = 1;
    impl_->physics_system->Update(fixed_dt, collision_steps, impl_->temp_allocator.get(), impl_->job_system.get());

    sync_transforms(registry);

    if (impl_->collision_callback) {
        auto events = impl_->contact_listener.GetEventsCopy();
        for (const auto& event : events) {
            auto a_it = impl_->network_to_entity.find(event.entity_a_network_id);
            auto b_it = impl_->network_to_entity.find(event.entity_b_network_id);
            if (a_it != impl_->network_to_entity.end() && b_it != impl_->network_to_entity.end()) {
                impl_->collision_callback(a_it->second, b_it->second, event);
            }
        }
    }
}

void PhysicsSystem::sync_transforms(entt::registry& registry) {
    if (!impl_->initialized) {
        return;
    }

    auto& body_interface = impl_->physics_system->GetBodyInterface();

    // sorted_character_buf was populated in update() this tick. It may be
    // stale only if sync_transforms is ever called outside the post-update
    // window; rebuild defensively if empty/mismatched.
    if (impl_->sorted_character_buf.size() != impl_->characters.size()) {
        impl_->sorted_character_buf.clear();
        impl_->sorted_character_buf.reserve(impl_->characters.size());
        for (const auto& kv : impl_->characters) impl_->sorted_character_buf.push_back(kv.first);
        std::sort(impl_->sorted_character_buf.begin(), impl_->sorted_character_buf.end(),
                  [&](entt::entity a, entt::entity b) {
                      auto ai = impl_->entity_to_network.find(a);
                      auto bi = impl_->entity_to_network.find(b);
                      uint32_t av = (ai != impl_->entity_to_network.end()) ? ai->second : 0;
                      uint32_t bv = (bi != impl_->entity_to_network.end()) ? bi->second : 0;
                      return av < bv;
                  });
    }

    for (entt::entity entity : impl_->sorted_character_buf) {
        auto state_it = impl_->characters.find(entity);
        if (state_it == impl_->characters.end()) {
            continue;
        }
        auto& state = state_it->second;
        auto* transform = registry.try_get<ecs::Transform>(entity);
        if (!transform) {
            continue;
        }

        JPH::RVec3 p = state.controller->GetPosition();
        transform->x = static_cast<float>(p.GetX());
        transform->y = static_cast<float>(p.GetY());
        transform->z = static_cast<float>(p.GetZ());
    }

    auto view = registry.view<ecs::Transform, ecs::PhysicsBody, ecs::RigidBody, ecs::NetworkId, ecs::Collider>();
    for (auto entity : view) {
        const auto& rigid_body = view.get<ecs::RigidBody>(entity);
        auto& physics_body = view.get<ecs::PhysicsBody>(entity);

        if (rigid_body.motion_type != ecs::PhysicsMotionType::Dynamic || !physics_body.needs_sync) {
            continue;
        }
        if (impl_->characters.find(entity) != impl_->characters.end()) {
            continue;
        }

        const auto& network_id = view.get<ecs::NetworkId>(entity);
        auto it = impl_->network_to_body.find(network_id.id);
        if (it == impl_->network_to_body.end()) {
            continue;
        }

        const auto& collider = view.get<ecs::Collider>(entity);
        JPH::RVec3 position = body_interface.GetPosition(it->second);

        auto& transform = view.get<ecs::Transform>(entity);
        transform.x = static_cast<float>(position.GetX());
        transform.y = static_cast<float>(position.GetY()) - collider.offset_y;
        transform.z = static_cast<float>(position.GetZ());
    }
}

void PhysicsSystem::apply_impulse(entt::registry& registry, entt::entity entity, float impulse_x, float impulse_y,
                                  float impulse_z) {
    if (!impl_->initialized) {
        return;
    }

    auto char_it = impl_->characters.find(entity);
    if (char_it != impl_->characters.end()) {
        auto& ctrl = *char_it->second.controller;
        JPH::Vec3 v = ctrl.GetLinearVelocity();
        float mass = ctrl.GetMass() > 0.0f ? ctrl.GetMass() : 1.0f;
        v += JPH::Vec3(impulse_x, impulse_y, impulse_z) / mass;
        ctrl.SetLinearVelocity(v);
        return;
    }

    auto* network_id = registry.try_get<ecs::NetworkId>(entity);
    if (!network_id) {
        return;
    }

    auto it = impl_->network_to_body.find(network_id->id);
    if (it == impl_->network_to_body.end()) {
        return;
    }

    auto& body_interface = impl_->physics_system->GetBodyInterface();
    body_interface.AddImpulse(it->second, JPH::Vec3(impulse_x, impulse_y, impulse_z));
}

void PhysicsSystem::set_velocity(entt::registry& registry, entt::entity entity, float vel_x, float vel_y, float vel_z) {
    if (!impl_->initialized) {
        return;
    }

    auto char_it = impl_->characters.find(entity);
    if (char_it != impl_->characters.end()) {
        char_it->second.controller->SetLinearVelocity(JPH::Vec3(vel_x, vel_y, vel_z));
        return;
    }

    auto* network_id = registry.try_get<ecs::NetworkId>(entity);
    if (!network_id) {
        return;
    }

    auto it = impl_->network_to_body.find(network_id->id);
    if (it == impl_->network_to_body.end()) {
        return;
    }

    auto& body_interface = impl_->physics_system->GetBodyInterface();
    body_interface.SetLinearVelocity(it->second, JPH::Vec3(vel_x, vel_y, vel_z));
}

void PhysicsSystem::move_kinematic(entt::registry& registry, entt::entity entity, float x, float y, float z, float dt) {
    if (!impl_->initialized) {
        return;
    }

    auto* network_id = registry.try_get<ecs::NetworkId>(entity);
    if (!network_id) {
        return;
    }

    auto it = impl_->network_to_body.find(network_id->id);
    if (it == impl_->network_to_body.end()) {
        return;
    }

    auto& body_interface = impl_->physics_system->GetBodyInterface();
    body_interface.MoveKinematic(it->second, JPH::RVec3(x, y, z), JPH::Quat::sIdentity(), dt);
}

bool PhysicsSystem::are_colliding(entt::registry& registry, entt::entity a, entt::entity b) {
    auto* net_a = registry.try_get<ecs::NetworkId>(a);
    auto* net_b = registry.try_get<ecs::NetworkId>(b);
    if (!net_a || !net_b) {
        return false;
    }
    return impl_->contact_listener.HasContact(net_a->id, net_b->id);
}

void PhysicsSystem::set_collision_callback(CollisionCallback callback) {
    impl_->collision_callback = std::move(callback);
    impl_->contact_listener.SetCollisionCallback(impl_->collision_callback);
}

std::vector<ecs::CollisionEvent> PhysicsSystem::get_collision_events() const {
    return impl_->contact_listener.GetEventsCopy();
}

bool PhysicsSystem::raycast(const glm::vec3& origin, const glm::vec3& direction, float max_distance,
                            RaycastHit& out) const {
    out = RaycastHit{};
    if (!impl_->initialized) {
        return false;
    }

    glm::vec3 d = direction;
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len <= 1e-6f) {
        return false;
    }
    d /= len;

    JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z),
                      JPH::Vec3(d.x * max_distance, d.y * max_distance, d.z * max_distance));

    JPH::RayCastResult hit;
    if (!impl_->physics_system->GetNarrowPhaseQuery().CastRay(ray, hit)) {
        return false;
    }

    JPH::RVec3 hp = ray.GetPointOnRay(hit.mFraction);
    out.position =
        glm::vec3(static_cast<float>(hp.GetX()), static_cast<float>(hp.GetY()), static_cast<float>(hp.GetZ()));
    out.distance = max_distance * hit.mFraction;

    {
        JPH::BodyLockRead lock(impl_->physics_system->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            JPH::Vec3 n = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hp);
            out.normal = glm::vec3(n.GetX(), n.GetY(), n.GetZ());
        }
    }

    out.entity = entt::null;
    auto body_it = impl_->body_to_network.find(hit.mBodyID.GetIndex());
    if (body_it != impl_->body_to_network.end()) {
        auto entity_it = impl_->network_to_entity.find(body_it->second);
        if (entity_it != impl_->network_to_entity.end()) {
            out.entity = entity_it->second;
        }
    }
    out.hit = true;
    return true;
}

bool PhysicsSystem::overlap_sphere(const glm::vec3& center, float radius, std::vector<entt::entity>& out) const {
    out.clear();
    if (!impl_->initialized || radius <= 0.0f) {
        return false;
    }

    // Cache the SphereShape across calls; AoE / aggro queries repeat the
    // same radius and JPH::SphereShape construction allocates each time.
    if (!impl_->overlap_sphere_shape || impl_->overlap_sphere_cached_radius != radius) {
        impl_->overlap_sphere_shape = new JPH::SphereShape(radius);
        impl_->overlap_sphere_cached_radius = radius;
    }
    const JPH::Shape* sphere = impl_->overlap_sphere_shape.GetPtr();
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = 0.0f;
    settings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;

    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    JPH::RMat44 xform = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));

    impl_->physics_system->GetNarrowPhaseQuery().CollideShape(sphere, JPH::Vec3::sReplicate(1.0f), xform, settings,
                                                              JPH::RVec3(center.x, center.y, center.z), collector);

    std::unordered_set<uint32_t> seen;
    for (const auto& h : collector.mHits) {
        auto body_it = impl_->body_to_network.find(h.mBodyID2.GetIndex());
        if (body_it == impl_->body_to_network.end()) {
            continue;
        }
        if (!seen.insert(body_it->second).second) {
            continue;
        }
        auto ent_it = impl_->network_to_entity.find(body_it->second);
        if (ent_it != impl_->network_to_entity.end()) {
            out.push_back(ent_it->second);
        }
    }
    return !out.empty();
}

void PhysicsSystem::get_gravity(float& x, float& y, float& z) const {
    x = impl_->gravity_x;
    y = impl_->gravity_y;
    z = impl_->gravity_z;
}

void PhysicsSystem::set_gravity(float x, float y, float z) {
    impl_->gravity_x = x;
    impl_->gravity_y = y;
    impl_->gravity_z = z;

    if (impl_->initialized) {
        impl_->physics_system->SetGravity(JPH::Vec3(x, y, z));
    }
    for (auto& [entity, state] : impl_->characters) {
        state.gravity_y = y;
    }
}

void PhysicsSystem::update_body_shape(entt::registry& registry, entt::entity entity, float radius, float half_height) {
    if (!impl_->initialized) {
        return;
    }

    auto char_it = impl_->characters.find(entity);
    if (char_it != impl_->characters.end()) {
        JPH::Ref<JPH::Shape> shape = new JPH::CapsuleShape(half_height, radius);
        JPH::DefaultBroadPhaseLayerFilter bp_filter(impl_->object_vs_broadphase_filter, CollisionLayers::MOVING);
        JPH::DefaultObjectLayerFilter obj_filter(impl_->object_layer_pair_filter, CollisionLayers::MOVING);
        JPH::BodyFilter body_filter;
        JPH::ShapeFilter shape_filter;
        char_it->second.controller->SetShape(shape, 1.5f * char_it->second.controller->GetCharacterPadding(), bp_filter,
                                             obj_filter, body_filter, shape_filter, *impl_->temp_allocator);
        return;
    }

    auto* network_id = registry.try_get<ecs::NetworkId>(entity);
    if (!network_id) {
        return;
    }

    auto it = impl_->network_to_body.find(network_id->id);
    if (it == impl_->network_to_body.end()) {
        return;
    }

    auto* collider = registry.try_get<ecs::Collider>(entity);
    if (!collider) {
        return;
    }

    JPH::ShapeRefC shape;
    switch (collider->type) {
        case ecs::ColliderType::Capsule:
            shape = new JPH::CapsuleShape(half_height, radius);
            break;
        case ecs::ColliderType::Sphere:
            shape = new JPH::SphereShape(radius);
            break;
        default:
            return;
    }

    auto& body_interface = impl_->physics_system->GetBodyInterface();
    body_interface.SetShape(it->second, shape, true, JPH::EActivation::Activate);
}

void PhysicsSystem::optimize_broadphase() {
    if (impl_->initialized) {
        impl_->physics_system->OptimizeBroadPhase();
    }
}

void update_physics(PhysicsSystem& physics, entt::registry& registry, float dt) {
    physics.update(registry, dt);
}

} // namespace mmo::server::systems
