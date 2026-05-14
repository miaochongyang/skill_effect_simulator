#include "io/config_loader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "io/json_value.hpp"

namespace sim::io {
namespace {

std::string SanitizeCsvCell(std::string cell) {
    if (!cell.empty() && static_cast<unsigned char>(cell[0]) == 0xEF &&
        cell.size() >= 3 &&
        static_cast<unsigned char>(cell[1]) == 0xBB &&
        static_cast<unsigned char>(cell[2]) == 0xBF) {
        cell.erase(0, 3); // UTF-8 BOM
    }
    while (!cell.empty() && (cell.back() == '\r' || cell.back() == '\n' || cell.back() == ' ' || cell.back() == '\t')) {
        cell.pop_back();
    }
    std::size_t begin = 0;
    while (begin < cell.size() && (cell[begin] == ' ' || cell[begin] == '\t')) {
        ++begin;
    }
    if (begin > 0) {
        cell.erase(0, begin);
    }
    return cell;
}

double GetNumber(const JsonValue::Object& obj, const char* key, const double default_value, const bool required) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        if (required) {
            throw std::runtime_error(std::string("Missing required number field: ") + key);
        }
        return default_value;
    }
    if (!it->second.IsNumber()) {
        throw std::runtime_error(std::string("Field is not number: ") + key);
    }
    return it->second.AsNumber();
}

int GetInt(const JsonValue::Object& obj, const char* key, const int default_value, const bool required) {
    return static_cast<int>(GetNumber(obj, key, static_cast<double>(default_value), required));
}

std::string GetString(
    const JsonValue::Object& obj,
    const char* key,
    const std::string& default_value,
    const bool required
) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        if (required) {
            throw std::runtime_error(std::string("Missing required string field: ") + key);
        }
        return default_value;
    }
    if (!it->second.IsString()) {
        throw std::runtime_error(std::string("Field is not string: ") + key);
    }
    return it->second.AsString();
}

} // namespace

