#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "core/sim_types.hpp"

namespace sim::ecs {
class MonsterPool;
}

namespace sim::io {
class MetricsLogger;
}

namespace sim::spatial {
class UniformGrid;
}

namespace sim::systems {

class ProjectileWeaponSystem {
public:
    void Init(const sim::core::ProjectileWeaponDef& def, std::uint32_t seed);
    float Tick(
        float dt,
        const sim::core::Player& player,
        sim::ecs::MonsterPool& pool,
        const sim::spatial::UniformGrid& grid,
        sim::io::MetricsLogger& metrics
    );

private:
    struct ProjectileRuntime {
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float lifetime = 0.0f;
        float traveled = 0.0f;
        int pierce_left = 0;
        bool active = false;
        std::vector<int> hit_targets;
    };

    sim::core::ProjectileWeaponDef def_{};
    float cooldown_remaining_ = 0.0f;
    std::mt19937 rng_{42u};
    std::vector<ProjectileRuntime> projectiles_;

    int FindNearestTarget(const sim::ecs::MonsterPool& pool, float radius) const;
    void SpawnProjectilesToward(float target_x, float target_y, const sim::core::Player& player);
    float ResolveProjectiles(
        float dt,
        const sim::core::Player& player,
        sim::ecs::MonsterPool& pool,
        const sim::spatial::UniformGrid& grid,
        sim::io::MetricsLogger& metrics
    );
};

} // namespace sim::systems
