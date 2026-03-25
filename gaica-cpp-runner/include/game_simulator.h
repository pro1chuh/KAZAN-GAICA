#pragma once

#include "types.h"
#include <vector>
#include <map>
#include <random>

class GameSimulator {
public:
    GameSimulator(const GameConfig& config);
    
    void initialize_round(int seed, const std::string& level_id);
    void update_tick(const Command& cmd1, const Command& cmd2);
    
    const PlayerState& get_player(int slot) const { 
        return slot == 1 ? players[0] : players[1]; 
    }
    const GameConfig& get_config() const { return config; }
    
    int get_current_tick() const { return current_tick; }
    double get_time_seconds() const { return current_tick / (double)config.tick_rate; }
    bool is_round_over() const { return round_over; }
    int get_winner() const { return winner; }
    std::string get_end_reason() const { return end_reason; }
    
    std::string get_hello_message(int player_id) const;
    std::string get_round_start_message(int player_id) const;
    std::string get_tick_message(int player_id) const;
    std::string get_round_end_message() const;
    
    // Для веб-визуализации
    std::string get_state_json() const;

private:
    struct WeaponStats {
        int max_ammo;
        double cooldown;
        double range;
        double breakability;
    };
    
    GameConfig config;
    std::vector<PlayerState> players;
    std::vector<PickupItem> pickups;
    std::vector<Projectile> projectiles;
    std::map<std::string, WeaponStats> weapon_stats;
    std::mt19937 rng;
    
    int current_tick = 0;
    bool round_over = false;
    int winner = -1;
    std::string end_reason = "";
    std::string current_level = "";
    
    int next_pickup_id = 1;
    int next_projectile_id = 1;
    
    void update_player(PlayerState& player, const Command& cmd, double dt);
    void apply_movement(PlayerState& player, const Command& cmd, double dt);
    void apply_kick(int kicker_id);
    void update_projectiles(double dt);
    void apply_physics();
    void check_collisions();
    void check_round_end();
    void spawn_pickup(const std::string& weapon_type, int ammo, const Vec2& pos, const Vec2& vel);
    void spawn_weapons();
    
    double distance(const Vec2& a, const Vec2& b) const;
    bool is_on_floor(const Vec2& pos) const;
    
    static constexpr double PLAYER_RADIUS = 10.0;
    static constexpr double KICK_RANGE = 28.0;
    static constexpr double KICK_DOT_THRESHOLD = 0.45;
    static constexpr double STUN_DURATION = 0.5;
    static constexpr double STUN_SPEED = 350.0;
    static constexpr double PICKUP_RANGE = 25.0;
    static constexpr double WEAPON_PICKUP_COOLDOWN = 0.25;
};