#include <gtest/gtest.h>
#include "engine/graphics_settings.hpp"
#include <cstdio>
#include <filesystem>

using namespace mmo::engine;

static const std::string TEST_FILE = "/tmp/mmo_test_settings.cfg";

class GraphicsSettingsTest : public ::testing::Test {
protected:
    void TearDown() override {
        std::remove(TEST_FILE.c_str());
    }
};

TEST_F(GraphicsSettingsTest, DefaultValues) {
    GraphicsSettings s;
    EXPECT_TRUE(s.fog_enabled);
    EXPECT_TRUE(s.grass_enabled);
    EXPECT_TRUE(s.skybox_enabled);
    EXPECT_EQ(s.shadow_mode, 2);
    EXPECT_EQ(s.ao_mode, 1);
    EXPECT_TRUE(s.bloom_enabled);
    EXPECT_FLOAT_EQ(s.bloom_strength, 0.20f);
    EXPECT_FALSE(s.volumetric_fog);
    EXPECT_FALSE(s.god_rays);
    EXPECT_FALSE(s.show_fps);
}

TEST_F(GraphicsSettingsTest, GetDrawDistanceMapping) {
    GraphicsSettings s;
    s.draw_distance = 0;
    EXPECT_FLOAT_EQ(s.get_draw_distance(), 500.0f);
    s.draw_distance = 1;
    EXPECT_FLOAT_EQ(s.get_draw_distance(), 1000.0f);
    s.draw_distance = 3;
    EXPECT_FLOAT_EQ(s.get_draw_distance(), 4000.0f);
    s.draw_distance = 6;
    EXPECT_FLOAT_EQ(s.get_draw_distance(), 32000.0f);
}

TEST_F(GraphicsSettingsTest, SaveCreatesFile) {
    GraphicsSettings s;
    EXPECT_TRUE(s.save(TEST_FILE));
    EXPECT_TRUE(std::filesystem::exists(TEST_FILE));
}

TEST_F(GraphicsSettingsTest, LoadNonexistentFileReturnsFalse) {
    GraphicsSettings s;
    EXPECT_FALSE(s.load("/tmp/does_not_exist_12345.cfg"));
    // Defaults should be preserved
    EXPECT_TRUE(s.fog_enabled);
    EXPECT_EQ(s.shadow_mode, 2);
}

TEST_F(GraphicsSettingsTest, SaveLoadRoundTripsAllFields) {
    GraphicsSettings original;
    original.fog_enabled = false;
    original.grass_enabled = false;
    original.skybox_enabled = false;
    original.trees_enabled = false;
    original.rocks_enabled = false;
    original.show_fps = true;
    original.show_debug_hud = true;
    original.draw_distance = 5;
    original.frustum_culling = false;
    original.shadow_mode = 1;
    original.shadow_cascades = 3;
    original.shadow_resolution = 3;
    original.ao_mode = 2;
    original.bloom_enabled = false;
    original.bloom_strength = 0.77f;
    original.volumetric_fog = true;
    original.god_rays = true;
    original.vsync_mode = 2;
    original.window_mode = 1;
    original.anisotropic_filter = 3;

    ASSERT_TRUE(original.save(TEST_FILE));

    GraphicsSettings loaded;
    ASSERT_TRUE(loaded.load(TEST_FILE));

    EXPECT_EQ(loaded.fog_enabled, false);
    EXPECT_EQ(loaded.grass_enabled, false);
    EXPECT_EQ(loaded.skybox_enabled, false);
    EXPECT_EQ(loaded.trees_enabled, false);
    EXPECT_EQ(loaded.rocks_enabled, false);
    EXPECT_EQ(loaded.show_fps, true);
    EXPECT_EQ(loaded.show_debug_hud, true);
    EXPECT_EQ(loaded.draw_distance, 5);
    EXPECT_EQ(loaded.frustum_culling, false);
    EXPECT_EQ(loaded.shadow_mode, 1);
    EXPECT_EQ(loaded.shadow_cascades, 3);
    EXPECT_EQ(loaded.shadow_resolution, 3);
    EXPECT_EQ(loaded.ao_mode, 2);
    EXPECT_EQ(loaded.bloom_enabled, false);
    EXPECT_NEAR(loaded.bloom_strength, 0.77f, 0.01f);
    EXPECT_EQ(loaded.volumetric_fog, true);
    EXPECT_EQ(loaded.god_rays, true);
    EXPECT_EQ(loaded.vsync_mode, 2);
    EXPECT_EQ(loaded.window_mode, 1);
    EXPECT_EQ(loaded.anisotropic_filter, 3);
}

