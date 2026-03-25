#include "game_simulator.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>

static const double BULLET_SPEED = 500.0;
static const double BULLET_LIFETIME = 2.0;

GameSimulator::GameSimulator(const GameConfig& config) 
    : config(config), players(2), rng(std::random_device{}()) {
    
    players[0].id = 1;
    players[1].id = 2;
    players[0].facing = Vec2(1, 0);
    players[1].facing = Vec2(-1, 0);
    players[0].color = "#ef4444";
    players[1].color = "#22c55e";
    players[0].character = "orange";
    players[1].character = "lime";
    
    weapon_stats["revolver"] = {4, 0.4, 240.0, 1.0};
    weapon_stats["uzi"] = {18, 0.1, 180.0, 0.25};
}

void GameSimulator::initialize_round(int seed, const std::string& level_id) {
    rng.seed(seed);
    current_tick = 0;
    round_over = false;
    winner = -1;
    end_reason = "";
    current_level = level_id;
    
    pickups.clear();
    projectiles.clear();
    next_pickup_id = 1;
    next_projectile_id = 1;
    
    for (auto& player : players) {
        player.alive = true;
        player.velocity = Vec2(0, 0);
        player.stun_remaining = 0;
        player.has_weapon = true;
        player.weapon_type = "revolver";
        player.weapon_ammo = 4;
        player.shoot_cooldown = 0;
        player.kick_cooldown = 0;
        player.last_kick_seq = -1;
        player.last_pickup_seq = -1;
        
        std::uniform_real_distribution<double> dist(100, 700);
        player.position = Vec2(dist(rng), dist(rng));
        
        Vec2 to_center(400, 300);
        player.facing = (to_center - player.position).normalize();
        if (player.facing.length() < 0.01) player.facing = Vec2(1, 0);
    }
    
    spawn_pickup("revolver", 4, Vec2(350, 250), Vec2(0, 0));
    spawn_pickup("uzi", 18, Vec2(450, 350), Vec2(0, 0));
    spawn_pickup("revolver", 4, Vec2(200, 200), Vec2(0, 0));
    spawn_pickup("uzi", 18, Vec2(600, 400), Vec2(0, 0));
}

void GameSimulator::update_tick(const Command& cmd1, const Command& cmd2) {
    if (round_over) return;
    
    double dt = config.tick_dt;
    
    for (auto& player : players) {
        if (!player.alive) continue;
        player.shoot_cooldown = std::max(0.0, player.shoot_cooldown - dt);
        player.kick_cooldown = std::max(0.0, player.kick_cooldown - dt);
        if (player.stun_remaining > 0) {
            player.stun_remaining = std::max(0.0, player.stun_remaining - dt);
        }
    }
    
    for (auto& pickup : pickups) {
        pickup.cooldown = std::max(0.0, pickup.cooldown - dt);
    }
    
    update_player(players[0], cmd1, dt);
    update_player(players[1], cmd2, dt);
    
    update_projectiles(dt);
    apply_physics();
    check_collisions();
    check_round_end();
    
    current_tick++;
}

void GameSimulator::update_player(PlayerState& player, const Command& cmd, double dt) {
    if (!player.alive) return;
    
    if (player.stun_remaining > 0) {
        Vec2 dir = player.stun_direction;
        if (dir.length() < 0.01) dir = player.facing;
        player.position = player.position + dir * (STUN_SPEED * dt);
        return;
    }
    
    if (cmd.aim_x != 0 || cmd.aim_y != 0) {
        player.facing = Vec2(cmd.aim_x, cmd.aim_y).normalize();
    } else if (cmd.move_x != 0 || cmd.move_y != 0) {
        player.facing = Vec2(cmd.move_x, cmd.move_y).normalize();
    }
    
    apply_movement(player, cmd, dt);
    
    if (cmd.kick && cmd.seq != player.last_kick_seq && player.kick_cooldown <= 0) {
        player.last_kick_seq = cmd.seq;
        apply_kick(player.id);
        player.kick_cooldown = 1.0;
    }
    
    if (cmd.pickup && cmd.seq != player.last_pickup_seq) {
        player.last_pickup_seq = cmd.seq;
        for (auto it = pickups.begin(); it != pickups.end(); ++it) {
            if (distance(player.position, it->position) <= PICKUP_RANGE && it->cooldown <= 0) {
                if (player.has_weapon) {
                    spawn_pickup(player.weapon_type, player.weapon_ammo, player.position, player.facing * 80);
                }
                player.has_weapon = true;
                player.weapon_type = it->weapon_type;
                player.weapon_ammo = it->ammo;
                player.shoot_cooldown = (it->weapon_type == "uzi" ? 0.1 : 0.4);
                pickups.erase(it);
                break;
            }
        }
    }
    
    if (cmd.shoot && player.has_weapon && player.shoot_cooldown <= 0 && player.weapon_ammo > 0) {
        Projectile proj;
        proj.id = next_projectile_id++;
        proj.owner_id = player.id;
        proj.position = player.position;
        proj.velocity = player.facing * BULLET_SPEED;
        proj.remaining_life = BULLET_LIFETIME;
        projectiles.push_back(proj);
        
        player.weapon_ammo--;
        player.shoot_cooldown = (player.weapon_type == "uzi" ? 0.1 : 0.4);
    }
}

