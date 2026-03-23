from __future__ import annotations

import math
import sys
from dataclasses import dataclass, field
from typing import Tuple, List


@dataclass
class AdvancedBot:
    """Убийца — адаптивная стратегия, точный прицел, умный путефайндинг"""

    command_seq: int = 0
    player_id:   int = 0
    enemy_id:    int = 0
    tick_rate:   int = 30

    level_width:     float = 1000
    level_height:    float = 1000
    floor_grid_size: float = 64.0
    floor_cells: set  = field(default_factory=set)
    obstacles:   list = field(default_factory=list)

    # Предсказание врага (скользящее среднее скорости)
    last_enemy_x:  float = 0
    last_enemy_y:  float = 0
    last_enemy_vx: float = 0
    last_enemy_vy: float = 0

    # Стрейф
    strafe_dir:    float = 1.0
    strafe_timer:  int   = 0
    last_strategy: str   = ""

    # Застревание
    stuck_timer: int   = 0
    last_mx:     float = 0
    last_my:     float = 0

    # Константы
    PLAYER_RADIUS:    float = 10.0
    KICK_DISTANCE:    float = 28.0
    KICK_ANGLE_DOT:   float = 0.40   # чуть мягче — кикаем чаще
    REVOLVER_RANGE:   float = 240.0
    UZI_RANGE:        float = 180.0
    PICKUP_RANGE:     float = 30.0   # чуть больше — раньше подбираем
    WALL_AVOID_DIST:  float = 35.0
    BULLET_DANGER_DIST: float = 90.0  # реагируем раньше

    def log(self, msg: str):
        print(f"[BOT] {msg}", file=sys.stderr, flush=True)

    # ═══════════════════════════════ HELPERS ═════════════════════════════════

    def _get_pos(self, obj: dict) -> Tuple[float, float]:
        if not obj:
            return 0.0, 0.0
        pos = obj.get("position") or obj.get("center")
        if not pos:
            return 0.0, 0.0
        if isinstance(pos, dict):
            return float(pos.get("x", 0)), float(pos.get("y", 0))
        if isinstance(pos, (list, tuple)) and len(pos) >= 2:
            return float(pos[0]), float(pos[1])
        return 0.0, 0.0

    def _get_facing(self, obj: dict) -> Tuple[float, float]:
        f = obj.get("facing", [1, 0])
        if isinstance(f, dict):
            return float(f.get("x", 1)), float(f.get("y", 0))
        if isinstance(f, (list, tuple)) and len(f) >= 2:
            return float(f[0]), float(f[1])
        return 1.0, 0.0

    def _norm(self, x: float, y: float) -> Tuple[float, float]:
        l = math.hypot(x, y)
        return (x / l, y / l) if l > 1e-9 else (0.0, 0.0)

    def _dot(self, ax, ay, bx, by) -> float:
        return ax * bx + ay * by

    def _is_grounded(self, x: float, y: float) -> bool:
        if not self.floor_cells:
            return True
        return (int(x // self.floor_grid_size), int(y // self.floor_grid_size)) in self.floor_cells

    def _ob_unpack(self, ob: dict):
        center = ob.get("center") or ob.get("position") or [0, 0]
        ox = float(center.get("x", 0) if isinstance(center, dict) else center[0])
        oy = float(center.get("y", 0) if isinstance(center, dict) else center[1])
        hs = ob.get("half_size") or [0, 0]
        hx = float(hs.get("x", 0) if isinstance(hs, dict) else hs[0])
        hy = float(hs.get("y", 0) if isinstance(hs, dict) else hs[1])
        return ox, oy, hx, hy

    def _circle_rect(self, cx, cy, r, ox, oy, hx, hy) -> bool:
        nx = max(ox - hx, min(cx, ox + hx))
        ny = max(oy - hy, min(cy, oy + hy))
        return (cx - nx) ** 2 + (cy - ny) ** 2 <= r * r

    def _solid_obs(self, obstacles) -> list:
        out = []
        for ob in obstacles:
            if not ob.get("solid", True):
                continue
            kind = str(ob.get("kind", "")).lower()
            if kind in ("door", "trigger", "pickup"):
                continue
            out.append(ob)
        return out

    def _wall_avoidance(self, x, y, obstacles) -> Tuple[float, float]:
        ax, ay = 0.0, 0.0
        for ob in self._solid_obs(obstacles):
            ox, oy, hx, hy = self._ob_unpack(ob)
            cx = max(ox - hx, min(x, ox + hx))
            cy = max(oy - hy, min(y, oy + hy))
            dx, dy = x - cx, y - cy
            d = math.hypot(dx, dy)
            if 0.01 < d < self.WALL_AVOID_DIST:
                f = (self.WALL_AVOID_DIST - d) / self.WALL_AVOID_DIST
                ax += dx / d * f * 2.0
                ay += dy / d * f * 2.0
        # Отталкивание от границ карты
        margin = self.WALL_AVOID_DIST
        if x < margin:       ax += (margin - x) / margin * 2
        if x > self.level_width  - margin: ax -= (margin - (self.level_width  - x)) / margin * 2
        if y < margin:       ay += (margin - y) / margin * 2
        if y > self.level_height - margin: ay -= (margin - (self.level_height - y)) / margin * 2
        return ax, ay

    def _can_move(self, x, y, obstacles) -> bool:
        if not (self.PLAYER_RADIUS <= x <= self.level_width - self.PLAYER_RADIUS):
            return False
        if not (self.PLAYER_RADIUS <= y <= self.level_height - self.PLAYER_RADIUS):
            return False
        # Пол проверяем мягко — если floor_cells пустой, считаем везде можно
        if self.floor_cells and not self._is_grounded(x, y):
            return False
        for ob in self._solid_obs(obstacles):
            ox, oy, hx, hy = self._ob_unpack(ob)
            if self._circle_rect(x, y, self.PLAYER_RADIUS + 3, ox, oy, hx, hy):
                return False
        return True

    def _path_dir(self, fx, fy, tx, ty, obstacles) -> Tuple[float, float]:
        """Поиск направления к цели с обходом препятствий. Lookahead 40px."""
        dx, dy = self._norm(tx - fx, ty - fy)
        # Прямой путь
        if self._can_move(fx + dx * 40, fy + dy * 40, obstacles):
            return dx, dy
        # Пробуем углы с разным lookahead
        best, best_score = (0.0, 0.0), float("inf")
        for ang in [30, -30, 60, -60, 90, -90, 120, -120, 150, -150]:
            r = math.radians(ang)
            nx = dx * math.cos(r) - dy * math.sin(r)
            ny = dx * math.sin(r) + dy * math.cos(r)
            for step in (40, 25):
                if self._can_move(fx + nx * step, fy + ny * step, obstacles):
                    # Оцениваем: насколько этот вектор ведёт к цели
                    score = -self._dot(nx, ny, dx, dy)  # меньше = лучше (ближе к прямому)
                    dist_to_target = math.hypot(tx - (fx + nx * step), ty - (fy + ny * step))
                    score += dist_to_target * 0.001
                    if score < best_score:
                        best_score, best = score, (nx, ny)
                    break
        return best

    def _bullet_dodge(self, mx, my, projectiles) -> Tuple[float, float]:
        """Уклонение от пуль: перпендикулярно траектории + упреждение."""
        ddx, ddy = 0.0, 0.0
        for proj in projectiles:
            px, py = self._get_pos(proj)

            vel = proj.get("velocity") or proj.get("vel")
            pvx, pvy = 0.0, 0.0
            if vel:
                pvx = float(vel.get("x", 0) if isinstance(vel, dict) else vel[0])
                pvy = float(vel.get("y", 0) if isinstance(vel, dict) else vel[1])

            # Предсказываем позицию пули через несколько тиков
            future_ticks = 4
            fpx = px + pvx * future_ticks
            fpy = py + pvy * future_ticks

            # Расстояние до предсказанной позиции
            tmx, tmy = mx - fpx, my - fpy
            d = math.hypot(tmx, tmy)

            # Также смотрим на текущую позицию
            tmx0, tmy0 = mx - px, my - py
            d0 = math.hypot(tmx0, tmy0)

            danger_d = max(d, 1.0)
            if d > self.BULLET_DANGER_DIST and d0 > self.BULLET_DANGER_DIST:
                continue

            spd = math.hypot(pvx, pvy)
            if spd > 0.1:
                bx, by = pvx / spd, pvy / spd
                # Летит ли пуля в нашу сторону?
                approach = bx * (-tmx0 / (d0 + 0.01)) + by * (-tmy0 / (d0 + 0.01))
                if approach < 0.1:
                    continue  # пуля уходит
                # Уклоняемся строго перпендикулярно
                perp_x, perp_y = -by, bx
                if self._dot(perp_x, perp_y, tmx0, tmy0) < 0:
                    perp_x, perp_y = -perp_x, -perp_y
                eff_d = min(d, d0)
                f = max(0, (self.BULLET_DANGER_DIST - eff_d) / self.BULLET_DANGER_DIST)
                ddx += perp_x * f * 2.5
                ddy += perp_y * f * 2.5
            else:
                if d0 > 0.01:
                    f = max(0, (self.BULLET_DANGER_DIST - d0) / self.BULLET_DANGER_DIST)
                    ddx += tmx0 / d0 * f * 1.5
                    ddy += tmy0 / d0 * f * 1.5
        return ddx, ddy

    def _update_enemy(self, ex, ey):
        vx = ex - self.last_enemy_x
        vy = ey - self.last_enemy_y
        self.last_enemy_vx = vx * 0.4 + self.last_enemy_vx * 0.6
        self.last_enemy_vy = vy * 0.4 + self.last_enemy_vy * 0.6
        self.last_enemy_x, self.last_enemy_y = ex, ey

    def _aim_lead(self, mx, my, ex, ey, bullet_speed: float) -> Tuple[float, float]:
        """Упреждающий прицел с учётом скорости снаряда."""
        d = math.hypot(ex - mx, ey - my)
        t = (d / bullet_speed) * self.tick_rate
        # Итеративное уточнение (2 итерации)
        for _ in range(2):
            px = ex + self.last_enemy_vx * t
            py = ey + self.last_enemy_vy * t
            d2 = math.hypot(px - mx, py - my)
            t = (d2 / bullet_speed) * self.tick_rate
        ax, ay = self._norm(ex + self.last_enemy_vx * t - mx,
                             ey + self.last_enemy_vy * t - my)
        if ax == 0 and ay == 0:
            ax, ay = self._norm(ex - mx, ey - my)
        return ax, ay

    def _weapon_type(self, obj: dict) -> str:
        w = obj.get("weapon")
        if not w:
            return "none"
        t = str(w.get("type", "none")).lower()
        return "none" if t == "none" else t

    def _strafe_vec(self, dx, dy, interval: int) -> Tuple[float, float]:
        self.strafe_timer += 1
        if self.strafe_timer >= interval:
            self.strafe_dir *= -1
            self.strafe_timer = 0
        angle = math.atan2(dy, dx) + math.radians(90 * self.strafe_dir)
        return math.cos(angle), math.sin(angle)

    def _is_weapon_kind(self, kind: str) -> bool:
        return any(w in kind for w in (
            "revolver", "uzi", "gun", "weapon", "rifle", "shotgun",
            "pistol", "smg", "machinegun", "sniper", "cannon", "blaster"
        ))

    def _best_pickup(self, mx, my, pickups, want_weapon=False, want_health=False):
        best_score = float("inf")
        bx, by, bd = 0.0, 0.0, 9999.0
        for p in pickups:
            if p.get("cooldown", 0) > 0:
                continue
            px, py = self._get_pos(p)
            d = math.hypot(px - mx, py - my)
            kind = str(p.get("kind", p.get("type", ""))).lower()
            is_wpn = self._is_weapon_kind(kind)
            is_hp  = "health" in kind or "medkit" in kind or "heal" in kind
            if want_weapon and not is_wpn:
                continue
            if want_health and not is_hp:
                continue
            priority = 0 if is_wpn else (50 if is_hp else 150)
            score = d + priority
            if score < best_score:
                best_score, bx, by, bd = score, px, py, d
        return bx, by, bd

    def _any_weapon_nearby(self, mx, my, pickups, radius=40) -> bool:
        """Есть ли оружие прямо под ногами?"""
        for p in pickups:
            if p.get("cooldown", 0) > 0:
                continue
            px, py = self._get_pos(p)
            kind = str(p.get("kind", p.get("type", ""))).lower()
            if self._is_weapon_kind(kind) and math.hypot(px - mx, py - my) <= radius:
                return True
        return False

    # ═══════════════════════════════ LIFECYCLE ════════════════════════════════

    def on_hello(self, msg: dict):
        self.player_id = msg.get("player_id", 0)
        self.tick_rate = msg.get("tick_rate", 30)
        self.log(f"Connected! Player {self.player_id}")

    def on_round_start(self, msg: dict):
        level = msg.get("level", {})
        self.level_width  = level.get("width", 1000)
        self.level_height = level.get("height", 1000)
        self.enemy_id     = msg.get("enemy_id", 0)
        self.command_seq  = 0
        self.obstacles    = level.get("static_obstacles", [])
        floor = level.get("floor", {})
        self.floor_grid_size = floor.get("grid_size", 64)
        self.floor_cells.clear()
        for cell in floor.get("cells", []):
            if len(cell) >= 2:
                self.floor_cells.add((int(cell[0]), int(cell[1])))
        self.last_enemy_x = self.last_enemy_y = 0
        self.last_enemy_vx = self.last_enemy_vy = 0
        self.strafe_timer  = 0
        self.strafe_dir    = 1.0
        self.last_strategy = ""
        self.stuck_timer   = 0
        self.last_mx = self.last_my = 0
        self.log(f"Round start! {self.level_width}x{self.level_height}, obs={len(self.obstacles)}, cells={len(self.floor_cells)}")

    def on_round_end(self, msg: dict):
        result = msg.get("result", {})
        self.log(f"Round end: {result.get('reason')} winner={result.get('winner_slot')}")

    # ═══════════════════════════════ MAIN TICK ════════════════════════════════

    def on_tick(self, msg: dict) -> dict:
        tick     = msg.get("tick", 0)
        you      = msg.get("you", {})
        enemy    = msg.get("enemy", {})
        snapshot = msg.get("snapshot", {})

        mx, my   = self._get_pos(you)
        ex, ey   = self._get_pos(enemy)
        e_alive  = enemy.get("alive", False)
        my_alive = you.get("alive", True)

        my_wtype   = self._weapon_type(you)
        has_weapon = my_wtype != "none"
        e_wtype    = self._weapon_type(enemy)
        e_has_gun  = e_wtype != "none"

        shoot_cd = you.get("shoot_cooldown", 0)
        kick_cd  = you.get("kick_cooldown", 0)
        stun     = you.get("stun_remaining", 0)
        my_hp    = you.get("health", 100)

        obstacles   = snapshot.get("obstacles", self.obstacles)
        pickups     = snapshot.get("pickups", [])
        projectiles = snapshot.get("projectiles", [])

        dx   = ex - mx
        dy   = ey - my
        dist = math.hypot(dx, dy) if e_alive else 9999

        if not my_alive or stun > 0:
            return self._cmd()

        if e_alive:
            self._update_enemy(ex, ey)

        # ── Детектор застревания ───────────────────────────────────────────
        moved = math.hypot(mx - self.last_mx, my - self.last_my)
        if moved < 1.5:
            self.stuck_timer += 1
        else:
            self.stuck_timer = max(0, self.stuck_timer - 2)
        self.last_mx, self.last_my = mx, my
        is_stuck = self.stuck_timer > 15

        # ══════════════════════════════════════════════════════════════════════
        # СТРАТЕГИЯ
        # ══════════════════════════════════════════════════════════════════════
        if has_weapon and not e_has_gun:
            strategy = "HUNT"
        elif has_weapon and e_has_gun:
            strategy = "DUEL"
        elif not has_weapon and e_has_gun:
            strategy = "EVADE"
        else:
            strategy = "KICK"

        if strategy != self.last_strategy:
            self.strafe_timer = 0
            self.last_strategy = strategy

        # ── 1. ЦЕЛЬ ────────────────────────────────────────────────────────
        target_x, target_y = ex, ey
        target_type = "enemy"
        need_pickup = False

        # Подбираем оружие с умом
        if not has_weapon:
            bx, by, bd = self._best_pickup(mx, my, pickups, want_weapon=True)
            if bd < 9999:
                # Всегда берём если близко; берём если враг вооружён; берём если не далеко
                worth_it = bd < 180 or e_has_gun or bd < dist * 0.7
                if worth_it:
                    target_x, target_y = bx, by
                    target_type = "pickup"
                    need_pickup = True
                    if bd <= self.PICKUP_RANGE:
                        return self._cmd(0, 0, 1, 0, False, False, True)

        # Аптечка при низком HP
        if my_hp <= 30 and strategy not in ("KICK",):
            hx2, hy2, hd = self._best_pickup(mx, my, pickups, want_health=True)
            if hd < 250:
                target_x, target_y = hx2, hy2
                target_type = "pickup"
                need_pickup = True
                if hd <= self.PICKUP_RANGE:
                    return self._cmd(0, 0, 1, 0, False, False, True)

        # ── 2. ДВИЖЕНИЕ ────────────────────────────────────────────────────
        move_x, move_y = 0.0, 0.0
        to_ex, to_ey   = self._norm(dx, dy)

        if is_stuck:
            # Застряли — резкий рандомный манёвр
            escape_angle = math.pi * (0.5 + self.strafe_dir * 0.5)
            move_x = math.cos(escape_angle + tick * 0.3)
            move_y = math.sin(escape_angle + tick * 0.3)
            self.stuck_timer = 0
        elif target_type == "enemy" and e_alive:
            sx, sy = self._strafe_vec(dx, dy, interval=22)

            if strategy == "HUNT":
                # Преследуем — 80% вперёд + 20% зигзаг
                fwd_x = to_ex if dist > 50 else 0.0
                fwd_y = to_ey if dist > 50 else 0.0
                move_x = fwd_x * 0.8 + sx * 0.2
                move_y = fwd_y * 0.8 + sy * 0.2

            elif strategy == "DUEL":
                # Дуэль: держим дистанцию + зигзаг
                if my_wtype == "revolver":
                    ideal_min, ideal_max = 120, 200
                elif my_wtype == "uzi":
                    ideal_min, ideal_max = 80, 150
                else:
                    ideal_min, ideal_max = 90, 170

                if dist > ideal_max:
                    dist_x, dist_y = to_ex, to_ey
                elif dist < ideal_min:
                    # Отступаем — но сначала пробуем вбок, не прямо назад (не в стену)
                    back_x, back_y = -to_ex, -to_ey
                    # Проверяем что назад можно идти
                    if not self._can_move(mx + back_x * 30, my + back_y * 30, obstacles):
                        # Стена сзади — идём вбок
                        dist_x, dist_y = sx, sy
                    else:
                        dist_x, dist_y = back_x, back_y
                else:
                    dist_x, dist_y = 0.0, 0.0

                # 50/50: дистанция + зигзаг
                move_x = dist_x * 0.5 + sx * 0.5
                move_y = dist_y * 0.5 + sy * 0.5

            elif strategy == "EVADE":
                # Убегаем от врага зигзагом
                if dist < 200:
                    move_x = -to_ex * 0.6 + sx * 0.4
                    move_y = -to_ey * 0.6 + sy * 0.4
                else:
                    move_x = sx
                    move_y = sy

            elif strategy == "KICK":
                # Rush в кик — 85% вперёд + 15% зигзаг
                move_x = to_ex * 0.85 + sx * 0.15
                move_y = to_ey * 0.85 + sy * 0.15

        elif target_type == "pickup":
            pd = math.hypot(target_x - mx, target_y - my)
            px_dir, py_dir = self._path_dir(mx, my, target_x, target_y, obstacles)
            if strategy == "EVADE" and e_alive and dist < 160:
                # К пикапу но уклоняясь от вооружённого врага
                move_x = px_dir * 0.55 - to_ex * 0.7
                move_y = py_dir * 0.55 - to_ey * 0.7
            else:
                move_x = px_dir
                move_y = py_dir

        # ── 3. УКЛОНЕНИЕ ОТ ПУЛЬ ──────────────────────────────────────────
        ddx, ddy = self._bullet_dodge(mx, my, projectiles)
        dw = 2.2 if strategy in ("DUEL", "EVADE") else 1.5
        move_x += ddx * dw
        move_y += ddy * dw

        # ── 4. СТЕНЫ ───────────────────────────────────────────────────────
        avx, avy = self._wall_avoidance(mx, my, obstacles)
        move_x += avx
        move_y += avy

        # ── 5. ОБХОД ПРЕПЯТСТВИЙ ──────────────────────────────────────────
        if not is_stuck and (move_x != 0 or move_y != 0):
            nmx, nmy = self._norm(move_x, move_y)
            if not self._can_move(mx + nmx * 30, my + nmy * 30, obstacles):
                bpx, bpy = self._path_dir(mx, my, target_x, target_y, obstacles)
                if bpx != 0 or bpy != 0:
                    move_x = bpx + ddx * 0.3 + avx
                    move_y = bpy + ddy * 0.3 + avy

        # ── 6. НОРМАЛИЗАЦИЯ ────────────────────────────────────────────────
        ml = math.hypot(move_x, move_y)
        if ml > 0.01:
            move_x /= ml
            move_y /= ml

        # ── 7. ПРИЦЕЛ С УПРЕЖДЕНИЕМ ───────────────────────────────────────
        aim_x, aim_y = 1.0, 0.0
        if e_alive:
            spd = 420.0 if my_wtype == "revolver" else 310.0
            aim_x, aim_y = self._aim_lead(mx, my, ex, ey, spd)

        # ── 8. СТРЕЛЬБА ────────────────────────────────────────────────────
        shoot = False
        if has_weapon and shoot_cd <= 0 and e_alive:
            to_ex2, to_ey2 = self._norm(dx, dy)
            dot = self._dot(aim_x, aim_y, to_ex2, to_ey2)
            if my_wtype == "revolver":
                shoot = dist <= self.REVOLVER_RANGE and dot > 0.86
            elif my_wtype == "uzi":
                # Uzi — поливаем при любом прицеле в зоне
                shoot = dist <= self.UZI_RANGE and dot > 0.60
            else:
                shoot = dot > 0.72

        # ── 9. КИК ────────────────────────────────────────────────────────
        # Кикаем всегда когда возможно — даже при наличии оружия (DPS+)
        kick = False
        if e_alive and dist <= self.KICK_DISTANCE and kick_cd <= 0:
            fx, fy = self._get_facing(you)
            tx, ty = self._norm(dx, dy)
            if self._dot(fx, fy, tx, ty) >= self.KICK_ANGLE_DOT:
                kick = True

        # ── 10. ПОДБОР ─────────────────────────────────────────────────────
        # Подбираем если: идём к пикапу И он рядом
        # ИЛИ наступили на оружие (даже если уже есть — не теряем)
        pickup = False
        if need_pickup and target_type == "pickup":
            pickup = True
        elif self._any_weapon_nearby(mx, my, pickups, radius=self.PICKUP_RANGE):
            # Оружие прямо под ногами — подбираем всегда
            pickup = True

        # ── 11. ЛОГ ────────────────────────────────────────────────────────
        if tick % 30 == 0:
            self.log(f"T{tick} {strategy} d={dist:.0f} me={my_wtype}({my_hp}hp) en={e_wtype} "
                     f"tgt={target_type} stuck={self.stuck_timer} projs={len(projectiles)}")

        return self._cmd(move_x, move_y, aim_x, aim_y, shoot, kick, pickup)

    def _cmd(self, mx=0, my=0, ax=1, ay=0, shoot=False, kick=False, pickup=False):
        self.command_seq += 1
        return {
            "type":    "command",
            "seq":     self.command_seq - 1,
            "move":    [max(-1.0, min(1.0, float(mx))), max(-1.0, min(1.0, float(my)))],
            "aim":     [max(-1.0, min(1.0, float(ax))), max(-1.0, min(1.0, float(ay)))],
            "shoot":   bool(shoot),
            "kick":    bool(kick),
            "pickup":  bool(pickup),
            "drop":    False,
            "throw":   False,
        }
