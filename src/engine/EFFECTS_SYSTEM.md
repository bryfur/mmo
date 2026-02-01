# Particle Effects System

A data-driven particle effects system for the engine. Effects are defined in JSON files and rendered using the existing model pipeline.

## Architecture

The effects system is split into two layers:

### Engine Layer (Generic, No File I/O)
- **effect_definition.hpp** - Data structures for effect definitions
- **effect_system.hpp/cpp** - Runtime particle system (update, physics, lifecycle)
- **effect_renderer.cpp** - Particle rendering using model pipeline

### Client/Game Layer (Game-Specific)
- **effect_loader.hpp/cpp** - JSON parsing and effect registry
- Game code manages when and where to spawn effects

## Core Components

### 1. Effect Definition (Data Structure)

```cpp
struct EffectDefinition {
    std::string name;
    std::vector<EmitterDefinition> emitters;
    float duration;
    float default_range;
    bool loop;
};

struct EmitterDefinition {
    std::string model;              // Model to render
    SpawnMode spawn_mode;            // BURST or CONTINUOUS
    int spawn_count;                 // Particles to spawn
    float particle_lifetime;         // Particle lifespan
    VelocityDefinition velocity;     // Motion behavior
    RotationDefinition rotation;     // Rotation behavior
    AppearanceDefinition appearance; // Visual properties
};
```

### 2. Effect System (Runtime)

```cpp
class EffectSystem {
public:
    // Spawn a new effect
    int spawn_effect(
        const EffectDefinition* definition,
        const glm::vec3& position,
        const glm::vec3& direction = {1, 0, 0},
        float range = -1.0f
    );

    // Update all active effects and particles
    void update(float dt,
                std::function<float(float, float)> get_terrain_height = nullptr);

    // Get effects for rendering
    const std::vector<EffectInstance>& get_effects() const;
};
```

### 3. Runtime Structures

```cpp
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 rotation;
    float scale;
    float opacity;
    glm::vec4 color;
    float age, lifetime;
    std::string model;
};

struct EmitterInstance {
    const EmitterDefinition* definition;
    std::vector<Particle> particles;
    float age;
    glm::vec3 origin;
    glm::vec3 direction;
};

struct EffectInstance {
    const EffectDefinition* definition;
    std::vector<EmitterInstance> emitters;
    float age;
};
```

## Usage Example

### 1. Load Effect Definitions (Client Layer)

```cpp
#include "client/effect_loader.hpp"

mmo::EffectRegistry effect_registry;

// Load all effects from directory
effect_registry.load_effects_directory("data/effects");

// Or load individual files
effect_registry.load_effect("data/effects/fireball.json");

// Get a definition
const engine::EffectDefinition* fireball = effect_registry.get_effect("fireball");
```

### 2. Create Effect System (Engine Layer)

```cpp
#include "engine/effect_system.hpp"

engine::EffectSystem effect_system;

// Spawn an effect
glm::vec3 position = {100, 0, 100};
glm::vec3 direction = {1, 0, 0};  // Facing direction
float range = 200.0f;

effect_system.spawn_effect(fireball, position, direction, range);
```

### 3. Update Effects (Game Loop)

```cpp
// In your game update loop
float dt = 0.016f;  // Delta time

// Optional: provide terrain height callback for ground collision
auto get_height = [](float x, float z) -> float {
    return terrain.get_height(x, z);
};

effect_system.update(dt, get_height);
```

### 4. Render Effects

```cpp
// In your render loop
effect_renderer.draw_particle_effects(
    render_pass,
    command_buffer,
    effect_system,
    view_matrix,
    projection_matrix,
    camera_position
);
```

## Velocity Types

### Directional
Particles move in the direction the effect was spawned with.
```cpp
velocity.type = VelocityType::DIRECTIONAL;
velocity.speed = 400.0f;
velocity.spread_angle = 15.0f;  // Random spread
```

### Radial
Particles shoot outward in random directions (explosions, bursts).
```cpp
velocity.type = VelocityType::RADIAL;
velocity.speed = 100.0f;
```

### Orbital
Particles orbit around the spawn point.
```cpp
velocity.type = VelocityType::ORBITAL;
velocity.orbit_radius = 50.0f;
velocity.orbit_speed = 2.0f;      // Rotations per second
velocity.height_variation = 10.0f; // Vertical oscillation
```

### Custom
Particles move in a hardcoded world-space direction.
```cpp
velocity.type = VelocityType::CUSTOM;
velocity.direction = glm::vec3(0, 1, 0);  // Always upward
velocity.speed = 100.0f;
```

## Curves

Properties can be animated over particle lifetime using curves:

```cpp
enum class CurveType {
    CONSTANT,      // Fixed value
    LINEAR,        // Linear interpolation
    EASE_IN,       // Accelerate
    EASE_OUT,      // Decelerate
    EASE_IN_OUT,   // Smooth both ends
    FADE_OUT_LATE, // Stay at start, fade near end
};

struct Curve {
    CurveType type;
    float start_value;
    float end_value;
    float fade_start;  // For FADE_OUT_LATE

    float evaluate(float t) const;  // t = 0.0 to 1.0
};
```

## JSON Format

See example effect files in `data/effects/`:
- `projectile.json` - Directional projectile with rotation
- `orbit.json` - Orbital particles
- `arrow.json` - Arcing projectile with gravity

### Minimal Example

```json
{
  "name": "fireball",
  "duration": 0.5,
  "emitters": [{
    "model": "spell_fireball",
    "spawn_mode": "burst",
    "spawn_count": 1,
    "lifetime": 0.5,
    "velocity": {
      "type": "directional",
      "speed": 400.0
    },
    "appearance": {
      "opacity_over_lifetime": {
        "type": "fade_out_late",
        "start": 1.0,
        "end": 0.0,
        "fade_start": 0.8
      }
    }
  }]
}
```

## Physics Features

- **Gravity**: `velocity.gravity = glm::vec3(0, -9.8, 0)`
- **Drag**: `velocity.drag = 0.1f` (velocity damping)
- **Face Velocity**: `rotation.face_velocity = true` (orient to movement)
- **Terrain Collision**: Particles snap to terrain height if callback provided

## Performance Considerations

- Particles are stored in vectors per emitter
- Dead particles are removed each frame
- Completed effects are automatically cleaned up
- Each particle renders one model (can be expensive for high counts)
- Consider GPU instancing for high particle counts (future optimization)

## Integration Checklist

- [ ] Add `EffectSystem` to game state
- [ ] Create `EffectRegistry` and load JSON definitions on startup
- [ ] Replace old effect spawning with `effect_system.spawn_effect()`
- [ ] Call `effect_system.update()` in game loop
- [ ] Call `effect_renderer.draw_particle_effects()` in render loop
- [ ] Add new source files to CMakeLists.txt
- [ ] Test with existing effect definitions

## Future Enhancements

- Particle collision with world geometry
- Particle-to-particle interactions
- GPU instancing for high particle counts
- Sprite particles (billboards) in addition to mesh particles
- Particle trails and ribbons
- Sub-emitters (particles that spawn more particles)
- Texture animation sequences
- Sound effects attached to particles
