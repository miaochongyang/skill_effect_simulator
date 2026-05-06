#include "spatial/uniform_grid.hpp"

#include <algorithm>
#include <cmath>

#include "ecs/monster_pool.hpp"

namespace sim::spatial {

void UniformGrid::Init(const float half_extent, const float cell_size) {
    half_extent_ = half_extent;
    cell_size_ = cell_size;
    width_ = static_cast<int>(std::ceil((2.0f * half_extent_) / cell_size_));
    height_ = width_;
    cell_heads_.assign(static_cast<std::size_t>(width_ * height_), -1);
}

bool UniformGrid::PositionToCell(float x, float y, int& out_cx, int& out_cy) const noexcept {
    const float fx = (x + half_extent_) / cell_size_;
    const float fy = (y + half_extent_) / cell_size_;
    const int cx = static_cast<int>(fx);
    const int cy = static_cast<int>(fy);
    if (cx < 0 || cy < 0 || cx >= width_ || cy >= height_) {
        return false;
    }
    out_cx = cx;
    out_cy = cy;
    return true;
}

void UniformGrid::Rebuild(sim::ecs::MonsterPool& pool) {
    std::fill(cell_heads_.begin(), cell_heads_.end(), -1);
    for (const int id : pool.DenseActive()) {
        int cx = 0;
        int cy = 0;
        if (!PositionToCell(pool.X(id), pool.Y(id), cx, cy)) {
            pool.SetNextInCell(id, -1);
            continue;
        }
        const int cell = CellIndex(cx, cy);
        pool.SetNextInCell(id, cell_heads_[cell]);
        cell_heads_[cell] = id;
    }
}

} // namespace sim::spatial
