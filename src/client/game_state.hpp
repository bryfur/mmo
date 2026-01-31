#pragma once

namespace mmo::client {

enum class GameState {
    Connecting,     // Connecting to server, waiting for ClassList
    ClassSelect,    // Server connected, choosing class
    Spawning,       // Class selected, waiting for player ID
    Playing
};

} // namespace mmo::client
