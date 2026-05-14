#pragma once

#include <string>

#include "core/sim_types.hpp"

namespace sim::io {

class ConfigLoader {
public:
    static sim::core::PlayerBuildConfig LoadPlayerBuild(const std::string& path);
    static sim::core::PlayerProfileConfig LoadPlayerProfileTemplate(const std::string& path);
    static sim::core::LevelDesignConfig LoadLevelDesign(const std::string& path);
    static std::vector<sim::core::SpawnEvent> LoadSpawnEventsCsv(
        const std::string& path,
        const std::string& scenario_id,
        const std::string& variant_id,
        float fixed_dt,
        int max_spawn_per_tick_budget,
        int& out_level_duration_sec
    );
    static std::vector<sim::core::MonsterDef> LoadMonsterDefCsv(const std::string& path);
    static std::vector<sim::core::DifficultyRow> LoadLevelDifficultyCsv(
        const std::string& path,
        const std::string& scenario_id,
        const std::string& variant_id
    );
    static sim::core::ProjectileWeaponDef LoadProjectileWeaponJson(const std::string& path);

private:
    static std::string ReadFileToString(const std::string& path);
};

} // namespace sim::io
