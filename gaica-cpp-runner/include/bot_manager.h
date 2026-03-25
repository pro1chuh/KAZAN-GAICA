// bot_manager.h
#pragma once

#include "types.h"
#include "game_simulator.h"
#include "socket_server.h"
#include "logger.h"
#include "dependency_checker.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <thread>
#include <future>
#include <chrono>

class BotManager {
public:
    BotManager(const std::string& base_dir);
    ~BotManager();
    
    void set_simulator(GameSimulator* sim) { current_simulator = sim; }
    
    MatchResult run_series(
        const std::string& bot_a_path,
        const std::string& bot_b_path,
        int num_rounds,
        int seed,
        const std::string& output_dir
    );
    
    MatchResult run_match(
        const std::string& bot_a_path,
        const std::string& bot_b_path,
        int seed,
        int round_number,
        MatchLogger* logger,
        MatchOutcome* outcome
    );
    
    // Проверка зависимостей ботов
    bool check_bot_dependencies(const std::string& bot_path, MatchLogger* logger, const std::string& bot_name);

private:
    std::string base_dir;
    int port_counter = 9999;
    GameSimulator* current_simulator = nullptr;
    std::chrono::steady_clock::time_point match_start_time;
    DependencyChecker dep_checker;
    
    std::string get_next_port() { return std::to_string(port_counter++); }
    bool start_bot_process(const std::string& bot_path, const std::string& host, int port, void* pi, MatchLogger* logger, int bot_slot);
    void cleanup_bot_process(void* pi);
};