#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "io/config_loader.hpp"
#include "systems/game_director.hpp"

namespace {

std::string ResolveConfigPath(const std::string& file_name) {
    namespace fs = std::filesystem;
    const fs::path cwd = fs::current_path();

    // 覆盖常见启动目录：
    // - 项目根目录: ./config
    // - build 目录: ../config
    // - build/bin: ../../config
    // - build/bin/<Config>: ../../../config
    const std::vector<fs::path> candidates = {
        cwd / "config" / file_name,
        cwd / ".." / "config" / file_name,
        cwd / ".." / ".." / "config" / file_name,
        cwd / ".." / ".." / ".." / "config" / file_name
    };

    for (const auto& p : candidates) {
        std::error_code ec;
        if (fs::exists(p, ec) && !ec) {
            return p.lexically_normal().string();
        }
    }
    throw std::runtime_error("Cannot locate config file: " + file_name);
}

} // namespace

int main() {
    using clock = std::chrono::high_resolution_clock;

    try {
        // 配置文件是无头模拟器的“输入域”：
        // - player_profile_template.json 控制角色属性、成长与战斗乘区
        // - level_design.json 控制关卡时间轴、怪物参数和网格参数
        const std::string player_profile_path = ResolveConfigPath("player_profile_template.json");
        const std::string level_design_path = ResolveConfigPath("level_design.json");
        const sim::core::PlayerProfileConfig profile_cfg = sim::io::ConfigLoader::LoadPlayerProfileTemplate(player_profile_path);
        sim::core::LevelDesignConfig level_cfg = sim::io::ConfigLoader::LoadLevelDesign(level_design_path);
        if (!profile_cfg.main_weapon_file.empty()) {
            level_cfg.projectile_weapon_def = sim::io::ConfigLoader::LoadProjectileWeaponJson(profile_cfg.main_weapon_file);
        }

        sim::systems::GameDirector director(/*player_contact_radius=*/0.9f);
        director.Init(profile_cfg, level_cfg);

        const auto start = clock::now();
        director.Run();
        const auto end = clock::now();
        const std::chrono::duration<double, std::milli> elapsed_ms = end - start;

        director.ExportMetrics("metrics_summary.csv", "metrics_timeline.csv", "metrics_waves.csv");

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Headless simulation finished.\n";
        std::cout << "Simulated time: " << director.Metrics().SimulatedTimeSec() << " sec\n";
        std::cout << "Wall-clock time: " << elapsed_ms.count() << " ms\n";
        std::cout << "Kills: " << director.Metrics().TotalKills() << "\n";
        std::cout << "Peak alive monsters: " << director.Metrics().PeakAliveMonsters() << "\n";
        std::cout << "Summary CSV: metrics_summary.csv\n";
        std::cout << "Timeline CSV: metrics_timeline.csv\n";
        std::cout << "Wave CSV: metrics_waves.csv\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Simulation failed: " << e.what() << "\n";
        return 1;
    }
}
