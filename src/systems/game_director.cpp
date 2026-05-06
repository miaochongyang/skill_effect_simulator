#include "systems/game_director.hpp"

#include <algorithm>
#include <cstdint>

#include "systems/combat_system.hpp"

namespace sim::systems {

GameDirector::GameDirector(const float player_contact_radius) : movement_system_(player_contact_radius) {}

void GameDirector::Init(
    const sim::core::PlayerBuildConfig& player_cfg,
    const sim::core::LevelDesignConfig& level_cfg
) {
    // Init 阶段只做一次性工作：把所有容量和参数准备好，避免运行中扩容抖动。
    fixed_dt_ = level_cfg.fixed_dt;
    max_survival_time_sec_ = player_cfg.max_survival_time_sec;
    if (level_cfg.level_duration_sec > 0) {
        max_survival_time_sec_ = std::min(max_survival_time_sec_, static_cast<float>(level_cfg.level_duration_sec));
    }
    current_time_ = 0.0f;

    player_.hp = player_cfg.initial_hp;
    pool_.Init(level_cfg.max_monsters);
    grid_.Init(level_cfg.grid_half_extent, level_cfg.grid_cell_size);
    wave_system_.Init(
        level_cfg.spawn_events,
        level_cfg.monster_defs,
        level_cfg.difficulty_rows,
        level_cfg.fixed_dt,
        level_cfg.world_units_per_px,
        42u
    );
    skill_system_.Init(player_cfg.skills);
}

void GameDirector::Run() {
    float next_sample_t = 0.0f;

    // 固定步长 Tick：保证结果可重复，是回放/联网同步/批量实验的重要基础。
    while (player_.IsAlive() && current_time_ < max_survival_time_sec_) {
        // 1) 生成逻辑：按时间轴激活敌人到对象池。
        wave_system_.Tick(current_time_, pool_, metrics_);
        // 2) 移动逻辑：批量更新坐标并计算贴脸伤害。
        movement_system_.Tick(fixed_dt_, pool_, player_, metrics_);
        // 3) 空间重建：把更新后位置重新映射到 uniform grid。
        grid_.Rebuild(pool_);

        // 4) 技能结算：技能系统产生 hitbox，战斗系统做局部范围判定。
        const auto& hitboxes = skill_system_.Tick(fixed_dt_);
        CombatSystem::Resolve(hitboxes, pool_, grid_, metrics_);

        // 5) 统计采样：记录峰值与每秒时间线数据，便于离线分析。
        metrics_.UpdatePeakAlive(pool_.AliveCount());

        current_time_ += fixed_dt_;
        if (current_time_ >= next_sample_t) {
            metrics_.AddTimeline(
                current_time_,
                static_cast<std::uint32_t>(pool_.AliveCount()),
                metrics_.TotalKills()
            );
            next_sample_t += 1.0f;
        }
    }

    metrics_.SetSimulatedTime(current_time_);
}

void GameDirector::ExportMetrics(
    const std::string& summary_path,
    const std::string& timeline_path,
    const std::string& wave_summary_path
) const {
    metrics_.SaveCsv(summary_path, timeline_path, wave_summary_path);
}

} // namespace sim::systems
