#include "systems/combat_system.hpp"

#include <algorithm>

#include "ecs/monster_pool.hpp"
#include "io/metrics_logger.hpp"
#include "spatial/uniform_grid.hpp"

namespace sim::systems {

void CombatSystem::Resolve(
    const std::vector<sim::core::HitboxEvent>& hitboxes,
    sim::ecs::MonsterPool& pool,
    const sim::spatial::UniformGrid& grid,
    sim::io::MetricsLogger& metrics
) {
    for (const sim::core::HitboxEvent& h : hitboxes) {
        int min_cx = 0;
        int min_cy = 0;
        int max_cx = 0;
        int max_cy = 0;

        if (!grid.PositionToCell(-h.radius, -h.radius, min_cx, min_cy)) {
            min_cx = 0;
            min_cy = 0;
        }
        if (!grid.PositionToCell(h.radius, h.radius, max_cx, max_cy)) {
            max_cx = grid.Width() - 1;
            max_cy = grid.Height() - 1;
        }

        min_cx = std::max(min_cx, 0);
        min_cy = std::max(min_cy, 0);
        max_cx = std::min(max_cx, grid.Width() - 1);
        max_cy = std::min(max_cy, grid.Height() - 1);

        int targets_hit = 0;
        const float radius2 = h.radius * h.radius;

        // 只扫描命中域覆盖格子，避免技能对全场怪物做距离判断。
        for (int cy = min_cy; cy <= max_cy; ++cy) {
            for (int cx = min_cx; cx <= max_cx; ++cx) {
                int id = grid.CellHead(grid.CellIndex(cx, cy));
                while (id != -1) {
                    const int next_id = pool.NextInCell(id);
                    if (pool.IsActive(id)) {
                        const float dx = pool.X(id);
                        const float dy = pool.Y(id);
                        if ((dx * dx + dy * dy) <= radius2) {
                            pool.AddDamage(id, h.damage);
                            metrics.AddSkillDamage(h.skill_id, h.damage);
                            ++targets_hit;

                            if (pool.HP(id) <= 0.0f) {
                                pool.Deactivate(id);
                                metrics.AddKill();
                            }
                            if (targets_hit >= h.max_targets) {
                                goto hitbox_done;
                            }
                        }
                    }
                    id = next_id;
                }
            }
        }
hitbox_done:
        continue;
    }
}

} // namespace sim::systems
