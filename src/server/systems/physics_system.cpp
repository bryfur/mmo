// JoltPhysics integration for MMO server
// Must include Jolt.h first before any other Jolt headers

#include <Jolt/Jolt.h>

// Jolt includes
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>

#include "physics_system.hpp"
#include <unordered_map>
#include <iostream>
#include <thread>

// Disable common warnings from Jolt
JPH_SUPPRESS_WARNINGS

namespace mmo::systems {

// Jolt memory allocation hooks
static void* JoltAlloc(size_t inSize) {
    return malloc(inSize);
}

static void* JoltAlignedAlloc(size_t inSize, size_t inAlignment) {
    return aligned_alloc(inAlignment, inSize);
}

static void JoltFree(void* inBlock) {
    free(inBlock);
}

static void JoltAlignedFree(void* inBlock) {
    free(inBlock);
}

// Broadphase layer interface
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        object_to_broad_phase_[CollisionLayers::NON_MOVING] = JPH::BroadPhaseLayer(0);
        object_to_broad_phase_[CollisionLayers::MOVING] = JPH::BroadPhaseLayer(1);
        object_to_broad_phase_[CollisionLayers::TRIGGER] = JPH::BroadPhaseLayer(1);
    }

    uint32_t GetNumBroadPhaseLayers() const override {
        return 2;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return object_to_broad_phase_[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case 0: return "NON_MOVING";
            case 1: return "MOVING";
            default: return "UNKNOWN";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer object_to_broad_phase_[CollisionLayers::NUM_LAYERS];
};

// Object vs broadphase layer filter
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case CollisionLayers::NON_MOVING:
                return inLayer2 == JPH::BroadPhaseLayer(1); // Only collide with moving
            case CollisionLayers::MOVING:
                return true; // Collide with everything
            case CollisionLayers::TRIGGER:
                return inLayer2 == JPH::BroadPhaseLayer(1); // Only detect moving objects
            default:
                return false;
        }
    }
};

// Object layer pair filter
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
            case CollisionLayers::NON_MOVING:
                return inObject2 == CollisionLayers::MOVING; // Static only with dynamic
            case CollisionLayers::MOVING:
                return true; // Dynamic with everything
            case CollisionLayers::TRIGGER:
                return inObject2 == CollisionLayers::MOVING; // Triggers only detect moving
            default:
                return false;
        }
    }
};

// Body activation listener
class BodyActivationListenerImpl : public JPH::BodyActivationListener {
public:
    void OnBodyActivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override {
        // Could track active bodies here
    }

    void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override {
        // Could track deactivated bodies here
    }
};

// Contact listener for collision events
class ContactListenerImpl : public JPH::ContactListener {
public:
    void SetCollisionCallback(CollisionCallback callback) {
        callback_ = std::move(callback);
    }

    void ClearEvents() {
        events_.clear();
    }

    const std::vector<ecs::CollisionEvent>& GetEvents() const {
        return events_;
    }

    JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                           JPH::RVec3Arg inBaseOffset,
                                           const JPH::CollideShapeResult& inCollisionResult) override {
        // Accept all contacts
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                       const JPH::ContactManifold& inManifold,
                       JPH::ContactSettings& ioSettings) override {
        RecordCollision(inBody1, inBody2, inManifold);
    }

    void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                           const JPH::ContactManifold& inManifold,
                           JPH::ContactSettings& ioSettings) override {
        // Could track persistent contacts if needed
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override {
        // Contact ended
    }

private:
    void RecordCollision(const JPH::Body& body1, const JPH::Body& body2,
                        const JPH::ContactManifold& manifold) {
        ecs::CollisionEvent event;
        event.entity_a_network_id = static_cast<uint32_t>(body1.GetUserData());
        event.entity_b_network_id = static_cast<uint32_t>(body2.GetUserData());
        
        // Get contact point (average of contact points)
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
        
        events_.push_back(event);
    }

    CollisionCallback callback_;
    std::vector<ecs::CollisionEvent> events_;
};

// Physics system implementation
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
    
    // Maps network ID to Jolt BodyID and back
    std::unordered_map<uint32_t, JPH::BodyID> network_to_body;
    std::unordered_map<uint32_t, uint32_t> body_to_network; // BodyID index to network ID
    
    // Maps entt entity to network ID for quick lookup
    std::unordered_map<entt::entity, uint32_t> entity_to_network;
    
    float gravity_x = 0.0f;
    float gravity_y = -9.81f;  // Default gravity (can be 0 for 2D top-down)
    float gravity_z = 0.0f;
    
    CollisionCallback collision_callback;
    
    // Terrain height callback for ground snapping
    PhysicsSystem::TerrainHeightCallback terrain_height_callback;
};

