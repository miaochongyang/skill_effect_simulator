#include "systems/movement_system.hpp"

#include <algorithm>
#include <cmath>

#include "ecs/monster_pool.hpp"
#include "io/metrics_logger.hpp"

namespace sim::systems {

void MovementSystem::Tick(
    const float dt,
    sim::ecs::MonsterPool& pool,
    sim::core::Player& player,
    sim::io::MetricsLogger& metrics
) const {
    // 紧凑线性遍历 dense active 列表，最大化连续内存读取效率。
    for (const int id : pool.DenseActive()) {
        const float px = pool.X(id);
        const float py = pool.Y(id);
        const float len2 = px * px + py * py;

        if (len2 > 1e-6f) {
            const float inv_len = 1.0f / std::sqrt(len2);
            const float nx = px - px * inv_len * pool.Speed(id) * dt;
            const float ny = py - py * inv_len * pool.Speed(id) * dt;
            pool.SetPosition(id, nx, ny);
        }

        // 玩家受击改为离散攻击模型：
        // 当怪物进入攻击射程并且冷却就绪时触发一次攻击。
        pool.AddAttackCooldown(id, dt);
        const float attack_range = std::max(pool.AttackRange(id), player_contact_radius_);
        if (len2 <= attack_range * attack_range && pool.AttackCooldown(id) <= 0.0f) {
            const float damage = pool.AttackDamage(id) * player.final_damage_taken_multiplier;
            player.hp -= damage;
            metrics.AddDamageTaken(damage);
            pool.SetAttackCooldown(id, pool.AttackIntervalSec(id));
        }
    }
}

} // namespace sim::systems
