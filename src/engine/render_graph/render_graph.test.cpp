#include <gtest/gtest.h>

#include "engine/render_graph/render_graph.hpp"

#include <sstream>
#include <vector>

using namespace mmo::engine::render_graph;

namespace {

TextureDesc make_color_desc(uint32_t w = 256, uint32_t h = 256) {
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    d.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    d.mip_levels = 1;
    d.array_layers = 1;
    d.sample_count = 1;
    return d;
}

TEST(RenderGraph, EmptyGraphCompiles) {
    RenderGraph g;
    g.begin_frame(nullptr);
    EXPECT_TRUE(g.compile());
    EXPECT_EQ(g.pass_count(), 0u);
    g.execute();
    g.end_frame();
}

TEST(RenderGraph, LinearTopoOrder_ABC) {
    RenderGraph g;
    g.begin_frame(nullptr);
    std::vector<std::string> order;

    g.add_pass(
        "A", PassType::Generic, [](PassBuilder& b) { b.create_color_target("X", make_color_desc()); },
        [&](RenderPassContext&) { order.emplace_back("A"); });

    g.add_pass(
        "B", PassType::Generic,
        [](PassBuilder& b) {
            b.read("X");
            b.create_color_target("Y", make_color_desc());
        },
        [&](RenderPassContext&) { order.emplace_back("B"); });

    g.add_pass(
        "C", PassType::Generic,
        [](PassBuilder& b) {
            b.read("Y");
            b.create_color_target("Z", make_color_desc());
        },
        [&](RenderPassContext&) { order.emplace_back("C"); });

    ASSERT_TRUE(g.compile());
    g.execute();
    g.end_frame();

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "A");
    EXPECT_EQ(order[1], "B");
    EXPECT_EQ(order[2], "C");
}

TEST(RenderGraph, DiamondTopoOrder) {
    RenderGraph g;
    g.begin_frame(nullptr);
    std::vector<std::string> order;

    g.add_pass(
        "A", PassType::Generic, [](PassBuilder& b) { b.create_color_target("X", make_color_desc()); },
        [&](RenderPassContext&) { order.emplace_back("A"); });

    g.add_pass(
        "B", PassType::Generic,
        [](PassBuilder& b) {
            b.read("X");
            b.create_color_target("Y", make_color_desc());
        },
        [&](RenderPassContext&) { order.emplace_back("B"); });

    g.add_pass(
        "C", PassType::Generic,
        [](PassBuilder& b) {
            b.read("X");
            b.create_color_target("Z", make_color_desc());
        },
        [&](RenderPassContext&) { order.emplace_back("C"); });

    g.add_pass(
        "D", PassType::Generic,
        [](PassBuilder& b) {
            b.read("Y");
            b.read("Z");
            b.create_color_target("W", make_color_desc());
        },
        [&](RenderPassContext&) { order.emplace_back("D"); });

    ASSERT_TRUE(g.compile());
    g.execute();

    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order.front(), "A");
    EXPECT_EQ(order.back(), "D");

    auto idx = [&](const std::string& s) {
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == s) {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    };
    EXPECT_LT(idx("A"), idx("B"));
    EXPECT_LT(idx("A"), idx("C"));
    EXPECT_LT(idx("B"), idx("D"));
    EXPECT_LT(idx("C"), idx("D"));

    g.end_frame();
}

TEST(RenderGraph, TransientReuseSharesUnderlyingTexture) {
    RenderGraph g;
    g.begin_frame(nullptr);

    SDL_GPUTexture* tex_x = nullptr;
    SDL_GPUTexture* tex_z = nullptr;

    // A writes X. B reads X, writes Y (different desc would force separate pool).
    // C writes Z with same desc as X -- since X is no longer needed after B, Z should
    // reuse X's underlying texture.
    g.add_pass(
        "A", PassType::Generic, [](PassBuilder& b) { b.create_color_target("X", make_color_desc()); },
        [&](RenderPassContext& ctx) {
            tex_x = ctx.get_texture(ctx.command_buffer() == nullptr ? ResourceHandle{} : ResourceHandle{});
            (void)ctx;
        });

    g.add_pass(
        "B", PassType::Generic,
        [](PassBuilder& b) {
            b.read("X");
            // unique desc so Y doesn't conflict with X reuse logic
            TextureDesc d = make_color_desc(128, 128);
            b.create_color_target("Y", d);
        },
        [&](RenderPassContext&) {});

    g.add_pass(
        "C", PassType::Generic,
        [](PassBuilder& b) {
            b.read("Y");
            b.create_color_target("Z", make_color_desc());
        },
        [&](RenderPassContext&) {});

    ASSERT_TRUE(g.compile());

    // Inspect the underlying resolved textures.
    auto h_x = g.find_resource("X");
    auto h_z = g.find_resource("Z");
    ASSERT_TRUE(h_x.valid());
    ASSERT_TRUE(h_z.valid());
    tex_x = g.resource(h_x).resolved_texture;
    tex_z = g.resource(h_z).resolved_texture;
    ASSERT_NE(tex_x, nullptr);
    ASSERT_NE(tex_z, nullptr);
    EXPECT_EQ(tex_x, tex_z) << "Z should reuse X's texture from the pool";
    EXPECT_GE(g.last_reuse_count(), 1u);

    g.execute();
    g.end_frame();
}

