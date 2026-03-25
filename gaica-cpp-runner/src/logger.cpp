// logger.cpp
#include "logger.h"
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace fs = std::filesystem;

MatchLogger::MatchLogger(const std::string& output_dir, bool use_timestamp) : output_dir(output_dir) {
    if (use_timestamp) {
        match_dir = create_match_directory();
    } else {
        match_dir = output_dir;
    }
    
    ensure_directory();
    
    std::string log_path = match_dir + "/match.log";
    log_file.open(log_path, std::ios::out | std::ios::trunc);
    
    std::string bot_a_path = match_dir + "/bot_a.stderr.log";
    bot_a_stderr.open(bot_a_path, std::ios::out | std::ios::trunc);
    
    std::string bot_b_path = match_dir + "/bot_b.stderr.log";
    bot_b_stderr.open(bot_b_path, std::ios::out | std::ios::trunc);
    
    log("=== Match started at " + timestamp() + " ===");
    log("Output directory: " + match_dir);
}

MatchLogger::~MatchLogger() {
    if (log_file.is_open()) {
        log("=== Match finished at " + timestamp() + " ===");
        log_file.close();
    }
    if (bot_a_stderr.is_open()) bot_a_stderr.close();
    if (bot_b_stderr.is_open()) bot_b_stderr.close();
}

std::string MatchLogger::create_match_directory() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_now);
#endif
    
    std::stringstream ss;
    ss << output_dir << "/match_"
       << std::put_time(&tm_now, "%Y%m%d_%H%M%S");
    
    return ss.str();
}

void MatchLogger::ensure_directory() {
    try {
        fs::create_directories(match_dir);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create directory " << match_dir << ": " << e.what() << "\n";
    }
}

std::string MatchLogger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void MatchLogger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_file.is_open()) {
        log_file << "[" << timestamp() << "] " << message << "\n";
        log_file.flush();
    }
    std::cout << message << "\n";
}

void MatchLogger::log_bot_stderr(int bot_slot, const std::string& line) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::ofstream* file = (bot_slot == 1) ? &bot_a_stderr : &bot_b_stderr;
    if (file->is_open()) {
        *file << line;
        file->flush();
    }
}

std::string MatchLogger::escape_json(const std::string& s) {
    std::stringstream ss;
    for (char c : s) {
        switch (c) {
            case '"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default: ss << c; break;
        }
    }
    return ss.str();
}

