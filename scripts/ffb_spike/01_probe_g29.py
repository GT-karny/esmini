"""F7b Day-1 spike: 01 — Probe G29 via SDL2 and dump haptic capabilities.

Purpose
-------
Answer question 3 preconditions: which SDL_Haptic effects does the connected
wheel actually expose? SPRING with a movable center is the working hypothesis
for target-angle tracking; if SPRING is unsupported we fall back to a
constant-force servo loop (matches the existing SDLFFBSink emulation path).

No FFB is emitted here. Read-only probe.
"""
from __future__ import annotations

import ctypes
import json
import sys
import time
from pathlib import Path

import sdl2
import sdl2.ext

OUT_DIR = Path(__file__).with_suffix("").parent / "logs"
OUT_DIR.mkdir(exist_ok=True)


def _query_caps(haptic) -> dict:
    caps = sdl2.SDL_HapticQuery(haptic)
    return {
        "constant": bool(caps & sdl2.SDL_HAPTIC_CONSTANT),
        "sine":     bool(caps & sdl2.SDL_HAPTIC_SINE),
        "triangle": bool(caps & sdl2.SDL_HAPTIC_TRIANGLE),
        "sawtoothup":   bool(caps & sdl2.SDL_HAPTIC_SAWTOOTHUP),
        "sawtoothdown": bool(caps & sdl2.SDL_HAPTIC_SAWTOOTHDOWN),
        "ramp":     bool(caps & sdl2.SDL_HAPTIC_RAMP),
        "spring":   bool(caps & sdl2.SDL_HAPTIC_SPRING),
        "damper":   bool(caps & sdl2.SDL_HAPTIC_DAMPER),
        "inertia":  bool(caps & sdl2.SDL_HAPTIC_INERTIA),
        "friction": bool(caps & sdl2.SDL_HAPTIC_FRICTION),
        "custom":   bool(caps & sdl2.SDL_HAPTIC_CUSTOM),
        "gain":     bool(caps & sdl2.SDL_HAPTIC_GAIN),
        "autocenter": bool(caps & sdl2.SDL_HAPTIC_AUTOCENTER),
        "status":   bool(caps & sdl2.SDL_HAPTIC_STATUS),
        "pause":    bool(caps & sdl2.SDL_HAPTIC_PAUSE),
        "raw_hex":  hex(caps),
    }


def main() -> int:
    if sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK | sdl2.SDL_INIT_HAPTIC) != 0:
        print(f"[FAIL] SDL_Init: {sdl2.SDL_GetError().decode(errors='ignore')}")
        return 2

    try:
        n_joy = sdl2.SDL_NumJoysticks()
        print(f"[INFO] Detected {n_joy} joystick(s)")
        report = {"joysticks": [], "haptic_num": sdl2.SDL_NumHaptics()}

        # Also enumerate raw haptic devices (Wheels sometimes show up as haptic
        # without a joystick face; on Windows, G29 is usually joystick+haptic).
        n_hap = report["haptic_num"]
        print(f"[INFO] Detected {n_hap} standalone haptic device(s)")

        for i in range(n_joy):
            name = sdl2.SDL_JoystickNameForIndex(i)
            entry: dict = {
                "index": i,
                "name": name.decode(errors="ignore") if name else None,
                "is_haptic": None,
                "num_axes": None,
                "num_buttons": None,
                "num_hats": None,
                "haptic_caps": None,
                "haptic_num_effects": None,
                "haptic_num_playing": None,
                "haptic_max_effects": None,
            }

            joy = sdl2.SDL_JoystickOpen(i)
            if not joy:
                entry["error"] = f"open failed: {sdl2.SDL_GetError().decode(errors='ignore')}"
                report["joysticks"].append(entry)
                continue

            try:
                entry["num_axes"] = sdl2.SDL_JoystickNumAxes(joy)
                entry["num_buttons"] = sdl2.SDL_JoystickNumButtons(joy)
                entry["num_hats"] = sdl2.SDL_JoystickNumHats(joy)
                entry["is_haptic"] = bool(sdl2.SDL_JoystickIsHaptic(joy))

                if entry["is_haptic"]:
                    haptic = sdl2.SDL_HapticOpenFromJoystick(joy)
                    if haptic:
                        try:
                            entry["haptic_caps"] = _query_caps(haptic)
                            entry["haptic_num_effects"] = sdl2.SDL_HapticNumEffects(haptic)
                            entry["haptic_num_playing"] = sdl2.SDL_HapticNumEffectsPlaying(haptic)
                            entry["haptic_max_effects"] = sdl2.SDL_HapticNumEffects(haptic)
                        finally:
                            sdl2.SDL_HapticClose(haptic)
                    else:
                        entry["haptic_error"] = sdl2.SDL_GetError().decode(errors="ignore")
            finally:
                sdl2.SDL_JoystickClose(joy)

            report["joysticks"].append(entry)
            print(f"[INFO] joystick[{i}]: {entry['name']} axes={entry['num_axes']} "
                  f"btns={entry['num_buttons']} haptic={entry['is_haptic']}")
            if entry["haptic_caps"]:
                caps = entry["haptic_caps"]
                print(f"       caps: constant={caps['constant']} spring={caps['spring']} "
                      f"damper={caps['damper']} friction={caps['friction']} "
                      f"gain={caps['gain']} autocenter={caps['autocenter']} "
                      f"num_effects={entry['haptic_num_effects']}")

        out_path = OUT_DIR / "01_probe_g29.json"
        out_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"[OK] wrote {out_path}")
        return 0

    finally:
        sdl2.SDL_Quit()


if __name__ == "__main__":
    sys.exit(main())
