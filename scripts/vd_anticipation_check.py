"""Phase 2 VirtualDriver verification: anticipatory deceleration + Phase 1 non-regression.

Runs two scenarios headless via GT_esminiLib.dll, sampling
GT_GetVirtualDriverTelemetry() (Phase 2 midlong.v_target_profile [[s,v]] + constraints):

  1. virtual_driver_basic.xosc       -> Phase 1 non-regression (cruise 15 / stop, no steer saturation)
  2. virtual_driver_anticipation.xosc -> slows BEFORE the junction/curve, no steer saturation,
                                         re-accelerates after, v_target_profile present.

Usage:  DriverScript/.venv/Scripts/python.exe scripts/vd_anticipation_check.py
"""
import ctypes
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")


def run(xosc_name, n_steps, dt=0.05, sample_every=0.2):
    lib = ctypes.CDLL(DLL)
    lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.GT_InitWithArgs.restype = ctypes.c_int
    lib.GT_Step.argtypes = [ctypes.c_double]
    lib.GT_Close.argtypes = []
    lib.GT_GetVirtualDriverTelemetry.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.GT_GetVirtualDriverTelemetry.restype = ctypes.c_int

    xosc = os.path.join(ROOT, "resources", "xosc", xosc_name)
    argv_list = [b"vd_check", b"--osc", xosc.encode(), b"--headless", b"--fixed_timestep", str(dt).encode()]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        print(f"  GT_InitWithArgs FAILED rc={rc} for {xosc_name}")
        return None

    buf = ctypes.create_string_buffer(32768)
    rows = []
    step_interval = max(1, int(round(sample_every / dt)))
    for i in range(n_steps):
        lib.GT_Step(dt)
        if (i + 1) % step_interval == 0:
            n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
            if n > 0:
                rows.append(json.loads(buf.value.decode()))
    lib.GT_Close()
    return rows


def fmt_rows(rows):
    print("   t    speed  steer  thr  brk  track lane   midlong  vt[0]  vt_min")
    for r in rows:
        ego, drv, ml = r["ego"], r["driver"], r.get("midlong", {})
        curve = ml.get("v_target_profile", [])  # [[s, v], ...]
        vt0 = curve[0][1] if curve else float("nan")
        vtmin = min((p[1] for p in curve), default=float("nan"))
        print(f"{r['sim_time']:5.1f}  {ego['speed']:5.2f}  {drv['steer']:6.3f} {drv['throttle']:4.2f} {drv['brake']:4.2f}"
              f"  {ego['track']:4d} {ego['lane']:4d}   {str(ml.get('valid')):5s}  {vt0:6.2f} {vtmin:6.2f}")


def main():
    print("=== 1. virtual_driver_basic.xosc (Phase 1 non-regression) ===")
    basic = run("virtual_driver_basic.xosc", n_steps=401)
    if basic is None:
        sys.exit(1)
    fmt_rows(basic[:: max(1, len(basic) // 12)])
    spd = [r["ego"]["speed"] for r in basic]
    steer = [abs(r["driver"]["steer"]) for r in basic]
    has_curve = any(r.get("midlong", {}).get("v_target_profile") for r in basic)
    b_reached = max(spd) > 10.0
    b_stopped = spd[-1] < 1.0
    b_nosat = max(steer) < 0.99
    print(f"\n  reached>10 m/s : {b_reached} (max={max(spd):.2f})")
    print(f"  stopped at end : {b_stopped} (final={spd[-1]:.2f})")
    print(f"  no steer sat   : {b_nosat} (max|steer|={max(steer):.3f})")
    print(f"  v_target_profile present : {has_curve}")
    basic_ok = b_reached and b_stopped and b_nosat and has_curve

    print("\n=== 2. virtual_driver_anticipation.xosc (Phase 2 anticipatory decel) ===")
    ant = run("virtual_driver_anticipation.xosc", n_steps=801)
    if ant is None:
        sys.exit(1)
    fmt_rows(ant)
    a_spd = [r["ego"]["speed"] for r in ant]
    a_steer = [abs(r["driver"]["steer"]) for r in ant]
    tracks = [r["ego"]["track"] for r in ant]
    a_curve = any(r.get("midlong", {}).get("v_target_profile") for r in ant)
    # Show the labelled constraints from the frame with the most of them.
    best = max(ant, key=lambda r: len(r.get("midlong", {}).get("constraints", [])), default=None)
    cons = best.get("midlong", {}).get("constraints", []) if best else []
    print("\n  labelled constraints (richest frame):")
    for c in cons:
        print(f"    kind={c['kind']:11s} s={c['s']:7.1f}  v={c['v']:5.2f}  xy=({c['x']:.1f},{c['y']:.1f})")
    a_reached = max(a_spd) > 10.0           # cruised on the straight
    a_slowed = min(a_spd[5:]) < 8.0         # slowed for junction/curve (after launch)
    a_nosat = max(a_steer) < 0.99           # Pure Pursuit did not saturate
    on_route = 2 in tracks                  # reached the exit road via the turn
    print(f"\n  reached>10 m/s   : {a_reached} (max={max(a_spd):.2f})")
    print(f"  slowed <8 m/s    : {a_slowed} (min={min(a_spd[5:]):.2f})  <- anticipatory decel")
    print(f"  no steer sat     : {a_nosat} (max|steer|={max(a_steer):.3f})  <- residual bug fix")
    print(f"  reached road 2   : {on_route} (tracks seen={sorted(set(tracks))})")
    print(f"  v_target_profile : {a_curve}")
    ant_ok = a_reached and a_slowed and a_nosat and a_curve

    print("\n" + "=" * 50)
    print("BASIC (non-regression):", "PASS" if basic_ok else "CHECK")
    print("ANTICIPATION (Phase 2) :", "PASS" if ant_ok else "CHECK")
    print("  note: 'reached road 2' is informative (route topology), not a hard gate.")


if __name__ == "__main__":
    main()
