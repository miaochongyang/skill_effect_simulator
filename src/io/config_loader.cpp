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
#include <unordered_set>
#include <utility>
#include <vector>

#include "io/json_value.hpp"

namespace sim::io {
namespace {

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
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open file: " + path);
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
        cols.push_back(token);
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
        cols.push_back(token);
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
        cols.push_back(token);
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

    cfg.monster_defs = LoadMonsterDefCsv(monster_csv_path);
    cfg.difficulty_rows = LoadLevelDifficultyCsv(
        difficulty_csv_path,
        cfg.spawn_scenario_id,
        cfg.spawn_variant_id
    );

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
