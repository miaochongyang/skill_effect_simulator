#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sim::core {

// 玩家状态只保留热路径所需字段；避免把冷数据混在每 Tick 访问路径里。
struct Player {
    float hp = 1500.0f;
    float max_hp = 1500.0f;

    // ===== 当前机制已接入并参与结算的属性 =====
    float attack_power = 1.0f;
    float attack_speed = 1.0f;
    float cooldown_reduction = 0.0f; // [0, 0.9]
    float crit_chance = 0.05f;       // [0, 1)
    float crit_multiplier = 1.5f;    // >= 1
    float damage_multiplier = 1.0f;
    int projectile_count_bonus = 0;
    float aoe_radius_multiplier = 1.0f;
    float final_damage_taken_multiplier = 1.0f;
    float regen_hp_per_sec = 0.0f;
    float life_steal = 0.0f;

    // 时间成长（当前接入 time_growth；level_growth 保留可扩展）
    float base_max_hp = 1500.0f;
    float base_attack_power = 1.0f;
    float time_growth_k_hp = 0.0f;
    float time_growth_k_atk = 0.0f;

    [[nodiscard]] bool IsAlive() const noexcept { return hp > 0.0f; }
};

struct PlayerProfileConfig {
    std::string schema_version = "v1";
    std::string profile_id;
    std::string display_name;

    // simulation
    float player_hp = 1500.0f;
    float max_survival_time_sec = 1800.0f;

    // base_stats
    float max_hp = 1500.0f;
    float base_attack_power = 1.0f;
    float base_attack_speed = 1.0f;
    float base_cooldown_reduction = 0.0f;
    float base_crit_chance = 0.05f;
    float base_crit_multiplier = 1.5f;
    float base_luck = 0.0f;

    // combat
    float damage_multiplier = 1.0f;
    int projectile_count_bonus = 0;
    float aoe_radius_multiplier = 1.0f;
    float status_power_multiplier = 1.0f; // 当前仅记录，不参与结算
    float final_damage_taken_multiplier = 1.0f;

    // defense（当前接入 regen/life_steal，其余仅记录）
    float armor = 0.0f;                 // 参考字段
    float evasion = 0.0f;               // 参考字段
    float block_chance = 0.0f;          // 参考字段
    float block_flat_reduction = 0.0f;  // 参考字段
    float regen_hp_per_sec = 0.0f;
    float life_steal = 0.0f;

    // progression.time_growth
    float time_growth_k_hp = 0.0f;
    float time_growth_k_atk = 0.0f;

    // 预留字段：当前不接入主循环但保留配置信息
    std::string target_priority = "nearest";
    std::string collision_policy = "discrete_tick";
    std::string rng_stream_id = "player_default_stream";
    std::string main_weapon_file = "config/weapon_projectile_standard.json";
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

struct ProjectileStatusEffectDef {
    std::string id;
    float chance = 0.0f;
    float duration = 0.0f;
    float slow_ratio = 0.0f;
    std::string stack_rule;
};

struct ProjectileUpgradeChannelDef {
    double base = 0.0;
    double min = 0.0;
    double max = 0.0;
    bool has_max = false;
    std::string stack_mode;
};

struct ProjectileUpgradeModifierDef {
    std::string channel;
    std::string op; // add | mul
    double value = 0.0;
};

struct ProjectileUpgradeOptionDef {
    std::string option_id;
    std::string rarity;
    int weight = 1;
    int max_pick = 1;
    std::vector<ProjectileUpgradeModifierDef> modifiers;
};

struct ProjectileWeaponDef {
    std::string schema_version = "v1";
    std::string weapon_id;
    std::string weapon_name;
    std::string weapon_type = "projectile_ranged";
    std::string rng_stream_id = "weapon_default";
    bool use_deterministic_rng = true;

    // base
    float cooldown = 1.0f;
    int charges = 1;
    int burst_count = 1;
    float burst_interval = 0.0f;
    int projectiles_per_shot = 1;

    // projectile
    float projectile_speed = 10.0f;
    float max_lifetime = 0.0f;
    float max_travel_distance = 0.0f;
    float spawn_offset = 0.0f;
    float inherit_owner_velocity_ratio = 0.0f;
    float initial_spread_angle = 0.0f;
    std::string spread_pattern = "symmetric";
    int pierce_count = 0;
    int ricochet_count = 0;
    bool can_hit_same_target_again = false;
    float re_hit_cooldown = 0.0f;

    // collision
    std::string collision_shape = "circle";
    float collision_radius = 0.0f;
    bool friendly_fire = false;
    bool destroy_on_block = true;

    // damage
    float base_damage = 0.0f;
    std::string damage_type = "physical";
    float crit_chance = 0.0f;
    float crit_multiplier = 1.5f;
    float hit_interval_per_target = 0.0f;
    int max_targets_per_tick = 1;
    float knockback_distance = 0.0f;

    // targeting
    std::string targeting_mode = "nearest_enemy";
    float targeting_search_radius = 0.0f;
    std::string targeting_fallback_mode = "forward";
    bool allow_dead_target = false;

    std::vector<ProjectileStatusEffectDef> status_effects;
    std::unordered_map<std::string, ProjectileUpgradeChannelDef> upgrade_channels;
    std::vector<ProjectileUpgradeOptionDef> upgrade_options;
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
    ProjectileWeaponDef projectile_weapon_def{};
    std::string projectile_weapon_config_file = "config/weapon_projectile_standard.json";
};

} // namespace sim::core
