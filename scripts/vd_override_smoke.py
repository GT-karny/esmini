"""feature:F7 override cycle smoke — auto -> manual latch -> resume -> auto.

Builds a temp variant of virtual_driver_basic.xosc whose VirtualDriverController
uses a per-run config with input_type=network, runs it headless via the DLL,
and from a background thread sends PedalSteer (PSTC) packets to UDP 9100.

Six phases, all verified against telemetry.override + .indicator + transition
edge fields (manual_transition / auto_transition):

  phase 1 (auto, no input)   -> lateral=false, longitudinal=false
  phase 2 (steer 0.6)        -> lateral=true (latch), manual_transition edge seen
  phase 3 (indicator + steer)-> indicator.left=true, still MANUAL
  phase 4 (release wheel)    -> LATCH HOLDS (lateral stays true)
  phase 5 (RESUME pulse)     -> lateral=false + auto_transition edge seen
  phase 6 (brake, then RESUME)-> longitudinal latch, then return to AUTO
                                (covers the longitudinal domain too)

Run via DriverScript/.venv (see generate_baseline.py for why)."""
import ctypes
import json
import os
import socket
import struct
import sys
import tempfile
import threading
import time
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")
BASIC = os.path.join(ROOT, "resources", "xosc", "virtual_driver_basic.xosc")

INPUT_PORT = 9100
MAGIC = 0x50535443  # 'PSTC'
WIRE = struct.Struct("<I4diI")  # magic, steering, throttle, brake, clutch, gear, buttons

# Must match gt_esmini::ButtonBits (VehicleCommand.hpp).
BTN_OVERRIDE       = 1 << 0
BTN_INDICATOR_LEFT = 1 << 1
BTN_AUTO_RESUME    = 1 << 7

# Shared command the sender thread emits; main thread updates it per phase.
_cmd = {"steering": 0.0, "throttle": 0.0, "brake": 0.0, "buttons": 0, "send": False}
_stop = threading.Event()


def _sender():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    while not _stop.is_set():
        if _cmd["send"]:
            pkt = WIRE.pack(MAGIC, _cmd["steering"], _cmd["throttle"], _cmd["brake"],
                            0.0, 0, _cmd["buttons"] & 0xFFFFFFFF)
            try:
                s.sendto(pkt, ("127.0.0.1", INPUT_PORT))
            except OSError:
                pass
        time.sleep(1.0 / 50.0)
    s.close()


def _make_variant(tmpdir: str) -> str:
    cfg_path = os.path.join(tmpdir, "virtual_driver.json")
    base = {}
    shipped = os.path.join(ROOT, "GT_esmini", "config", "virtual_driver.json")
    if os.path.exists(shipped):
        base = json.loads(open(shipped, encoding="utf-8").read())
    base["input_type"] = "network"
    base["input_port"] = INPUT_PORT
    base["input_transport"] = "udp"
    open(cfg_path, "w", encoding="utf-8").write(json.dumps(base, indent=2))

    tree = ET.parse(BASIC)
    root = tree.getroot()
    ctrl = root.find(".//ObjectController/Controller")
    props = ctrl.find("Properties")
    p = ET.SubElement(props, "Property")
    p.set("name", "ConfigFile")
    p.set("value", cfg_path)
    out = os.path.join(os.path.dirname(BASIC), "virtual_driver_basic.override.temp.xosc")
    tree.write(out, encoding="utf-8", xml_declaration=True)
    return out