PhysicsSystem::PhysicsSystem() : impl_(std::make_unique<Impl>()) {}

PhysicsSystem::~PhysicsSystem() {
    if (impl_->initialized) {
        shutdown();
    }
}

void PhysicsSystem::initialize(uint32_t max_bodies, uint32_t max_body_pairs,
                               uint32_t max_contact_constraints) {
    if (impl_->initialized) {
        return;
    }

    // Register Jolt allocation hooks
    JPH::RegisterDefaultAllocator();
    
    // Create factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt types
    JPH::RegisterTypes();

    // Create temp allocator (10MB)
    impl_->temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

    // Create job system with threads
    int num_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    impl_->job_system = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, num_threads);

    // Create physics system
    impl_->physics_system = std::make_unique<JPH::PhysicsSystem>();
    impl_->physics_system->Init(
        max_bodies,
        0,  // No body mutexes needed for single-threaded access pattern
        max_body_pairs,
        max_contact_constraints,
        impl_->broad_phase_layer_interface,
        impl_->object_vs_broadphase_filter,
        impl_->object_layer_pair_filter
    );

    // Set gravity (0 for top-down 2D game, or low for platformer feel)
    impl_->physics_system->SetGravity(JPH::Vec3(impl_->gravity_x, impl_->gravity_y, impl_->gravity_z));

    // Set listeners
    impl_->physics_system->SetBodyActivationListener(&impl_->body_activation_listener);
    impl_->physics_system->SetContactListener(&impl_->contact_listener);

    impl_->initialized = true;
}

void PhysicsSystem::shutdown() {
    if (!impl_->initialized) {
        return;
    }

    // Remove all bodies
    auto& body_interface = impl_->physics_system->GetBodyInterface();
    for (auto& [network_id, body_id] : impl_->network_to_body) {
        body_interface.RemoveBody(body_id);
        body_interface.DestroyBody(body_id);
    }
    impl_->network_to_body.clear();
    impl_->body_to_network.clear();
    impl_->entity_to_network.clear();

    // Destroy systems in reverse order
    impl_->physics_system.reset();
    impl_->job_system.reset();
    impl_->temp_allocator.reset();

    // Destroy factory
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    // Unregister types
    JPH::UnregisterTypes();

    impl_->initialized = false;
}

void PhysicsSystem::create_bodies(entt::registry& registry) {
    if (!impl_->initialized) {
        return;
    }

    auto& body_interface = impl_->physics_system->GetBodyInterface();

    // Find entities with Collider + RigidBody but no PhysicsBody yet
    auto view = registry.view<ecs::Transform, ecs::Collider, ecs::RigidBody, ecs::NetworkId>(
        entt::exclude<ecs::PhysicsBody>);

    for (auto entity : view) {
        const auto& transform = view.get<ecs::Transform>(entity);
        const auto& collider = view.get<ecs::Collider>(entity);
        const auto& rigid_body = view.get<ecs::RigidBody>(entity);
        const auto& network_id = view.get<ecs::NetworkId>(entity);

        // Create shape based on collider type
        JPH::ShapeRefC shape;
        switch (collider.type) {
            case ecs::ColliderType::Sphere:
                shape = new JPH::SphereShape(collider.radius);
                break;
            case ecs::ColliderType::Box:
                shape = new JPH::BoxShape(JPH::Vec3(
                    collider.half_extents_x, 
                    collider.half_extents_y, 
                    collider.half_extents_z));
                break;
            case ecs::ColliderType::Capsule:
                shape = new JPH::CapsuleShape(collider.half_height, collider.radius);
                break;
            case ecs::ColliderType::Cylinder:
                shape = new JPH::CylinderShape(collider.half_height, collider.radius);
                break;
            default:
                shape = new JPH::SphereShape(collider.radius);
                break;
        }

        // Determine motion type and layer
        JPH::EMotionType motion_type;
        JPH::ObjectLayer layer;
        
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

        // Convert to 3D position for Jolt:
        // - transform.x = world X
        // - transform.y = world Z (horizontal)  
        // - transform.z = world Y (height/elevation)
        // Add collider.offset_y to elevate the collision shape (e.g., for capsules centered at character midpoint)
        JPH::RVec3 position(transform.x, transform.z + collider.offset_y, transform.y);
        
        // Create body settings
        JPH::BodyCreationSettings settings(
            shape,
            position,
            JPH::Quat::sIdentity(),
            motion_type,
            layer
        );

        // Set physics properties
        settings.mFriction = rigid_body.friction;
        settings.mRestitution = rigid_body.restitution;
        settings.mLinearDamping = rigid_body.linear_damping;
        settings.mAngularDamping = rigid_body.angular_damping;
        
        if (rigid_body.motion_type == ecs::PhysicsMotionType::Dynamic) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = rigid_body.mass;
        }

        // Lock rotation for characters
        if (rigid_body.lock_rotation) {
            settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | 
                                    JPH::EAllowedDOFs::TranslationY | 
                                    JPH::EAllowedDOFs::TranslationZ;
        }

        // Store network ID in user data for collision callbacks
        settings.mUserData = network_id.id;

        // Create the body
        JPH::Body* body = body_interface.CreateBody(settings);
        if (body == nullptr) {
            std::cerr << "[Physics] Failed to create body for entity " << network_id.id << std::endl;
            continue;
        }

        // Add to world
        body_interface.AddBody(body->GetID(), 
            motion_type == JPH::EMotionType::Static ? 
            JPH::EActivation::DontActivate : JPH::EActivation::Activate);

        // Track mappings
        impl_->network_to_body[network_id.id] = body->GetID();
        impl_->body_to_network[body->GetID().GetIndex()] = network_id.id;
        impl_->entity_to_network[entity] = network_id.id;

        // Add PhysicsBody component
        registry.emplace<ecs::PhysicsBody>(entity, body->GetID().GetIndexAndSequenceNumber(), true);
    }
}

