#pragma once

#include <string>

#include "core/sim_types.hpp"
#include "ecs/monster_pool.hpp"
#include "io/metrics_logger.hpp"
#include "spatial/uniform_grid.hpp"
#include "systems/movement_system.hpp"
#include "systems/projectile_weapon_system.hpp"
#include "systems/wave_system.hpp"

namespace sim::systems {

class GameDirector {
public:
    explicit GameDirector(float player_contact_radius);

    void Init(const sim::core::PlayerProfileConfig& profile_cfg, const sim::core::LevelDesignConfig& level_cfg);
    void Run();
    void ExportMetrics(
        const std::string& summary_path,
        const std::string& timeline_path,
        const std::string& wave_summary_path
    ) const;

    [[nodiscard]] const sim::io::MetricsLogger& Metrics() const noexcept { return metrics_; }

private:
    float fixed_dt_ = 1.0f / 60.0f;
    float max_survival_time_sec_ = 30.0f * 60.0f;
    float current_time_ = 0.0f;

    sim::core::Player player_{};
    sim::ecs::MonsterPool pool_{};
    sim::spatial::UniformGrid grid_{};
    sim::systems::WaveSystem wave_system_{};
    sim::systems::MovementSystem movement_system_;
    sim::systems::ProjectileWeaponSystem projectile_weapon_system_{};
    sim::io::MetricsLogger metrics_{};
};

} // namespace sim::systems
