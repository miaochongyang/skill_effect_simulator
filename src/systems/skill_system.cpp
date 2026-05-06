#include "systems/skill_system.hpp"

#include <utility>

namespace sim::systems {

void SkillSystem::Init(std::vector<sim::core::SkillDef> defs) {
    skills_.clear();
    skills_.reserve(defs.size());

    for (sim::core::SkillDef& def : defs) {
        sim::core::SkillRuntime rt{};
        rt.def = std::move(def);
        rt.cooldown_remaining = 0.0f;
        skills_.push_back(std::move(rt));
    }

    emit_buffer_.clear();
    emit_buffer_.reserve(skills_.size());
}

const std::vector<sim::core::HitboxEvent>& SkillSystem::Tick(const float dt) {
    emit_buffer_.clear();
    for (sim::core::SkillRuntime& s : skills_) {
        s.cooldown_remaining -= dt;
        if (s.cooldown_remaining <= 0.0f) {
            s.cooldown_remaining += s.def.cooldown;
            emit_buffer_.push_back(sim::core::HitboxEvent{
                s.def.skill_id, s.def.radius, s.def.damage, s.def.max_targets
            });
        }
    }
    return emit_buffer_;
}

} // namespace sim::systems