TEST_F(GraphicsSettingsTest, FloatPrecisionPreserved) {
    GraphicsSettings original;
    original.bloom_strength = 0.123456f;
    ASSERT_TRUE(original.save(TEST_FILE));

    GraphicsSettings loaded;
    ASSERT_TRUE(loaded.load(TEST_FILE));
    EXPECT_NEAR(loaded.bloom_strength, 0.123456f, 0.001f);
}

// --- Edge case tests ---

TEST_F(GraphicsSettingsTest, LoadFileWithUnknownKeys) {
    // Unknown keys should be silently ignored
    {
        std::ofstream out(TEST_FILE);
        out << "fog_enabled=0\n";
        out << "totally_unknown_key=999\n";
        out << "shadow_mode=1\n";
        out << "another_fake=hello\n";
    }
    GraphicsSettings s;
    ASSERT_TRUE(s.load(TEST_FILE));
    EXPECT_FALSE(s.fog_enabled);
    EXPECT_EQ(s.shadow_mode, 1);
    // Other fields remain at defaults
    EXPECT_TRUE(s.grass_enabled);
    EXPECT_TRUE(s.bloom_enabled);
}

TEST_F(GraphicsSettingsTest, LoadFileWithMalformedValues) {
    // Malformed integer values should leave defaults intact
    {
        std::ofstream out(TEST_FILE);
        out << "shadow_mode=abc\n";
        out << "draw_distance=!@#\n";
        out << "bloom_strength=not_a_float\n";
        out << "ao_mode=2\n"; // this one is valid
    }
    GraphicsSettings s;
    ASSERT_TRUE(s.load(TEST_FILE));
    EXPECT_EQ(s.shadow_mode, 2);     // default, "abc" failed to parse
    EXPECT_EQ(s.draw_distance, 3);   // default, "!@#" failed to parse
    EXPECT_FLOAT_EQ(s.bloom_strength, 0.20f); // default, "not_a_float" failed
    EXPECT_EQ(s.ao_mode, 2);         // successfully parsed
}

TEST_F(GraphicsSettingsTest, LoadFileWithComments) {
    // Lines starting with '#' should be skipped
    {
        std::ofstream out(TEST_FILE);
        out << "# This is a comment\n";
        out << "fog_enabled=0\n";
        out << "# Another comment\n";
        out << "shadow_mode=0\n";
    }
    GraphicsSettings s;
    ASSERT_TRUE(s.load(TEST_FILE));
    EXPECT_FALSE(s.fog_enabled);
    EXPECT_EQ(s.shadow_mode, 0);
}

TEST_F(GraphicsSettingsTest, LoadEmptyFileKeepsDefaults) {
    {
        std::ofstream out(TEST_FILE);
        // Write nothing
    }
    GraphicsSettings s;
    ASSERT_TRUE(s.load(TEST_FILE));
    // All defaults should be intact
    EXPECT_TRUE(s.fog_enabled);
    EXPECT_TRUE(s.grass_enabled);
    EXPECT_TRUE(s.skybox_enabled);
    EXPECT_TRUE(s.trees_enabled);
    EXPECT_TRUE(s.rocks_enabled);
    EXPECT_FALSE(s.show_fps);
    EXPECT_FALSE(s.show_debug_hud);
    EXPECT_EQ(s.draw_distance, 3);
    EXPECT_TRUE(s.frustum_culling);
    EXPECT_EQ(s.anisotropic_filter, 4);
    EXPECT_EQ(s.vsync_mode, 0);
    EXPECT_EQ(s.shadow_mode, 2);
    EXPECT_EQ(s.shadow_cascades, 1);
    EXPECT_EQ(s.shadow_resolution, 2);
    EXPECT_EQ(s.ao_mode, 1);
    EXPECT_TRUE(s.bloom_enabled);
    EXPECT_FLOAT_EQ(s.bloom_strength, 0.20f);
    EXPECT_FALSE(s.volumetric_fog);
    EXPECT_FALSE(s.god_rays);
    EXPECT_EQ(s.window_mode, 0);
    EXPECT_EQ(s.resolution_index, 0);
}

