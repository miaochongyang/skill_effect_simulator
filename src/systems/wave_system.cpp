#include "systems/wave_system.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <algorithm>
#include <utility>

#include "ecs/monster_pool.hpp"
#include "io/metrics_logger.hpp"

namespace sim::systems {

sim::core::SpawnProfile WaveSystem::BuildSpawnProfile(
    const int monster_id,
    const float spawn_time_sec,
    const sim::core::SpawnEvent& event
) const {
    const auto it_def = monster_def_by_id_.find(monster_id);
    if (it_def == monster_def_by_id_.end()) {
        // 加载阶段应已保证外键合法，这里只做兜底。
        return sim::core::SpawnProfile{};
    }
    const sim::core::MonsterDef& def = it_def->second;

    float g_atk = 1.0f;
    float g_interval = 1.0f;
    float g_range = 1.0f;
    float g_hp = 1.0f;
    float g_move = 1.0f;

    float t_atk = 1.0f;
    float t_interval = 1.0f;
    float t_range = 1.0f;
    float t_hp = 1.0f;
    float t_move = 1.0f;

    // 当前时间命中的难度阶段：
    // - monster_id=0 读全局系数 G_*
    // - monster_id=当前怪物 读类型系数 T_*（缺失按 1.0）
    for (const auto& row : difficulty_rows_) {
        if (spawn_time_sec < row.start_time_sec || spawn_time_sec >= row.end_time_sec) {
            continue;
        }
        if (row.monster_id == 0) {
            g_atk = row.global_atk_scale;
            g_interval = row.global_atk_interval_scale;
            g_range = row.global_atk_range_scale;
            g_hp = row.global_hp_scale;
            g_move = row.global_move_speed_scale;
        } else if (row.monster_id == monster_id) {
            t_atk = row.type_atk_scale;
            t_interval = row.type_atk_interval_scale;
            t_range = row.type_atk_range_scale;
            t_hp = row.type_hp_scale;
            t_move = row.type_move_speed_scale;
        }
    }

    sim::core::SpawnProfile p{};
    p.hp = def.base_hp * g_hp * t_hp * event.attr_hp_scale;
    p.attack_damage = def.base_attack * g_atk * t_atk * event.attr_atk_scale;
    p.move_speed = def.base_move_speed_px_sec * world_units_per_px_ * g_move * t_move * event.attr_spd_scale;
    p.attack_range = def.base_attack_range_px * world_units_per_px_ * g_range * t_range;
    p.attack_interval_sec = def.base_attack_interval_sec * g_interval * t_interval;
    return p;
}

void WaveSystem::Init(
    std::vector<sim::core::SpawnEvent> events,
    std::vector<sim::core::MonsterDef> monster_defs,
    std::vector<sim::core::DifficultyRow> difficulty_rows,
    const float fixed_dt,
    const float world_units_per_px,
    const std::uint32_t global_seed
) {
    events_.clear();
    events_.reserve(events.size());
    for (sim::core::SpawnEvent& e : events) {
        EventRuntime rt{};
        rt.event = std::move(e);
        rt.spawned_count = 0;
        rt.next_spawn_tick = rt.event.trigger_tick;
        events_.push_back(std::move(rt));
    }

    std::sort(events_.begin(), events_.end(), [](const EventRuntime& a, const EventRuntime& b) {
        if (a.event.trigger_tick != b.event.trigger_tick) {
            return a.event.trigger_tick < b.event.trigger_tick;
        }
        return a.event.event_id < b.event.event_id;
    });

    next_event_ = 0;
    current_tick_ = 0;
    fixed_dt_ = fixed_dt;
    world_units_per_px_ = world_units_per_px;
    global_seed_ = global_seed;

    monster_def_by_id_.clear();
    for (auto& d : monster_defs) {
        monster_def_by_id_[d.monster_id] = std::move(d);
    }
    difficulty_rows_ = std::move(difficulty_rows);
}

void WaveSystem::Tick(float current_time, sim::ecs::MonsterPool& pool, sim::io::MetricsLogger& metrics) {
    current_tick_ = static_cast<int>(std::lround(current_time / fixed_dt_));

    // 单向扫描事件窗口；每个事件按 interval_tick 逐个生成，贴合开发手册定义。
    for (std::size_t i = 0; i < events_.size(); ++i) {
        EventRuntime& rt = events_[i];
        const auto& e = rt.event;

        if (current_tick_ < e.trigger_tick) {
            continue;
        }
        if (rt.spawned_count >= e.count) {
            continue;
        }

        std::mt19937 local_rng(global_seed_ ^ static_cast<std::uint32_t>(e.seed_offset) ^ static_cast<std::uint32_t>(e.event_id));
        std::uniform_real_distribution<float> unit01(0.0f, 1.0f);
        std::uniform_real_distribution<float> unit_angle(0.0f, 2.0f * static_cast<float>(std::numbers::pi));

        int spawned_this_tick = 0;
        while (rt.spawned_count < e.count && current_tick_ >= rt.next_spawn_tick) {
            // 4.4 公式：时钟方向 -> 基准点 + 圆盘扰动。
            const float theta = static_cast<float>(e.spawn_angle_clock % 12) * (static_cast<float>(std::numbers::pi) / 6.0f);
            const float base_distance = e.spawn_distance_px * world_units_per_px_;
            const float cx = std::cos(theta) * base_distance;
            const float cy = std::sin(theta) * base_distance;

            const float r = std::sqrt(unit01(local_rng)) * (e.spawn_random_radius_px * world_units_per_px_);
            const float phi = unit_angle(local_rng);
            const float px = cx + std::cos(phi) * r;
            const float py = cy + std::sin(phi) * r;

            const float spawn_time_sec = static_cast<float>(rt.next_spawn_tick) * fixed_dt_;
            const sim::core::SpawnProfile p = BuildSpawnProfile(e.monster_id, spawn_time_sec, e);

            const int id = pool.Activate(
                px,
                py,
                p.hp,
                p.move_speed,
                p.attack_damage,
                p.attack_interval_sec,
                p.attack_range
            );
            if (id >= 0) {
                metrics.AddSpawnSuccess();
            } else {
                metrics.AddSpawnFail();
            }
            ++rt.spawned_count;
            ++spawned_this_tick;
            rt.next_spawn_tick += e.interval_tick;
        }

        if (spawned_this_tick > 0) {
            // 这里的“波次”对应 CSV 事件窗口 event_id。
            metrics.AddWaveSummary(
                static_cast<std::uint32_t>(e.event_id),
                e.trigger_time_sec,
                e.count,
                rt.spawned_count,
                static_cast<std::uint32_t>(pool.AliveCount())
            );
        }
    }
}

} // namespace sim::systems
