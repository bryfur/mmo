#include <gtest/gtest.h>

#include "engine/core/hash.hpp"

#include <unordered_map>

using namespace mmo::engine::core;

TEST(Hash, Fnv1aEmptyStringReturnsOffsetBasis) {
    EXPECT_EQ(fnv1a(""), k_fnv1a_offset_basis_32);
    EXPECT_EQ(fnv1a64(""), k_fnv1a_offset_basis_64);
}

TEST(Hash, Fnv1aKnownVectors) {
    // Reference values from the canonical FNV-1a 32-bit specification.
    EXPECT_EQ(fnv1a("a"), 0xe40c292cu);
    EXPECT_EQ(fnv1a("foobar"), 0xbf9cf968u);
}

TEST(Hash, Fnv1a64KnownVectors) {
    EXPECT_EQ(fnv1a64("a"), 0xaf63dc4c8601ec8cull);
    EXPECT_EQ(fnv1a64("foobar"), 0x85944171f73967e8ull);
}

TEST(Hash, ConstexprEvaluates) {
    constexpr uint32_t h = fnv1a("constexpr");
    static_assert(h != 0);
    EXPECT_EQ(h, fnv1a("constexpr"));
}

TEST(StringId, EqualityFromSameString) {
    StringId a("render");
    StringId b("render");
    EXPECT_EQ(a, b);
}

TEST(StringId, DifferentStringsDiffer) {
    StringId a("render");
    StringId b("physics");
    EXPECT_NE(a, b);
}

TEST(StringId, LiteralMatchesConstructor) {
    constexpr StringId lit = "abc"_sid;
    StringId ctor("abc");
    EXPECT_EQ(lit, ctor);
}

TEST(StringId, UsableAsHashKey) {
    std::unordered_map<StringId, int> m;
    m[StringId("alpha")] = 1;
    m[StringId("beta")] = 2;
    EXPECT_EQ(m[StringId("alpha")], 1);
    EXPECT_EQ(m[StringId("beta")], 2);
}
