#include "ecs/monster_pool.hpp"

namespace sim::ecs {

void MonsterPool::Init(const std::size_t capacity) {
    x_.assign(capacity, 0.0f);
    y_.assign(capacity, 0.0f);
    hp_.assign(capacity, 0.0f);
    speed_.assign(capacity, 0.0f);
    attack_damage_.assign(capacity, 0.0f);
    attack_interval_sec_.assign(capacity, 1.0f);
    attack_range_.assign(capacity, 0.0f);
    attack_cooldown_.assign(capacity, 0.0f);
    active_.assign(capacity, 0);
    sparse_pos_.assign(capacity, -1);
    next_in_cell_.assign(capacity, -1);

    dense_active_.clear();
    dense_active_.reserve(capacity);
    free_ids_.clear();
    free_ids_.reserve(capacity);

    for (int i = static_cast<int>(capacity) - 1; i >= 0; --i) {
        free_ids_.push_back(i);
    }
}

int MonsterPool::Activate(
    const float x,
    const float y,
    const float hp,
    const float move_speed,
    const float attack_damage,
    const float attack_interval_sec,
    const float attack_range
) {
    if (free_ids_.empty()) {
        return -1;
    }
    const int id = free_ids_.back();
    free_ids_.pop_back();

    active_[id] = 1;
    x_[id] = x;
    y_[id] = y;
    hp_[id] = hp;
    speed_[id] = move_speed;
    attack_damage_[id] = attack_damage;
    attack_interval_sec_[id] = attack_interval_sec;
    attack_range_[id] = attack_range;
    attack_cooldown_[id] = 0.0f;
    next_in_cell_[id] = -1;

    sparse_pos_[id] = static_cast<int>(dense_active_.size());
    dense_active_.push_back(id);
    return id;
}

void MonsterPool::Deactivate(const int id) {
    if (active_[id] == 0) {
        return;
    }

    active_[id] = 0;
    hp_[id] = 0.0f;
    attack_cooldown_[id] = 0.0f;
    next_in_cell_[id] = -1;

    const int remove_pos = sparse_pos_[id];
    const int last_id = dense_active_.back();
    dense_active_[remove_pos] = last_id;
    sparse_pos_[last_id] = remove_pos;
    dense_active_.pop_back();

    sparse_pos_[id] = -1;
    free_ids_.push_back(id);
}

} // namespace sim::ecs
