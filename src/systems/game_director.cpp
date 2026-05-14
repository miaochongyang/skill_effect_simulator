#include "systems/game_director.hpp"

#include <algorithm>
#include <cstdint>

namespace sim::systems {

GameDirector::GameDirector(const float player_contact_radius) : movement_system_(player_contact_radius) {}

void GameDirector::Init(
    const sim::core::PlayerProfileConfig& profile_cfg,
    const sim::core::LevelDesignConfig& level_cfg
) {
    // Init 阶段只做一次性工作：把所有容量和参数准备好，避免运行中扩容抖动。
    fixed_dt_ = level_cfg.fixed_dt;
    max_survival_time_sec_ = profile_cfg.max_survival_time_sec;
    if (level_cfg.level_duration_sec > 0) {
        max_survival_time_sec_ = std::min(max_survival_time_sec_, static_cast<float>(level_cfg.level_duration_sec));
    }
    current_time_ = 0.0f;

    player_.base_max_hp = profile_cfg.max_hp;
    player_.base_attack_power = profile_cfg.base_attack_power;
    player_.time_growth_k_hp = profile_cfg.time_growth_k_hp;
    player_.time_growth_k_atk = profile_cfg.time_growth_k_atk;
    player_.max_hp = profile_cfg.max_hp;
    player_.hp = std::min(profile_cfg.player_hp, profile_cfg.max_hp);
    player_.attack_power = profile_cfg.base_attack_power;
    player_.attack_speed = profile_cfg.base_attack_speed;
    player_.cooldown_reduction = profile_cfg.base_cooldown_reduction;
    player_.crit_chance = profile_cfg.base_crit_chance;
    player_.crit_multiplier = profile_cfg.base_crit_multiplier;
    player_.damage_multiplier = profile_cfg.damage_multiplier;
    player_.projectile_count_bonus = profile_cfg.projectile_count_bonus;
    player_.aoe_radius_multiplier = profile_cfg.aoe_radius_multiplier;
    player_.final_damage_taken_multiplier = profile_cfg.final_damage_taken_multiplier;
    player_.regen_hp_per_sec = profile_cfg.regen_hp_per_sec;
    player_.life_steal = profile_cfg.life_steal;
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
    projectile_weapon_system_.Init(level_cfg.projectile_weapon_def, 42u);
}

void GameDirector::Run() {
    float next_sample_t = 0.0f;

    // 固定步长 Tick：保证结果可重复，是回放/联网同步/批量实验的重要基础。
    while (player_.IsAlive() && current_time_ < max_survival_time_sec_) {
        // time_growth：按时间更新当前攻击与生命上限。
        player_.attack_power = player_.base_attack_power * (1.0f + player_.time_growth_k_atk * current_time_);
        player_.max_hp = player_.base_max_hp * (1.0f + player_.time_growth_k_hp * current_time_);
        player_.hp = std::min(player_.hp, player_.max_hp);

        // 1) 生成逻辑：按时间轴激活敌人到对象池。
        wave_system_.Tick(current_time_, pool_, metrics_);
        // 2) 移动逻辑：批量更新坐标并计算贴脸伤害。
        movement_system_.Tick(fixed_dt_, pool_, player_, metrics_);
        // 3) 空间重建：把更新后位置重新映射到 uniform grid。
        grid_.Rebuild(pool_);

        // 4) 战斗结算：远程弹道武器推进 + 命中判定。
        const float dealt_damage = projectile_weapon_system_.Tick(fixed_dt_, player_, pool_, grid_, metrics_);

        // 5) Tick 尾结算：吸血和回血。
        if (player_.life_steal > 0.0f && dealt_damage > 0.0f) {
            player_.hp = std::min(player_.max_hp, player_.hp + dealt_damage * player_.life_steal);
        }
        if (player_.regen_hp_per_sec > 0.0f) {
            player_.hp = std::min(player_.max_hp, player_.hp + player_.regen_hp_per_sec * fixed_dt_);
        }

        // 6) 统计采样：记录峰值与每秒时间线数据，便于离线分析。
        metrics_.UpdatePeakAlive(pool_.AliveCount());

        current_time_ += fixed_dt_;
        if (current_time_ >= next_sample_t) {
            metrics_.AddTimeline(
                current_time_,
                static_cast<std::uint32_t>(pool_.AliveCount()),
                metrics_.TotalKills(),
                player_
            );
            next_sample_t += 1.0f;
        }
    }

    metrics_.SetSimulatedTime(current_time_);
    metrics_.SetFinalPlayerState(player_);
}

void GameDirector::ExportMetrics(
    const std::string& summary_path,
    const std::string& timeline_path,
    const std::string& wave_summary_path
) const {
    metrics_.SaveCsv(summary_path, timeline_path, wave_summary_path);
}

} // namespace sim::systems
