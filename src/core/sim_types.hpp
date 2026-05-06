#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim::core {

// 玩家状态只保留热路径所需字段；避免把冷数据混在每 Tick 访问路径里。
struct Player {
    float hp = 1500.0f;

    [[nodiscard]] bool IsAlive() const noexcept { return hp > 0.0f; }
};

// 技能定义：形状 + 频率 + 伤害是割草游戏技能逻辑的核心抽象。
struct SkillDef {
    std::uint8_t skill_id = 0;
    std::string name;
    float cooldown = 1.0f;
    float radius = 3.0f;
    float damage = 8.0f;
    int max_targets = 999999;
};

// Runtime 结构和静态定义分离，便于热更新技能参数或做多 build 对比实验。
struct SkillRuntime {
    SkillDef def{};
    float cooldown_remaining = 0.0f;
};

// Hitbox 在本模拟器中是“离散事件”，每次技能触发只生成一次结算域。
struct HitboxEvent {
    std::uint8_t skill_id = 0;
    float radius = 0.0f;
    float damage = 0.0f;
    int max_targets = 0;
};

struct SpawnProfile {
    float hp = 20.0f;
    float move_speed = 2.0f;
    float attack_damage = 10.0f;
    float attack_interval_sec = 1.0f;
    float attack_range = 1.0f;
};

struct MonsterDef {
    std::string schema_version = "v1";
    int monster_id = 1;
    std::string monster_name;
    float base_attack = 10.0f;
    float base_attack_interval_sec = 1.0f;
    float base_attack_range_px = 24.0f;
    float base_hp = 100.0f;
    float base_move_speed_px_sec = 120.0f;
    std::string notes;
};

struct DifficultyRow {
    std::string schema_version = "v1";
    std::string scenario_id = "default";
    std::string variant_id = "A";
    int phase_id = 1;
    float start_time_sec = 0.0f;
    float end_time_sec = 1.0f;
    int monster_id = 0; // 0 = 全局行，>0 = 类型行

    float global_atk_scale = 1.0f;
    float global_atk_interval_scale = 1.0f;
    float global_atk_range_scale = 1.0f;
    float global_hp_scale = 1.0f;
    float global_move_speed_scale = 1.0f;

    float type_atk_scale = 1.0f;
    float type_atk_interval_scale = 1.0f;
    float type_atk_range_scale = 1.0f;
    float type_hp_scale = 1.0f;
    float type_move_speed_scale = 1.0f;
    std::string notes;
};

// CSV 刷怪事件（spawn_events.csv）的一行定义。
// 执行主键为 trigger_tick，trigger_time_sec 主要用于日志和可读性。
struct SpawnEvent {
    std::string schema_version = "v1";
    std::string scenario_id = "default";
    std::string variant_id = "A";
    int level_duration_sec = 180;

    int event_id = 0;
    float trigger_time_sec = 0.0f;
    int trigger_tick = 0;
    int monster_id = 1;
    int count = 1;
    int spawn_angle_clock = 12;        // [1,12]
    float spawn_distance_px = 900.0f;  // 基准距离（像素）
    float spawn_random_radius_px = 120.0f;
    int interval_tick = 1;
    int spawn_zone_id = 0;
    int seed_offset = 0;
    float attr_hp_scale = 1.0f;
    float attr_atk_scale = 1.0f;
    float attr_spd_scale = 1.0f;
    std::string phase_tag = "sustain";
    std::string notes;
};

// 玩家 build 配置：把角色属性和技能组合抽到配置层，支持批量自动化回归。
struct PlayerBuildConfig {
    float initial_hp = 1500.0f;
    float max_survival_time_sec = 30.0f * 60.0f;
    std::vector<SkillDef> skills;
};

// 关卡配置：所有会影响吞吐的参数都集中在这里，便于压测时快速扫参。
struct LevelDesignConfig {
    float fixed_dt = 1.0f / 60.0f;
    std::size_t max_monsters = 120000;
    float grid_half_extent = 160.0f;
    float grid_cell_size = 2.5f;
    float world_units_per_px = 0.05f; // 像素到世界单位缩放（900px -> 45world）
    std::string spawn_scenario_id = "baseline_horde";
    std::string spawn_variant_id = "A";
    int level_duration_sec = 180;
    std::vector<SpawnEvent> spawn_events;
    std::vector<MonsterDef> monster_defs;
    std::vector<DifficultyRow> difficulty_rows;
};

} // namespace sim::core
