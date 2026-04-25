#pragma once

#include "player_repository.hpp"
#include <entt/entt.hpp>

namespace mmo::server::persistence {

// Bridge between live ECS state and PlayerSnapshot. Lives outside the
// repository so the repo stays free of game_components dependencies.

/// Build a PlayerSnapshot from the entity's components. Caller supplies the
/// player's display name (the persistence key).
PlayerSnapshot snapshot_from_entity(const entt::registry& registry,
                                    entt::entity entity,
                                    const std::string& name);

/// Apply a loaded snapshot onto an existing entity. Components that aren't
/// present yet are emplaced; existing ones are overwritten. Position/health
/// override the spawn defaults set by World::add_player.
void apply_snapshot_to_entity(entt::registry& registry,
                              entt::entity entity,
                              const PlayerSnapshot& snap);

} // namespace mmo::server::persistence
