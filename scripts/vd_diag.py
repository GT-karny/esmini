"""VirtualDriver diagnostic harness.

Injects VirtualDriverController into an arbitrary xosc, runs it headless, and
dumps a rich per-step telemetry trace (speed, steer, errors, preview endpoint)
so we can classify route-following failures: is the *preview* (route path) wrong,
or does *physics* overshoot a turn it was asked to take too fast?

Usage:  python scripts/vd_diag.py <scenario.xosc> [sim_seconds] [dt]
"""
import ctypes
import json
import os
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REL = os.path.join(ROOT, "build", "GT_esmini", "Release")
DLL = os.path.join(REL, "GT_esminiLib.dll")


def inject_virtual_driver(src_xosc: str, dst_xosc: str) -> None:
    """Mirror of backend _generate_virtual_driver_variant."""
    tree = ET.parse(src_xosc)
    root = tree.getroot()
    entity = root.find(".//ScenarioObject")
    oc = entity.find("ObjectController")
    if oc is not None:
        entity.remove(oc)
    ctrl = ET.Element("Controller")
    ctrl.set("name", "VirtualDriverController")
    props = ET.SubElement(ctrl, "Properties")
    p1 = ET.SubElement(props, "Property")
    p1.set("name", "esminiController")
    p1.set("value", "VirtualDriverController")
    oc = ET.Element("ObjectController")
    oc.append(ctrl)
    insert_pos = None
    for i, child in enumerate(entity):
        if child.tag in ("Vehicle", "CatalogReference"):
            insert_pos = i + 1
            break
    entity.insert(insert_pos if insert_pos is not None else len(entity), oc)
    ego_name = entity.get("name", "")
    for private in root.findall(".//Init/Actions/Private"):
        if private.get("entityRef") != ego_name:
            continue
        pa = ET.SubElement(private, "PrivateAction")
        act = ET.SubElement(pa, "ActivateControllerAction")
        act.set("longitudinal", "true")
        act.set("lateral", "true")
    tree.write(dst_xosc, encoding="utf-8", xml_declaration=True)


def main():
    scenario = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "resources", "xosc", "traffic_lights_gt.xosc")  # GT variant: Ego actually stops at red
    sim_s = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    dt = float(sys.argv[3]) if len(sys.argv) > 3 else 0.05

    variant = os.path.join(ROOT, "resources", "xosc",
                           os.path.basename(scenario).replace(".xosc", "_vd_diag.xosc"))
    inject_virtual_driver(scenario, variant)

    lib = ctypes.CDLL(DLL)
    lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.GT_InitWithArgs.restype = ctypes.c_int
    lib.GT_Step.argtypes = [ctypes.c_double]
    lib.GT_Close.argtypes = []
    lib.GT_GetVirtualDriverTelemetry.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.GT_GetVirtualDriverTelemetry.restype = ctypes.c_int

    argv_list = [b"vd_diag", b"--osc", variant.encode(), b"--headless", b"--fixed_timestep",
                 str(dt).encode()]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    if lib.GT_InitWithArgs(len(argv_list), argv) != 0:
        print("GT_InitWithArgs failed")
        sys.exit(1)

    buf = ctypes.create_string_buffer(32768)
    nsteps = int(sim_s / dt)
    rows = []
    frames = {}  # full telemetry by rounded time (for preview inspection)
    for i in range(nsteps):
        lib.GT_Step(dt)
        t = (i + 1) * dt
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n <= 0:
            continue
        tel = json.loads(buf.value.decode())
        if abs(t * 2 - round(t * 2)) < 1e-6:  # every 0.5s
            ego, drv = tel["ego"], tel["driver"]
            pv = tel["preview"]["points"]
            endp = pv[-1] if pv else {"x": 0, "y": 0}
            vset = pv[0]["v"] if pv else 0.0
            rows.append((round(t, 2), ego["x"], ego["y"], ego["speed"], drv["steer"],
                         ego.get("lane", 0), ego.get("offset", 0.0), vset,
                         ego.get("track", 0), ego.get("s", 0.0)))
            frames[round(t, 1)] = tel
    lib.GT_Close()

    print("  t       x        y      speed  v_set  steer  road/lane  offset      s")
    for r in rows:
        sat = " <SAT" if abs(r[4]) > 0.98 else ""
        big = " <OFFLANE" if abs(r[6]) > 2.5 else ""
        print(f"{r[0]:6.2f} {r[1]:8.2f} {r[2]:8.2f} {r[3]:6.2f} {r[7]:6.2f}  {r[4]:6.3f}  {r[8]:3d}/{r[5]:<3d}  "
              f"{r[6]:6.2f} {r[9]:7.1f}{sat}{big}")

    max_off = max(abs(r[6]) for r in rows) if rows else 0
    sat_frac = sum(1 for r in rows if abs(r[4]) > 0.98) / max(1, len(rows))
    print()
    print(f"max |lane_offset| = {max_off:.2f} m,  steer-saturated frames = {sat_frac*100:.0f}%")
    print("Read: lane_offset >~ half-lane (esp. growing & staying) => off the routed lane.")
    print("      steer saturated during turn => physics couldn't follow (too fast).")
    print("      steer NOT saturated but offset large => preview/route led it off-lane.")

    # Save full frames for manual preview inspection
    out = os.path.join(ROOT, "scripts", "vd_diag_frames.json")
    with open(out, "w") as f:
        json.dump(frames, f)
    print(f"\nFull per-0.1s telemetry saved to {out}")


if __name__ == "__main__":
    main()
