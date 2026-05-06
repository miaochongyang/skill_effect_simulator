#pragma once

#include <vector>

namespace sim::ecs {
class MonsterPool;
}

namespace sim::spatial {

// 均匀网格是本项目 broad-phase 的核心，用 O(N) 重建换取查询阶段的大幅降维。
class UniformGrid {
public:
    void Init(float half_extent, float cell_size);
    void Rebuild(sim::ecs::MonsterPool& pool);

    [[nodiscard]] bool PositionToCell(float x, float y, int& out_cx, int& out_cy) const noexcept;
    [[nodiscard]] int CellIndex(int cx, int cy) const noexcept { return cy * width_ + cx; }

    [[nodiscard]] int Width() const noexcept { return width_; }
    [[nodiscard]] int Height() const noexcept { return height_; }
    [[nodiscard]] int CellHead(int index) const noexcept { return cell_heads_[index]; }

private:
    float half_extent_ = 160.0f;
    float cell_size_ = 2.5f;
    int width_ = 0;
    int height_ = 0;

    // 每个 cell 仅存链表头索引，具体链由 MonsterPool::next_in_cell 维护。
    std::vector<int> cell_heads_;
};

} // namespace sim::spatial