TEST_F(GraphicsSettingsTest, SaveOverwritesExistingFile) {
    // Save once with one set of values
    GraphicsSettings s1;
    s1.shadow_mode = 0;
    s1.fog_enabled = false;
    ASSERT_TRUE(s1.save(TEST_FILE));

    // Save again with different values
    GraphicsSettings s2;
    s2.shadow_mode = 1;
    s2.fog_enabled = true;
    ASSERT_TRUE(s2.save(TEST_FILE));

    // Load should reflect the second save
    GraphicsSettings loaded;
    ASSERT_TRUE(loaded.load(TEST_FILE));
    EXPECT_EQ(loaded.shadow_mode, 1);
    EXPECT_TRUE(loaded.fog_enabled);
}

TEST_F(GraphicsSettingsTest, LoadWithExtraWhitespaceAroundValues) {
    // The current parser does not trim whitespace, so "shadow_mode" with spaces
    // in the key won't match. But trailing whitespace in value is passed to stoi
    // which does handle leading whitespace. Test what the parser actually handles.
    {
        std::ofstream out(TEST_FILE);
        // stoi/stof skip leading whitespace in the value
        out << "shadow_mode= 1\n";
        out << "bloom_strength= 0.55\n";
        out << "ao_mode=2\n";
    }
    GraphicsSettings s;
    ASSERT_TRUE(s.load(TEST_FILE));
    // stoi(" 1") = 1, stof(" 0.55") = 0.55 - leading space in value is fine
    EXPECT_EQ(s.shadow_mode, 1);
    EXPECT_NEAR(s.bloom_strength, 0.55f, 0.01f);
    EXPECT_EQ(s.ao_mode, 2);
}

TEST_F(GraphicsSettingsTest, BooleanValuesZeroAndOne) {
    {
        std::ofstream out(TEST_FILE);
        out << "fog_enabled=1\n";
        out << "grass_enabled=0\n";
        out << "show_fps=1\n";
        out << "frustum_culling=0\n";
    }
    GraphicsSettings s;
    ASSERT_TRUE(s.load(TEST_FILE));
    EXPECT_TRUE(s.fog_enabled);
    EXPECT_FALSE(s.grass_enabled);
    EXPECT_TRUE(s.show_fps);
    EXPECT_FALSE(s.frustum_culling);
}

TEST_F(GraphicsSettingsTest, BooleanNonZeroTreatedAsTrue) {
    // The parser treats anything != "0" as true for booleans
    {
        std::ofstream out(TEST_FILE);
        out << "fog_enabled=true\n";
        out << "grass_enabled=yes\n";
        out << "show_fps=42\n";
    }
    GraphicsSettings s;
    s.fog_enabled = false;
    s.grass_enabled = false;
    s.show_fps = false;
    ASSERT_TRUE(s.load(TEST_FILE));
    EXPECT_TRUE(s.fog_enabled);
    EXPECT_TRUE(s.grass_enabled);
    EXPECT_TRUE(s.show_fps);
}

TEST_F(GraphicsSettingsTest, NegativeIntegerValues) {
    {
        std::ofstream out(TEST_FILE);
        out << "shadow_mode=-1\n";
        out << "draw_distance=-5\n";
    }
    GraphicsSettings s;
    ASSERT_TRUE(s.load(TEST_FILE));
    // stoi handles negative numbers; the struct stores them as-is
    EXPECT_EQ(s.shadow_mode, -1);
    EXPECT_EQ(s.draw_distance, -5);
}

TEST_F(GraphicsSettingsTest, SaveToVeryLongFilePath) {
    // Attempting to save to a deeply nested nonexistent directory should fail
    std::string long_path = "/tmp";
    for (int i = 0; i < 50; i++) {
        long_path += "/nonexistent_dir_" + std::to_string(i);
    }
    long_path += "/settings.cfg";

    GraphicsSettings s;
    EXPECT_FALSE(s.save(long_path));
}
