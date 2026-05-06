#include "io/metrics_logger.hpp"

#include <algorithm>
#include <fstream>

namespace sim::io {

void MetricsLogger::AddSkillDamage(const std::uint8_t skill_id, const float d) noexcept {
    if (skill_id < skill_total_damage_.size()) {
        skill_total_damage_[skill_id] += static_cast<double>(d);
    }
}

void MetricsLogger::UpdatePeakAlive(const std::size_t alive) noexcept {
    peak_alive_monsters_ = std::max(peak_alive_monsters_, alive);
}

void MetricsLogger::AddTimeline(const float time_sec, const std::uint32_t alive, const std::uint64_t kills) {
    timeline_.push_back(TimelineSample{time_sec, alive, kills});
}

void MetricsLogger::AddWaveSummary(
    const std::uint32_t wave_index,
    const float trigger_time_sec,
    const int planned_spawn_count,
    const int actual_spawn_count,
    const std::uint32_t alive_after_spawn
) {
    wave_summaries_.push_back(WaveSummary{
        wave_index,
        trigger_time_sec,
        planned_spawn_count,
        actual_spawn_count,
        alive_after_spawn,
        total_spawns_,
        failed_spawns_,
        total_kills_,
        damage_taken_
    });
}

void MetricsLogger::SaveCsv(
    const std::string& summary_path,
    const std::string& timeline_path,
    const std::string& wave_summary_path
) const {
    {
        std::ofstream out(summary_path);
        out << "metric,value\n";
        out << "simulated_time_sec," << simulated_time_sec_ << "\n";
        out << "total_spawns," << total_spawns_ << "\n";
        out << "failed_spawns," << failed_spawns_ << "\n";
        out << "total_kills," << total_kills_ << "\n";
        out << "damage_taken," << damage_taken_ << "\n";
        out << "peak_alive_monsters," << peak_alive_monsters_ << "\n";
        for (std::size_t i = 0; i < skill_total_damage_.size(); ++i) {
            if (skill_total_damage_[i] > 0.0) {
                out << "skill_" << i << "_damage," << skill_total_damage_[i] << "\n";
            }
        }
    }
    {
        std::ofstream out(timeline_path);
        out << "time_sec,alive_monsters,total_kills\n";
        for (const TimelineSample& s : timeline_) {
            out << s.time_sec << "," << s.alive << "," << s.kills << "\n";
        }
    }
    {
        std::ofstream out(wave_summary_path);
        out << "wave_index,trigger_time_sec,planned_spawn_count,actual_spawn_count,spawn_success_rate,"
               "alive_after_spawn,total_spawns,failed_spawns,total_kills,damage_taken\n";
        for (const WaveSummary& s : wave_summaries_) {
            const double success_rate = (s.planned_spawn_count > 0)
                ? (static_cast<double>(s.actual_spawn_count) / static_cast<double>(s.planned_spawn_count))
                : 0.0;
            out << s.wave_index << ","
                << s.trigger_time_sec << ","
                << s.planned_spawn_count << ","
                << s.actual_spawn_count << ","
                << success_rate << ","
                << s.alive_after_spawn << ","
                << s.total_spawns << ","
                << s.failed_spawns << ","
                << s.total_kills << ","
                << s.damage_taken << "\n";
        }
    }
}

} // namespace sim::io
