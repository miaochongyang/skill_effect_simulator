#pragma once

#include <vector>

#include "core/sim_types.hpp"

namespace sim::systems {

class SkillSystem {
public:
    void Init(std::vector<sim::core::SkillDef> defs);
    const std::vector<sim::core::HitboxEvent>& Tick(float dt);

private:
    std::vector<sim::core::SkillRuntime> skills_;
    std::vector<sim::core::HitboxEvent> emit_buffer_;
};

} // namespace sim::systems
