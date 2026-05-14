#include "systems/projectile_weapon_system.hpp"

#include <algorithm>
#include <cmath>

#include "ecs/monster_pool.hpp"
#include "io/metrics_logger.hpp"
#include "spatial/uniform_grid.hpp"

namespace sim::systems {

void ProjectileWeaponSystem::Init(const sim::core::ProjectileWeaponDef& def, const std::uint32_t seed) {
    def_ = def;
    cooldown_remaining_ = 0.0f;
    rng_.seed(seed);
    projectiles_.clear();
    projectiles_.reserve(4096);
}

int ProjectileWeaponSystem::FindNearestTarget(const sim::ecs::MonsterPool& pool, const float radius) const {
    const float radius2 = radius * radius;
    int best_id = -1;
    float best_dist2 = radius2;
    for (const int id : pool.DenseActive()) {
        const float dx = pool.X(id);
        const float dy = pool.Y(id);
        const float dist2 = dx * dx + dy * dy;
        if (dist2 <= best_dist2) {
            best_dist2 = dist2;
            best_id = id;
        }
    }
    return best_id;
}

void ProjectileWeaponSystem::SpawnProjectilesToward(
    const float target_x,
    const float target_y,
    const sim::core::Player& player
) {
    float dir_x = target_x;
    float dir_y = target_y;
    const float len2 = dir_x * dir_x + dir_y * dir_y;
    if (len2 <= 1e-6f) {
        dir_x = 1.0f;
        dir_y = 0.0f;
    } else {
        const float inv_len = 1.0f / std::sqrt(len2);
        dir_x *= inv_len;
        dir_y *= inv_len;
    }

    const int projectile_count = std::max(1, def_.projectiles_per_shot + player.projectile_count_bonus);
    for (int i = 0; i < projectile_count; ++i) {
        ProjectileRuntime p{};
        p.x = dir_x * def_.spawn_offset;
        p.y = dir_y * def_.spawn_offset;
        p.vx = dir_x * def_.projectile_speed;
        p.vy = dir_y * def_.projectile_speed;
        p.lifetime = 0.0f;
        p.traveled = 0.0f;
        p.pierce_left = def_.pierce_count;
        p.active = true;
        p.hit_targets.clear();
        projectiles_.push_back(p);
    }
}

float ProjectileWeaponSystem::ResolveProjectiles(
    const float dt,
    const sim::core::Player& player,
    sim::ecs::MonsterPool& pool,
    const sim::spatial::UniformGrid& grid,
    sim::io::MetricsLogger& metrics
) {
    float damage_dealt_total = 0.0f;
    const float hit_radius = std::max(def_.collision_radius * player.aoe_radius_multiplier, 0.01f);
    const float hit_radius2 = hit_radius * hit_radius;
    const float max_lifetime = def_.max_lifetime;
    const float max_distance = def_.max_travel_distance;
    std::uniform_real_distribution<float> unit01(0.0f, 1.0f);

    for (ProjectileRuntime& p : projectiles_) {
        if (!p.active) {
            continue;
        }

        const float prev_x = p.x;
        const float prev_y = p.y;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.lifetime += dt;
        p.traveled += std::sqrt((p.x - prev_x) * (p.x - prev_x) + (p.y - prev_y) * (p.y - prev_y));

        if ((max_lifetime > 0.0f && p.lifetime > max_lifetime) ||
            (max_distance > 0.0f && p.traveled > max_distance)) {
            p.active = false;
            continue;
        }

        int cx = 0;
        int cy = 0;
        if (!grid.PositionToCell(p.x, p.y, cx, cy)) {
            if (def_.destroy_on_block) {
                p.active = false;
            }
            continue;
        }

        // 仅扫描当前弹体所在格及周边邻域，避免全图扫描。
        const int min_cx = std::max(0, cx - 1);
        const int max_cx = std::min(grid.Width() - 1, cx + 1);
        const int min_cy = std::max(0, cy - 1);
        const int max_cy = std::min(grid.Height() - 1, cy + 1);

        int targets_hit_this_tick = 0;
        for (int y = min_cy; y <= max_cy; ++y) {
            for (int x = min_cx; x <= max_cx; ++x) {
                int id = grid.CellHead(grid.CellIndex(x, y));
                while (id != -1) {
                    const int next_id = pool.NextInCell(id);
                    if (pool.IsActive(id)) {
                        const float dx = pool.X(id) - p.x;
                        const float dy = pool.Y(id) - p.y;
                        if (dx * dx + dy * dy <= hit_radius2) {
                            if (!def_.can_hit_same_target_again &&
                                std::find(p.hit_targets.begin(), p.hit_targets.end(), id) != p.hit_targets.end()) {
                                id = next_id;
                                continue;
                            }

                            float damage = def_.base_damage * player.attack_power * player.damage_multiplier;
                            const float final_crit_chance = std::clamp(def_.crit_chance + player.crit_chance, 0.0f, 0.95f);
                            const float final_crit_multiplier = std::max(1.0f, def_.crit_multiplier * player.crit_multiplier);
                            if (unit01(rng_) < final_crit_chance) {
                                damage *= final_crit_multiplier;
                            }
                            pool.AddDamage(id, damage);
                            metrics.AddSkillDamage(0, damage);
                            damage_dealt_total += damage;
                            p.hit_targets.push_back(id);

                            if (def_.knockback_distance > 0.0f) {
                                const float mx = pool.X(id);
                                const float my = pool.Y(id);
                                const float mlen2 = mx * mx + my * my;
                                if (mlen2 > 1e-6f) {
                                    const float inv_len = 1.0f / std::sqrt(mlen2);
                                    pool.SetPosition(
                                        id,
                                        mx + mx * inv_len * def_.knockback_distance,
                                        my + my * inv_len * def_.knockback_distance
                                    );
                                }
                            }
                            ++targets_hit_this_tick;
                            if (pool.HP(id) <= 0.0f) {
                                pool.Deactivate(id);
                                metrics.AddKill();
                            }
                            if (targets_hit_this_tick >= std::max(1, def_.max_targets_per_tick)) {
                                goto projectile_done;
                            }
                            if (def_.destroy_on_block && p.pierce_left <= 0) {
                                p.active = false;
                                goto projectile_done;
                            }
                            if (p.pierce_left > 0) {
                                --p.pierce_left;
                            }
                        }
                    }
                    id = next_id;
                }
            }
        }
projectile_done:
        continue;
    }

    // 紧凑回收无效弹体，控制内存增长。
    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(), [](const ProjectileRuntime& p) { return !p.active; }),
        projectiles_.end()
    );
    return damage_dealt_total;
}

float ProjectileWeaponSystem::Tick(
    const float dt,
    const sim::core::Player& player,
    sim::ecs::MonsterPool& pool,
    const sim::spatial::UniformGrid& grid,
    sim::io::MetricsLogger& metrics
) {
    cooldown_remaining_ -= dt;
    if (cooldown_remaining_ <= 0.0f) {
        const float cdr = std::clamp(player.cooldown_reduction, 0.0f, 0.9f);
        const float spd = std::max(player.attack_speed, 1e-6f);
        const float final_cooldown = std::max(dt, def_.cooldown * (1.0f - cdr) / spd);
        cooldown_remaining_ += final_cooldown;
        const int target_id = FindNearestTarget(pool, def_.targeting_search_radius);
        if (target_id >= 0) {
            SpawnProjectilesToward(pool.X(target_id), pool.Y(target_id), player);
        } else if (def_.targeting_fallback_mode == "forward") {
            SpawnProjectilesToward(1.0f, 0.0f, player);
        }
    }

    return ResolveProjectiles(dt, player, pool, grid, metrics);
}

} // namespace sim::systems