void GameSimulator::apply_movement(PlayerState& player, const Command& cmd, double dt) {
    double mx = std::clamp(cmd.move_x, -1.0, 1.0);
    double my = std::clamp(cmd.move_y, -1.0, 1.0);
    Vec2 move_dir(mx, my);
    
    if (move_dir.length() > 0.01) {
        move_dir = move_dir.normalize();
    }
    
    Vec2 target_vel = move_dir * config.player_speed;
    Vec2 vel_diff = target_vel - player.velocity;
    double diff_len = vel_diff.length();
    
    if (diff_len > 0) {
        double max_accel = (move_dir.length() > 0.1) ? config.acceleration : config.deceleration;
        double max_change = max_accel * dt;
        
        if (diff_len > max_change) {
            vel_diff = vel_diff.normalize() * max_change;
        }
        
        player.velocity = player.velocity + vel_diff;
    }
    
    if (player.velocity.length() > config.player_speed) {
        player.velocity = player.velocity.normalize() * config.player_speed;
    }
    
    player.position = player.position + player.velocity * dt;
}

void GameSimulator::apply_kick(int kicker_id) {
    PlayerState& kicker = players[kicker_id - 1];
    PlayerState& target = players[1 - (kicker_id - 1)];
    
    Vec2 to_target = target.position - kicker.position;
    double dist = to_target.length();
    
    if (dist > KICK_RANGE) return;
    
    Vec2 dir = to_target.normalize();
    if (kicker.facing.dot(dir) < KICK_DOT_THRESHOLD) return;
    
    target.stun_remaining = STUN_DURATION;
    target.stun_direction = dir;
    target.velocity = dir * STUN_SPEED;
    
    if (target.has_weapon) {
        spawn_pickup(target.weapon_type, target.weapon_ammo, target.position, dir * 100);
        target.has_weapon = false;
        target.weapon_type = "";
        target.weapon_ammo = 0;
    }
}

void GameSimulator::update_projectiles(double dt) {
    for (auto it = projectiles.begin(); it != projectiles.end();) {
        it->position = it->position + it->velocity * dt;
        it->remaining_life -= dt;
        
        bool hit = false;
        
        for (auto& player : players) {
            if (!player.alive) continue;
            if (player.id == it->owner_id) continue;
            
            if ((player.position - it->position).length() < PLAYER_RADIUS) {
                player.alive = false;
                hit = true;
                break;
            }
        }
        
        if (hit || it->remaining_life <= 0) {
            it = projectiles.erase(it);
        } else {
            ++it;
        }
    }
}

void GameSimulator::apply_physics() {
    for (auto& player : players) {
        if (!player.alive) continue;
        player.position.x = std::clamp(player.position.x, PLAYER_RADIUS, config.arena_width - PLAYER_RADIUS);
        player.position.y = std::clamp(player.position.y, PLAYER_RADIUS, config.arena_height - PLAYER_RADIUS);
    }
}

void GameSimulator::check_collisions() {
    double min_dist = PLAYER_RADIUS * 2;
    Vec2 delta = players[1].position - players[0].position;
    double dist = delta.length();
    
    if (dist < min_dist && players[0].alive && players[1].alive) {
        Vec2 normal = delta.normalize();
        double overlap = (min_dist - dist) / 2;
        players[0].position = players[0].position - normal * overlap;
        players[1].position = players[1].position + normal * overlap;
    }
}

void GameSimulator::check_round_end() {
    int alive_count = 0;
    int last_alive = -1;
    
    for (const auto& player : players) {
        if (player.alive) {
            alive_count++;
            last_alive = player.id;
        }
    }
    
    if (alive_count <= 1) {
        round_over = true;
        if (alive_count == 1) {
            winner = last_alive;
            end_reason = "elimination";
        } else {
            end_reason = "double_elimination";
        }
    } else if (current_tick >= config.max_ticks) {
        round_over = true;
        end_reason = "time_limit";
    }
}

void GameSimulator::spawn_pickup(const std::string& weapon_type, int ammo, const Vec2& pos, const Vec2& vel) {
    PickupItem pickup;
    pickup.id = next_pickup_id++;
    pickup.position = pos;
    pickup.weapon_type = weapon_type;
    pickup.ammo = ammo;
    pickup.cooldown = WEAPON_PICKUP_COOLDOWN;
    pickups.push_back(pickup);
}

double GameSimulator::distance(const Vec2& a, const Vec2& b) const {
    return (a - b).length();
}

std::string GameSimulator::get_hello_message(int player_id) const {
    std::stringstream ss;
    ss << "{\"type\":\"hello\",\"player_id\":" << player_id 
       << ",\"tick_rate\":" << config.tick_rate << "}";
    return ss.str();
}