std::string ConfigLoader::ReadFileToString(const std::string& path) {
    auto ResolveDataPath = [](const std::string& rel_or_abs_path) -> std::string {
        namespace fs = std::filesystem;
        const fs::path raw(rel_or_abs_path);
        if (raw.is_absolute()) {
            return raw.string();
        }
        const fs::path cwd = fs::current_path();
        const std::vector<fs::path> candidates = {
            cwd / rel_or_abs_path,
            cwd / ".." / rel_or_abs_path,
            cwd / ".." / ".." / rel_or_abs_path,
            cwd / ".." / ".." / ".." / rel_or_abs_path
        };
        for (const auto& p : candidates) {
            std::error_code ec;
            if (fs::exists(p, ec) && !ec) {
                return p.lexically_normal().string();
            }
        }
        return rel_or_abs_path;
    };

    const std::string resolved_path = ResolveDataPath(path);
    std::ifstream in(resolved_path, std::ios::in | std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open file: " + resolved_path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<sim::core::SpawnEvent> ConfigLoader::LoadSpawnEventsCsv(
    const std::string& path,
    const std::string& scenario_id,
    const std::string& variant_id,
    const float fixed_dt,
    const int max_spawn_per_tick_budget,
    int& out_level_duration_sec
) {
    auto ResolveDataPath = [](const std::string& rel_or_abs_path) -> std::string {
        namespace fs = std::filesystem;
        const fs::path raw(rel_or_abs_path);
        if (raw.is_absolute()) {
            return raw.string();
        }
        const fs::path cwd = fs::current_path();
        const std::vector<fs::path> candidates = {
            cwd / rel_or_abs_path,
            cwd / ".." / rel_or_abs_path,
            cwd / ".." / ".." / rel_or_abs_path,
            cwd / ".." / ".." / ".." / rel_or_abs_path
        };
        for (const auto& p : candidates) {
            std::error_code ec;
            if (fs::exists(p, ec) && !ec) {
                return p.lexically_normal().string();
            }
        }
        return rel_or_abs_path;
    };

    const std::string resolved_path = ResolveDataPath(path);
    std::ifstream in(resolved_path);
    if (!in) {
        throw std::runtime_error("Cannot open file: " + resolved_path);
    }

    auto SplitCsvLine = [](const std::string& line) -> std::vector<std::string> {
        std::vector<std::string> cols;
        std::string token;
        token.reserve(32);
        bool in_quotes = false;
        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes;
                continue;
            }
            if (c == ',' && !in_quotes) {
                cols.push_back(token);
                token.clear();
                continue;
            }
            token.push_back(c);
        }
        cols.push_back(SanitizeCsvCell(token));
        for (std::string& c : cols) {
            c = SanitizeCsvCell(std::move(c));
        }
        return cols;
    };

    auto ToInt = [](const std::string& s, const char* field_name, int event_id) -> int {
        try {
            return std::stoi(s);
        } catch (...) {
            throw std::runtime_error(
                "CSV parse error: field=" + std::string(field_name) + ", event_id=" + std::to_string(event_id) +
                ", value=" + s
            );
        }
    };
    auto ToFloat = [](const std::string& s, const char* field_name, int event_id) -> float {
        try {
            return std::stof(s);
        } catch (...) {
            throw std::runtime_error(
                "CSV parse error: field=" + std::string(field_name) + ", event_id=" + std::to_string(event_id) +
                ", value=" + s
            );
        }
    };

    std::string header_line;
    if (!std::getline(in, header_line)) {
        throw std::runtime_error("spawn_events.csv is empty.");
    }
    const std::vector<std::string> headers = SplitCsvLine(header_line);
    std::map<std::string, int> idx;
    for (int i = 0; i < static_cast<int>(headers.size()); ++i) {
        idx[headers[i]] = i;
    }

    const std::vector<std::string> required_fields = {
        "schema_version", "scenario_id", "variant_id", "level_duration_sec", "event_id",
        "trigger_time_sec", "trigger_tick", "monster_id", "count", "spawn_angle_clock",
        "spawn_distance_px", "spawn_random_radius_px", "interval_tick", "spawn_zone_id",
        "seed_offset", "attr_hp_scale", "attr_atk_scale", "attr_spd_scale", "phase_tag", "notes"
    };
    for (const auto& f : required_fields) {
        if (idx.find(f) == idx.end()) {
            throw std::runtime_error("spawn_events.csv missing required column: " + f);
        }
    }

    std::vector<sim::core::SpawnEvent> out;
    std::unordered_set<int> event_id_set;
    std::optional<int> level_duration;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> cols = SplitCsvLine(line);
        if (cols.size() < headers.size()) {
            cols.resize(headers.size());
        }
        auto Get = [&](const char* key) -> const std::string& { return cols[idx.at(key)]; };

        if (Get("scenario_id") != scenario_id || Get("variant_id") != variant_id) {
            continue;
        }

        sim::core::SpawnEvent e{};
        e.schema_version = Get("schema_version");
        e.scenario_id = Get("scenario_id");
        e.variant_id = Get("variant_id");
        e.level_duration_sec = ToInt(Get("level_duration_sec"), "level_duration_sec", -1);
        e.event_id = ToInt(Get("event_id"), "event_id", -1);
        e.trigger_time_sec = ToFloat(Get("trigger_time_sec"), "trigger_time_sec", e.event_id);
        e.trigger_tick = ToInt(Get("trigger_tick"), "trigger_tick", e.event_id);
        e.monster_id = ToInt(Get("monster_id"), "monster_id", e.event_id);
        e.count = ToInt(Get("count"), "count", e.event_id);
        e.spawn_angle_clock = ToInt(Get("spawn_angle_clock"), "spawn_angle_clock", e.event_id);
        e.spawn_distance_px = ToFloat(Get("spawn_distance_px"), "spawn_distance_px", e.event_id);
        e.spawn_random_radius_px = ToFloat(Get("spawn_random_radius_px"), "spawn_random_radius_px", e.event_id);
        e.interval_tick = ToInt(Get("interval_tick"), "interval_tick", e.event_id);
        e.spawn_zone_id = ToInt(Get("spawn_zone_id"), "spawn_zone_id", e.event_id);
        e.seed_offset = ToInt(Get("seed_offset"), "seed_offset", e.event_id);
        e.attr_hp_scale = ToFloat(Get("attr_hp_scale"), "attr_hp_scale", e.event_id);
        e.attr_atk_scale = ToFloat(Get("attr_atk_scale"), "attr_atk_scale", e.event_id);
        e.attr_spd_scale = ToFloat(Get("attr_spd_scale"), "attr_spd_scale", e.event_id);
        e.phase_tag = Get("phase_tag");
        e.notes = Get("notes");

        // ===== 文档规则校验 =====
        if (e.schema_version != "v1") {
            throw std::runtime_error("Unsupported schema_version for event_id=" + std::to_string(e.event_id));
        }
        if (e.level_duration_sec <= 0) {
            throw std::runtime_error("level_duration_sec must be > 0, event_id=" + std::to_string(e.event_id));
        }
        if (e.trigger_tick < 0) {
            throw std::runtime_error("trigger_tick must be >= 0, event_id=" + std::to_string(e.event_id));
        }
        // 额外一致性校验：若 trigger_time_sec 与 fixed_dt 推导出的 tick 明显不一致，
        // 说明 CSV 可能基于另一套 fixed_dt 生成，会导致整条时间轴提前/延后。
        const int expected_tick_from_time = static_cast<int>(std::lround(static_cast<double>(e.trigger_time_sec) / fixed_dt));
        if (std::abs(expected_tick_from_time - e.trigger_tick) > 1) {
            throw std::runtime_error(
                "trigger_tick mismatch with trigger_time_sec, event_id=" + std::to_string(e.event_id) +
                ", trigger_time_sec=" + std::to_string(e.trigger_time_sec) +
                ", trigger_tick=" + std::to_string(e.trigger_tick) +
                ", expected_tick=" + std::to_string(expected_tick_from_time) +
                " under fixed_dt=" + std::to_string(fixed_dt)
            );
        }
        const int max_tick = static_cast<int>(std::llround(static_cast<double>(e.level_duration_sec) / fixed_dt));
        if (e.trigger_tick > max_tick) {
            throw std::runtime_error("trigger_tick exceeds level duration, event_id=" + std::to_string(e.event_id));
        }
        if (e.event_id < 1 || !event_id_set.insert(e.event_id).second) {
            throw std::runtime_error("event_id invalid or duplicated: " + std::to_string(e.event_id));
        }
        if (e.count < 1 || e.interval_tick < 1) {
            throw std::runtime_error("count/interval_tick invalid, event_id=" + std::to_string(e.event_id));
        }
        if (e.spawn_angle_clock < 1 || e.spawn_angle_clock > 12) {
            throw std::runtime_error("spawn_angle_clock out of range, event_id=" + std::to_string(e.event_id));
        }
        if (e.spawn_distance_px <= 0.0f || e.spawn_random_radius_px < 0.0f) {
            throw std::runtime_error("spawn radius invalid, event_id=" + std::to_string(e.event_id));
        }
        if (e.attr_hp_scale < 1.0f || e.attr_atk_scale < 1.0f || e.attr_spd_scale < 1.0f) {
            throw std::runtime_error("attr scale must be >= 1.0, event_id=" + std::to_string(e.event_id));
        }
        if (e.phase_tag != "build_up" && e.phase_tag != "sustain" && e.phase_tag != "burst" && e.phase_tag != "rest") {
            throw std::runtime_error("phase_tag invalid, event_id=" + std::to_string(e.event_id));
        }

        if (!level_duration.has_value()) {
            level_duration = e.level_duration_sec;
        } else if (*level_duration != e.level_duration_sec) {
            throw std::runtime_error("level_duration_sec must be consistent within scenario+variant.");
        }

        out.push_back(std::move(e));
    }

    if (out.empty()) {
        throw std::runtime_error("No spawn events matched scenario_id=" + scenario_id + ", variant_id=" + variant_id);
    }

    std::sort(out.begin(), out.end(), [](const sim::core::SpawnEvent& a, const sim::core::SpawnEvent& b) {
        if (a.trigger_tick != b.trigger_tick) {
            return a.trigger_tick < b.trigger_tick;
        }
        return a.event_id < b.event_id;
    });

    for (std::size_t i = 1; i < out.size(); ++i) {
        if (out[i].trigger_tick < out[i - 1].trigger_tick) {
            throw std::runtime_error("events must be sorted by trigger_tick (non-decreasing).");
        }
    }

    std::map<int, int> per_tick_budget;
    for (const auto& e : out) {
        // 保守预算校验：同一 trigger_tick 上的瞬时并发生成量不能超预算。
        per_tick_budget[e.trigger_tick] += e.count;
    }
    for (const auto& kv : per_tick_budget) {
        if (kv.second > max_spawn_per_tick_budget) {
            throw std::runtime_error(
                "spawn budget exceeded at tick=" + std::to_string(kv.first) +
                ", count=" + std::to_string(kv.second) +
                ", budget=" + std::to_string(max_spawn_per_tick_budget)
            );
        }
    }

    out_level_duration_sec = level_duration.value_or(180);
    return out;
}

