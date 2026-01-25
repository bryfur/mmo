#pragma once

#include "client/scene/render_scene.hpp"
#include "client/scene/ui_scene.hpp"
#include "common/ecs/components.hpp"
#include <entt/entt.hpp>
#include <cstring>

namespace mmo {

/**
 * RenderSystem collects renderable entities from the ECS registry
 * and populates RenderScene with the data needed to render them.
 * 
 * This decouples ECS queries from the Renderer - the Game uses
 * RenderSystem to populate scenes, then passes scenes to Renderer.
 */
class RenderSystem {
public:
    RenderSystem() = default;
    ~RenderSystem() = default;
    
    /**
     * Collect all renderable entities and add them to the render scene.
     * Call this after game logic updates, before rendering.
     * 
     * @param registry The ECS registry containing entities
     * @param scene The render scene to populate
     * @param local_player_id Network ID of the local player
     */
    void collect_entities(entt::registry& registry, RenderScene& scene, uint32_t local_player_id);
    
    /**
     * Collect entity shadows for the shadow pass.
     * 
     * @param registry The ECS registry containing entities
     * @param scene The render scene to populate with shadow commands
     */
    void collect_entity_shadows(entt::registry& registry, RenderScene& scene);
};

} // namespace mmo
