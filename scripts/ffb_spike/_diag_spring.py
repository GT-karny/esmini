"""Diagnose SPRING effect creation on G29. Try variants; report which shape works."""
from __future__ import annotations

import ctypes
import sdl2

def _err() -> str:
    return sdl2.SDL_GetError().decode(errors="ignore")

def _try(name: str, build) -> None:
    eff = build()
    eid = sdl2.SDL_HapticNewEffect(haptic, ctypes.byref(eff))
    if eid < 0:
        print(f"  [FAIL] {name}: {_err()}")
    else:
        print(f"  [ OK ] {name}: effect_id={eid}")
        sdl2.SDL_HapticDestroyEffect(haptic, eid)

sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK | sdl2.SDL_INIT_HAPTIC)
joystick = sdl2.SDL_JoystickOpen(0)
haptic = sdl2.SDL_HapticOpenFromJoystick(joystick)
print(f"HapticNumAxes = {sdl2.SDL_HapticNumAxes(haptic)}")
print(f"HAPTIC_INFINITY exposed: {'SDL_HAPTIC_INFINITY' in dir(sdl2)}")
try:
    print(f"HAPTIC_INFINITY value  : {sdl2.SDL_HAPTIC_INFINITY}")
except Exception as e:
    print(f"HAPTIC_INFINITY value  : (unavailable) {e!r}")

INF = 0xFFFFFFFF  # SDL_HAPTIC_INFINITY

def build_minimal():
    eff = sdl2.SDL_HapticEffect()
    eff.type = sdl2.SDL_HAPTIC_SPRING
    cond = eff.condition
    cond.length = INF
    cond.right_coeff[0] = 0x4000
    cond.left_coeff[0]  = 0x4000
    cond.right_sat[0]   = 0x7FFF
    cond.left_sat[0]    = 0x7FFF
    return eff

def build_with_dir():
    eff = build_minimal()
    eff.condition.direction.type = sdl2.SDL_HAPTIC_CARTESIAN
    eff.condition.direction.dir[0] = 1
    return eff

def build_all_axes():
    eff = build_minimal()
    for i in (1, 2):
        eff.condition.right_coeff[i] = 0x4000
        eff.condition.left_coeff[i]  = 0x4000
        eff.condition.right_sat[i]   = 0x7FFF
        eff.condition.left_sat[i]    = 0x7FFF
    return eff

def build_condtype_set():
    eff = build_minimal()
    eff.condition.type = sdl2.SDL_HAPTIC_SPRING
    return eff

def build_zero_coeff():
    eff = sdl2.SDL_HapticEffect()
    eff.type = sdl2.SDL_HAPTIC_SPRING
    cond = eff.condition
    cond.length = INF
    cond.right_coeff[0] = 0
    cond.left_coeff[0]  = 0
    cond.right_sat[0]   = 0x7FFF
    cond.left_sat[0]    = 0x7FFF
    return eff

_try("minimal (coeff=0x4000, [0] only)", build_minimal)
_try("with CARTESIAN direction",         build_with_dir)
_try("all 3 axes populated",             build_all_axes)
_try("condition.type set",               build_condtype_set)
_try("zero coeff (matches C++)",         build_zero_coeff)

sdl2.SDL_HapticClose(haptic)
sdl2.SDL_JoystickClose(joystick)
sdl2.SDL_Quit()
