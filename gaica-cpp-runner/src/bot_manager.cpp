// bot_manager.cpp
#include "bot_manager.h"
#include "json_utils.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <string>
#include <iomanip>

#ifdef _WIN32
    #include <windows.h>
    #include <winsock2.h>
    #include <shellapi.h>
    #include <psapi.h>
    #include <sys/stat.h>
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <fcntl.h>
    #include <sys/stat.h>
#endif

static bool directory_exists(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        return false;
    }
    return (info.st_mode & S_IFDIR) != 0;
}

BotManager::BotManager(const std::string& base_dir) : base_dir(base_dir) {
    match_start_time = std::chrono::steady_clock::now();
}

BotManager::~BotManager() {
}

#ifdef _WIN32
bool BotManager::start_bot_process(const std::string& script_path, const std::string& host, int port, void* pi, MatchLogger* logger, int bot_slot) {
    PROCESS_INFORMATION* proc_info = static_cast<PROCESS_INFORMATION*>(pi);
    DWORD attr = GetFileAttributesA(script_path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        std::string err = "Script not found: " + script_path;
        if (logger) logger->log_bot_stderr(bot_slot, err + "\n");
        return false;
    }
    
    std::string python_path = "py";
    std::string cmd = python_path + " \"" + script_path + "\" " + host + " " + std::to_string(port);
    
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(proc_info, sizeof(PROCESS_INFORMATION));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    HANDLE hStderrRead, hStderrWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    
    if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
        if (logger) logger->log_bot_stderr(bot_slot, "Failed to create pipe for stderr\n");
        return false;
    }
    
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);
    si.hStdError = hStderrWrite;
    si.hStdOutput = hStderrWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;
    
    char cmd_buf[2048];
    strcpy_s(cmd_buf, cmd.c_str());
    
    BOOL success = CreateProcess(
        NULL, cmd_buf, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, proc_info
    );
    
    CloseHandle(hStderrWrite);
    
    if (!success) {
        if (logger) logger->log_bot_stderr(bot_slot, "CreateProcess failed, code: " + std::to_string(GetLastError()) + "\n");
        CloseHandle(hStderrRead);
        return false;
    }
    
    std::thread([hStderrRead, logger, bot_slot]() {
        char buffer[256];
        DWORD bytesRead;
        while (ReadFile(hStderrRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            if (logger) logger->log_bot_stderr(bot_slot, buffer);
        }
        CloseHandle(hStderrRead);
    }).detach();
    
    return true;
}

void BotManager::cleanup_bot_process(void* pi) {
    PROCESS_INFORMATION* proc_info = static_cast<PROCESS_INFORMATION*>(pi);
    if (proc_info->hProcess) {
        TerminateProcess(proc_info->hProcess, 0);
        CloseHandle(proc_info->hProcess);
    }
    if (proc_info->hThread) CloseHandle(proc_info->hThread);
}
#endif

bool BotManager::check_bot_dependencies(const std::string& bot_path, MatchLogger* logger, const std::string& bot_name) {
    if (logger) {
        logger->log("  Checking dependencies for " + bot_name + "...");
        logger->log("    Bot path: " + bot_path);
    }
    
    if (!directory_exists(bot_path)) {
        if (logger) {
            logger->log("    [ERROR] Bot directory does not exist: " + bot_path);
        }
        return false;
    }
    
    auto result = dep_checker.check_bot(bot_path);
    
    if (logger) {
        if (!result.allowed_imports.empty()) {
            std::string allowed = "    [OK] Server libraries: ";
            for (const auto& imp : result.allowed_imports) {
                allowed += imp + " ";
            }
            logger->log(allowed);
        } else {
            logger->log("    [INFO] No external server libraries detected");
        }
        
        if (!result.disallowed_imports.empty()) {
            std::string disallowed = "    [BLOCKED] DISALLOWED (NOT on server): ";
            for (const auto& imp : result.disallowed_imports) {
                disallowed += imp + " ";
            }
            logger->log(disallowed);
            logger->log("    [WARNING] This bot uses libraries that are NOT available on the GAICA server!");
            logger->log("    [WARNING] The match may fail when submitted to the platform!");
        }
        
        if (!result.unknown_imports.empty()) {
            std::string unknown = "    [LOCAL] Local/Unknown modules: ";
            for (const auto& imp : result.unknown_imports) {
                unknown += imp + " ";
            }
            logger->log(unknown);
            logger->log("    [INFO] These appear to be local files in your bot directory");
        }
        
        if (result.valid) {
            logger->log("    [OK] Dependency check passed");
        } else {
            logger->log("    [FAIL] Dependency check FAILED!");
        }
    }
    
    return result.valid;
}

