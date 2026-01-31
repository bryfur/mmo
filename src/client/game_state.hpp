#pragma once

namespace mmo {

enum class GameState {
    Connecting,     // Connecting to server, waiting for ClassList
    ClassSelect,    // Server connected, choosing class
    Spawning,       // Class selected, waiting for player ID
    Playing
};

} // namespace mmo
