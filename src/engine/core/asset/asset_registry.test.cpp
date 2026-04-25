#include <gtest/gtest.h>

#include "engine/core/asset/asset_registry.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using mmo::engine::core::asset::AssetRegistry;

namespace {

struct StubAsset {
    int value = 0;
    bool reload_from(const fs::path& p) {
        std::ifstream in(p);
        if (!in) return false;
        in >> value;
        return true;
    }
};

fs::path tmp_path(const std::string& name) {
    auto p = fs::temp_directory_path() /
             ("mmo_assetreg_" + std::to_string(::getpid()) + "_" + name);
    return p;
}

void write_int(const fs::path& p, int v) {
    std::ofstream o(p, std::ios::trunc);
    o << v;
}

} // namespace

TEST(AssetRegistry, LoadAndGet) {
    auto path = tmp_path("a");
    write_int(path, 42);

    AssetRegistry<StubAsset> reg;
    auto h = reg.load("a", path);
    ASSERT_TRUE(h.is_valid());

    auto* a = reg.get(h);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->value, 42);

    fs::remove(path);
}

TEST(AssetRegistry, ReloadUpdatesValue) {
    auto path = tmp_path("b");
    write_int(path, 1);

    AssetRegistry<StubAsset> reg;
    auto h = reg.load("b", path);
    ASSERT_TRUE(h.is_valid());
    EXPECT_EQ(reg.get(h)->value, 1);

    write_int(path, 7);
    EXPECT_TRUE(reg.reload(h));
    EXPECT_EQ(reg.get(h)->value, 7);

    fs::remove(path);
}

TEST(AssetRegistry, FindByName) {
    auto path = tmp_path("c");
    write_int(path, 9);

    AssetRegistry<StubAsset> reg;
    auto h = reg.load("named", path);
    ASSERT_TRUE(h.is_valid());

    auto h2 = reg.find("named");
    EXPECT_EQ(h, h2);
    EXPECT_EQ(reg.find("missing").is_valid(), false);

    fs::remove(path);
}

TEST(AssetRegistry, UnloadInvalidatesHandle) {
    auto path = tmp_path("d");
    write_int(path, 5);

    AssetRegistry<StubAsset> reg;
    auto h = reg.load("d", path);
    ASSERT_TRUE(h.is_valid());

    reg.unload(h);
    EXPECT_EQ(reg.get(h), nullptr);

    fs::remove(path);
}

TEST(AssetRegistry, CustomLoadFn) {
    auto path = tmp_path("e");
    write_int(path, 0);

    AssetRegistry<StubAsset> reg;
    reg.set_load_fn([](const fs::path&, StubAsset& out) {
        out.value = 1234;
        return true;
    });
    auto h = reg.load("e", path);
    ASSERT_TRUE(h.is_valid());
    EXPECT_EQ(reg.get(h)->value, 1234);

    fs::remove(path);
}

TEST(AssetRegistry, FailedLoadReturnsInvalid) {
    AssetRegistry<StubAsset> reg;
    auto h = reg.load("ghost", tmp_path("does_not_exist"));
    EXPECT_FALSE(h.is_valid());
}
