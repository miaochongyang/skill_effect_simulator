#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace sim::systems {

enum class CollisionShape : std::uint8_t {
    Circle = 0,
    Aabb = 1,
    Sector = 2
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct SkillHit {
    std::uint32_t monster_id = 0;
    float damage = 0.0f;
    Vec2 knockback_delta{};
    float slow_multiplier = 1.0f; // Multiply this value to monster move speed.
    float slow_duration = 0.0f;   // External system stores remaining slow time on monster state.
};

// Def must provide POD-like numeric fields and external SoA monster views.
template <typename Def>
concept SkillDefContract = requires(const Def& d) {
    { d.cd } -> std::convertible_to<float>;
    { d.tick_interval } -> std::convertible_to<float>;
    { d.active_duration } -> std::convertible_to<float>;
    { d.radius } -> std::convertible_to<float>;
    { d.damage } -> std::convertible_to<float>;
    { d.max_targets } -> std::convertible_to<std::size_t>;

    { d.shape } -> std::convertible_to<CollisionShape>;
    { d.half_width } -> std::convertible_to<float>;
    { d.half_height } -> std::convertible_to<float>;
    { d.sector_angle_rad } -> std::convertible_to<float>;

    { d.knockback_distance } -> std::convertible_to<float>;
    { d.slow_ratio } -> std::convertible_to<float>;
    { d.slow_duration } -> std::convertible_to<float>;

    { d.monster_count } -> std::convertible_to<std::size_t>;
    { d.monster_ids } -> std::same_as<const std::uint32_t*>;
    { d.monster_x } -> std::same_as<const float*>;
    { d.monster_y } -> std::same_as<const float*>;
    { d.monster_mass } -> std::same_as<const float*>;
    { d.monster_knockback_resist } -> std::same_as<const float*>;
    { d.monster_slow_resist } -> std::same_as<const float*>;

    { d.out_hits } -> std::same_as<SkillHit*>;
    { d.out_hit_capacity } -> std::convertible_to<std::size_t>;
};

template <SkillDefContract Def>
class NumericSkillComponent final {
public:
    explicit NumericSkillComponent(const Def& def) noexcept : def_(&def) {}

    void update(float dt) noexcept {
        if (dt <= 0.0f || def_ == nullptr) {
            return;
        }

        affected_count_ = 0U;
        cooldown_remaining_ = std::max(0.0f, cooldown_remaining_ - dt);

        if (!active_) {
            return;
        }

        if (active_time_remaining_ > 0.0f) {
            active_time_remaining_ -= dt;
            if (active_time_remaining_ <= 0.0f) {
                active_ = false;
                tick_accumulator_ = 0.0f;
                return;
            }
        }

        const float tick_interval = def_->tick_interval;
        if (tick_interval <= 0.0f) {
            // Avoid division-by-zero and unbounded loops.
            return;
        }

        tick_accumulator_ += dt;
        while (tick_accumulator_ >= tick_interval) {
            tick_accumulator_ -= tick_interval;
            emitOneTick();
        }
    }

    void activate(Vec2 origin, Vec2 direction) noexcept {
        if (def_ == nullptr) {
            return;
        }
        if (cooldown_remaining_ > 0.0f) {
            return;
        }

        origin_ = origin;
        direction_ = normalizeSafe(direction, Vec2{1.0f, 0.0f});

        cooldown_remaining_ = std::max(0.0f, def_->cd);
        active_time_remaining_ = std::max(0.0f, def_->active_duration);
        tick_accumulator_ = 0.0f;
        affected_count_ = 0U;
        active_ = true;
    }

    [[nodiscard]] std::span<const SkillHit> getAffectedMonsters() const noexcept {
        if (def_ == nullptr || def_->out_hits == nullptr || affected_count_ == 0U) {
            return {};
        }
        return std::span<const SkillHit>(def_->out_hits, affected_count_);
    }

private:
    static constexpr float kEps = 1e-6f;
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTwoPi = 2.0f * kPi;

    const Def* def_ = nullptr;
    Vec2 origin_{};
    Vec2 direction_{1.0f, 0.0f};

    float cooldown_remaining_ = 0.0f;
    float tick_accumulator_ = 0.0f;
    float active_time_remaining_ = 0.0f;