def main() -> int:
    tmpdir = tempfile.mkdtemp(prefix="vd_override_")
    xosc = _make_variant(tmpdir)

    lib = ctypes.CDLL(DLL)
    lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.GT_InitWithArgs.restype = ctypes.c_int
    lib.GT_Step.argtypes = [ctypes.c_double]
    lib.GT_Close.argtypes = []
    lib.GT_GetVirtualDriverTelemetry.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.GT_GetVirtualDriverTelemetry.restype = ctypes.c_int

    argv_list = [b"vd_ovr", b"--osc", xosc.encode(), b"--headless", b"--fixed_timestep", b"0.05"]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    print("GT_InitWithArgs rc =", rc)
    if rc != 0:
        return 1

    th = threading.Thread(target=_sender, daemon=True)
    th.start()

    buf = ctypes.create_string_buffer(16384)

    def tel():
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        return json.loads(buf.value.decode()) if n > 0 else None

    def settle():
        """Give the 50Hz sender thread time to push at least two packets with
        the new _cmd state before the next Step drains the UDP socket. Without
        this, GT_Step in this Python loop runs faster than wall-clock 50Hz and
        Poll() can drain a stale packet as "latest", missing the transition."""
        time.sleep(0.08)

    def run(seconds):
        """Step and return (last_frame, any_manual_edge, any_auto_edge).

        Transition edges live for a single controller frame; sampling only the
        last frame would race the edge. Latch on any frame in the window."""
        settle()
        last = None
        any_manual_edge = False
        any_auto_edge = False
        for _ in range(int(seconds / 0.05)):
            lib.GT_Step(0.05)
            t = tel()
            if t:
                last = t
                ov = t.get("override", {})
                if ov.get("manual_transition"):
                    any_manual_edge = True
                if ov.get("auto_transition"):
                    any_auto_edge = True
            # Also let the sender push a packet BETWEEN steps so the pulse
            # window sees a fresh packet in every Poll drain.
            time.sleep(0.005)
        return last, any_manual_edge, any_auto_edge

    # Phase timings are compressed to fit under the base scenario's StopAction
    # (~t=13s) — past that the VirtualDriver deactivates and telemetry latches
    # to the last live frame, hiding downstream override transitions.

    # Phase 1: auto (no input)
    _cmd["send"] = False
    p1, e1m, e1a = run(1.5)

    # Phase 2: steering override -> latch to MANUAL, edge fires
    _cmd.update(steering=0.6, throttle=0.0, brake=0.0, buttons=0, send=True)
    p2, e2m, e2a = run(1.5)

    # Phase 3: manual left indicator (+ steer keeps override engaged)
    _cmd.update(steering=0.6, buttons=BTN_INDICATOR_LEFT, send=True)
    p3, e3m, e3a = run(1.0)

    # Phase 4: release wheel — LATCH must hold (feature:F7 core guarantee)
    _cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=True)
    p4, e4m, e4a = run(1.0)

    # Phase 5: RESUME pulse — held across ~6 controller frames so that at least
    # one Poll() drains a packet with the bit set as the "latest" (the
    # NetworkInputBridge keeps only the last packet per Poll). Then release.
    _cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=BTN_AUTO_RESUME, send=True)
    p5_pulse, e5pm, e5pa = run(0.4)
    _cmd.update(buttons=0)
    p5, e5m, e5a = run(1.0)
    # Combine: the auto_transition edge may have fired during the pulse window.
    e5a = e5a or e5pa
    e5m = e5m or e5pm

    # Phase 6: longitudinal side — brake to latch, release brake (latch holds),
    # then RESUME to return to AUTO. Mirrors the real-world flow: driver
    # intervenes with brake, lifts foot, then hits RESUME. Holding brake
    # THROUGH the RESUME pulse would (correctly) re-latch on the next frame.
    _cmd.update(steering=0.0, throttle=0.0, brake=0.5, buttons=0, send=True)
    p6a, e6am, e6aa = run(1.5)
    _cmd.update(brake=0.0, buttons=0)
    p6a_release, e6arm, e6ara = run(0.6)  # latch must hold with brake released
    _cmd.update(brake=0.0, buttons=BTN_AUTO_RESUME)
    p6_pulse, e6pm, e6pa = run(0.4)
    _cmd.update(buttons=0)
    p6b, e6bm, e6ba = run(0.8)
    e6ba = e6ba or e6pa

    _stop.set()
    th.join(timeout=1.0)
    lib.GT_Close()

    try:
        os.remove(xosc)
    except OSError:
        pass

    def _ov(f):
        return None if f is None else f.get("override")

    def _ind(f):
        return None if f is None else f.get("indicator")

    def _st(f):
        return None if f is None else f.get("sim_time")

    print(f"phase1 auto      : t={_st(p1)} ov={_ov(p1)} ind={_ind(p1)} edges m/a={e1m}/{e1a}")
    print(f"phase2 steer     : t={_st(p2)} ov={_ov(p2)} ind={_ind(p2)} edges m/a={e2m}/{e2a}")
    print(f"phase3 steer+ind : t={_st(p3)} ov={_ov(p3)} ind={_ind(p3)} edges m/a={e3m}/{e3a}")
    print(f"phase4 release   : t={_st(p4)} ov={_ov(p4)} ind={_ind(p4)} edges m/a={e4m}/{e4a}")
    print(f"phase5 pulse     : t={_st(p5_pulse)} ov={_ov(p5_pulse)} pulseedge m/a={e5pm}/{e5pa}")
    print(f"phase5 resume    : t={_st(p5)} ov={_ov(p5)} ind={_ind(p5)} edges m/a={e5m}/{e5a}")
    print(f"phase6a brake    : t={_st(p6a)} ov={_ov(p6a)} edges m/a={e6am}/{e6aa}")
    print(f"phase6a release  : t={_st(p6a_release)} ov={_ov(p6a_release)} edges m/a={e6arm}/{e6ara}")
    print(f"phase6 pulse     : t={_st(p6_pulse)} ov={_ov(p6_pulse)} pulseedge m/a={e6pm}/{e6pa}")
    print(f"phase6b resume   : t={_st(p6b)} ov={_ov(p6b)} edges m/a={e6bm}/{e6ba}")

    checks = []
    def check(name, cond):
        checks.append((name, bool(cond)))

    check("p1 not None", p1 is not None)
    check("p2 not None", p2 is not None)
    check("p3 not None", p3 is not None)
    check("p4 not None", p4 is not None)
    check("p5 not None", p5 is not None)
    check("p6a not None", p6a is not None)
    check("p6b not None", p6b is not None)
    check("p1 auto (lateral)", p1 and not p1["override"]["lateral"])
    check("p2 manual latch (lateral)", p2 and p2["override"]["lateral"])
    check("p2 manual_transition edge seen", e2m)
    check("p3 indicator.left", p3 and p3["indicator"]["left"])
    check("p4 LATCH HOLDS after release", p4 and p4["override"]["lateral"])
    check("p5 back to AUTO after resume", p5 and not p5["override"]["lateral"])
    check("p5 auto_transition edge seen", e5a)
    check("p6a longitudinal latch (brake)", p6a and p6a["override"]["longitudinal"])
    check("p6a release: latch HOLDS", p6a_release and p6a_release["override"]["longitudinal"])
    check("p6b longitudinal back to AUTO", p6b and not p6b["override"]["longitudinal"])
    check("p6b auto_transition edge seen", e6ba)

    ok = all(v for _, v in checks)
    for name, v in checks:
        print(f"  {'PASS' if v else 'FAIL'}  {name}")
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
