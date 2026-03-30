#pragma once

#include <fstream>
#include <string>
#include <unordered_map>

namespace mmo::engine {

struct GraphicsSettings {
    bool fog_enabled = true;
    bool grass_enabled = true;
    bool skybox_enabled = true;
    bool trees_enabled = true;
    bool rocks_enabled = true;
    bool show_fps = false;
    bool show_debug_hud = false;

    // Culling settings
    int draw_distance = 3;    // 0=500, 1=1000, 2=2000, 3=4000, 4=8000, 5=16000, 6=32000
    bool frustum_culling = true;

    // Quality settings
    int anisotropic_filter = 4; // 0=off, 1=2x, 2=4x, 3=8x, 4=16x
    int vsync_mode = 0;      // 0=immediate, 1=vsync, 2=mailbox
    int shadow_mode = 2;     // 0=off, 1=hard, 2=PCSS
    int shadow_cascades = 1; // 0=1, 1=2, 2=3, 3=4
    int shadow_resolution = 2; // 0=512, 1=1024, 2=2048, 3=4096
    int ao_mode = 1;         // 0=off, 1=SSAO, 2=GTAO
    bool bloom_enabled = true;       // Bloom post-processing
    float bloom_strength = 0.20f;    // Bloom intensity (0.0 - 1.0)
    bool volumetric_fog = false;     // Atmospheric fog haze (expensive, off by default)
    bool god_rays = false;           // Light shafts through shadows (requires volumetric_fog)
    int window_mode = 0;     // 0=windowed, 1=borderless fullscreen, 2=exclusive fullscreen
    int resolution_index = 0; // index into available native display modes

    float get_draw_distance() const {
        constexpr float distances[] = {500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f, 32000.0f};
        return distances[draw_distance];
    }

    bool save(const std::string& path) const {
        std::ofstream out(path);
        if (!out.is_open()) return false;

        out << "fog_enabled=" << fog_enabled << "\n";
        out << "grass_enabled=" << grass_enabled << "\n";
        out << "skybox_enabled=" << skybox_enabled << "\n";
        out << "trees_enabled=" << trees_enabled << "\n";
        out << "rocks_enabled=" << rocks_enabled << "\n";
        out << "show_fps=" << show_fps << "\n";
        out << "show_debug_hud=" << show_debug_hud << "\n";
        out << "draw_distance=" << draw_distance << "\n";
        out << "frustum_culling=" << frustum_culling << "\n";
        out << "anisotropic_filter=" << anisotropic_filter << "\n";
        out << "vsync_mode=" << vsync_mode << "\n";
        out << "shadow_mode=" << shadow_mode << "\n";
        out << "shadow_cascades=" << shadow_cascades << "\n";
        out << "shadow_resolution=" << shadow_resolution << "\n";
        out << "ao_mode=" << ao_mode << "\n";
        out << "bloom_enabled=" << bloom_enabled << "\n";
        out << "bloom_strength=" << bloom_strength << "\n";
        out << "volumetric_fog=" << volumetric_fog << "\n";
        out << "god_rays=" << god_rays << "\n";
        out << "window_mode=" << window_mode << "\n";
        out << "resolution_index=" << resolution_index << "\n";

        return out.good();
    }

    bool load(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        std::unordered_map<std::string, std::string> kvs;
        std::string line;
        while (std::getline(in, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            kvs[line.substr(0, eq)] = line.substr(eq + 1);
        }

        auto read_bool = [&](const char* key, bool& val) {
            auto it = kvs.find(key);
            if (it != kvs.end()) val = (it->second != "0");
        };
        auto read_int = [&](const char* key, int& val) {
            auto it = kvs.find(key);
            if (it != kvs.end()) {
                try { val = std::stoi(it->second); } catch (...) { /* ignore malformed values */ }
            }
        };
        auto read_float = [&](const char* key, float& val) {
            auto it = kvs.find(key);
            if (it != kvs.end()) {
                try { val = std::stof(it->second); } catch (...) { /* ignore malformed values */ }
            }
        };

        read_bool("fog_enabled", fog_enabled);
        read_bool("grass_enabled", grass_enabled);
        read_bool("skybox_enabled", skybox_enabled);
        read_bool("trees_enabled", trees_enabled);
        read_bool("rocks_enabled", rocks_enabled);
        read_bool("show_fps", show_fps);
        read_bool("show_debug_hud", show_debug_hud);
        read_int("draw_distance", draw_distance);
        read_bool("frustum_culling", frustum_culling);
        read_int("anisotropic_filter", anisotropic_filter);
        read_int("vsync_mode", vsync_mode);
        read_int("shadow_mode", shadow_mode);
        read_int("shadow_cascades", shadow_cascades);
        read_int("shadow_resolution", shadow_resolution);
        read_int("ao_mode", ao_mode);
        read_bool("bloom_enabled", bloom_enabled);
        read_float("bloom_strength", bloom_strength);
        read_bool("volumetric_fog", volumetric_fog);
        read_bool("god_rays", god_rays);
        read_int("window_mode", window_mode);
        read_int("resolution_index", resolution_index);

        return true;
    }
};

} // namespace mmo::engine