    std::uint32_t affected_count_ = 0;
    bool active_ = false;

    [[nodiscard]] static float dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }

    [[nodiscard]] static float lengthSq(Vec2 v) noexcept { return dot(v, v); }

    [[nodiscard]] static Vec2 normalizeSafe(Vec2 v, Vec2 fallback) noexcept {
        const float l2 = lengthSq(v);
        if (l2 <= kEps) {
            return fallback;
        }
        const float inv_len = 1.0f / std::sqrt(l2);
        return Vec2{v.x * inv_len, v.y * inv_len};
    }

    [[nodiscard]] static float validateSectorAngle(float angle_rad) noexcept {
        if (angle_rad <= 0.0f) {
            return 0.0f;
        }
        if (angle_rad > kTwoPi) {
            return kTwoPi;
        }
        return angle_rad;
    }

    [[nodiscard]] bool intersectsShape(Vec2 monster_pos) const noexcept {
        const Vec2 rel{monster_pos.x - origin_.x, monster_pos.y - origin_.y};

        switch (static_cast<CollisionShape>(def_->shape)) {
        case CollisionShape::Circle: {
            const float r = std::max(0.0f, def_->radius);
            return lengthSq(rel) <= (r * r);
        }
        case CollisionShape::Aabb: {
            const float hw = std::max(0.0f, def_->half_width);
            const float hh = std::max(0.0f, def_->half_height);
            return std::abs(rel.x) <= hw && std::abs(rel.y) <= hh;
        }
        case CollisionShape::Sector: {
            const float r = std::max(0.0f, def_->radius);
            const float angle = validateSectorAngle(def_->sector_angle_rad);
            if (angle <= 0.0f) {
                return false;
            }
            if (angle >= kTwoPi) {
                return lengthSq(rel) <= (r * r);
            }

            const float dist2 = lengthSq(rel);
            if (dist2 > (r * r)) {
                return false;
            }

            const Vec2 n = normalizeSafe(rel, direction_);
            const float cos_half = std::cos(0.5f * angle);
            return dot(n, direction_) >= cos_half;
        }
        default:
            return false;
        }
    }

    void emitOneTick() noexcept {
        if (def_ == nullptr || def_->out_hits == nullptr) {
            return;
        }
        if (def_->radius <= 0.0f || def_->damage <= 0.0f || def_->max_targets == 0U || def_->out_hit_capacity == 0U) {
            return;
        }

        const std::size_t max_hits = std::min(def_->max_targets, def_->out_hit_capacity);
        if (max_hits == 0U) {
            return;
        }

        const float slow_ratio = std::clamp(def_->slow_ratio, 0.0f, 1.0f);

        std::size_t written = 0U;
        for (std::size_t i = 0; i < def_->monster_count; ++i) {
            if (written >= max_hits) {
                break;
            }

            const Vec2 mp{def_->monster_x[i], def_->monster_y[i]};
            if (!intersectsShape(mp)) {
                continue;
            }

            const Vec2 radial = normalizeSafe(Vec2{mp.x - origin_.x, mp.y - origin_.y}, direction_);
            const float mass = std::max(def_->monster_mass[i], kEps);
            const float kb_resist = std::clamp(def_->monster_knockback_resist[i], 0.0f, 1.0f);
            const float slow_resist = std::clamp(def_->monster_slow_resist[i], 0.0f, 1.0f);

            const float kb_scale = std::max(0.0f, 1.0f - kb_resist) / mass;
            const float kb_len = std::max(0.0f, def_->knockback_distance) * kb_scale;
            const Vec2 kb_delta{radial.x * kb_len, radial.y * kb_len};

            SkillHit& h = def_->out_hits[written];
            h.monster_id = def_->monster_ids[i];
            h.damage = def_->damage;
            h.knockback_delta = kb_delta;
            h.slow_multiplier = 1.0f - (slow_ratio * (1.0f - slow_resist));
            h.slow_duration = std::max(0.0f, def_->slow_duration);

            ++written;
        }

        affected_count_ = static_cast<std::uint32_t>(written);
    }
};

template <SkillDefContract Def>
static_assert(sizeof(NumericSkillComponent<Def>) <= 64U, "NumericSkillComponent state exceeds 64 bytes.");

} // namespace sim::systems
