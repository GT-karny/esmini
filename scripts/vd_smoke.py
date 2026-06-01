"""Phase 1 VirtualDriver verification harness.

Loads GT_esminiLib.dll, runs virtual_driver_basic.xosc headless, and samples
GT_GetVirtualDriverTelemetry() once per simulated second. Confirms the ego moves
(physics), the SpeedAction is tracked (~15 m/s then 0), and the C-API returns JSON.
"""
import ctypes
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REL = os.path.join(ROOT, "build", "GT_esmini", "Release")
DLL = os.path.join(REL, "GT_esminiLib.dll")

lib = ctypes.CDLL(DLL)
lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
lib.GT_InitWithArgs.restype = ctypes.c_int
lib.GT_Step.argtypes = [ctypes.c_double]
lib.GT_Close.argtypes = []
lib.GT_GetVirtualDriverTelemetry.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
lib.GT_GetVirtualDriverTelemetry.restype = ctypes.c_int

xosc = os.path.join(ROOT, "resources", "xosc", "virtual_driver_basic.xosc")
argv_list = [b"vd_smoke", b"--osc", xosc.encode(), b"--headless", b"--fixed_timestep", b"0.05"]
argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
rc = lib.GT_InitWithArgs(len(argv_list), argv)
print("GT_InitWithArgs rc =", rc)
if rc != 0:
    sys.exit(1)

buf = ctypes.create_string_buffer(16384)
dt = 0.05
samples = []
for i in range(401):  # 0..20s
    lib.GT_Step(dt)
    t = (i + 1) * dt
    if abs(t - round(t)) < 1e-6:  # every ~1s
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n > 0:
            tel = json.loads(buf.value.decode())
            ego = tel["ego"]
            drv = tel["driver"]
            npts = len(tel["preview"]["points"])
            samples.append((round(t, 1), ego["x"], ego["y"], ego["speed"],
                            drv["throttle"], drv["brake"], drv["steer"], npts,
                            tel["preview"]["valid"]))

lib.GT_Close()

print("  t      x        y       speed   thr    brk    steer  npts valid")
for s in samples:
    print(f"{s[0]:5.1f} {s[1]:8.2f} {s[2]:8.2f} {s[3]:6.2f}  {s[4]:5.2f}  {s[5]:5.2f}  {s[6]:6.3f}  {s[7]:3d}  {s[8]}")

# Sanity checks
xs = [s[1] for s in samples]
ys = [s[2] for s in samples]
spd = [s[3] for s in samples]
moved = (max(xs) - min(xs)) ** 2 + (max(ys) - min(ys)) ** 2 > 100  # >10 m travel
reached_speed = max(spd) > 10.0
stopped = spd[-1] < 1.0
print()
print(f"moved>10m       : {moved}")
print(f"reached >10 m/s : {reached_speed} (max={max(spd):.2f})")
print(f"stopped at end  : {stopped} (final={spd[-1]:.2f})")
print("RESULT:", "PASS" if (moved and reached_speed and stopped) else "CHECK")