void MatchLogger::save_outcome(const MatchOutcome& outcome) {
    std::string outcome_path = match_dir + "/outcome.json";
    std::ofstream out(outcome_path);
    
    out << "{\n";
    out << "  \"protocol_version\": \"" << escape_json(outcome.protocol_version) << "\",\n";
    out << "  \"runner\": \"" << escape_json(outcome.runner) << "\",\n";
    out << "  \"draw\": " << (outcome.draw ? "true" : "false") << ",\n";
    out << "  \"winner_slot\": " << outcome.winner_slot << ",\n";
    out << "  \"total_ticks\": " << outcome.total_ticks << ",\n";
    out << "  \"tick_rate\": " << outcome.tick_rate << ",\n";
    out << "  \"simulated_seconds\": " << std::fixed << std::setprecision(6) << outcome.simulated_seconds << ",\n";
    out << "  \"wall_time_seconds\": " << std::fixed << std::setprecision(6) << outcome.wall_time_seconds << ",\n";
    out << "  \"speedup\": " << std::fixed << std::setprecision(6) << outcome.speedup << ",\n";
    out << "  \"round_end_reason\": \"" << escape_json(outcome.round_end_reason) << "\",\n";
    out << "  \"map_id\": \"" << escape_json(outcome.map_id) << "\",\n";
    out << "  \"map_index\": " << outcome.map_index << ",\n";
    out << "  \"series_total_rounds\": " << outcome.series_total_rounds << ",\n";
    out << "  \"series_score\": {\n";
    out << "    \"1\": " << outcome.series_score.at(1) << ",\n";
    out << "    \"2\": " << outcome.series_score.at(2) << "\n";
    out << "  },\n";
    out << "  \"round_results\": [\n";
    for (size_t i = 0; i < outcome.round_results.size(); ++i) {
        const auto& r = outcome.round_results[i];
        out << "    {\n";
        out << "      \"series_round\": " << (i + 1) << ",\n";
        out << "      \"winner_slot\": " << r.winner_id << ",\n";
        out << "      \"draw\": " << (r.draw ? "true" : "false") << ",\n";
        out << "      \"reason\": \"" << escape_json(r.reason) << "\",\n";
        out << "      \"duration_seconds\": " << std::fixed << std::setprecision(6) << r.duration_seconds << "\n";
        out << "    }";
        if (i + 1 < outcome.round_results.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"bot_errors\": {\n";
    bool first = true;
    for (const auto& [slot, error] : outcome.bot_errors) {
        if (!first) out << ",\n";
        out << "    \"" << slot << "\": \"" << escape_json(error) << "\"";
        first = false;
    }
    out << "\n  },\n";
    out << "  \"bot_timings\": {\n";
    first = true;
    for (const auto& [slot, timing] : outcome.bot_timings) {
        if (!first) out << ",\n";
        out << "    \"" << slot << "\": {\n";
        out << "      \"response_wait_seconds\": " << std::fixed << std::setprecision(6) << timing.response_wait_seconds << ",\n";
        out << "      \"response_wait_budget_seconds\": " << std::fixed << std::setprecision(6) << timing.response_wait_budget_seconds << ",\n";
        out << "      \"tick_response_timeout_seconds\": " << std::fixed << std::setprecision(6) << timing.tick_response_timeout_seconds << ",\n";
        out << "      \"process_wall_time_seconds\": " << std::fixed << std::setprecision(6) << timing.process_wall_time_seconds << "\n";
        out << "    }";
        first = false;
    }
    out << "\n  },\n";
    out << "  \"hp\": {\n";
    out << "    \"1\": " << outcome.hp_1 << ",\n";
    out << "    \"2\": " << outcome.hp_2 << "\n";
    out << "  }\n";
    out << "}\n";
    
    out.close();
    log("Results saved to: " + outcome_path);
}

void MatchLogger::save_replay(const std::vector<std::string>& events, const MatchOutcome& outcome, int seed, const std::string& match_id) {
    std::string replay_path = match_dir + "/replay.json";
    std::ofstream out(replay_path);
    
    out << "{\n";
    out << "  \"meta\": {\n";
    out << "    \"match_id\": \"" << escape_json(match_id) << "\",\n";
    out << "    \"seed\": " << seed << ",\n";
    out << "    \"runner\": \"" << escape_json(outcome.runner) << "\",\n";
    out << "    \"protocol_version\": \"" << escape_json(outcome.protocol_version) << "\",\n";
    out << "    \"tick_rate\": " << outcome.tick_rate << ",\n";
    out << "    \"simulated_seconds\": " << std::fixed << std::setprecision(6) << outcome.simulated_seconds << ",\n";
    out << "    \"wall_time_seconds\": " << std::fixed << std::setprecision(6) << outcome.wall_time_seconds << ",\n";
    out << "    \"speedup\": " << std::fixed << std::setprecision(6) << outcome.speedup << ",\n";
    out << "    \"series_total_rounds\": " << outcome.series_total_rounds << ",\n";
    out << "    \"series_score\": {\n";
    out << "      \"1\": " << outcome.series_score.at(1) << ",\n";
    out << "      \"2\": " << outcome.series_score.at(2) << "\n";
    out << "    }\n";
    out << "  },\n";
    out << "  \"events\": [\n";
    for (size_t i = 0; i < events.size(); ++i) {
        out << "    " << events[i];
        if (i + 1 < events.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"result\": {\n";
    out << "    \"winner_slot\": " << outcome.winner_slot << ",\n";
    out << "    \"draw\": " << (outcome.draw ? "true" : "false") << ",\n";
    out << "    \"reason\": \"" << escape_json(outcome.round_end_reason) << "\",\n";
    out << "    \"series_total_rounds\": " << outcome.series_total_rounds << ",\n";
    out << "    \"series_score\": {\n";
    out << "      \"1\": " << outcome.series_score.at(1) << ",\n";
    out << "      \"2\": " << outcome.series_score.at(2) << "\n";
    out << "    },\n";
    out << "    \"ticks\": " << outcome.total_ticks << ",\n";
    out << "    \"tick_rate\": " << outcome.tick_rate << ",\n";
    out << "    \"simulated_seconds\": " << std::fixed << std::setprecision(6) << outcome.simulated_seconds << ",\n";
    out << "    \"wall_time_seconds\": " << std::fixed << std::setprecision(6) << outcome.wall_time_seconds << ",\n";
    out << "    \"speedup\": " << std::fixed << std::setprecision(6) << outcome.speedup << ",\n";
    out << "    \"map_id\": \"" << escape_json(outcome.map_id) << "\",\n";
    out << "    \"map_index\": " << outcome.map_index << "\n";
    out << "  }\n";
    out << "}\n";
    
    out.close();
}