std::vector<sim::core::MonsterDef> ConfigLoader::LoadMonsterDefCsv(const std::string& path) {
    auto ResolveDataPath = [](const std::string& rel_or_abs_path) -> std::string {
        namespace fs = std::filesystem;
        const fs::path raw(rel_or_abs_path);
        if (raw.is_absolute()) {
            return raw.string();
        }
        const fs::path cwd = fs::current_path();
        const std::vector<fs::path> candidates = {
            cwd / rel_or_abs_path,
            cwd / ".." / rel_or_abs_path,
            cwd / ".." / ".." / rel_or_abs_path,
            cwd / ".." / ".." / ".." / rel_or_abs_path
        };
        for (const auto& p : candidates) {
            std::error_code ec;
            if (fs::exists(p, ec) && !ec) {
                return p.lexically_normal().string();
            }
        }
        return rel_or_abs_path;
    };

    auto SplitCsvLine = [](const std::string& line) -> std::vector<std::string> {
        std::vector<std::string> cols;
        std::string token;
        bool in_quotes = false;
        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes;
                continue;
            }
            if (c == ',' && !in_quotes) {
                cols.push_back(token);
                token.clear();
                continue;
            }
            token.push_back(c);
        }
        cols.push_back(SanitizeCsvCell(token));
        for (std::string& c : cols) {
            c = SanitizeCsvCell(std::move(c));
        }
        return cols;
    };

    const std::string resolved_path = ResolveDataPath(path);
    std::ifstream in(resolved_path);
    if (!in) {
        throw std::runtime_error("Cannot open file: " + resolved_path);
    }

    std::string header_line;
    if (!std::getline(in, header_line)) {
        throw std::runtime_error("monster_def.csv is empty.");
    }
    const std::vector<std::string> headers = SplitCsvLine(header_line);
    std::map<std::string, int> idx;
    for (int i = 0; i < static_cast<int>(headers.size()); ++i) {
        idx[headers[i]] = i;
    }

    const std::vector<std::string> required_fields = {
        "schema_version", "monster_id", "monster_name", "base_attack", "base_attack_interval_sec",
        "base_attack_range_px", "base_hp", "base_move_speed_px_sec", "notes"
    };
    for (const auto& f : required_fields) {
        if (idx.find(f) == idx.end()) {
            throw std::runtime_error("monster_def.csv missing required column: " + f);
        }
    }

    auto ToInt = [](const std::string& s, const char* field_name, int key) -> int {
        try {
            return std::stoi(s);
        } catch (...) {
            throw std::runtime_error(
                "monster_def parse error: field=" + std::string(field_name) +
                ", monster_id=" + std::to_string(key) + ", value=" + s
            );
        }
    };
    auto ToFloat = [](const std::string& s, const char* field_name, int key) -> float {
        try {
            return std::stof(s);
        } catch (...) {
            throw std::runtime_error(
                "monster_def parse error: field=" + std::string(field_name) +
                ", monster_id=" + std::to_string(key) + ", value=" + s
            );
        }
    };

    std::vector<sim::core::MonsterDef> out;
    std::unordered_set<int> id_set;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> cols = SplitCsvLine(line);
        if (cols.size() < headers.size()) {
            cols.resize(headers.size());
        }
        auto Get = [&](const char* key) -> const std::string& { return cols[idx.at(key)]; };

        sim::core::MonsterDef d{};
        d.schema_version = Get("schema_version");
        d.monster_id = ToInt(Get("monster_id"), "monster_id", -1);
        d.monster_name = Get("monster_name");
        d.base_attack = ToFloat(Get("base_attack"), "base_attack", d.monster_id);
        d.base_attack_interval_sec = ToFloat(Get("base_attack_interval_sec"), "base_attack_interval_sec", d.monster_id);
        d.base_attack_range_px = ToFloat(Get("base_attack_range_px"), "base_attack_range_px", d.monster_id);
        d.base_hp = ToFloat(Get("base_hp"), "base_hp", d.monster_id);
        d.base_move_speed_px_sec = ToFloat(Get("base_move_speed_px_sec"), "base_move_speed_px_sec", d.monster_id);
        d.notes = Get("notes");

        if (d.schema_version != "v1") {
            throw std::runtime_error("Unsupported monster_def schema_version, monster_id=" + std::to_string(d.monster_id));
        }
        if (d.monster_id < 1 || !id_set.insert(d.monster_id).second) {
            throw std::runtime_error("monster_id invalid or duplicated: " + std::to_string(d.monster_id));
        }
        if (d.monster_name.empty()) {
            throw std::runtime_error("monster_name cannot be empty, monster_id=" + std::to_string(d.monster_id));
        }
        if (d.base_attack < 0.0f) {
            throw std::runtime_error("base_attack must be >= 0, monster_id=" + std::to_string(d.monster_id));
        }
        if (d.base_attack_interval_sec <= 0.0f) {
            throw std::runtime_error("base_attack_interval_sec must be > 0, monster_id=" + std::to_string(d.monster_id));
        }
        if (d.base_attack_range_px < 0.0f) {
            throw std::runtime_error("base_attack_range_px must be >= 0, monster_id=" + std::to_string(d.monster_id));
        }
        if (d.base_hp < 1.0f) {
            throw std::runtime_error("base_hp must be >= 1, monster_id=" + std::to_string(d.monster_id));
        }
        if (d.base_move_speed_px_sec < 0.0f) {
            throw std::runtime_error("base_move_speed_px_sec must be >= 0, monster_id=" + std::to_string(d.monster_id));
        }
        out.push_back(std::move(d));
    }

    if (out.empty()) {
        throw std::runtime_error("monster_def.csv has no valid records.");
    }
    return out;
}

