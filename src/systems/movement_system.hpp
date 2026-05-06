#pragma once

#include "core/sim_types.hpp"

namespace sim::ecs {
class MonsterPool;
}

namespace sim::io {
class MetricsLogger;
}

namespace sim::systems {

class MovementSystem {
public:
    explicit MovementSystem(float player_contact_radius) : player_contact_radius_(player_contact_radius) {}
    void Tick(float dt, sim::ecs::MonsterPool& pool, sim::core::Player& player, sim::io::MetricsLogger& metrics) const;

private:
    float player_contact_radius_ = 0.9f;
};

} // namespace sim::systems
