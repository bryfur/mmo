// Pull in the contract asserts so they are evaluated as part of the test
// binary compilation. The asserts live in contracts.hpp; this translation
// unit exists just to force the compiler to see them.

#include "protocol/contracts.hpp"

#include <gtest/gtest.h>

// One placeholder test so gtest_main links happy. The real value of this
// file is the compile-time static_asserts in contracts.hpp.
TEST(ProtocolContracts, AllMessageTypesSatisfyNetMessage) {
    SUCCEED() << "Compile-time static_asserts in contracts.hpp pin this down.";
}