std::vector<sim::core::DifficultyRow> ConfigLoader::LoadLevelDifficultyCsv(
    const std::string& path,
    const std::string& scenario_id,
    const std::string& variant_id
) {
    auto ResolveDataPath = [](const std::string& rel_or_abs_path) -> std::string {
        namespace fs = std::filesystem;
        const fs::path raw(rel_or_abs_path);
        if (raw.is_absolute()) {
            return raw.string();
        }
        const fs::path cwd = fs::current_path();
        const std::vector<fs::path> candidates = {
            cwd / rel_or_abs_path,
            cwd / ".." / rel_or_abs_path,
            cwd / ".." / ".." / rel_or_abs_path,
            cwd / ".." / ".." / ".." / rel_or_abs_path
        };
        for (const auto& p : candidates) {
            std::error_code ec;
            if (fs::exists(p, ec) && !ec) {
                return p.lexically_normal().string();
            }
        }
        return rel_or_abs_path;
    };

    auto SplitCsvLine = [](const std::string& line) -> std::vector<std::string> {
        std::vector<std::string> cols;
        std::string token;
        bool in_quotes = false;
        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes;
                continue;
            }
            if (c == ',' && !in_quotes) {
                cols.push_back(token);
                token.clear();
                continue;
            }
            token.push_back(c);
        }
        cols.push_back(SanitizeCsvCell(token));
        for (std::string& c : cols) {
            c = SanitizeCsvCell(std::move(c));
        }
        return cols;
    };

    const std::string resolved_path = ResolveDataPath(path);
    std::ifstream in(resolved_path);
    if (!in) {
        throw std::runtime_error("Cannot open file: " + resolved_path);
    }

    std::string header_line;
    if (!std::getline(in, header_line)) {
        throw std::runtime_error("level_difficulty.csv is empty.");
    }
    const std::vector<std::string> headers = SplitCsvLine(header_line);
    std::map<std::string, int> idx;
    for (int i = 0; i < static_cast<int>(headers.size()); ++i) {
        idx[headers[i]] = i;
    }

    const std::vector<std::string> required_fields = {
        "schema_version", "scenario_id", "variant_id", "phase_id", "start_time_sec", "end_time_sec",
        "monster_id", "global_atk_scale", "global_atk_interval_scale", "global_atk_range_scale",
        "global_hp_scale", "global_move_speed_scale", "type_atk_scale", "type_atk_interval_scale",
        "type_atk_range_scale", "type_hp_scale", "type_move_speed_scale", "notes"
    };
    for (const auto& f : required_fields) {
        if (idx.find(f) == idx.end()) {
            throw std::runtime_error("level_difficulty.csv missing required column: " + f);
        }
    }

    auto ToInt = [](const std::string& s, const char* field_name, int key) -> int {
        try {
            return std::stoi(s);
        } catch (...) {
            throw std::runtime_error(
                "level_difficulty parse error: field=" + std::string(field_name) +
                ", phase_id=" + std::to_string(key) + ", value=" + s
            );
        }
    };
    auto ToFloat = [](const std::string& s, const char* field_name, int key) -> float {
        try {
            return std::stof(s);
        } catch (...) {
            throw std::runtime_error(
                "level_difficulty parse error: field=" + std::string(field_name) +
                ", phase_id=" + std::to_string(key) + ", value=" + s
            );
        }
    };

    std::vector<sim::core::DifficultyRow> out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> cols = SplitCsvLine(line);
        if (cols.size() < headers.size()) {
            cols.resize(headers.size());
        }
        auto Get = [&](const char* key) -> const std::string& { return cols[idx.at(key)]; };
        if (Get("scenario_id") != scenario_id || Get("variant_id") != variant_id) {
            continue;
        }

        sim::core::DifficultyRow r{};
        r.schema_version = Get("schema_version");
        r.scenario_id = Get("scenario_id");
        r.variant_id = Get("variant_id");
        r.phase_id = ToInt(Get("phase_id"), "phase_id", -1);
        r.start_time_sec = ToFloat(Get("start_time_sec"), "start_time_sec", r.phase_id);
        r.end_time_sec = ToFloat(Get("end_time_sec"), "end_time_sec", r.phase_id);
        r.monster_id = ToInt(Get("monster_id"), "monster_id", r.phase_id);
        r.global_atk_scale = ToFloat(Get("global_atk_scale"), "global_atk_scale", r.phase_id);
        r.global_atk_interval_scale = ToFloat(Get("global_atk_interval_scale"), "global_atk_interval_scale", r.phase_id);
        r.global_atk_range_scale = ToFloat(Get("global_atk_range_scale"), "global_atk_range_scale", r.phase_id);
        r.global_hp_scale = ToFloat(Get("global_hp_scale"), "global_hp_scale", r.phase_id);
        r.global_move_speed_scale = ToFloat(Get("global_move_speed_scale"), "global_move_speed_scale", r.phase_id);
        r.type_atk_scale = ToFloat(Get("type_atk_scale"), "type_atk_scale", r.phase_id);
        r.type_atk_interval_scale = ToFloat(Get("type_atk_interval_scale"), "type_atk_interval_scale", r.phase_id);
        r.type_atk_range_scale = ToFloat(Get("type_atk_range_scale"), "type_atk_range_scale", r.phase_id);
        r.type_hp_scale = ToFloat(Get("type_hp_scale"), "type_hp_scale", r.phase_id);
        r.type_move_speed_scale = ToFloat(Get("type_move_speed_scale"), "type_move_speed_scale", r.phase_id);
        r.notes = Get("notes");

        if (r.schema_version != "v1") {
            throw std::runtime_error("Unsupported level_difficulty schema_version, phase_id=" + std::to_string(r.phase_id));
        }
        if (r.phase_id < 1) {
            throw std::runtime_error("phase_id must be >= 1.");
        }
        if (r.start_time_sec < 0.0f || r.end_time_sec <= r.start_time_sec) {
            throw std::runtime_error("Invalid phase time range, phase_id=" + std::to_string(r.phase_id));
        }
        if (r.monster_id < 0) {
            throw std::runtime_error("monster_id must be >= 0, phase_id=" + std::to_string(r.phase_id));
        }
        if (r.global_atk_scale < 1.0f || r.global_atk_interval_scale <= 0.0f || r.global_atk_range_scale < 1.0f ||
            r.global_hp_scale < 1.0f || r.global_move_speed_scale < 1.0f ||
            r.type_atk_scale < 1.0f || r.type_atk_interval_scale <= 0.0f || r.type_atk_range_scale < 1.0f ||
            r.type_hp_scale < 1.0f || r.type_move_speed_scale < 1.0f) {
            throw std::runtime_error("difficulty scale out of range, phase_id=" + std::to_string(r.phase_id));
        }
        out.push_back(std::move(r));
    }

    if (out.empty()) {
        throw std::runtime_error("No level_difficulty rows matched scenario_id=" + scenario_id + ", variant_id=" + variant_id);
    }

    // 校验：每个 phase 必须且仅有一条 monster_id=0 的全局行。
    std::map<int, int> global_row_count_by_phase;
    for (const auto& r : out) {
        if (r.monster_id == 0) {
            ++global_row_count_by_phase[r.phase_id];
        }
    }
    for (const auto& kv : global_row_count_by_phase) {
        if (kv.second != 1) {
            throw std::runtime_error("Each phase must have exactly one global row, phase_id=" + std::to_string(kv.first));
        }
    }

    // 校验：同一 scenario+variant 下，全局区间不允许重叠。
    std::vector<sim::core::DifficultyRow> globals;
    for (const auto& r : out) {
        if (r.monster_id == 0) {
            globals.push_back(r);
        }
    }
    std::sort(globals.begin(), globals.end(), [](const auto& a, const auto& b) {
        return a.start_time_sec < b.start_time_sec;
    });
    for (std::size_t i = 1; i < globals.size(); ++i) {
        if (globals[i].start_time_sec < globals[i - 1].end_time_sec) {
            throw std::runtime_error("Global difficulty phases overlap.");
        }
    }

    return out;
}

