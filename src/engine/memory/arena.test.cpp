#include <gtest/gtest.h>

#include "engine/memory/arena.hpp"

#include <cstdint>
#include <vector>

using namespace mmo::engine::memory;

TEST(Arena, AllocateRespectsAlignment) {
    Arena a(4096);
    void* p1 = a.allocate(1, 1);
    void* p16 = a.allocate(8, 16);
    void* p64 = a.allocate(4, 64);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p16, nullptr);
    ASSERT_NE(p64, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p16) % 16u, 0u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p64) % 64u, 0u);
}

TEST(Arena, ResetRewindsUsage) {
    Arena a(1024);
    a.allocate(256, 8);
    EXPECT_GE(a.bytes_used(), 256u);
    a.reset();
    EXPECT_EQ(a.bytes_used(), 0u);
}

TEST(Arena, GrowsAcrossMultipleChunks) {
    Arena a(64);
    std::vector<void*> ptrs;
    for (int i = 0; i < 32; ++i) {
        void* p = a.allocate(16, 8);
        ASSERT_NE(p, nullptr);
        ptrs.push_back(p);
    }
    EXPECT_GE(a.bytes_capacity(), 64u);
    EXPECT_GE(a.bytes_used(), 32u * 16u);
}

TEST(Arena, FixedBufferDoesNotOverrun) {
    alignas(16) std::byte buf[128];
    Arena a(buf, sizeof(buf));
    void* p = a.allocate(64, 8);
    ASSERT_NE(p, nullptr);
    void* q = a.allocate(64, 8);
    ASSERT_NE(q, nullptr);
    void* r = a.allocate(1, 1);
    EXPECT_EQ(r, nullptr); // out of fixed budget
}

TEST(Arena, CreateInvokesConstructorAndDestructor) {
    static int alive = 0;
    struct Tracked {
        Tracked() { ++alive; }
        ~Tracked() { --alive; }
        int v = 7;
    };
    {
        Arena a(1024);
        auto* t = a.create<Tracked>();
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->v, 7);
        EXPECT_EQ(alive, 1);
    }
    EXPECT_EQ(alive, 0);
}

TEST(Arena, ResetRunsDestructors) {
    static int alive = 0;
    struct Tracked {
        Tracked() { ++alive; }
        ~Tracked() { --alive; }
    };
    Arena a(1024);
    a.create<Tracked>();
    a.create<Tracked>();
    EXPECT_EQ(alive, 2);
    a.reset();
    EXPECT_EQ(alive, 0);
}

TEST(Arena, MoveTransfersOwnership) {
    Arena a(1024);
    a.allocate(64, 8);
    Arena b = std::move(a);
    EXPECT_GE(b.bytes_used(), 64u);
    // Intentional use of moved-from `a` to verify the post-move contract:
    // a moved-from Arena must still observe as empty (valid but empty state).
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(a.bytes_used(), 0u);
}
