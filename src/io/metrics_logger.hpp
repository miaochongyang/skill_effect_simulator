#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sim::io {

class MetricsLogger {
public:
    struct TimelineSample {
        float time_sec = 0.0f;
        std::uint32_t alive = 0;
        std::uint64_t kills = 0;
    };
    struct WaveSummary {
        std::uint32_t wave_index = 0;
        float trigger_time_sec = 0.0f;
        int planned_spawn_count = 0;
        int actual_spawn_count = 0;
        std::uint32_t alive_after_spawn = 0;
        std::uint64_t total_spawns = 0;
        std::uint64_t failed_spawns = 0;
        std::uint64_t total_kills = 0;
        double damage_taken = 0.0;
    };

    void SetSimulatedTime(double t) noexcept { simulated_time_sec_ = t; }
    void AddSpawnSuccess() noexcept { ++total_spawns_; }
    void AddSpawnFail() noexcept { ++failed_spawns_; }
    void AddKill() noexcept { ++total_kills_; }
    void AddDamageTaken(float d) noexcept { damage_taken_ += static_cast<double>(d); }
    void AddSkillDamage(std::uint8_t skill_id, float d) noexcept;
    void UpdatePeakAlive(std::size_t alive) noexcept;
    void AddTimeline(float time_sec, std::uint32_t alive, std::uint64_t kills);
    void AddWaveSummary(
        std::uint32_t wave_index,
        float trigger_time_sec,
        int planned_spawn_count,
        int actual_spawn_count,
        std::uint32_t alive_after_spawn
    );

    [[nodiscard]] std::uint64_t TotalKills() const noexcept { return total_kills_; }
    [[nodiscard]] std::uint64_t TotalSpawns() const noexcept { return total_spawns_; }
    [[nodiscard]] std::uint64_t FailedSpawns() const noexcept { return failed_spawns_; }
    [[nodiscard]] double DamageTaken() const noexcept { return damage_taken_; }
    [[nodiscard]] double SimulatedTimeSec() const noexcept { return simulated_time_sec_; }
    [[nodiscard]] std::size_t PeakAliveMonsters() const noexcept { return peak_alive_monsters_; }

    void SaveCsv(
        const std::string& summary_path,
        const std::string& timeline_path,
        const std::string& wave_summary_path
    ) const;

private:
    double simulated_time_sec_ = 0.0;
    std::uint64_t total_spawns_ = 0;
    std::uint64_t failed_spawns_ = 0;
    std::uint64_t total_kills_ = 0;
    double damage_taken_ = 0.0;
    std::size_t peak_alive_monsters_ = 0;
    std::array<double, 64> skill_total_damage_{}; // 预留 64 个技能槽位。
    std::vector<TimelineSample> timeline_;
    std::vector<WaveSummary> wave_summaries_;
};

} // namespace sim::io
