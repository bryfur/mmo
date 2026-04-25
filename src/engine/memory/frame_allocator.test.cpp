#include <gtest/gtest.h>

#include "engine/memory/frame_allocator.hpp"

using namespace mmo::engine::memory;

TEST(FrameAllocator, SwapFlipsThisAndLast) {
    FrameAllocator fa(1024);
    void* a = fa.this_frame().allocate(32, 8);
    ASSERT_NE(a, nullptr);
    EXPECT_GE(fa.this_frame().bytes_used(), 32u);

    fa.swap();
    EXPECT_GE(fa.last_frame().bytes_used(), 32u);
    EXPECT_EQ(fa.this_frame().bytes_used(), 0u);

    void* b = fa.this_frame().allocate(64, 8);
    ASSERT_NE(b, nullptr);
    EXPECT_GE(fa.this_frame().bytes_used(), 64u);
}

TEST(FrameAllocator, SwapResetsNewFrame) {
    FrameAllocator fa(1024);
    fa.this_frame().allocate(100, 8);
    fa.swap();
    fa.this_frame().allocate(50, 8);
    fa.swap();
    EXPECT_EQ(fa.this_frame().bytes_used(), 0u);
}

TEST(FrameAllocator, ContentSurvivesOneFrame) {
    FrameAllocator fa(1024);
    int* p = fa.this_frame().create<int>(123);
    ASSERT_NE(p, nullptr);
    *p = 555;
    fa.swap();
    EXPECT_EQ(*p, 555);
    fa.swap();
    // After two swaps, the original arena is reset; pointer is invalid by contract.
    // We only verified one-frame survival above.
}