sim::core::ProjectileWeaponDef ConfigLoader::LoadProjectileWeaponJson(const std::string& path) {
    const JsonValue root = JsonParser::ParseText(ReadFileToString(path));
    if (!root.IsObject()) {
        throw std::runtime_error("weapon_projectile_standard.json root must be object.");
    }
    const JsonValue::Object& obj = root.AsObject();

    auto GetReqObj = [](const JsonValue::Object& o, const char* key) -> const JsonValue::Object& {
        const auto it = o.find(key);
        if (it == o.end() || !it->second.IsObject()) {
            throw std::runtime_error(std::string("Missing required object field: ") + key);
        }
        return it->second.AsObject();
    };

    sim::core::ProjectileWeaponDef def{};
    def.schema_version = GetString(obj, "schema_version", "", true);
    def.weapon_id = GetString(obj, "weapon_id", "", true);
    def.weapon_name = GetString(obj, "weapon_name", "", true);
    def.weapon_type = GetString(obj, "weapon_type", "", true);

    const auto& base = GetReqObj(obj, "base");
    def.cooldown = static_cast<float>(GetNumber(base, "cooldown", 0.0, true));
    def.charges = GetInt(base, "charges", 1, false);
    def.burst_count = GetInt(base, "burst_count", 1, false);
    def.burst_interval = static_cast<float>(GetNumber(base, "burst_interval", 0.0, false));
    def.projectiles_per_shot = GetInt(base, "projectiles_per_shot", 1, false);

    const auto& projectile = GetReqObj(obj, "projectile");
    def.projectile_speed = static_cast<float>(GetNumber(projectile, "speed", 0.0, true));
    def.max_lifetime = static_cast<float>(GetNumber(projectile, "max_lifetime", 0.0, false));
    def.max_travel_distance = static_cast<float>(GetNumber(projectile, "max_travel_distance", 0.0, false));
    def.spawn_offset = static_cast<float>(GetNumber(projectile, "spawn_offset", 0.0, false));
    def.inherit_owner_velocity_ratio = static_cast<float>(
        GetNumber(projectile, "inherit_owner_velocity_ratio", 0.0, false)
    );
    def.initial_spread_angle = static_cast<float>(GetNumber(projectile, "initial_spread_angle", 0.0, false));
    def.spread_pattern = GetString(projectile, "spread_pattern", "symmetric", false);
    def.pierce_count = GetInt(projectile, "pierce_count", 0, false);
    def.ricochet_count = GetInt(projectile, "ricochet_count", 0, false);
    if (const auto it = projectile.find("can_hit_same_target_again"); it != projectile.end() && it->second.IsBool()) {
        def.can_hit_same_target_again = it->second.AsBool();
    }
    def.re_hit_cooldown = static_cast<float>(GetNumber(projectile, "re_hit_cooldown", 0.0, false));

    const auto& collision = GetReqObj(obj, "collision");
    def.collision_shape = GetString(collision, "shape", "circle", false);
    def.collision_radius = static_cast<float>(GetNumber(collision, "radius", 0.0, false));
    if (const auto it = collision.find("friendly_fire"); it != collision.end() && it->second.IsBool()) {
        def.friendly_fire = it->second.AsBool();
    }
    if (const auto it = collision.find("destroy_on_block"); it != collision.end() && it->second.IsBool()) {
        def.destroy_on_block = it->second.AsBool();
    }

    const auto& damage = GetReqObj(obj, "damage");
    def.base_damage = static_cast<float>(GetNumber(damage, "base_damage", 0.0, true));
    def.damage_type = GetString(damage, "damage_type", "physical", false);
    def.crit_chance = static_cast<float>(GetNumber(damage, "crit_chance", 0.0, false));
    def.crit_multiplier = static_cast<float>(GetNumber(damage, "crit_multiplier", 1.5, false));
    def.hit_interval_per_target = static_cast<float>(GetNumber(damage, "hit_interval_per_target", 0.0, false));
    def.max_targets_per_tick = GetInt(damage, "max_targets_per_tick", 1, false);
    def.knockback_distance = static_cast<float>(GetNumber(damage, "knockback_distance", 0.0, false));

    const auto& targeting = GetReqObj(obj, "targeting");
    def.targeting_mode = GetString(targeting, "mode", "nearest_enemy", false);
    def.targeting_search_radius = static_cast<float>(GetNumber(targeting, "search_radius", 0.0, false));
    def.targeting_fallback_mode = GetString(targeting, "fallback_mode", "forward", false);
    if (const auto it = targeting.find("allow_dead_target"); it != targeting.end() && it->second.IsBool()) {
        def.allow_dead_target = it->second.AsBool();
    }

    if (const auto rng = root.TryGet("rng"); rng != nullptr && rng->IsObject()) {
        const auto& r = rng->AsObject();
        if (const auto it = r.find("use_deterministic_rng"); it != r.end() && it->second.IsBool()) {
            def.use_deterministic_rng = it->second.AsBool();
        }
        def.rng_stream_id = GetString(r, "stream_id", def.rng_stream_id, false);
    }

    if (const auto se = root.TryGet("status_effects"); se != nullptr && se->IsArray()) {
        for (const auto& item : se->AsArray()) {
            if (!item.IsObject()) {
                continue;
            }
            const auto& e = item.AsObject();
            sim::core::ProjectileStatusEffectDef d{};
            d.id = GetString(e, "id", "", true);
            d.chance = static_cast<float>(GetNumber(e, "chance", 0.0, false));
            d.duration = static_cast<float>(GetNumber(e, "duration", 0.0, false));
            d.slow_ratio = static_cast<float>(GetNumber(e, "slow_ratio", 0.0, false));
            d.stack_rule = GetString(e, "stack_rule", "", false);
            def.status_effects.push_back(std::move(d));
        }
    }

    if (const auto channels = root.TryGet("upgrade_channels"); channels != nullptr && channels->IsObject()) {
        for (const auto& kv : channels->AsObject()) {
            if (!kv.second.IsObject()) {
                continue;
            }
            const auto& c = kv.second.AsObject();
            sim::core::ProjectileUpgradeChannelDef d{};
            d.base = GetNumber(c, "base", 0.0, true);
            d.min = GetNumber(c, "min", 0.0, false);
            if (const auto it_max = c.find("max"); it_max != c.end() && it_max->second.IsNumber()) {
                d.max = it_max->second.AsNumber();
                d.has_max = true;
            }
            d.stack_mode = GetString(c, "stack_mode", "", false);
            def.upgrade_channels[kv.first] = std::move(d);
        }
    }

    if (const auto options = root.TryGet("upgrade_options"); options != nullptr && options->IsArray()) {
        for (const auto& item : options->AsArray()) {
            if (!item.IsObject()) {
                continue;
            }
            const auto& o = item.AsObject();
            sim::core::ProjectileUpgradeOptionDef opt{};
            opt.option_id = GetString(o, "option_id", "", true);
            opt.rarity = GetString(o, "rarity", "", false);
            opt.weight = GetInt(o, "weight", 1, false);
            opt.max_pick = GetInt(o, "max_pick", 1, false);
            if (const auto mods = item.TryGet("modifiers"); mods != nullptr && mods->IsArray()) {
                for (const auto& m : mods->AsArray()) {
                    if (!m.IsObject()) {
                        continue;
                    }
                    const auto& mo = m.AsObject();
                    sim::core::ProjectileUpgradeModifierDef mod{};
                    mod.channel = GetString(mo, "channel", "", true);
                    mod.op = GetString(mo, "op", "", true);
                    mod.value = GetNumber(mo, "value", 0.0, true);
                    opt.modifiers.push_back(std::move(mod));
                }
            }
            def.upgrade_options.push_back(std::move(opt));
        }
    }

    // ===== 文档校验 =====
    if (def.schema_version != "v1") {
        throw std::runtime_error("Unsupported projectile weapon schema_version: " + def.schema_version);
    }
    if (def.weapon_type != "projectile_ranged") {
        throw std::runtime_error("weapon_type must be projectile_ranged.");
    }
    if (def.cooldown <= 0.0f) {
        throw std::runtime_error("base.cooldown must be > 0.");
    }
    if (def.projectile_speed < 0.0f) {
        throw std::runtime_error("projectile.speed must be >= 0.");
    }
    if (!(def.max_lifetime > 0.0f || def.max_travel_distance > 0.0f)) {
        throw std::runtime_error("projectile must have max_lifetime>0 or max_travel_distance>0.");
    }
    if (def.collision_radius < 0.0f) {
        throw std::runtime_error("collision.radius must be >= 0.");
    }
    if (def.crit_chance < 0.0f || def.crit_chance >= 1.0f) {
        throw std::runtime_error("damage.crit_chance must be in [0,1).");
    }
    for (const auto& s : def.status_effects) {
        if (s.slow_ratio < 0.0f || s.slow_ratio > 1.0f) {
            throw std::runtime_error("status_effects.slow_ratio must be in [0,1]. id=" + s.id);
        }
    }
    for (const auto& o : def.upgrade_options) {
        if (o.weight <= 0) {
            throw std::runtime_error("upgrade_options.weight must be > 0. option_id=" + o.option_id);
        }
        if (o.max_pick < 1) {
            throw std::runtime_error("upgrade_options.max_pick must be >=1. option_id=" + o.option_id);
        }
        for (const auto& m : o.modifiers) {
            if (def.upgrade_channels.find(m.channel) == def.upgrade_channels.end()) {
                throw std::runtime_error(
                    "upgrade modifier channel not found in upgrade_channels. option_id=" + o.option_id +
                    ", channel=" + m.channel
                );
            }
            if (m.op != "add" && m.op != "mul") {
                throw std::runtime_error("upgrade modifier op must be add/mul. option_id=" + o.option_id);
            }
        }
    }

    return def;
}