void PhysicsSystem::destroy_body(entt::registry& registry, entt::entity entity) {
    if (!impl_->initialized) {
        return;
    }

    auto* physics_body = registry.try_get<ecs::PhysicsBody>(entity);
    auto* network_id = registry.try_get<ecs::NetworkId>(entity);
    
    if (!physics_body || !network_id) {
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
    
    impl_->entity_to_network.erase(entity);
    registry.remove<ecs::PhysicsBody>(entity);
}

void PhysicsSystem::update(entt::registry& registry, float dt) {
    if (!impl_->initialized) {
        return;
    }

    // Clear previous collision events
    impl_->contact_listener.ClearEvents();

    // First, create bodies for new entities
    create_bodies(registry);

    auto& body_interface = impl_->physics_system->GetBodyInterface();
    
    // Handle teleport requests (e.g., respawns) - must happen before velocity updates
    auto teleport_view = registry.view<ecs::Transform, ecs::PhysicsBody, ecs::NetworkId>();
    for (auto entity : teleport_view) {
        auto& physics_body = teleport_view.get<ecs::PhysicsBody>(entity);
        if (!physics_body.needs_teleport) {
            continue;
        }
        
        const auto& transform = teleport_view.get<ecs::Transform>(entity);
        const auto& network_id = teleport_view.get<ecs::NetworkId>(entity);
        
        auto it = impl_->network_to_body.find(network_id.id);
        if (it != impl_->network_to_body.end()) {
            // Teleport body to new position (x, z->Y height, y->Z) and reset velocity
            JPH::RVec3 new_pos(transform.x, transform.z, transform.y);
            body_interface.SetPosition(it->second, new_pos, JPH::EActivation::Activate);
            body_interface.SetLinearVelocity(it->second, JPH::Vec3::sZero());
        }
        
        physics_body.needs_teleport = false;
    }
    
    // Update dynamic bodies - apply ECS velocity to physics bodies
    auto dynamic_view = registry.view<ecs::Transform, ecs::PhysicsBody, ecs::RigidBody, ecs::Velocity, ecs::NetworkId>();
    for (auto entity : dynamic_view) {
        const auto& rigid_body = dynamic_view.get<ecs::RigidBody>(entity);
        if (rigid_body.motion_type != ecs::PhysicsMotionType::Dynamic) {
            continue;
        }

        const auto& velocity = dynamic_view.get<ecs::Velocity>(entity);
        const auto& network_id = dynamic_view.get<ecs::NetworkId>(entity);

        auto it = impl_->network_to_body.find(network_id.id);
        if (it != impl_->network_to_body.end()) {
            // Set linear velocity: game (x, y, z) -> Jolt (x, z, y)
            // velocity.z is vertical (gravity/jumping), maps to Jolt Y
            JPH::Vec3 phys_vel(velocity.x, velocity.z, velocity.y);
            body_interface.SetLinearVelocity(it->second, phys_vel);
            
            // Activate the body if it has velocity
            if (velocity.x != 0.0f || velocity.y != 0.0f || velocity.z != 0.0f) {
                body_interface.ActivateBody(it->second);
            }
        }
    }

    // Update kinematic body positions from ECS transforms (for AI-controlled without velocity)
    auto kinematic_view = registry.view<ecs::Transform, ecs::PhysicsBody, ecs::RigidBody>();
    for (auto entity : kinematic_view) {
        const auto& rigid_body = kinematic_view.get<ecs::RigidBody>(entity);
        if (rigid_body.motion_type != ecs::PhysicsMotionType::Kinematic) {
            continue;
        }

        const auto& transform = kinematic_view.get<ecs::Transform>(entity);
        const auto* network_id = registry.try_get<ecs::NetworkId>(entity);
        if (!network_id) continue;

        auto it = impl_->network_to_body.find(network_id->id);
        if (it != impl_->network_to_body.end()) {
            // Move kinematic body to new position: game (x, y, z) -> Jolt (x, z, y)
            JPH::RVec3 new_pos(transform.x, transform.z, transform.y);
            body_interface.MoveKinematic(it->second, new_pos, JPH::Quat::sIdentity(), dt);
        }
    }

    // Step physics
    const int collision_steps = 1;
    impl_->physics_system->Update(dt, collision_steps, 
                                   impl_->temp_allocator.get(), 
                                   impl_->job_system.get());

    // Sync transforms back to ECS for dynamic bodies
    sync_transforms(registry);

    // Fire collision callbacks
    if (impl_->collision_callback) {
        for (const auto& event : impl_->contact_listener.GetEvents()) {
            // Find entities by network ID
            entt::entity entity_a = entt::null;
            entt::entity entity_b = entt::null;
            
            auto net_view = registry.view<ecs::NetworkId>();
            for (auto e : net_view) {
                const auto& net = net_view.get<ecs::NetworkId>(e);
                if (net.id == event.entity_a_network_id) entity_a = e;
                if (net.id == event.entity_b_network_id) entity_b = e;
            }
            
            if (entity_a != entt::null && entity_b != entt::null) {
                impl_->collision_callback(entity_a, entity_b, event);
            }
        }
    }
}

void PhysicsSystem::sync_transforms(entt::registry& registry) {
    if (!impl_->initialized) {
        return;
    }

    auto& body_interface = impl_->physics_system->GetBodyInterface();
    
    auto view = registry.view<ecs::Transform, ecs::PhysicsBody, ecs::RigidBody, ecs::NetworkId, ecs::Collider>();
    for (auto entity : view) {
        const auto& rigid_body = view.get<ecs::RigidBody>(entity);
        auto& physics_body = view.get<ecs::PhysicsBody>(entity);
        
        // Only sync dynamic bodies back to ECS
        if (rigid_body.motion_type != ecs::PhysicsMotionType::Dynamic || !physics_body.needs_sync) {
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
        float new_x = static_cast<float>(position.GetX());
        float new_y = static_cast<float>(position.GetZ()); // Jolt Z -> Game Y (horizontal)
        // Jolt Y is the collision center height, subtract offset_y to get ground-level transform.z
        float physics_center_z = static_cast<float>(position.GetY()); // Jolt Y -> collision center
        float new_z = physics_center_z - collider.offset_y; // Convert to ground level
        
        // Apply terrain height snapping if callback is set
        // This keeps entities locked to terrain height (no jumping/falling)
        if (impl_->terrain_height_callback) {
            float terrain_height = impl_->terrain_height_callback(new_x, new_y);
            
            // Always snap entity to terrain height (ground-locked movement)
            // For jumping/falling games, you'd only snap when new_z < terrain_height
            new_z = terrain_height;
            
            // Update physics body position to match (collision center at terrain + offset)
            float corrected_center_z = new_z + collider.offset_y;
            JPH::RVec3 corrected_pos(new_x, corrected_center_z, new_y);
            body_interface.SetPosition(it->second, corrected_pos, JPH::EActivation::DontActivate);
            
            // Zero out vertical velocity since we're ground-locked
            JPH::Vec3 vel = body_interface.GetLinearVelocity(it->second);
            if (vel.GetY() != 0) {
                body_interface.SetLinearVelocity(it->second, JPH::Vec3(vel.GetX(), 0.0f, vel.GetZ()));
            }
        }
        
        transform.x = new_x;
        transform.y = new_y;
        transform.z = new_z;
    }
}

void PhysicsSystem::apply_impulse(entt::registry& registry, entt::entity entity,
                                   float impulse_x, float impulse_y, float impulse_z) {
    if (!impl_->initialized) {
        return;
    }

    auto* network_id = registry.try_get<ecs::NetworkId>(entity);
    if (!network_id) return;

    auto it = impl_->network_to_body.find(network_id->id);
    if (it == impl_->network_to_body.end()) return;

    auto& body_interface = impl_->physics_system->GetBodyInterface();
    body_interface.AddImpulse(it->second, JPH::Vec3(impulse_x, impulse_y, impulse_z));
}

void PhysicsSystem::set_velocity(entt::registry& registry, entt::entity entity,
                                  float vel_x, float vel_y, float vel_z) {
    if (!impl_->initialized) {
        return;
    }

    auto* network_id = registry.try_get<ecs::NetworkId>(entity);
    if (!network_id) return;

    auto it = impl_->network_to_body.find(network_id->id);
    if (it == impl_->network_to_body.end()) return;

    auto& body_interface = impl_->physics_system->GetBodyInterface();
    body_interface.SetLinearVelocity(it->second, JPH::Vec3(vel_x, vel_y, vel_z));
}

void PhysicsSystem::move_kinematic(entt::registry& registry, entt::entity entity,
                                    float x, float y, float z, float dt) {
    if (!impl_->initialized) {
        return;
    }

    auto* network_id = registry.try_get<ecs::NetworkId>(entity);
    if (!network_id) return;

    auto it = impl_->network_to_body.find(network_id->id);
    if (it == impl_->network_to_body.end()) return;

    auto& body_interface = impl_->physics_system->GetBodyInterface();
    body_interface.MoveKinematic(it->second, JPH::RVec3(x, y, z), JPH::Quat::sIdentity(), dt);
}

bool PhysicsSystem::are_colliding(entt::registry& registry, entt::entity a, entt::entity b) {
    // Check recent collision events
    auto* net_a = registry.try_get<ecs::NetworkId>(a);
    auto* net_b = registry.try_get<ecs::NetworkId>(b);
    if (!net_a || !net_b) return false;

    for (const auto& event : impl_->contact_listener.GetEvents()) {
        if ((event.entity_a_network_id == net_a->id && event.entity_b_network_id == net_b->id) ||
            (event.entity_a_network_id == net_b->id && event.entity_b_network_id == net_a->id)) {
            return true;
        }
    }
    return false;
}

void PhysicsSystem::set_collision_callback(CollisionCallback callback) {
    impl_->collision_callback = std::move(callback);
    impl_->contact_listener.SetCollisionCallback(impl_->collision_callback);
}

const std::vector<ecs::CollisionEvent>& PhysicsSystem::get_collision_events() const {
    return impl_->contact_listener.GetEvents();
}

bool PhysicsSystem::ray_cast(float origin_x, float origin_y, float origin_z,
                              float dir_x, float dir_y, float dir_z,
                              float max_distance,
                              entt::entity& out_hit_entity,
                              float& out_hit_x, float& out_hit_y, float& out_hit_z) {
    if (!impl_->initialized) {
        return false;
    }

    JPH::RRayCast ray(
        JPH::RVec3(origin_x, origin_y, origin_z),
        JPH::Vec3(dir_x * max_distance, dir_y * max_distance, dir_z * max_distance)
    );

    JPH::RayCastResult hit;
    if (impl_->physics_system->GetNarrowPhaseQuery().CastRay(ray, hit)) {
        JPH::RVec3 hit_point = ray.GetPointOnRay(hit.mFraction);
        out_hit_x = static_cast<float>(hit_point.GetX());
        out_hit_y = static_cast<float>(hit_point.GetY());
        out_hit_z = static_cast<float>(hit_point.GetZ());
        
        // Find entity by body ID
        auto body_it = impl_->body_to_network.find(hit.mBodyID.GetIndex());
        if (body_it != impl_->body_to_network.end()) {
            // We'd need to look up the entity by network ID
            // For now, set to null - caller can use network ID from collision event
            out_hit_entity = entt::null;
        }
        
        return true;
    }

    return false;
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
}

void PhysicsSystem::optimize_broadphase() {
    if (impl_->initialized) {
        impl_->physics_system->OptimizeBroadPhase();
    }
}

void PhysicsSystem::set_terrain_height_callback(TerrainHeightCallback callback) {
    impl_->terrain_height_callback = std::move(callback);
}

// Convenience function
void update_physics(PhysicsSystem& physics, entt::registry& registry, float dt) {
    physics.update(registry, dt);
}

} // namespace mmo::systems
