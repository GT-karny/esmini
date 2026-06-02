"""C-1 verification: manual override + manual indicator over the network input.

Builds a temp variant of virtual_driver_basic.xosc whose VirtualDriverController
uses a per-run config with input_type=network, runs it headless via the DLL,
and from a background thread sends PedalSteer (PSTC) packets to UDP 9100:
  - phase 1 (auto):     no packets  -> override flags stay false
  - phase 2 (steer):    steering=0.6 -> override.lateral becomes true
  - phase 3 (indicator):buttons=INDICATOR_LEFT (+steer) -> indicator.left true

Asserts the telemetry reflects each phase. Run via DriverScript/.venv with the
embedded python + Release dir on PATH (see generate_baseline.py for why)."""
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
BTN_INDICATOR_LEFT = 1 << 1

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
    # Per-run network config (only input_type matters; other defaults are fine).
    cfg_path = os.path.join(tmpdir, "virtual_driver.json")
    base = {}
    shipped = os.path.join(ROOT, "GT_esmini", "config", "virtual_driver.json")
    if os.path.exists(shipped):
        base = json.loads(open(shipped, encoding="utf-8").read())
    base["input_type"] = "network"
    base["input_port"] = INPUT_PORT
    base["input_transport"] = "udp"
    open(cfg_path, "w", encoding="utf-8").write(json.dumps(base, indent=2))

    # Inject ConfigFile property into the controller of a copy of the basic xosc.
    tree = ET.parse(BASIC)
    root = tree.getroot()
    ctrl = root.find(".//ObjectController/Controller")
    props = ctrl.find("Properties")
    p = ET.SubElement(props, "Property")
    p.set("name", "ConfigFile")
    p.set("value", cfg_path)
    # Write beside the original so its relative LogicFile/SceneGraphFile paths
    # still resolve (.temp.xosc is gitignored). Removed in main()'s finally.
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

    def run(seconds):
        last = None
        for _ in range(int(seconds / 0.05)):
            lib.GT_Step(0.05)
            t = tel()
            if t:
                last = t
        return last

    # Phase 1: auto (no input)
    _cmd["send"] = False
    p1 = run(3.0)

    # Phase 2: steering override
    _cmd.update(steering=0.6, throttle=0.0, brake=0.0, buttons=0, send=True)
    p2 = run(2.0)

    # Phase 3: manual left indicator (+ steer to keep override engaged)
    _cmd.update(steering=0.6, buttons=BTN_INDICATOR_LEFT, send=True)
    p3 = run(2.0)

    _stop.set()
    th.join(timeout=1.0)
    lib.GT_Close()

    try:
        os.remove(xosc)
    except OSError:
        pass

    print("phase1 override:", p1 and p1["override"], "indicator:", p1 and p1["indicator"])
    print("phase2 override:", p2 and p2["override"], "indicator:", p2 and p2["indicator"])
    print("phase3 override:", p3 and p3["override"], "indicator:", p3 and p3["indicator"])

    ok = (
        p1 is not None and p2 is not None and p3 is not None
        and not p1["override"]["lateral"]
        and p2["override"]["lateral"]
        and p3["indicator"]["left"]
    )
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