sim::core::PlayerBuildConfig ConfigLoader::LoadPlayerBuild(const std::string& path) {
    const JsonValue root = JsonParser::ParseText(ReadFileToString(path));
    if (!root.IsObject()) {
        throw std::runtime_error("player_build.json root must be object.");
    }
    const JsonValue::Object& obj = root.AsObject();

    sim::core::PlayerBuildConfig cfg{};
    cfg.initial_hp = static_cast<float>(GetNumber(obj, "player_hp", cfg.initial_hp, false));
    cfg.max_survival_time_sec =
        static_cast<float>(GetNumber(obj, "max_survival_time_sec", cfg.max_survival_time_sec, false));

    const auto it_skills = obj.find("skills");
    if (it_skills == obj.end() || !it_skills->second.IsArray()) {
        throw std::runtime_error("player_build.json requires skills array.");
    }
    const JsonValue::Array& arr = it_skills->second.AsArray();
    cfg.skills.clear();
    cfg.skills.reserve(arr.size());

    for (const JsonValue& item : arr) {
        if (!item.IsObject()) {
            throw std::runtime_error("skills[] item must be object.");
        }
        const JsonValue::Object& skill_obj = item.AsObject();
        sim::core::SkillDef s{};

        const std::string id_str = GetString(skill_obj, "id", "", true);
        // 为了减少运行时哈希/字符串比较，配置加载阶段将 id 映射为紧凑索引。
        if (id_str == "garlic") {
            s.skill_id = 0;
            s.name = "garlic";
        } else if (id_str == "sword_ring") {
            s.skill_id = 1;
            s.name = "sword_ring";
        } else {
            s.skill_id = static_cast<std::uint8_t>(GetInt(skill_obj, "skill_id", 2, false));
            s.name = id_str;
        }

        s.cooldown = static_cast<float>(GetNumber(skill_obj, "cooldown", s.cooldown, false));
        s.radius = static_cast<float>(GetNumber(skill_obj, "radius", s.radius, false));
        s.damage = static_cast<float>(GetNumber(skill_obj, "damage", s.damage, false));
        s.max_targets = GetInt(skill_obj, "max_targets", s.max_targets, false);

        // 生产配置校验：尽早失败，避免把非法参数带入热循环导致难追踪错误。
        if (s.cooldown <= 0.0f) {
            throw std::runtime_error("Skill cooldown must be > 0: " + s.name);
        }
        if (s.radius <= 0.0f) {
            throw std::runtime_error("Skill radius must be > 0: " + s.name);
        }
        if (s.damage < 0.0f) {
            throw std::runtime_error("Skill damage must be >= 0: " + s.name);
        }
        if (s.max_targets <= 0) {
            throw std::runtime_error("Skill max_targets must be > 0: " + s.name);
        }

        cfg.skills.push_back(std::move(s));
    }

    if (cfg.skills.empty()) {
        throw std::runtime_error("player_build.json skills cannot be empty.");
    }
    if (cfg.initial_hp <= 0.0f) {
        throw std::runtime_error("player_hp must be > 0.");
    }
    if (cfg.max_survival_time_sec <= 0.0f) {
        throw std::runtime_error("max_survival_time_sec must be > 0.");
    }

    // 检测重复 skill_id，避免不同技能在 metrics 里覆盖到同一槽位。
    std::unordered_set<std::uint8_t> id_set;
    for (const auto& s : cfg.skills) {
        if (!id_set.insert(s.skill_id).second) {
            throw std::runtime_error("Duplicate skill_id found in player_build.json.");
        }
    }

    return cfg;
}