MatchResult BotManager::run_series(
    const std::string& bot_a_path,
    const std::string& bot_b_path,
    int num_rounds,
    int seed,
    const std::string& output_dir)
{
    MatchLogger logger(output_dir, true);
    MatchResult series_result;
    MatchOutcome outcome;
    
    series_result.total_ticks = 0;
    series_result.series_score_slot1 = 0;
    series_result.series_score_slot2 = 0;
    series_result.round_winners.clear();
    series_result.round_reasons.clear();
    series_result.duration_seconds = 0.0;
    
    outcome.protocol_version = "3.0";
    outcome.runner = "gaica_cpp_runner";
    outcome.tick_rate = 30;
    outcome.series_total_rounds = num_rounds;
    outcome.series_score[1] = 0;
    outcome.series_score[2] = 0;
    
    logger.log("\n========================================");
    logger.log("DEPENDENCY CHECK");
    logger.log("========================================");
    
    bool bot_a_ok = check_bot_dependencies(bot_a_path, &logger, "Bot A");
    bool bot_b_ok = check_bot_dependencies(bot_b_path, &logger, "Bot B");
    
    if (!bot_a_ok || !bot_b_ok) {
        logger.log("\n========================================");
        logger.log("⚠️  WARNING: Dependency check failed!");
        logger.log("========================================");
        logger.log("One or both bots use libraries that are NOT available");
        logger.log("on the GAICA server. The match may work locally but");
        logger.log("will likely FAIL when submitted to the platform.");
        logger.log("");
        logger.log("Recommended server libraries:");
        logger.log("  - numpy, scipy, scikit-learn");
        logger.log("  - xgboost, catboost");
        logger.log("  - torch (CPU only)");
        logger.log("========================================");
        
    } else {
        logger.log("\n✅ All dependencies are compatible with GAICA server!");
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    logger.log("\n=== Starting Series: " + std::to_string(num_rounds) + " rounds ===\n");
    
    for (int round = 1; round <= num_rounds; round++) {
        auto round_result = run_match(bot_a_path, bot_b_path, seed + round, round, &logger, &outcome);
        
        series_result.total_ticks += round_result.total_ticks;
        
        if (round_result.winner_slot == 1) {
            series_result.series_score_slot1++;
            outcome.series_score[1]++;
        } else if (round_result.winner_slot == 2) {
            series_result.series_score_slot2++;
            outcome.series_score[2]++;
        }
        
        series_result.round_winners.push_back(round_result.winner_slot);
        series_result.round_reasons.push_back(round_result.reason);
        
        outcome.round_results.push_back({
            round_result.winner_slot,
            round_result.draw,
            round_result.reason,
            round_result.duration_seconds
        });
        
        logger.log("  Round " + std::to_string(round) + " winner: " + 
                  (round_result.winner_slot == 1 ? "Bot A" : 
                   round_result.winner_slot == 2 ? "Bot B" : "Draw"));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    series_result.wall_time_seconds = duration.count() / 1000.0;
    
    if (series_result.series_score_slot1 > series_result.series_score_slot2) {
        series_result.winner_slot = 1;
        outcome.winner_slot = 1;
        outcome.draw = false;
    } else if (series_result.series_score_slot2 > series_result.series_score_slot1) {
        series_result.winner_slot = 2;
        outcome.winner_slot = 2;
        outcome.draw = false;
    } else {
        series_result.draw = true;
        outcome.draw = true;
    }
    
    series_result.simulated_seconds = series_result.total_ticks / 30.0;
    series_result.duration_seconds = series_result.simulated_seconds;
    outcome.total_ticks = series_result.total_ticks;
    outcome.wall_time_seconds = series_result.wall_time_seconds;
    outcome.simulated_seconds = series_result.simulated_seconds;
    outcome.speedup = outcome.simulated_seconds / outcome.wall_time_seconds;
    outcome.round_end_reason = "series_score";
    
    logger.save_outcome(outcome);
    
    std::vector<std::string> replay_events;
    logger.save_replay(replay_events, outcome, seed, "");
    
    logger.log("\n========================================");
    logger.log("Match logs saved to: " + logger.get_match_dir());
    logger.log("========================================");
    
    return series_result;
}

MatchResult BotManager::run_match(
    const std::string& bot_a_path,
    const std::string& bot_b_path,
    int seed,
    int round_number,
    MatchLogger* logger,
    MatchOutcome* outcome)
{
    MatchResult result;
    result.winner_slot = 0;
    result.draw = false;
    result.reason = "simulation_complete";
    result.total_ticks = 0;
    result.duration_seconds = 0.0;
    
    auto match_start = std::chrono::high_resolution_clock::now();
    
    try {
        GameConfig config;
        GameSimulator simulator(config);
        simulator.initialize_round(seed, "Level_" + std::to_string(seed % 10));
        
        int port1 = port_counter++;
        int port2 = port_counter++;
        
        if (logger) {
            logger->log("\nRound " + std::to_string(round_number) + " (seed " + std::to_string(seed) + "):");
            logger->log("  Port A: " + std::to_string(port1) + ", Port B: " + std::to_string(port2));
        }
        
        SocketServer server1(port1);
        SocketServer server2(port2);
        
        if (!server1.start() || !server2.start()) {
            result.reason = "socket_init_failed";
            if (logger) logger->log("  Failed to start socket servers");
            return result;
        }
        
        std::string bot_a_script = bot_a_path + "\\main.py";
        std::string bot_b_script = bot_b_path + "\\main.py";
        
#ifdef _WIN32
        PROCESS_INFORMATION pi_a, pi_b;
        
        if (logger) logger->log("  Starting Bot A...");
        if (!start_bot_process(bot_a_script, "127.0.0.1", port1, &pi_a, logger, 1)) {
            result.reason = "bot_a_start_failed";
            server1.stop();
            server2.stop();
            return result;
        }
        
        if (logger) logger->log("  Starting Bot B...");
        if (!start_bot_process(bot_b_script, "127.0.0.1", port2, &pi_b, logger, 2)) {
            cleanup_bot_process(&pi_a);
            result.reason = "bot_b_start_failed";
            server1.stop();
            server2.stop();
            return result;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        
        if (logger) logger->log("  Waiting for Bot A connection on port " + std::to_string(port1) + "...");
        auto bot1 = server1.accept_connection(15);
        if (!bot1) {
            cleanup_bot_process(&pi_a);
            cleanup_bot_process(&pi_b);
            result.reason = "bot_a_connection_timeout";
            if (logger) logger->log("  Bot A connection timeout!");
            server1.stop();
            server2.stop();
            return result;
        }
        if (logger) logger->log("  Bot A connected!");
        
        if (logger) logger->log("  Waiting for Bot B connection on port " + std::to_string(port2) + "...");
        auto bot2 = server2.accept_connection(15);
        if (!bot2) {
            cleanup_bot_process(&pi_a);
            cleanup_bot_process(&pi_b);
            result.reason = "bot_b_connection_timeout";
            if (logger) logger->log("  Bot B connection timeout!");
            server1.stop();
            server2.stop();
            return result;
        }
        if (logger) logger->log("  Bot B connected!");
        
        if (logger) logger->log("  Sending hello messages...");
        bot1->send_message(simulator.get_hello_message(1));
        bot2->send_message(simulator.get_hello_message(2));
        
        if (logger) logger->log("  Sending round start messages...");
        bot1->send_message(simulator.get_round_start_message(1));
        bot2->send_message(simulator.get_round_start_message(2));
        
        if (logger) logger->log("  Starting game simulation...");
        
        int tick_count = 0;
        auto bot_start_time = std::chrono::steady_clock::now();
        
        while (!simulator.is_round_over() && tick_count < 5000) {
            std::string bot1_raw = bot1->receive_message();
            std::string bot2_raw = bot2->receive_message();
            
            if (bot1_raw.empty() || bot2_raw.empty()) {
                if (logger) logger->log("  Bot disconnected");
                result.reason = "bot_disconnected";
                break;
            }
            
            auto cmd1_json = JsonUtils::parse_json_line(bot1_raw);
            auto cmd2_json = JsonUtils::parse_json_line(bot2_raw);
            
            Command cmd1, cmd2;
            
            auto parse_vector = [](const std::string& str) -> std::pair<double, double> {
                if (str.empty()) return {0, 0};
                std::string s = str;
                if (s.front() == '[') s = s.substr(1);
                if (s.back() == ']') s = s.substr(0, s.length() - 1);
                size_t comma = s.find(',');
                if (comma != std::string::npos) {
                    try {
                        double x = std::stod(s.substr(0, comma));
                        double y = std::stod(s.substr(comma + 1));
                        return {x, y};
                    } catch (...) {
                        return {0, 0};
                    }
                }
                return {0, 0};
            };
            
            std::string move_str = JsonUtils::get_or_default(cmd1_json, "move", "");
            auto [mx, my] = parse_vector(move_str);
            cmd1.move_x = mx;
            cmd1.move_y = my;
            
            std::string aim_str = JsonUtils::get_or_default(cmd1_json, "aim", "");
            auto [ax, ay] = parse_vector(aim_str);
            cmd1.aim_x = ax;
            cmd1.aim_y = ay;
            
            cmd1.shoot = JsonUtils::get_or_default(cmd1_json, "shoot", "false") == "true";
            cmd1.kick = JsonUtils::get_or_default(cmd1_json, "kick", "false") == "true";
            cmd1.pickup = JsonUtils::get_or_default(cmd1_json, "pickup", "false") == "true";
            cmd1.seq = std::stoi(JsonUtils::get_or_default(cmd1_json, "seq", "0"));
            
            move_str = JsonUtils::get_or_default(cmd2_json, "move", "");
            auto [mx2, my2] = parse_vector(move_str);
            cmd2.move_x = mx2;
            cmd2.move_y = my2;
            
            aim_str = JsonUtils::get_or_default(cmd2_json, "aim", "");
            auto [ax2, ay2] = parse_vector(aim_str);
            cmd2.aim_x = ax2;
            cmd2.aim_y = ay2;
            
            cmd2.shoot = JsonUtils::get_or_default(cmd2_json, "shoot", "false") == "true";
            cmd2.kick = JsonUtils::get_or_default(cmd2_json, "kick", "false") == "true";
            cmd2.pickup = JsonUtils::get_or_default(cmd2_json, "pickup", "false") == "true";
            cmd2.seq = std::stoi(JsonUtils::get_or_default(cmd2_json, "seq", "0"));
            
            simulator.update_tick(cmd1, cmd2);
            
            if (current_simulator) {
                *current_simulator = simulator;
            }
            
            bot1->send_message(simulator.get_tick_message(1));
            bot2->send_message(simulator.get_tick_message(2));
            
            tick_count++;
            result.total_ticks++;
            
            if (tick_count % 100 == 0 && logger) {
                logger->log("  Tick " + std::to_string(tick_count));
            }
        }
        
        auto bot_end_time = std::chrono::steady_clock::now();
        auto bot_wall_time = std::chrono::duration_cast<std::chrono::milliseconds>(bot_end_time - bot_start_time);
        
        if (outcome) {
            outcome->bot_timings[1].response_wait_seconds = bot_wall_time.count() / 1000.0;
            outcome->bot_timings[1].response_wait_budget_seconds = 60.0;
            outcome->bot_timings[1].tick_response_timeout_seconds = 1.0;
            outcome->bot_timings[2].response_wait_seconds = bot_wall_time.count() / 1000.0;
            outcome->bot_timings[2].response_wait_budget_seconds = 60.0;
            outcome->bot_timings[2].tick_response_timeout_seconds = 1.0;
        }
        
        if (logger) logger->log("  Round finished after " + std::to_string(result.total_ticks) + " ticks");
        
        bot1->send_message(simulator.get_round_end_message());
        bot2->send_message(simulator.get_round_end_message());
        
        cleanup_bot_process(&pi_a);
        cleanup_bot_process(&pi_b);
        
        server1.stop();
        server2.stop();
        
        result.winner_slot = simulator.get_winner();
        if (result.winner_slot == -1) {
            result.draw = true;
            result.winner_slot = 0;
        }
        result.reason = simulator.get_end_reason();
        
        if (logger) logger->log("  Winner: " + (result.draw ? "Draw" : std::to_string(result.winner_slot)));
        
        if (outcome) {
            const auto& player1 = simulator.get_player(1);
            const auto& player2 = simulator.get_player(2);
            outcome->hp_1 = player1.alive ? "100" : "0";
            outcome->hp_2 = player2.alive ? "100" : "0";
            outcome->map_id = "Level_" + std::to_string(seed % 10);
            outcome->map_index = seed % 10;
        }
        
#else
        result.reason = "not_implemented_on_linux";
#endif
        
    } catch (const std::exception& e) {
        std::string err = std::string("Exception: ") + e.what();
        if (logger) logger->log(err);
        result.reason = std::string("error: ") + e.what();
        if (outcome) outcome->bot_errors[1] = err;
        if (outcome) outcome->bot_errors[2] = err;
    }
    
    auto match_end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(match_end - match_start);
    result.wall_time_seconds = duration.count() / 1000.0;
    result.simulated_seconds = result.total_ticks / 30.0;
    result.duration_seconds = result.simulated_seconds;
    
    return result;
}