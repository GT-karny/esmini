"""Shared G29/SDL2 rig helpers for the F7b FFB characterization scripts (07-10).

Extracted from spike scripts 01-06 so every measurement uses one safety model,
one sign convention and one axis-normalization.

SAFETY MODEL (every script depends on this)
-------------------------------------------
* The wheel physically moves. `Rig.set_force` is the ONLY force entry point and
  it hard-clamps to `force_cap` (default 0.6, the Day-1 spike's max_force).
* `Rig.guard()` returns False once the wheel has travelled further than
  `excursion_limit` from the trial's reference — every measurement loop must
  check it and zero the force.
* `Rig.close()` ramps force to zero. It is wired to SIGINT and to a `finally`.

SIGN CONVENTION (spike README §1f, re-verified by script 06)
------------------------------------------------------------
Positive CONSTANT level pushes the wheel LEFT = toward NEGATIVE axis.
Axis positive = wheel turned RIGHT. To servo toward a positive target the
commanded force must therefore be NEGATIVE: u = -(Kp*err + ...).
"""
from __future__ import annotations

import csv
import ctypes
import math
import signal
import sys
import time
from pathlib import Path

try:                       # PySDL2 lives in scripts/ffb_spike/.venv only.
    import sdl2            # The pure-model paths (Servo / WheelModel, used by
except ImportError:        # `09 --dry`) must stay importable from any venv, so
    sdl2 = None            # the failure is deferred to Rig.open().

AXIS_FULL = 32767
STEERING_AXIS = 0
LOOP_HZ = 250.0
DT = 1.0 / LOOP_HZ
INF = 0xFFFFFFFF

LOGS = Path(__file__).parent / "logs"
PROFILES = Path(__file__).parent / "profiles"
LOGS.mkdir(exist_ok=True)


def constant_effect(level_frac: float) -> sdl2.SDL_HapticEffect:
    eff = sdl2.SDL_HapticEffect()
    eff.type = sdl2.SDL_HAPTIC_CONSTANT
    c = eff.constant
    c.direction.type = sdl2.SDL_HAPTIC_CARTESIAN
    c.direction.dir[0] = 1
    c.length = INF
    c.level = int(max(-1.0, min(1.0, level_frac)) * 32767)
    return eff