sim::core::PlayerProfileConfig ConfigLoader::LoadPlayerProfileTemplate(const std::string& path) {
    const JsonValue root = JsonParser::ParseText(ReadFileToString(path));
    if (!root.IsObject()) {
        throw std::runtime_error("player_profile_template.json root must be object.");
    }
    const JsonValue::Object& obj = root.AsObject();

    auto GetObj = [](const JsonValue::Object& src, const char* key) -> const JsonValue::Object& {
        const auto it = src.find(key);
        if (it == src.end() || !it->second.IsObject()) {
            throw std::runtime_error(std::string("Missing required object field: ") + key);
        }
        return it->second.AsObject();
    };

    sim::core::PlayerProfileConfig cfg{};
    cfg.schema_version = GetString(obj, "schema_version", "", true);
    cfg.profile_id = GetString(obj, "profile_id", "", true);
    cfg.display_name = GetString(obj, "display_name", "", true);

    const auto& simulation = GetObj(obj, "simulation");
    cfg.player_hp = static_cast<float>(GetNumber(simulation, "player_hp", cfg.player_hp, true));
    cfg.max_survival_time_sec = static_cast<float>(
        GetNumber(simulation, "max_survival_time_sec", cfg.max_survival_time_sec, true)
    );

    const auto& base_stats = GetObj(obj, "base_stats");
    cfg.max_hp = static_cast<float>(GetNumber(base_stats, "max_hp", cfg.max_hp, true));
    cfg.base_attack_power = static_cast<float>(GetNumber(base_stats, "base_attack_power", cfg.base_attack_power, true));
    cfg.base_attack_speed = static_cast<float>(GetNumber(base_stats, "base_attack_speed", cfg.base_attack_speed, true));
    cfg.base_cooldown_reduction = static_cast<float>(
        GetNumber(base_stats, "base_cooldown_reduction", cfg.base_cooldown_reduction, true)
    );
    cfg.base_crit_chance = static_cast<float>(GetNumber(base_stats, "base_crit_chance", cfg.base_crit_chance, true));
    cfg.base_crit_multiplier = static_cast<float>(
        GetNumber(base_stats, "base_crit_multiplier", cfg.base_crit_multiplier, true)
    );
    cfg.base_luck = static_cast<float>(GetNumber(base_stats, "base_luck", cfg.base_luck, false));

    const auto& combat = GetObj(obj, "combat");
    cfg.damage_multiplier = static_cast<float>(GetNumber(combat, "damage_multiplier", cfg.damage_multiplier, true));
    cfg.projectile_count_bonus = GetInt(combat, "projectile_count_bonus", cfg.projectile_count_bonus, false);
    cfg.aoe_radius_multiplier = static_cast<float>(
        GetNumber(combat, "aoe_radius_multiplier", cfg.aoe_radius_multiplier, true)
    );
    cfg.status_power_multiplier = static_cast<float>(
        GetNumber(combat, "status_power_multiplier", cfg.status_power_multiplier, false)
    );
    cfg.final_damage_taken_multiplier = static_cast<float>(
        GetNumber(combat, "final_damage_taken_multiplier", cfg.final_damage_taken_multiplier, true)
    );

    const auto& defense = GetObj(obj, "defense");
    cfg.armor = static_cast<float>(GetNumber(defense, "armor", cfg.armor, false));
    cfg.evasion = static_cast<float>(GetNumber(defense, "evasion", cfg.evasion, false));
    cfg.block_chance = static_cast<float>(GetNumber(defense, "block_chance", cfg.block_chance, false));
    cfg.block_flat_reduction = static_cast<float>(
        GetNumber(defense, "block_flat_reduction", cfg.block_flat_reduction, false)
    );
    cfg.regen_hp_per_sec = static_cast<float>(GetNumber(defense, "regen_hp_per_sec", cfg.regen_hp_per_sec, false));
    cfg.life_steal = static_cast<float>(GetNumber(defense, "life_steal", cfg.life_steal, false));

    if (const JsonValue* progression = root.TryGet("progression");
        progression != nullptr && progression->IsObject()) {
        if (const JsonValue* stat_growth = progression->TryGet("stat_growth");
            stat_growth != nullptr && stat_growth->IsObject()) {
            if (const JsonValue* time_growth = stat_growth->TryGet("time_growth");
                time_growth != nullptr && time_growth->IsObject()) {
                cfg.time_growth_k_hp = static_cast<float>(
                    GetNumber(time_growth->AsObject(), "k_hp", cfg.time_growth_k_hp, false)
                );
                cfg.time_growth_k_atk = static_cast<float>(
                    GetNumber(time_growth->AsObject(), "k_atk", cfg.time_growth_k_atk, false)
                );
            }
        }
    }

    if (const JsonValue* rules = root.TryGet("rules"); rules != nullptr && rules->IsObject()) {
        cfg.target_priority = GetString(rules->AsObject(), "target_priority", cfg.target_priority, false);
        cfg.collision_policy = GetString(rules->AsObject(), "collision_policy", cfg.collision_policy, false);
        cfg.rng_stream_id = GetString(rules->AsObject(), "rng_stream_id", cfg.rng_stream_id, false);
    }

    if (const JsonValue* loadout = root.TryGet("loadout"); loadout != nullptr && loadout->IsObject()) {
        if (const JsonValue* bindings = loadout->TryGet("weapon_bindings");
            bindings != nullptr && bindings->IsArray()) {
            for (const JsonValue& item : bindings->AsArray()) {
                if (!item.IsObject()) {
                    continue;
                }
                const JsonValue::Object& b = item.AsObject();
                const std::string slot = GetString(b, "slot", "", false);
                const std::string weapon_file = GetString(b, "weapon_file", "", false);
                bool enabled = true;
                if (const auto it = b.find("enabled"); it != b.end() && it->second.IsBool()) {
                    enabled = it->second.AsBool();
                }
                if (enabled && slot == "main_ranged" && !weapon_file.empty()) {
                    cfg.main_weapon_file = weapon_file;
                    break;
                }
            }
        }
    }

    // ===== 文档范围校验（当前接入字段）=====
    if (cfg.schema_version != "v1") {
        throw std::runtime_error("Unsupported player profile schema_version: " + cfg.schema_version);
    }
    if (cfg.profile_id.empty() || cfg.display_name.empty()) {
        throw std::runtime_error("profile_id/display_name cannot be empty.");
    }
    if (cfg.player_hp <= 0.0f || cfg.max_survival_time_sec <= 0.0f) {
        throw std::runtime_error("simulation.player_hp/max_survival_time_sec must be > 0.");
    }
    if (cfg.max_hp < 1.0f || cfg.base_attack_power < 0.0f || cfg.base_attack_speed <= 0.0f) {
        throw std::runtime_error("base_stats has invalid range.");
    }
    if (cfg.base_cooldown_reduction < 0.0f || cfg.base_cooldown_reduction > 0.9f) {
        throw std::runtime_error("base_cooldown_reduction must be in [0, 0.9].");
    }
    if (cfg.base_crit_chance < 0.0f || cfg.base_crit_chance >= 1.0f || cfg.base_crit_multiplier < 1.0f) {
        throw std::runtime_error("base crit fields invalid.");
    }
    if (cfg.damage_multiplier <= 0.0f || cfg.aoe_radius_multiplier <= 0.0f ||
        cfg.final_damage_taken_multiplier <= 0.0f) {
        throw std::runtime_error("combat multipliers must be > 0.");
    }
    if (cfg.projectile_count_bonus < 0) {
        throw std::runtime_error("projectile_count_bonus must be >= 0.");
    }
    if (cfg.regen_hp_per_sec < 0.0f || cfg.life_steal < 0.0f || cfg.life_steal > 1.0f) {
        throw std::runtime_error("defense.regen_hp_per_sec/life_steal out of range.");
    }
    if (cfg.evasion < 0.0f || cfg.evasion > 0.95f || cfg.block_chance < 0.0f || cfg.block_chance > 0.95f) {
        throw std::runtime_error("defense.evasion/block_chance out of range.");
    }

    return cfg;
}

