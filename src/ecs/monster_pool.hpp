#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sim::ecs {

// MonsterPool 使用 SoA 存储，每个字段独立数组，保证批处理时更高缓存命中率。
class MonsterPool {
public:
    void Init(std::size_t capacity);
    [[nodiscard]] std::size_t Capacity() const noexcept { return x_.size(); }
    [[nodiscard]] std::size_t AliveCount() const noexcept { return dense_active_.size(); }

    int Activate(
        float x,
        float y,
        float hp,
        float move_speed,
        float attack_damage,
        float attack_interval_sec,
        float attack_range
    );
    void Deactivate(int id);

    // ===== 热路径访问器：系统层通过这些接口访问数据，避免暴露实现细节。 =====
    [[nodiscard]] const std::vector<int>& DenseActive() const noexcept { return dense_active_; }
    [[nodiscard]] std::vector<int>& DenseActive() noexcept { return dense_active_; }

    [[nodiscard]] float X(int id) const noexcept { return x_[id]; }
    [[nodiscard]] float Y(int id) const noexcept { return y_[id]; }
    [[nodiscard]] float HP(int id) const noexcept { return hp_[id]; }
    [[nodiscard]] float Speed(int id) const noexcept { return speed_[id]; }
    [[nodiscard]] float AttackDamage(int id) const noexcept { return attack_damage_[id]; }
    [[nodiscard]] float AttackIntervalSec(int id) const noexcept { return attack_interval_sec_[id]; }
    [[nodiscard]] float AttackRange(int id) const noexcept { return attack_range_[id]; }
    [[nodiscard]] float AttackCooldown(int id) const noexcept { return attack_cooldown_[id]; }
    [[nodiscard]] bool IsActive(int id) const noexcept { return active_[id] != 0; }
    [[nodiscard]] int NextInCell(int id) const noexcept { return next_in_cell_[id]; }

    void SetPosition(int id, float nx, float ny) noexcept {
        x_[id] = nx;
        y_[id] = ny;
    }
    void AddDamage(int id, float damage) noexcept { hp_[id] -= damage; }
    void SetNextInCell(int id, int next) noexcept { next_in_cell_[id] = next; }
    void SetAttackCooldown(int id, float t) noexcept { attack_cooldown_[id] = t; }
    void AddAttackCooldown(int id, float dt) noexcept { attack_cooldown_[id] -= dt; }

private:
    std::vector<float> x_;
    std::vector<float> y_;
    std::vector<float> hp_;
    std::vector<float> speed_;
    std::vector<float> attack_damage_;
    std::vector<float> attack_interval_sec_;
    std::vector<float> attack_range_;
    std::vector<float> attack_cooldown_;
    std::vector<std::uint8_t> active_;

    // dense + sparse 结构，保证删除实体时 O(1) 且遍历只遍历活跃实体。
    std::vector<int> dense_active_;
    std::vector<int> sparse_pos_;
    std::vector<int> free_ids_;

    // 空间网格桶链表 next 指针（索引链），避免每格动态分配小容器。
    std::vector<int> next_in_cell_;
};

} // namespace sim::ecs
