#pragma once

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

class CombatSystem {
public:
    static void Resolve(
        const std::vector<sim::core::HitboxEvent>& hitboxes,
        sim::ecs::MonsterPool& pool,
        const sim::spatial::UniformGrid& grid,
        sim::io::MetricsLogger& metrics
    );
};

} // namespace sim::systems
