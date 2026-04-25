// Force the static_asserts in component_contracts.hpp to be evaluated.

#include "server/ecs/component_contracts.hpp"
#include "server/math/movement_speed.hpp" // also asserts Formula<MovementSpeed>

#include <gtest/gtest.h>

TEST(SystemContracts, AllExpectedTickableStateComponentsConform) {
    SUCCEED() << "static_asserts in component_contracts.hpp pin this down.";
}

TEST(SystemContracts, MovementSpeedConformsToFormula) {
    SUCCEED() << "static_assert(Formula<MovementSpeed>) in movement_speed.hpp.";
}
