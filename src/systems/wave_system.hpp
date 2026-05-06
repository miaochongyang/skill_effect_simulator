#pragma once

#include <cstdint>
#include <unordered_map>
#include <random>
#include <vector>

#include "core/sim_types.hpp"

namespace sim::ecs {
class MonsterPool;
}

namespace sim::io {
class MetricsLogger;
}

namespace sim::systems {

class WaveSystem {
public:
    void Init(
        std::vector<sim::core::SpawnEvent> events,
        std::vector<sim::core::MonsterDef> monster_defs,
        std::vector<sim::core::DifficultyRow> difficulty_rows,
        float fixed_dt,
        float world_units_per_px,
        std::uint32_t global_seed
    );
    void Tick(float current_time, sim::ecs::MonsterPool& pool, sim::io::MetricsLogger& metrics);

private:
    struct EventRuntime {
        sim::core::SpawnEvent event{};
        int spawned_count = 0;
        int next_spawn_tick = 0;
    };

    std::vector<EventRuntime> events_;
    std::size_t next_event_ = 0;
    int current_tick_ = 0;
    float fixed_dt_ = 1.0f / 60.0f;
    float world_units_per_px_ = 0.05f;
    std::uint32_t global_seed_ = 42u;
    std::unordered_map<int, sim::core::MonsterDef> monster_def_by_id_;
    std::vector<sim::core::DifficultyRow> difficulty_rows_;

    sim::core::SpawnProfile BuildSpawnProfile(int monster_id, float spawn_time_sec, const sim::core::SpawnEvent& event) const;
};

} // namespace sim::systems