sim::core::LevelDesignConfig ConfigLoader::LoadLevelDesign(const std::string& path) {
    const JsonValue root = JsonParser::ParseText(ReadFileToString(path));
    if (!root.IsObject()) {
        throw std::runtime_error("level_design.json root must be object.");
    }
    const JsonValue::Object& obj = root.AsObject();

    sim::core::LevelDesignConfig cfg{};
    cfg.fixed_dt = static_cast<float>(GetNumber(obj, "fixed_dt", cfg.fixed_dt, false));

    if (const JsonValue* pool = root.TryGet("pool"); pool != nullptr) {
        if (!pool->IsObject()) {
            throw std::runtime_error("level_design.pool must be object.");
        }
        cfg.max_monsters = static_cast<std::size_t>(
            GetInt(pool->AsObject(), "max_monsters", static_cast<int>(cfg.max_monsters), false)
        );
    }
    if (const JsonValue* grid = root.TryGet("grid"); grid != nullptr) {
        if (!grid->IsObject()) {
            throw std::runtime_error("level_design.grid must be object.");
        }
        cfg.grid_half_extent = static_cast<float>(
            GetNumber(grid->AsObject(), "half_extent", cfg.grid_half_extent, false)
        );
        cfg.grid_cell_size = static_cast<float>(
            GetNumber(grid->AsObject(), "cell_size", cfg.grid_cell_size, false)
        );
    }
    if (cfg.fixed_dt <= 0.0f) {
        throw std::runtime_error("fixed_dt must be > 0.");
    }
    if (cfg.max_monsters == 0) {
        throw std::runtime_error("pool.max_monsters must be > 0.");
    }
    if (cfg.grid_half_extent <= 0.0f) {
        throw std::runtime_error("grid.half_extent must be > 0.");
    }
    if (cfg.grid_cell_size <= 0.0f) {
        throw std::runtime_error("grid.cell_size must be > 0.");
    }

    if (const JsonValue* spawn_csv = root.TryGet("spawn_csv"); spawn_csv != nullptr) {
        if (!spawn_csv->IsObject()) {
            throw std::runtime_error("level_design.spawn_csv must be object.");
        }
        const JsonValue::Object& sc = spawn_csv->AsObject();
        const std::string csv_path = GetString(sc, "file", "config/spawn_events.csv", false);
        cfg.spawn_scenario_id = GetString(sc, "scenario_id", cfg.spawn_scenario_id, false);
        cfg.spawn_variant_id = GetString(sc, "variant_id", cfg.spawn_variant_id, false);
        cfg.world_units_per_px = static_cast<float>(GetNumber(sc, "world_units_per_px", cfg.world_units_per_px, false));
        const int max_spawn_per_tick_budget = GetInt(sc, "max_spawn_per_tick_budget", 512, false);
        if (cfg.world_units_per_px <= 0.0f) {
            throw std::runtime_error("spawn_csv.world_units_per_px must be > 0.");
        }
        int duration_sec = cfg.level_duration_sec;
        cfg.spawn_events = LoadSpawnEventsCsv(
            csv_path,
            cfg.spawn_scenario_id,
            cfg.spawn_variant_id,
            cfg.fixed_dt,
            max_spawn_per_tick_budget,
            duration_sec
        );
        cfg.level_duration_sec = duration_sec;
    } else {
        throw std::runtime_error("level_design.json requires spawn_csv object.");
    }

    std::string monster_csv_path = "config/monster_def.csv";
    std::string difficulty_csv_path = "config/level_difficulty.csv";
    if (const JsonValue* monster_csv = root.TryGet("monster_csv"); monster_csv != nullptr) {
        if (!monster_csv->IsObject()) {
            throw std::runtime_error("level_design.monster_csv must be object.");
        }
        monster_csv_path = GetString(monster_csv->AsObject(), "file", monster_csv_path, false);
    }
    if (const JsonValue* difficulty_csv = root.TryGet("difficulty_csv"); difficulty_csv != nullptr) {
        if (!difficulty_csv->IsObject()) {
            throw std::runtime_error("level_design.difficulty_csv must be object.");
        }
        difficulty_csv_path = GetString(difficulty_csv->AsObject(), "file", difficulty_csv_path, false);
    }
    if (const JsonValue* weapon_projectile = root.TryGet("weapon_projectile"); weapon_projectile != nullptr) {
        if (!weapon_projectile->IsObject()) {
            throw std::runtime_error("level_design.weapon_projectile must be object.");
        }
        cfg.projectile_weapon_config_file = GetString(
            weapon_projectile->AsObject(),
            "file",
            cfg.projectile_weapon_config_file,
            false
        );
    }

    cfg.monster_defs = LoadMonsterDefCsv(monster_csv_path);
    cfg.difficulty_rows = LoadLevelDifficultyCsv(
        difficulty_csv_path,
        cfg.spawn_scenario_id,
        cfg.spawn_variant_id
    );
    cfg.projectile_weapon_def = LoadProjectileWeaponJson(cfg.projectile_weapon_config_file);

    if (cfg.spawn_events.empty()) {
        throw std::runtime_error("No spawn events loaded.");
    }
    if (cfg.monster_defs.empty()) {
        throw std::runtime_error("No monster defs loaded.");
    }
    if (cfg.difficulty_rows.empty()) {
        throw std::runtime_error("No difficulty rows loaded.");
    }

    // 外键校验：spawn_events.monster_id 必须存在于 monster_def。
    std::unordered_set<int> monster_id_set;
    for (const auto& d : cfg.monster_defs) {
        monster_id_set.insert(d.monster_id);
    }
    for (const auto& e : cfg.spawn_events) {
        if (monster_id_set.find(e.monster_id) == monster_id_set.end()) {
            throw std::runtime_error(
                "spawn_events references unknown monster_id=" + std::to_string(e.monster_id) +
                ", event_id=" + std::to_string(e.event_id)
            );
        }
    }

    const float level_duration_by_csv = static_cast<float>(cfg.level_duration_sec);
    if (level_duration_by_csv > 0.0f) {
        // 若 player_build 未显式覆盖生存上限，后续可依据该值限制战斗总时长。
        // 这里只保证配置本身一致，不强制写回 player_build。
        if (cfg.fixed_dt <= 0.0f) {
            throw std::runtime_error("fixed_dt must be > 0.");
        }
    }

    return cfg;
}

} // namespace sim::io
