// types.h
#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cmath>
#include <random>

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
    
    Vec2() = default;
    Vec2(double x, double y) : x(x), y(y) {}
    
    Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    double length() const { return std::sqrt(x * x + y * y); }
    double length_sq() const { return x * x + y * y; }
    Vec2 normalize() const {
        double len = length();
        if (len < 1e-9) return Vec2(0, 0);
        return Vec2(x / len, y / len);
    }
    double dot(const Vec2& v) const { return x * v.x + y * v.y; }
};

struct GameConfig {
    int tick_rate = 30;
    double tick_dt = 1.0 / 30.0;
    double player_speed = 255.0;
    double acceleration = 2400.0;
    double deceleration = 2100.0;
    double bullet_speed = 500.0;
    double bullet_lifetime = 2.0;
    int max_ticks = 5400;
    double arena_width = 800.0;
    double arena_height = 600.0;
};

struct PlayerState {
    int id = 0;
    Vec2 position;
    Vec2 velocity;
    Vec2 facing = Vec2(1, 0);
    bool alive = true;
    double stun_remaining = 0;
    Vec2 stun_direction;
    bool has_weapon = false;
    std::string weapon_type = "";
    int weapon_ammo = 0;
    double shoot_cooldown = 0;
    double kick_cooldown = 0;
    int last_kick_seq = -1;
    int last_pickup_seq = -1;
    std::string color = "";
    std::string character = "lemon";
};

struct Command {
    double move_x = 0.0;
    double move_y = 0.0;
    double aim_x = 0.0;
    double aim_y = 0.0;
    bool shoot = false;
    bool kick = false;
    bool pickup = false;
    bool drop = false;
    bool throw_item = false;
    int seq = 0;
};

struct PickupItem {
    int id;
    Vec2 position;
    std::string weapon_type;
    int ammo;
    double cooldown;
};

struct Projectile {
    int id;
    int owner_id;
    Vec2 position;
    Vec2 velocity;
    double remaining_life;
};

struct RoundResult {
    int winner_id = 0;
    bool draw = false;
    std::string reason = "";
    double duration_seconds = 0.0;
};

struct MatchResult {
    int winner_slot = 0;
    bool draw = false;
    std::string reason = "";
    int series_score_slot1 = 0;
    int series_score_slot2 = 0;
    int total_ticks = 0;
    double wall_time_seconds = 0.0;
    double simulated_seconds = 0.0;
    double duration_seconds = 0.0;
    std::vector<int> round_winners;
    std::vector<std::string> round_reasons;
};

struct BotTiming {
    double response_wait_seconds = 0.0;
    double response_wait_budget_seconds = 60.0;
    double tick_response_timeout_seconds = 1.0;
    double process_wall_time_seconds = 0.0;
};

struct MatchOutcome {
    std::string protocol_version = "3.0";
    std::string runner = "gaica_cpp_runner";
    bool draw = false;
    int winner_slot = 0;
    int total_ticks = 0;
    int tick_rate = 30;
    double simulated_seconds = 0.0;
    double wall_time_seconds = 0.0;
    double speedup = 0.0;
    std::string round_end_reason = "";
    std::string map_id = "";
    int map_index = 0;
    int series_total_rounds = 1;
    std::map<int, int> series_score;
    std::vector<RoundResult> round_results;
    std::map<int, std::string> bot_errors;
    std::map<int, BotTiming> bot_timings;
    std::string hp_1 = "0";
    std::string hp_2 = "0";
};