class Rig:
    """Open the wheel once, command CONSTANT force, read the steering axis."""

    def __init__(self, gain: int = 100, force_cap: float = 0.6,
                 excursion_limit: float = 0.45):
        self.joystick = None
        self.haptic = None
        self.effect_id = -1
        self.gain = gain
        self.force_cap = force_cap
        self.excursion_limit = excursion_limit
        self._ref = 0.0
        self._closed = False

    # -- lifecycle ---------------------------------------------------------
    def open(self) -> "Rig":
        if sdl2 is None:
            raise RuntimeError("PySDL2 not available — run rig scripts with "
                               "scripts/ffb_spike/.venv/Scripts/python.exe")
        if sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK | sdl2.SDL_INIT_HAPTIC) != 0:
            raise RuntimeError("SDL_Init failed")
        self.joystick = sdl2.SDL_JoystickOpen(0)
        if not self.joystick:
            raise RuntimeError("SDL_JoystickOpen(0) failed — is the G29 connected?")
        self.haptic = sdl2.SDL_HapticOpenFromJoystick(self.joystick)
        if not self.haptic:
            raise RuntimeError("SDL_HapticOpenFromJoystick failed")
        sdl2.SDL_HapticSetGain(self.haptic, self.gain)
        eff = constant_effect(0.0)
        self.effect_id = sdl2.SDL_HapticNewEffect(self.haptic, ctypes.byref(eff))
        if self.effect_id < 0:
            raise RuntimeError(f"NewEffect(CONSTANT): {sdl2.SDL_GetError().decode(errors='ignore')}")
        sdl2.SDL_HapticRunEffect(self.haptic, self.effect_id, 1)
        signal.signal(signal.SIGINT, lambda *_: (self.close(), sys.exit(130)))
        return self

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            if self.haptic:
                for _ in range(10):          # ramp-to-zero, never a hard cut
                    self.set_force(0.0)
                    time.sleep(0.01)
                sdl2.SDL_HapticStopAll(self.haptic)
                if self.effect_id >= 0:
                    sdl2.SDL_HapticDestroyEffect(self.haptic, self.effect_id)
                sdl2.SDL_HapticClose(self.haptic)
            if self.joystick:
                sdl2.SDL_JoystickClose(self.joystick)
        finally:
            sdl2.SDL_Quit()

    def __enter__(self): return self.open()
    def __exit__(self, *exc): self.close()

    # -- io ----------------------------------------------------------------
    def set_force(self, u: float) -> float:
        u = max(-self.force_cap, min(self.force_cap, u))
        eff = constant_effect(u)
        sdl2.SDL_HapticUpdateEffect(self.haptic, self.effect_id, ctypes.byref(eff))
        return u

    def axis(self) -> float:
        sdl2.SDL_JoystickUpdate()
        return sdl2.SDL_JoystickGetAxis(self.joystick, STEERING_AXIS) / AXIS_FULL

    # -- helpers -----------------------------------------------------------
    def set_reference(self, ref: float) -> None:
        self._ref = ref

    def guard(self, a: float | None = None) -> bool:
        """True while the wheel is inside the safe excursion window."""
        if a is None:
            a = self.axis()
        return abs(a - self._ref) <= self.excursion_limit

    def settle(self, seconds: float = 1.0) -> float:
        """Zero force, wait, return the mean resting axis over the 2nd half."""
        self.set_force(0.0)
        vals = []
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < seconds:
            vals.append(self.axis())
            time.sleep(DT)
        tail = vals[len(vals) // 2:] or vals
        return sum(tail) / len(tail)

    def recenter(self, target: float = 0.0, tol: float = 0.010,
                 timeout: float = 3.0, kp: float = 6.0, kd: float = 0.30,
                 fstat: float = 0.15) -> float:
        """Drive the wheel back to `target`.

        Uses the Coulomb feed-forward (script 08/10 finding) because a plain P
        servo cannot close the last `f_hold/kp` of error against static
        friction — without it every recentre would just time out short.
        """
        prev_err, primed = 0.0, False
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < timeout:
            a = self.axis()
            err = target - a
            if abs(err) < tol:
                break
            derr = (err - prev_err) / DT if primed else 0.0
            prev_err, primed = err, True
            u = -(kp * err + kd * derr) - fstat * math.tanh(err / 0.01)
            self.set_force(u)
            time.sleep(DT)
        self.set_force(0.0)
        time.sleep(0.15)
        return self.axis()


class ServoParams:
    """Tunables for `Servo`. Defaults are the shipped C++ values (kp/kd/max_force
    from manual_drive.json `ffb.target_track_*`) plus the countermeasure knobs."""

    def __init__(self, **kw):
        self.kp = 4.0
        self.kd = 0.35
        self.max_force = 0.6
        self.fstat = 0.15          # Coulomb feed-forward magnitude
        self.ff_eps = 0.01         # error scale the feed-forward ramps in over
        self.ki = 30.0
        self.i_max = 0.25          # clamp on the integral CONTRIBUTION, not the state
        self.dither_amp = 0.12
        self.dither_hz = 30.0
        self.punch_force = 0.30
        self.punch_ms = 60.0
        self.punch_err = 0.008
        self.punch_stuck_s = 0.12
        for k, v in kw.items():
            if not hasattr(self, k):
                raise AttributeError(f"unknown servo param: {k}")
            setattr(self, k, v)

    @classmethod
    def from_args(cls, args) -> "ServoParams":
        keep = {k: getattr(args, k) for k in vars(cls()) if hasattr(args, k)}
        return cls(**keep)


class Servo:
    """Target-track position servo with a selectable stiction countermeasure.

    `method` is one of: none | ff | integrator | dither | punch | ff+i
    Sign convention per module docstring: u = -(Kp*err + Kd*derr).

    `step()` returns (u, u_feedback) — the second value EXCLUDES the stiction
    compensation. That separation matters for the product: the torque-proxy
    override signal must measure how hard the DRIVER is resisting, not how much
    known-friction compensation the servo is injecting.
    """

    def __init__(self, method: str, p: ServoParams):
        self.m = method
        self.p = p
        self.reset()

    def reset(self) -> None:
        self.prev_err = 0.0
        self.primed = False
        self.integ = 0.0
        self.stuck_since = None
        self.punch_until = 0.0
        self.last_axis = None
        self.punches = 0

    def step(self, target: float, actual: float, t: float, dt: float = DT):
        p = self.p
        err = target - actual
        derr = (err - self.prev_err) / dt if self.primed else 0.0
        self.prev_err, self.primed = err, True

        u_fb = -(p.kp * err + p.kd * derr)
        u = u_fb

        if self.m in ("ff", "ff+i"):
            u -= p.fstat * math.tanh(err / max(p.ff_eps, 1e-9))

        if self.m in ("integrator", "ff+i"):
            self.integ += err * dt
            if p.ki > 1e-9:   # anti-windup: bound the state so the clamp is exact
                lim = p.i_max / p.ki
                self.integ = max(-lim, min(lim, self.integ))
            u -= max(-p.i_max, min(p.i_max, p.ki * self.integ))

        if self.m == "dither":
            u -= p.dither_amp * math.sin(2.0 * math.pi * p.dither_hz * t)

        if self.m == "punch":
            moving = self.last_axis is not None and abs(actual - self.last_axis) > 2.0e-4
            if abs(err) > p.punch_err and not moving:
                if self.stuck_since is None:
                    self.stuck_since = t
                elif (t - self.stuck_since) > p.punch_stuck_s and t > self.punch_until:
                    self.punch_until = t + p.punch_ms / 1000.0
                    self.stuck_since = None
                    self.punches += 1
            else:
                self.stuck_since = None
            if t < self.punch_until:
                u = -math.copysign(p.punch_force, err)
            self.last_axis = actual

        clamp = lambda x: max(-p.max_force, min(p.max_force, x))
        return clamp(u), clamp(u_fb)


class WheelModel:
    """Coulomb + viscous wheel, fitted to the script 07 measurements.

    Used by `09 --dry` to screen many configurations without rig time. Fit:
      * breaks away at |f| > F_BREAK (07: 0.17 left / 0.20 right, no measurable
        dependence on initial angle)
      * stays moving down to |f| > F_KIN   (07 hysteresis: band ≈ 0.02)
      * speed once moving  v ≈ K_V * (|f| - F_KIN), saturating at V_MAX
        (07 force->velocity: linear to f≈0.40 with slope ≈3.35, then saturating)
      * force sign is inverted relative to axis (README §1f)
    """
    F_BREAK = 0.19
    F_KIN = 0.16
    K_V = 3.35
    V_MAX = 1.0
    TAU = 0.03

    def __init__(self, pos: float = 0.0):
        self.pos = pos
        self.vel = 0.0

    def step(self, u: float, dt: float) -> float:
        moving = abs(self.vel) > 1e-4
        thresh = self.F_KIN if moving else self.F_BREAK
        if abs(u) <= thresh:
            self.vel = 0.0 if not moving else self.vel * max(0.0, 1.0 - dt / self.TAU)
        else:
            v_t = -math.copysign(min(self.V_MAX, self.K_V * (abs(u) - self.F_KIN)), u)
            self.vel += (v_t - self.vel) * min(1.0, dt / self.TAU)
        self.pos = max(-1.0, min(1.0, self.pos + self.vel * dt))
        return self.pos


def banner(title: str, detail: str, yes: bool, countdown: int = 3) -> None:
    print("=" * 68)
    print(f"F7b G29 characterization — {title}")
    print("=" * 68)
    print("!! The wheel WILL move. Keep hands and objects clear.")
    print(f"   {detail}")
    if not yes:
        for k in range(countdown, 0, -1):
            print(f"   starting in {k} s...", end="\r")
            time.sleep(1)
        print()


def write_csv(path: Path, header: list[str], rows) -> Path:
    with path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        w.writerows(rows)
    return path


def pstdev(xs) -> float:
    xs = list(xs)
    if len(xs) < 2:
        return 0.0
    m = sum(xs) / len(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))