std::string GameSimulator::get_round_start_message(int player_id) const {
    std::stringstream ss;
    ss << "{\"type\":\"round_start\",\"player_id\":" << player_id
       << ",\"enemy_id\":" << (player_id == 1 ? 2 : 1)
       << ",\"tick_rate\":" << config.tick_rate 
       << ",\"level\":{\"identifier\":\"" << current_level << "\"}}";
    return ss.str();
}

std::string GameSimulator::get_tick_message(int player_id) const {
    std::stringstream ss;
    const PlayerState& me = players[player_id - 1];
    const PlayerState& enemy = players[1 - (player_id - 1)];
    
    ss << "{\"type\":\"tick\",\"tick\":" << current_tick
       << ",\"time_seconds\":" << get_time_seconds()
       << ",\"you\":{"
       << "\"id\":" << me.id
       << ",\"position\":{\"x\":" << me.position.x << ",\"y\":" << me.position.y << "}"
       << ",\"facing\":{\"x\":" << me.facing.x << ",\"y\":" << me.facing.y << "}"
       << ",\"alive\":" << (me.alive ? "true" : "false")
       << ",\"weapon\":" << (me.has_weapon ? "{\"type\":\"" + me.weapon_type + "\",\"ammo\":" + std::to_string(me.weapon_ammo) + "}" : "null")
       << "}"
       << ",\"enemy\":{"
       << "\"id\":" << enemy.id
       << ",\"position\":{\"x\":" << enemy.position.x << ",\"y\":" << enemy.position.y << "}"
       << ",\"alive\":" << (enemy.alive ? "true" : "false")
       << "}"
       << ",\"snapshot\":{\"pickups\":[";
    
    bool first = true;
    for (const auto& p : pickups) {
        if (!first) ss << ",";
        ss << "{\"id\":" << p.id << ",\"type\":\"" << p.weapon_type << "\",\"position\":[" << p.position.x << "," << p.position.y << "]}";
        first = false;
    }
    
    ss << "],\"projectiles\":[";
    first = true;
    for (const auto& p : projectiles) {
        if (!first) ss << ",";
        ss << "{\"id\":" << p.id << ",\"owner\":" << p.owner_id
           << ",\"position\":[" << p.position.x << "," << p.position.y << "]}";
        first = false;
    }
    
    ss << "],\"obstacles\":[]}}";
    return ss.str();
}

std::string GameSimulator::get_round_end_message() const {
    std::stringstream ss;
    ss << "{\"type\":\"round_end\",\"result\":{";
    if (winner > 0) ss << "\"winner_id\":" << winner << ",";
    ss << "\"reason\":\"" << end_reason << "\"}}";
    return ss.str();
}

std::string GameSimulator::get_state_json() const {
    std::stringstream ss;
    ss << "{\"type\":\"state\",\"tick\":" << current_tick
       << ",\"time_seconds\":" << get_time_seconds()
       << ",\"status\":\"" << (round_over ? "finished" : "running") << "\""
       << ",\"bots_connected\":[1,2]"
       << ",\"color_roles\":["
       << "{\"color\":\"red\",\"player_id\":1,\"bot_name\":\"Bot A\"},"
       << "{\"color\":\"green\",\"player_id\":2,\"bot_name\":\"Bot B\"}"
       << "]"
       << ",\"players\":[";
    
    for (size_t i = 0; i < players.size(); i++) {
        if (i > 0) ss << ",";
        ss << "{\"id\":" << players[i].id
           << ",\"position\":[" << players[i].position.x << "," << players[i].position.y << "]"
           << ",\"facing\":[" << players[i].facing.x << "," << players[i].facing.y << "]"
           << ",\"alive\":" << (players[i].alive ? "true" : "false")
           << ",\"color\":\"" << players[i].color << "\""
           << ",\"weapon\":" << (players[i].has_weapon ? "{\"type\":\"" + players[i].weapon_type + "\",\"ammo\":" + std::to_string(players[i].weapon_ammo) + "}" : "null")
           << "}";
    }
    
    ss << "],\"pickups\":[";
    bool first = true;
    for (const auto& p : pickups) {
        if (!first) ss << ",";
        ss << "{\"id\":" << p.id << ",\"type\":\"" << p.weapon_type << "\",\"position\":[" << p.position.x << "," << p.position.y << "]}";
        first = false;
    }
    
    ss << "],\"projectiles\":[";
    first = true;
    for (const auto& p : projectiles) {
        if (!first) ss << ",";
        ss << "{\"id\":" << p.id << ",\"owner\":" << p.owner_id
           << ",\"position\":[" << p.position.x << "," << p.position.y << "]}";
        first = false;
    }
    
    ss << "],\"result\":";
    if (round_over && winner > 0) {
        ss << "{\"winner_id\":" << winner << ",\"reason\":\"" << end_reason << "\"}";
    } else {
        ss << "null";
    }
    
    ss << "}";
    return ss.str();
}