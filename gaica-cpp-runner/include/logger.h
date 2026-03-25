// logger.h
#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <mutex>
#include <ctime>

class MatchLogger {
public:
    MatchLogger(const std::string& output_dir, bool use_timestamp = true);
    ~MatchLogger();
    
    void log(const std::string& message);
    void log_bot_stderr(int bot_slot, const std::string& line);
    void save_outcome(const MatchOutcome& outcome);
    void save_replay(const std::vector<std::string>& events, const MatchOutcome& outcome, int seed, const std::string& match_id);
    
    std::string get_match_dir() const { return match_dir; }
    
private:
    std::string output_dir;
    std::string match_dir;
    std::ofstream log_file;
    std::ofstream bot_a_stderr;
    std::ofstream bot_b_stderr;
    std::mutex log_mutex;
    
    std::string timestamp();
    std::string escape_json(const std::string& s);
    void ensure_directory();
    std::string create_match_directory();
};