TEST(RenderGraph, ImportedTextureSurfacesUnchanged) {
    RenderGraph g;
    g.begin_frame(nullptr);
    auto* sentinel = reinterpret_cast<SDL_GPUTexture*>(uintptr_t{0xDEADBEEF});

    SDL_GPUTexture* observed = nullptr;
    g.add_pass(
        "UseImported", PassType::Generic,
        [&](PassBuilder& b) {
            b.import_texture("shadow_atlas", sentinel, 4096, 4096, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
            b.read("shadow_atlas");
        },
        [&](RenderPassContext& ctx) {
            auto h = g.find_resource("shadow_atlas");
            observed = ctx.get_texture(h);
        });

    ASSERT_TRUE(g.compile());
    g.execute();
    EXPECT_EQ(observed, sentinel);
    g.end_frame();
}

TEST(RenderGraph, CycleDetectedAndSkipped) {
    RenderGraph g;
    g.begin_frame(nullptr);
    bool a_ran = false;
    bool b_ran = false;

    // A writes X reads Y. B writes Y reads X. Cycle.
    g.add_pass(
        "A", PassType::Generic, [](PassBuilder& b) { b.create_color_target("X", make_color_desc()); },
        [&](RenderPassContext&) { a_ran = true; });
    // post-hoc: declare A reads Y and B writes Y reads X by manual access.
    {
        auto h_x = g.find_resource("X");
        // Reach into pass A's reads to force cycle.
        g.pass_at(0).reads.push_back(g.declare_transient_texture("Y", make_color_desc(64, 64)));
        g.add_pass(
            "B", PassType::Generic,
            [](PassBuilder& b) {
                b.write("Y");
                b.read("X");
            },
            [&](RenderPassContext&) { b_ran = true; });
        (void)h_x;
    }

    EXPECT_FALSE(g.compile());
    g.execute();
    EXPECT_FALSE(a_ran);
    EXPECT_FALSE(b_ran);

    g.end_frame();
}

TEST(RenderGraph, DumpDotProducesDigraph) {
    RenderGraph g;
    g.begin_frame(nullptr);
    g.add_pass(
        "Producer", PassType::Generic, [](PassBuilder& b) { b.create_color_target("Tex", make_color_desc()); },
        [](RenderPassContext&) {});
    g.add_pass("Consumer", PassType::Generic, [](PassBuilder& b) { b.read("Tex"); }, [](RenderPassContext&) {});
    ASSERT_TRUE(g.compile());

    std::ostringstream os;
    g.dump_dot(os);
    const std::string out = os.str();
    EXPECT_NE(out.find("digraph RenderGraph"), std::string::npos);
    EXPECT_NE(out.find("Producer"), std::string::npos);
    EXPECT_NE(out.find("Consumer"), std::string::npos);
    EXPECT_NE(out.find("Tex"), std::string::npos);
    g.end_frame();
}

TEST(RenderGraph, TypedAddPassPassesDataThrough) {
    RenderGraph g;
    g.begin_frame(nullptr);

    struct PassData {
        ResourceHandle color;
        int marker = 0;
    };
    int captured = 0;

    g.add_pass<PassData>(
        "Typed",
        [](PassBuilder& b, PassData& d) {
            d.color = b.create_color_target("typed_color", make_color_desc());
            d.marker = 42;
        },
        [&](RenderPassContext&, const PassData& d) {
            captured = d.marker;
            EXPECT_TRUE(d.color.valid());
        });

    ASSERT_TRUE(g.compile());
    g.execute();
    EXPECT_EQ(captured, 42);
    g.end_frame();
}

} // namespace
