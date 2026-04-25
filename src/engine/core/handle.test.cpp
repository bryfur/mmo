#include <gtest/gtest.h>

#include "engine/core/handle.hpp"

#include <string>
#include <unordered_set>

using namespace mmo::engine::core;

namespace {
struct MeshTag {};
struct TexTag {};
} // namespace

TEST(Handle, InvalidIsNotValid) {
    auto h = Handle<MeshTag>::invalid();
    EXPECT_FALSE(h.is_valid());
}

TEST(Handle, EqualityCompares) {
    Handle<MeshTag> a{1, 2};
    Handle<MeshTag> b{1, 2};
    Handle<MeshTag> c{1, 3};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(Handle, HashSpecializationCompiles) {
    std::unordered_set<Handle<MeshTag>> set;
    set.insert(Handle<MeshTag>{1, 1});
    set.insert(Handle<MeshTag>{2, 1});
    set.insert(Handle<MeshTag>{1, 1});
    EXPECT_EQ(set.size(), 2u);
}

TEST(HandlePool, AcquireReturnsLiveHandle) {
    HandlePool<MeshTag, std::string> pool;
    auto h = pool.acquire("hello");
    EXPECT_TRUE(h.is_valid());
    auto* ptr = pool.get(h);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, "hello");
}

TEST(HandlePool, ReleaseInvalidatesOldHandle) {
    HandlePool<MeshTag, std::string> pool;
    auto h = pool.acquire("a");
    EXPECT_TRUE(pool.release(h));
    EXPECT_EQ(pool.get(h), nullptr);
}

TEST(HandlePool, GenerationalReuseDetectsStale) {
    HandlePool<MeshTag, int> pool;
    auto h1 = pool.acquire(10);
    auto idx1 = h1.index;
    pool.release(h1);

    auto h2 = pool.acquire(20);
    EXPECT_EQ(h2.index, idx1);               // index recycled
    EXPECT_NE(h2.generation, h1.generation); // generation bumped

    EXPECT_EQ(pool.get(h1), nullptr); // stale handle returns null
    auto* live = pool.get(h2);
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(*live, 20);
}

TEST(HandlePool, MultipleAcquireAndContains) {
    HandlePool<MeshTag, int> pool;
    auto a = pool.acquire(1);
    auto b = pool.acquire(2);
    auto c = pool.acquire(3);
    EXPECT_TRUE(pool.contains(a));
    EXPECT_TRUE(pool.contains(b));
    EXPECT_TRUE(pool.contains(c));
    EXPECT_EQ(pool.size(), 3u);
    pool.release(b);
    EXPECT_FALSE(pool.contains(b));
    EXPECT_EQ(pool.size(), 2u);
}

TEST(HandlePool, DoubleReleaseFails) {
    HandlePool<MeshTag, int> pool;
    auto h = pool.acquire(7);
    EXPECT_TRUE(pool.release(h));
    EXPECT_FALSE(pool.release(h));
}

TEST(HandlePool, TagsAreTypeSafe) {
    Handle<MeshTag> mh{0, 1};
    Handle<TexTag> th{0, 1};
    static_assert(!std::is_same_v<decltype(mh), decltype(th)>);
}
