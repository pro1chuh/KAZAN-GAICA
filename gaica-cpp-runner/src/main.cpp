#include "bot_manager.h"
#include "web_server.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <csignal>

namespace fs = std::filesystem;

struct CLIArgs {
    std::string bot_a_path;
    std::string bot_b_path;
    int series_rounds = 3;
    int seed = 42;
    std::string output_dir = "./results";
    int web_port = 0;
    bool show_help = false;
};

std::atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

void print_usage(const char* program) {
    std::cout << "GAICA C++ Game Runner\n\n"
              << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
              << "  --bot-a <path>         Path to first bot's directory\n"
              << "  --bot-b <path>         Path to second bot's directory\n"
              << "  --rounds <n>           Number of rounds in series (default: 3)\n"
              << "  --seed <int>           Random seed (default: 42)\n"
              << "  --output <dir>         Output directory (default: ./results)\n"
              << "  --web-port <port>      Web visualization port (enables visualization mode)\n"
              << "  --help                 Print this message\n\n"
              << "Examples:\n"
              << "  # Run match without visualization (exits when done):\n"
              << "  " << program << " --bot-a C:\\bots\\my_bot --bot-b C:\\bots\\opponent\n\n"
              << "  # Run with visualization (server keeps running):\n"
              << "  " << program << " --bot-a C:\\bots\\my_bot --bot-b C:\\bots\\opponent --web-port 8080\n";
}

CLIArgs parse_args(int argc, char* argv[]) {
    CLIArgs args;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help") {
            args.show_help = true;
        } else if (arg == "--bot-a" && i + 1 < argc) {
            args.bot_a_path = argv[++i];
        } else if (arg == "--bot-b" && i + 1 < argc) {
            args.bot_b_path = argv[++i];
        } else if (arg == "--rounds" && i + 1 < argc) {
            args.series_rounds = std::stoi(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            args.seed = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            args.output_dir = argv[++i];
        } else if (arg == "--web-port" && i + 1 < argc) {
            args.web_port = std::stoi(argv[++i]);
        }
    }
    return args;
}

int main(int argc, char* argv[]) {
    CLIArgs args = parse_args(argc, argv);
    
    if (args.show_help) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (args.bot_a_path.empty() || args.bot_b_path.empty()) {
        std::cerr << "Error: --bot-a and --bot-b paths are required\n";
        print_usage(argv[0]);
        return 1;
    }
    
    fs::create_directories(args.output_dir);
    
    bool visualization_mode = (args.web_port > 0);
    
    std::cout << "========================================\n";
    std::cout << "GAICA C++ Game Runner v2.0\n";
    std::cout << "========================================\n";
    std::cout << "Bot A: " << args.bot_a_path << "\n";
    std::cout << "Bot B: " << args.bot_b_path << "\n";
    std::cout << "Series Rounds: " << args.series_rounds << "\n";
    std::cout << "Seed: " << args.seed << "\n";
    if (visualization_mode) {
        std::cout << "Web Port: " << args.web_port << " (visualization mode)\n";
    } else {
        std::cout << "Mode: headless\n";
    }
    std::cout << "Output: " << args.output_dir << "\n";
    std::cout << "========================================\n\n";
    
    signal(SIGINT, signal_handler);
    
    try {
        BotManager manager(".");
        
        GameConfig config;
        GameSimulator simulator(config);
        WebServer* web_server = nullptr;
        
        if (visualization_mode) {
            manager.set_simulator(&simulator);
            web_server = new WebServer(args.web_port, &simulator);
            if (web_server->start()) {
                std::cout << "Web server started: http://localhost:" << args.web_port << "\n";
                std::cout << "Open this URL in your browser to watch the match!\n";
                std::cout << "Press Ctrl+C to stop...\n\n";
            } else {
                std::cout << "Warning: Could not start web server\n";
                delete web_server;
                web_server = nullptr;
                visualization_mode = false;
            }
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        
        MatchResult result = manager.run_series(
            args.bot_a_path,
            args.bot_b_path,
            args.series_rounds,
            args.seed,
            args.output_dir
        );
        
        auto end = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0;
        
        std::cout << "\n========================================\n";
        std::cout << "SERIES RESULTS\n";
        std::cout << "========================================\n";
        std::cout << "Bot A Wins: " << result.series_score_slot1 << "\n";
        std::cout << "Bot B Wins: " << result.series_score_slot2 << "\n";
        
        if (result.draw) {
            std::cout << "Result: DRAW!\n";
        } else {
            std::cout << "Winner: Bot " << (result.winner_slot == 1 ? "A" : "B") << "\n";
        }
        
        std::cout << "\nStatistics:\n";
        std::cout << "  Total Ticks: " << result.total_ticks << "\n";
        std::cout << "  Wall Time: " << total_time << "s\n";
        std::cout << "  Avg per Round: " << (total_time / args.series_rounds) << "s\n";
        std::cout << "  Simulated Time: " << result.simulated_seconds << "s\n";
        std::cout << "  Speedup: " << (result.simulated_seconds / total_time) << "x\n";
        std::cout << "========================================\n";

        if (!visualization_mode) {
            std::cout << "\n========================================\n";
            std::cout << "Match logs saved to: " << args.output_dir << "/match_*\n";
            std::cout << "Each match has its own timestamped folder\n";
            std::cout << "========================================\n";
        }
        
        if (visualization_mode && web_server) {
            std::cout << "\nVisualization mode active. Server keeps running...\n";
            std::cout << "Press Ctrl+C to stop.\n";
            while (running) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        if (web_server) {
            web_server->stop();
            delete web_server;
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL ERROR: " << e.what() << "\n";
        return 1;
    }
}