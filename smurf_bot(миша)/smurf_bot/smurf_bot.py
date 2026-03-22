from __future__ import annotations

import math
import sys
import random
import time
from dataclasses import dataclass, field
from typing import Any, Tuple, List, Set, Optional, Dict
from collections import deque


@dataclass
class TerminatorBot:
    """БОТ-ТЕРМИНАТОР - Использует все механики GAICA для победы"""
    
    command_seq: int = 0
    player_id: int = 0
    enemy_id: int = 0
    tick_rate: int = 30
    
    # Размеры карты
    level_width: float = 1000
    level_height: float = 1000
    floor_grid_size: float = 64.0
    floor_cells: Set[Tuple[int, int]] = field(default_factory=set)
    
    # Препятствия
    obstacles: List[dict] = field(default_factory=list)
    doors: List[Tuple[float, float, int]] = field(default_factory=list)
    breakables: Dict[int, dict] = field(default_factory=dict)  # стекло, ящики
    letterboxes: List[Tuple[float, float, int]] = field(default_factory=list)
    
    # Предсказание врага (глубокое)
    enemy_pos_history: deque = field(default_factory=lambda: deque(maxlen=30))
    enemy_vel_history: deque = field(default_factory=lambda: deque(maxlen=10))
    last_enemy_x: float = 0
    last_enemy_y: float = 0
    last_enemy_vx: float = 0
    last_enemy_vy: float = 0
    enemy_last_shoot_time: float = 0
    enemy_weapon: str = "unknown"
    enemy_weapon_last_seen: float = 0
    
    # Память о предметах
    weapon_on_ground: Dict[int, Tuple[float, float, str]] = field(default_factory=dict)
    last_pickup_attempt: int = 0
    
    # Тактические состояния
    battle_mode: str = "aggressive"  # aggressive, defensive, ringout, steal_weapon, door_control
    strafe_dir: float = 1.0
    strafe_timer: int = 0
    stuck_counter: int = 0
    last_position: Tuple[float, float] = (0, 0)
    stuck_threshold: int = 30
    escape_mode: bool = False
    shield_mode: bool = False
    last_shield_drop: int = -100
    last_kick_for_weapon: int = -100
    door_control_target: Optional[Tuple[float, float, int]] = None
    
    # Навигация
    path_cache: Dict[Tuple[int, int], List[Tuple[int, int]]] = field(default_factory=dict)
    cached_path: List[Tuple[int, int]] = field(default_factory=list)
    path_index: int = 0
    last_path_target: Tuple[int, int] = (-1, -1)
    
    # Константы
    PLAYER_RADIUS: float = 10.0
    BULLET_SPEED: float = 500.0
    KICK_DISTANCE: float = 28.0
    KICK_ANGLE_DOT: float = 0.45
    REVOLVER_RANGE: float = 240.0
    UZI_RANGE: float = 180.0
    PICKUP_RANGE: float = 35.0
    WALL_AVOID_DIST: float = 35.0
    PATH_UPDATE_INTERVAL: int = 10
    SHIELD_DROP_COOLDOWN: int = 50
    STUN_DURATION: float = 0.5
    STUN_SPEED: float = 350.0
    
    # Веса для решений
    WEAPON_PRIORITY = {"Uzi": 10, "Revolver": 7}
    RINGOUT_BONUS = 50
    
    def log(self, msg: str):
        print(f"[BOT] {msg}", file=sys.stderr, flush=True)
    
    # ========== БАЗОВЫЕ МЕТОДЫ ==========
    
    def _get_pos(self, obj: dict) -> Tuple[float, float]:
        if not obj:
            return 0.0, 0.0
        pos = obj.get("position")
        if not pos:
            pos = obj.get("center")
        if not pos:
            return 0.0, 0.0
        if isinstance(pos, dict):
            return float(pos.get("x", 0.0)), float(pos.get("y", 0.0))
        if isinstance(pos, (list, tuple)) and len(pos) >= 2:
            return float(pos[0]), float(pos[1])
        return 0.0, 0.0
    
    def _get_facing(self, obj: dict) -> Tuple[float, float]:
        facing = obj.get("facing", [1, 0])
        if isinstance(facing, dict):
            return float(facing.get("x", 1)), float(facing.get("y", 0))
        if isinstance(facing, (list, tuple)) and len(facing) >= 2:
            return float(facing[0]), float(facing[1])
        return 1.0, 0.0
    
    def _vec_norm(self, x: float, y: float) -> Tuple[float, float]:
        l = math.hypot(x, y)
        if l < 1e-9:
            return 0.0, 0.0
        return x / l, y / l
    
    def _vec_dot(self, ax, ay, bx, by):
        return ax * bx + ay * by
    
    def _distance(self, x1, y1, x2, y2):
        return math.hypot(x1 - x2, y1 - y2)
    
    def _world_to_grid(self, x: float, y: float) -> Tuple[int, int]:
        return int(x // self.floor_grid_size), int(y // self.floor_grid_size)
    
    def _grid_to_world(self, gx: int, gy: int) -> Tuple[float, float]:
        return (gx + 0.5) * self.floor_grid_size, (gy + 0.5) * self.floor_grid_size
    
    def _is_grounded(self, x: float, y: float) -> bool:
        if not self.floor_cells:
            return True
        cx, cy = self._world_to_grid(x, y)
        return (cx, cy) in self.floor_cells
    
    def _circle_rect_collision(self, cx, cy, radius, rx, ry, hx, hy) -> bool:
        nearest_x = max(rx - hx, min(cx, rx + hx))
        nearest_y = max(ry - hy, min(cy, ry + hy))
        dx = cx - nearest_x
        dy = cy - nearest_y
        return (dx * dx + dy * dy) <= radius * radius
    
    def _ray_rect_intersection(self, x1, y1, x2, y2, rx, ry, hx, hy) -> bool:
        left = rx - hx
        right = rx + hx
        top = ry - hy
        bottom = ry + hy
        
        dx = x2 - x1
        dy = y2 - y1
        
        p = [-dx, dx, -dy, dy]
        q = [x1 - left, right - x1, y1 - top, bottom - y1]
        
        tmin, tmax = 0.0, 1.0
        
        for i in range(4):
            if abs(p[i]) < 1e-9:
                if q[i] < 0:
                    return False
            else:
                t = q[i] / p[i]
                if p[i] < 0:
                    if t > tmin:
                        tmin = t
                else:
                    if t < tmax:
                        tmax = t
        
        return tmin <= tmax
    
    def _can_move_to(self, x: float, y: float) -> bool:
        if x < self.PLAYER_RADIUS or x > self.level_width - self.PLAYER_RADIUS:
            return False
        if y < self.PLAYER_RADIUS or y > self.level_height - self.PLAYER_RADIUS:
            return False
        if not self._is_grounded(x, y):
            return False
        
        for ob in self.obstacles:
            if not ob.get("solid", True):
                continue
            kind = str(ob.get("kind", "")).lower()
            if kind == "door":
                continue
            if kind in ["glass", "box"]:
                continue
            
            center = ob.get("center") or ob.get("position") or [0, 0]
            if isinstance(center, dict):
                ox, oy = center.get("x", 0), center.get("y", 0)
            else:
                ox, oy = center[0], center[1]
            
            hs = ob.get("half_size") or [0, 0]
            if isinstance(hs, dict):
                hx, hy = hs.get("x", 0), hs.get("y", 0)
            else:
                hx, hy = hs[0], hs[1]
            
            if self._circle_rect_collision(x, y, self.PLAYER_RADIUS + 2, ox, oy, hx, hy):
                return False
        
        return True
    
    # ========== ПОИСК ПУТИ (A*) ==========
    
    def _build_walkable_grid(self):
        self.grid_width = int(math.ceil(self.level_width / self.floor_grid_size))
        self.grid_height = int(math.ceil(self.level_height / self.floor_grid_size))
        self.walkable_grid = [[True for _ in range(self.grid_width)] for _ in range(self.grid_height)]
        
        for ob in self.obstacles:
            if not ob.get("solid", True):
                continue
            kind = str(ob.get("kind", "")).lower()
            if kind in ["door", "glass", "box"]:
                continue
            
            center = ob.get("center") or ob.get("position") or [0, 0]
            if isinstance(center, dict):
                ox, oy = center.get("x", 0), center.get("y", 0)
            else:
                ox, oy = center[0], center[1]
            
            hs = ob.get("half_size") or [0, 0]
            if isinstance(hs, dict):
                hx, hy = hs.get("x", 0), hs.get("y", 0)
            else:
                hx, hy = hs[0], hs[1]
            
            left = int((ox - hx) // self.floor_grid_size)
            right = int((ox + hx) // self.floor_grid_size)
            top = int((oy - hy) // self.floor_grid_size)
            bottom = int((oy + hy) // self.floor_grid_size)
            
            for gy in range(max(0, top), min(self.grid_height, bottom + 1)):
                for gx in range(max(0, left), min(self.grid_width, right + 1)):
                    self.walkable_grid[gy][gx] = False
    
    def _is_cell_walkable(self, gx, gy):
        if gx < 0 or gx >= self.grid_width or gy < 0 or gy >= self.grid_height:
            return False
        if (gx, gy) not in self.floor_cells:
            return False
        if not self.walkable_grid[gy][gx]:
            return False
        return True
    
    def _heuristic(self, a, b):
        return abs(a[0] - b[0]) + abs(a[1] - b[1])
    
    def _astar(self, start: Tuple[int, int], goal: Tuple[int, int]) -> List[Tuple[int, int]]:
        if not self._is_cell_walkable(start[0], start[1]) or not self._is_cell_walkable(goal[0], goal[1]):
            return []
        
        open_set = []
        heapq.heappush(open_set, (0, start))
        
        came_from = {}
        g_score = {start: 0}
        f_score = {start: self._heuristic(start, goal)}
        
        while open_set:
            _, current = heapq.heappop(open_set)
            
            if current == goal:
                path = []
                while current in came_from:
                    path.append(current)
                    current = came_from[current]
                path.reverse()
                return path
            
            for dx, dy in [(0, 1), (0, -1), (1, 0), (-1, 0)]:
                neighbor = (current[0] + dx, current[1] + dy)
                if not self._is_cell_walkable(neighbor[0], neighbor[1]):
                    continue
                
                tentative_g = g_score[current] + 1
                if tentative_g < g_score.get(neighbor, float("inf")):
                    came_from[neighbor] = current
                    g_score[neighbor] = tentative_g
                    f_score[neighbor] = tentative_g + self._heuristic(neighbor, goal)
                    heapq.heappush(open_set, (f_score[neighbor], neighbor))
        
        return []
    
    def _get_path_direction(self, from_x, from_y, to_x, to_y) -> Tuple[float, float]:
        from_grid = self._world_to_grid(from_x, from_y)
        to_grid = self._world_to_grid(to_x, to_y)
        
        if to_grid != self.last_path_target or not self.cached_path:
            self.last_path_target = to_grid
            self.cached_path = self._astar(from_grid, to_grid)
            self.path_index = 0
        
        if self.cached_path and len(self.cached_path) > self.path_index:
            target_grid = self.cached_path[self.path_index]
            target_x, target_y = self._grid_to_world(target_grid[0], target_grid[1])
            
            dist_to_target = math.hypot(target_x - from_x, target_y - from_y)
            if dist_to_target < self.floor_grid_size * 0.5:
                self.path_index += 1
            
            return self._vec_norm(target_x - from_x, target_y - from_y)
        
        return self._vec_norm(to_x - from_x, to_y - from_y)
    
    # ========== ПРОДВИНУТОЕ ПРЕДСКАЗАНИЕ ВРАГА ==========
    
    def _update_enemy_prediction(self, x: float, y: float, alive: bool, weapon: Optional[dict]):
        current_time = time.time()
        self.enemy_pos_history.append((x, y, current_time))
        
        # Запоминаем оружие врага
        if weapon and weapon.get("type"):
            self.enemy_weapon = weapon.get("type")
            self.enemy_weapon_last_seen = current_time
        
        if len(self.enemy_pos_history) >= 3:
            x1, y1, t1 = self.enemy_pos_history[-3]
            x2, y2, t2 = self.enemy_pos_history[-2]
            x3, y3, t3 = self.enemy_pos_history[-1]
            
            dt1 = t2 - t1 if t2 > t1 else 0.033
            dt2 = t3 - t2 if t3 > t2 else 0.033
            
            if dt1 > 0 and dt2 > 0:
                vx1 = (x2 - x1) / dt1
                vy1 = (y2 - y1) / dt1
                vx2 = (x3 - x2) / dt2
                vy2 = (y3 - y2) / dt2
                
                self.last_enemy_vx = vx2 * 0.5 + self.last_enemy_vx * 0.5
                self.last_enemy_vy = vy2 * 0.5 + self.last_enemy_vy * 0.5
                
                ax = (vx2 - vx1) / dt2 if dt2 > 0 else 0
                ay = (vy2 - vy1) / dt2 if dt2 > 0 else 0
                self.enemy_vel_history.append((self.last_enemy_vx, self.last_enemy_vy, ax, ay))
        
        self.last_enemy_x = x
        self.last_enemy_y = y
    
    def _predict_enemy_position(self, time_ahead: float = 0.2) -> Tuple[float, float]:
        if len(self.enemy_pos_history) < 2:
            return self.last_enemy_x, self.last_enemy_y
        
        x, y, _ = self.enemy_pos_history[-1]
        
        pred_x = x + self.last_enemy_vx * time_ahead
        pred_y = y + self.last_enemy_vy * time_ahead
        
        if len(self.enemy_vel_history) >= 2:
            vx, vy, ax, ay = self.enemy_vel_history[-1]
            pred_x += 0.5 * ax * time_ahead * time_ahead
            pred_y += 0.5 * ay * time_ahead * time_ahead
        
        pred_x = max(self.PLAYER_RADIUS, min(self.level_width - self.PLAYER_RADIUS, pred_x))
        pred_y = max(self.PLAYER_RADIUS, min(self.level_height - self.PLAYER_RADIUS, pred_y))
        
        return pred_x, pred_y
    
    def _calculate_lead_shot(self, mx, my, ex, ey, evx, evy) -> Tuple[float, float]:
        dx = ex - mx
        dy = ey - my
        
        a = evx * evx + evy * evy - self.BULLET_SPEED * self.BULLET_SPEED
        b = 2 * (dx * evx + dy * evy)
        c = dx * dx + dy * dy
        
        disc = b * b - 4 * a * c
        
        if disc < 0 or abs(a) < 1e-9:
            return self._vec_norm(dx, dy)
        
        t1 = (-b - math.sqrt(disc)) / (2 * a)
        t2 = (-b + math.sqrt(disc)) / (2 * a)
        
        t = min(t for t in [t1, t2] if t > 0)
        if t <= 0 or t > 0.5:
            t = 0.2
        
        target_x = ex + evx * t
        target_y = ey + evy * t
        
        return self._vec_norm(target_x - mx, target_y - my)
    
    # ========== ПРОВЕРКА ВИДИМОСТИ ==========
    
    def _can_see_enemy(self, mx, my, ex, ey) -> Tuple[bool, Optional[dict]]:
        dist = math.hypot(ex - mx, ey - my)
        if dist < 1:
            return True, None
        
        nx = (ex - mx) / dist
        ny = (ey - my) / dist
        step_size = 8.0
        steps = int(dist / step_size) + 1
        
        for i in range(steps + 1):
            check_x = mx + nx * i * step_size
            check_y = my + ny * i * step_size
            
            if math.hypot(check_x - ex, check_y - ey) < self.PLAYER_RADIUS:
                return True, None
            
            for ob in self.obstacles:
                if not ob.get("solid", True):
                    continue
                
                kind = str(ob.get("kind", "")).lower()
                if kind == "door":
                    continue
                if kind in ["glass", "box"]:
                    continue
                
                center = ob.get("center") or ob.get("position") or [0, 0]
                if isinstance(center, dict):
                    ox, oy = center.get("x", 0), center.get("y", 0)
                else:
                    ox, oy = center[0], center[1]
                
                hs = ob.get("half_size") or [0, 0]
                if isinstance(hs, dict):
                    hx, hy = hs.get("x", 0), hs.get("y", 0)
                else:
                    hx, hy = hs[0], hs[1]
                
                if self._ray_rect_intersection(mx, my, check_x, check_y, ox, oy, hx, hy):
                    if kind == "wall":
                        return False, ob
                    return False, ob
        
        return True, None
    
    # ========== ТАКТИЧЕСКИЕ МЕТОДЫ ==========
    
    def _find_ringout_direction(self, enemy_x, enemy_y) -> Tuple[float, float]:
        """Находит направление для выбивания в яму"""
        best_dir = (0.0, 0.0)
        best_score = 0.0
        
        for angle in [0, 45, 90, 135, 180, 225, 270, 315]:
            rad = math.radians(angle)
            ux, uy = math.cos(rad), math.sin(rad)
            
            tx = enemy_x + ux * 45
            ty = enemy_y + uy * 45
            
            if not self._is_grounded(tx, ty):
                score = 2.0
                tx2 = enemy_x + ux * 90
                ty2 = enemy_y + uy * 90
                if not self._is_grounded(tx2, ty2):
                    score = 3.0
                
                if score > best_score:
                    best_score = score
                    best_dir = (ux, uy)
        
        return best_dir
    
    def _is_near_pit(self, x: float, y: float) -> bool:
        """Проверяет, находится ли игрок у края ямы"""
        for dx in [-32, 0, 32]:
            for dy in [-32, 0, 32]:
                test_x = x + dx
                test_y = y + dy
                if not self._is_grounded(test_x, test_y):
                    return True
        return False
    
    def _should_steal_weapon(self, my_weapon: str, enemy_weapon: str, dist: float) -> bool:
        """Стоит ли кикать чтобы украсть оружие"""
        if dist > self.KICK_DISTANCE:
            return False
        if enemy_weapon == "Uzi":
            if not my_weapon or my_weapon == "Revolver":
                return True
        return False
    
    def _find_letterboxes(self) -> List[Tuple[float, float, int]]:
        """Находит все почтовые ящики"""
        boxes = []
        for ob in self.obstacles:
            if str(ob.get("kind", "")).lower() == "letterbox":
                x, y = self._get_pos(ob)
                boxes.append((x, y, ob.get("id", 0)))
        return boxes
    
    def _find_best_pickup(self, mx, my, pickups) -> Tuple[float, float, float, int, str]:
        best_dist = float("inf")
        best_x, best_y = 0, 0
        best_id = 0
        best_type = ""
        
        for p in pickups:
            if p.get("cooldown", 0) > 0:
                continue
            
            px, py = self._get_pos(p)
            d = math.hypot(px - mx, py - my)
            ptype = str(p.get("type", ""))
            priority = 0.7 if ptype == "Uzi" else 1.0
            score = d * priority
            
            if score < best_dist:
                best_dist = score
                best_x, best_y = px, py
                best_id = p.get("id", 0)
                best_type = ptype
        
        return best_x, best_y, best_dist, best_id, best_type
    
    def _find_doors(self) -> List[Tuple[float, float, int]]:
        doors = []
        for ob in self.obstacles:
            if str(ob.get("kind", "")).lower() == "door":
                x, y = self._get_pos(ob)
                doors.append((x, y, ob.get("id", 0)))
        return doors
    
    def _find_nearest_door(self, mx, my) -> Optional[Tuple[float, float, int]]:
        best_dist = float("inf")
        best_door = None
        for x, y, did in self.doors:
            dist = math.hypot(x - mx, y - my)
            if dist < best_dist:
                best_dist = dist
                best_door = (x, y, did)
        return best_door
    
    def _is_bullet_coming(self, mx, my, projectiles) -> Tuple[bool, Tuple[float, float]]:
        danger_x, danger_y = 0.0, 0.0
        is_danger = False
        
        for proj in projectiles:
            px, py = self._get_pos(proj)
            pvx, pvy = 0, 0
            vel = proj.get("velocity")
            if vel:
                if isinstance(vel, dict):
                    pvx, pvy = vel.get("x", 0), vel.get("y", 0)
                elif isinstance(vel, (list, tuple)) and len(vel) >= 2:
                    pvx, pvy = vel[0], vel[1]
            
            to_me_x, to_me_y = mx - px, my - py
            dist_proj = math.hypot(to_me_x, to_me_y)
            
            if dist_proj < 100 and (pvx != 0 or pvy != 0):
                proj_dir = self._vec_norm(pvx, pvy)
                to_me_dir = self._vec_norm(to_me_x, to_me_y)
                dot = self._vec_dot(proj_dir[0], proj_dir[1], to_me_dir[0], to_me_dir[1])
                
                if dot > 0.5 and dist_proj < 80:
                    is_danger = True
                    danger_x += to_me_dir[0]
                    danger_y += to_me_dir[1]
        
        if is_danger:
            danger_x, danger_y = self._vec_norm(danger_x, danger_y)
        return is_danger, (danger_x, danger_y)
    
    def _find_explore_direction(self, mx, my) -> Tuple[float, float]:
        directions = [(1, 0), (0, 1), (-1, 0), (0, -1),
                      (0.707, 0.707), (-0.707, 0.707), (0.707, -0.707), (-0.707, -0.707)]
        for dx, dy in directions:
            test_x = mx + dx * 40
            test_y = my + dy * 40
            if self._can_move_to(test_x, test_y):
                return dx, dy
        center_x = self.level_width / 2
        center_y = self.level_height / 2
        return self._vec_norm(center_x - mx, center_y - my)
    
    def _find_safe_move(self, from_x, from_y, to_x, to_y) -> Tuple[float, float]:
        dir_x, dir_y = self._vec_norm(to_x - from_x, to_y - from_y)
        angles = [0, 30, -30, 60, -60, 90, -90, 120, -120, 150, -150, 180]
        
        for angle in angles:
            rad = math.radians(angle)
            new_x = dir_x * math.cos(rad) - dir_y * math.sin(rad)
            new_y = dir_x * math.sin(rad) + dir_y * math.cos(rad)
            test_x = from_x + new_x * 20
            test_y = from_y + new_y * 20
            if self._can_move_to(test_x, test_y):
                return new_x, new_y
        return 0.0, 0.0
    
    # ========== ОСНОВНЫЕ МЕТОДЫ ==========
    
    def on_hello(self, msg: dict):
        self.player_id = msg.get("player_id", 0)
        self.tick_rate = msg.get("tick_rate", 30)
        self.log(f"TERMINATOR ACTIVATED! Player {self.player_id}")
    
    def on_round_start(self, msg: dict):
        level = msg.get("level", {})
        self.level_width = level.get("width", 1000)
        self.level_height = level.get("height", 1000)
        self.enemy_id = msg.get("enemy_id", 0)
        self.command_seq = 0
        
        self.obstacles = level.get("static_obstacles", [])
        self.doors = self._find_doors()
        self.letterboxes = self._find_letterboxes()
        
        floor = level.get("floor", {})
        self.floor_grid_size = floor.get("grid_size", 64)
        self.floor_cells.clear()
        for cell in floor.get("cells", []):
            if len(cell) >= 2:
                self.floor_cells.add((int(cell[0]), int(cell[1])))
        
        self._build_walkable_grid()
        
        self.enemy_pos_history.clear()
        self.enemy_vel_history.clear()
        self.last_enemy_x = self.last_enemy_y = 0
        self.last_enemy_vx = self.last_enemy_vy = 0
        self.stuck_counter = 0
        self.escape_mode = False
        self.shield_mode = False
        self.battle_mode = "aggressive"
        
        self.log(f"Round start! {self.grid_width}x{self.grid_height} | Doors: {len(self.doors)} | Letterboxes: {len(self.letterboxes)}")
    
    def on_round_end(self, msg: dict):
        result = msg.get("result", {})
        self.log(f"VICTORY! Reason: {result.get('reason')}")
    
    def on_tick(self, msg: dict) -> dict:
        tick = msg.get("tick", 0)
        you = msg.get("you", {})
        enemy = msg.get("enemy", {})
        snapshot = msg.get("snapshot", {})
        
        # Позиции
        mx, my = self._get_pos(you)
        ex, ey = self._get_pos(enemy)
        enemy_alive = enemy.get("alive", False)
        my_alive = you.get("alive", True)
        
        # Состояния
        weapon = you.get("weapon")
        has_weapon = weapon and weapon.get("type") not in (None, "none")
        weapon_type = weapon.get("type", "") if weapon else ""
        shoot_cd = you.get("shoot_cooldown", 0)
        kick_cd = you.get("kick_cooldown", 0)
        stun = you.get("stun_remaining", 0)
        
        # Обновляем мир
        self.obstacles = snapshot.get("obstacles", self.obstacles)
        pickups = snapshot.get("pickups", [])
        projectiles = snapshot.get("projectiles", [])
        
        # Обновляем двери и ящики
        self.doors = self._find_doors()
        
        # Расстояние до врага
        dx = ex - mx
        dy = ey - my
        dist = math.hypot(dx, dy) if enemy_alive else 999
        
        # Обновляем предсказание врага
        if enemy_alive:
            self._update_enemy_prediction(ex, ey, enemy_alive, enemy.get("weapon"))
        
        # Обнаружение застревания
        current_pos = (mx, my)
        if self.last_position == (0, 0):
            self.last_position = current_pos
        else:
            moved = math.hypot(current_pos[0] - self.last_position[0], 
                              current_pos[1] - self.last_position[1])
            if moved < 5:
                self.stuck_counter += 1
            else:
                self.stuck_counter = 0
                self.escape_mode = False
        self.last_position = current_pos
        
        if self.stuck_counter > self.stuck_threshold and not self.escape_mode:
            self.escape_mode = True
            self.log(f"STUCK! Entering escape mode")
        
        # Проверка на летящие пули
        bullet_danger, danger_dir = self._is_bullet_coming(mx, my, projectiles)
        
        # ========== ВЫБОР СТРАТЕГИИ ==========
        
        # 1. Режим побега (если застрял)
        if self.escape_mode:
            self.battle_mode = "escape"
        
        # 2. Выбивание в яму
        elif enemy_alive:
            ringout_dir = self._find_ringout_direction(ex, ey)
            if ringout_dir[0] != 0 or ringout_dir[1] != 0:
                if dist <= self.KICK_DISTANCE * 1.5:
                    self.battle_mode = "ringout"
                elif self._is_near_pit(ex, ey):
                    self.battle_mode = "ringout"
        
        # 3. Кража оружия
        elif enemy_alive and self._should_steal_weapon(weapon_type, self.enemy_weapon, dist):
            self.battle_mode = "steal_weapon"
        
        # 4. Контроль дверей (если враг за дверью)
        elif enemy_alive and dist < 150 and self.doors:
            self.battle_mode = "door_control"
        
        # 5. Поиск Uzi через letterbox
        elif weapon_type == "Revolver" and self.letterboxes:
            self.battle_mode = "find_uzi"
        
        # 6. Поиск оружия
        elif not has_weapon and pickups:
            self.battle_mode = "search_weapon"
        
        # 7. Агрессивный бой
        elif has_weapon and enemy_alive and dist < self.REVOLVER_RANGE:
            self.battle_mode = "aggressive"
        
        # 8. Оборонительный режим
        elif bullet_danger:
            self.battle_mode = "defensive"
        
        else:
            self.battle_mode = "approach"
        
        # Логирование
        if tick % 100 == 0:
            wt = weapon_type if has_weapon else "none"
            self.log(f"[{tick}] POS:({mx:.0f},{my:.0f}) ENEMY:({ex:.0f},{ey:.0f}) WEAPON:{wt} MODE:{self.battle_mode} DANGER:{bullet_danger}")
        
        if not my_alive or stun > 0:
            return self._cmd(0, 0, 1, 0, False, False, False, False)
        
        # ========== ТАКТИКА ЩИТА ==========
        can_use_shield = (has_weapon and enemy_alive and dist < 150 and 
                          bullet_danger and tick - self.last_shield_drop > self.SHIELD_DROP_COOLDOWN)
        
        if can_use_shield and self.battle_mode != "ringout":
            facing_x, facing_y = self._get_facing(you)
            self.log(f"SHIELD DEPLOYED!")
            self.shield_mode = True
            self.last_shield_drop = tick
            return self._cmd(0, 0, facing_x, facing_y, False, False, False, True)
        
        if self.shield_mode:
            for p in pickups:
                if p.get("cooldown", 0) > 0:
                    continue
                px, py = self._get_pos(p)
                if math.hypot(px - mx, py - my) < self.PICKUP_RANGE:
                    self.log(f"SHIELD: picking weapon back")
                    self.shield_mode = False
                    return self._cmd(0, 0, 1, 0, False, False, True, False)
        
        # ========== ОПРЕДЕЛЯЕМ ЦЕЛЬ ==========
        target_x, target_y = ex, ey
        target_type = "enemy"
        
        # Режим побега
        if self.battle_mode == "escape":
            nearest_door = self._find_nearest_door(mx, my)
            if nearest_door:
                target_x, target_y, _ = nearest_door
                if math.hypot(target_x - mx, target_y - my) < 50:
                    self.escape_mode = False
                    self.stuck_counter = 0
            else:
                target_x, target_y = self.level_width / 2, self.level_height / 2
        
        # Режим выбивания
        elif self.battle_mode == "ringout" and enemy_alive:
            ringout_dir = self._find_ringout_direction(ex, ey)
            if ringout_dir[0] != 0 or ringout_dir[1] != 0:
                target_x = ex - ringout_dir[0] * 30
                target_y = ey - ringout_dir[1] * 30
                target_type = "ringout"
        
        # Режим кражи оружия
        elif self.battle_mode == "steal_weapon" and enemy_alive:
            target_x, target_y = ex, ey
            if dist <= self.KICK_DISTANCE:
                return self._cmd(0, 0, dx/dist, dy/dist, False, True, False, False)
        
        # Режим поиска Uzi
        elif self.battle_mode == "find_uzi" and self.letterboxes:
            best_dist = float("inf")
            for lx, ly, lid in self.letterboxes:
                d = math.hypot(lx - mx, ly - my)
                if d < best_dist:
                    best_dist = d
                    target_x, target_y = lx, ly
            target_type = "letterbox"
            if best_dist < self.PICKUP_RANGE:
                return self._cmd(0, 0, 1, 0, False, True, False, False)
        
        # Режим поиска оружия
        elif self.battle_mode == "search_weapon" and pickups:
            px, py, pd, _, ptype = self._find_best_pickup(mx, my, pickups)
            if pd < 500:
                target_x, target_y = px, py
                target_type = "pickup"
                if pd <= self.PICKUP_RANGE:
                    return self._cmd(0, 0, 1, 0, False, False, True, False)
        
        # ========== ДВИЖЕНИЕ ==========
        if tick % self.PATH_UPDATE_INTERVAL == 0 or not self.cached_path:
            self._build_walkable_grid()
            self.cached_path = []
        
        move_x, move_y = self._get_path_direction(mx, my, target_x, target_y)
        
        if move_x == 0 and move_y == 0:
            move_x, move_y = self._find_explore_direction(mx, my)
        
        # Стрейф
        if has_weapon and enemy_alive and 50 < dist < 200 and self.battle_mode == "aggressive":
            self.strafe_timer += 1
            if self.strafe_timer > 40:
                self.strafe_dir *= -1
                self.strafe_timer = 0
            angle = math.atan2(dy, dx) + math.radians(90 * self.strafe_dir)
            strafe_x, strafe_y = math.cos(angle), math.sin(angle)
            move_x += strafe_x * 0.5
            move_y += strafe_y * 0.5
            move_x, move_y = self._vec_norm(move_x, move_y)
        
        # Уклонение от пуль
        if bullet_danger:
            move_x += danger_dir[0] * 1.2
            move_y += danger_dir[1] * 1.2
            move_x, move_y = self._vec_norm(move_x, move_y)
        
        # ========== ПРИЦЕЛИВАНИЕ ==========
        aim_x, aim_y = 1.0, 0.0
        
        if enemy_alive:
            can_see, _ = self._can_see_enemy(mx, my, ex, ey)
            if can_see:
                bullet_time = dist / self.BULLET_SPEED
                pred_x, pred_y = self._predict_enemy_position(bullet_time)
                
                if abs(self.last_enemy_vx) > 10 or abs(self.last_enemy_vy) > 10:
                    aim_x, aim_y = self._calculate_lead_shot(mx, my, ex, ey, self.last_enemy_vx, self.last_enemy_vy)
                else:
                    aim_x, aim_y = self._vec_norm(pred_x - mx, pred_y - my)
            else:
                aim_x, aim_y = self._vec_norm(dx, dy)
            
            if aim_x == 0 and aim_y == 0:
                aim_x, aim_y = self._vec_norm(dx, dy)
        
        # ========== СТРЕЛЬБА ==========
        shoot = False
        if has_weapon and shoot_cd <= 0 and enemy_alive and self.battle_mode != "escape":
            can_see, _ = self._can_see_enemy(mx, my, ex, ey)
            if can_see:
                if weapon_type == "Revolver" and dist <= self.REVOLVER_RANGE:
                    shoot = True
                elif weapon_type == "Uzi" and dist <= self.UZI_RANGE:
                    shoot = True
                elif weapon_type not in ["Revolver", "Uzi"]:
                    shoot = True
        
        # ========== КИК ==========
        kick = False
        if enemy_alive and dist <= self.KICK_DISTANCE and kick_cd <= 0:
            fx, fy = self._get_facing(you)
            tox, toy = self._vec_norm(dx, dy)
            if self._vec_dot(fx, fy, tox, toy) >= self.KICK_ANGLE_DOT:
                kick = True
                # Если это кик для кражи оружия
                if self.battle_mode == "steal_weapon":
                    self.last_kick_for_weapon = tick
        
        # ========== ПОДБОР ==========
        pickup = False
        if not has_weapon and pickups and self.battle_mode == "search_weapon":
            px, py, pd, _, _ = self._find_best_pickup(mx, my, pickups)
            if pd <= self.PICKUP_RANGE:
                pickup = True
        
        # ========== КОМАНДА ==========
        return self._cmd(move_x, move_y, aim_x, aim_y, shoot, kick, pickup, False)
    
    def _cmd(self, mx=0, my=0, ax=1, ay=0, shoot=False, kick=False, pickup=False, throw=False):
        cmd = {
            "type": "command",
            "seq": self.command_seq,
            "move": [max(-1, min(1, mx)), max(-1, min(1, my))],
            "aim": [max(-1, min(1, ax)), max(-1, min(1, ay))],
            "shoot": shoot,
            "kick": kick,
            "pickup": pickup,
            "drop": False,
            "throw": throw
        }
        self.command_seq += 1
        return cmd


# Импорт heapq для A*
import heapq