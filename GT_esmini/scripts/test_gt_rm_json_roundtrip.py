#!/usr/bin/env python3
"""Subprocess-isolated roundtrip test for the GT ODR side-model JSON C API (P9a).

For each probe fixture the PARENT spawns a CHILD python process (this same file,
run with --child) that loads GT_esminiLib.dll via GtOdrMetadataLib, calls
GT_RM_Init + all six JSON getters, and json.dumps the results to a tempfile. The
parent then reads that file and asserts on the parsed structures. The DLL may
flood stdout or crash outright, so the parent NEVER parses child stdout -- only
the result file (and the child's exit code).

Run:
    python test_gt_rm_json_roundtrip.py [--dll <path-to-GT_esminiLib.dll>]

Default DLL: <repo_root>/build/GT_esmini/Release/GT_esminiLib.dll, with repo_root
derived from this file's location (.../GT_esmini/scripts/ -> repo root).

Exit 0 on success, 1 on any assertion failure or hard error. Fixtures that are
absent on disk are SKIPPED with a notice (not failed).
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

# repo_root = .../<repo>/GT_esmini/scripts/test_gt_rm_json_roundtrip.py -> up 3
_THIS = os.path.abspath(__file__)
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(_THIS), "..", ".."))
DEFAULT_DLL = os.path.join(REPO_ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")

# scripts dir on sys.path so the child can import rm_lib.
SCRIPTS_DIR = os.path.dirname(_THIS)

FLOAT_TOL = 1e-9


# ---------------------------------------------------------------------------
# Child worker: load DLL, init, fetch all six getters, dump to result file.
# ---------------------------------------------------------------------------
def _run_child(dll_path, xodr_path, result_path, do_truncation):
    if SCRIPTS_DIR not in sys.path:
        sys.path.insert(0, SCRIPTS_DIR)
    import ctypes
    from rm_lib import GtOdrMetadataLib

    out = {"ok": False, "error": None}
    try:
        lib = GtOdrMetadataLib(dll_path)
        rc = lib.Init(xodr_path)
        out["init_rc"] = rc
        if rc != 0:
            out["error"] = f"GT_RM_Init returned {rc} for {xodr_path}"
            _write(result_path, out)
            return 0  # reported as a controlled failure via out["error"]

        out["audit"] = lib.GetOdrAuditJson()
        out["user_data"] = lib.GetUserDataJson()
        out["signals"] = lib.GetSignalSemanticsJson()
        out["junctions"] = lib.GetJunctionPrioritiesJson()
        out["cross_paths"] = lib.GetCrosswalksJson()
        out["railroad"] = lib.GetRailroadJson()

        if do_truncation:
            # Verify the truncation path directly via ctypes: a tiny buffer must
            # still return the FULL required length (== the size-probe length).
            fn = lib.lib.GT_RM_GetRailroadJson
            full_len = fn(None, 0)
            tiny = ctypes.create_string_buffer(4)
            trunc_len = fn(tiny, 4)
            out["trunc_full_len"] = full_len
            out["trunc_reported_len"] = trunc_len

        lib.Close()
        out["ok"] = True
    except Exception as exc:  # noqa: BLE001 -- report cleanly, never crash the parent
        out["error"] = f"{type(exc).__name__}: {exc}"
    _write(result_path, out)
    return 0


def _write(path, obj):
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(obj, fh)


# ---------------------------------------------------------------------------
# Parent: spawn one child per probe, read its result file, assert.
# ---------------------------------------------------------------------------
class ProbeFailure(Exception):
    pass


def _spawn(dll_path, xodr_path, do_truncation=False):
    fd, result_path = tempfile.mkstemp(suffix=".json", prefix="gt_rm_probe_")
    os.close(fd)
    try:
        cmd = [
            sys.executable, _THIS, "--child",
            "--dll", dll_path,
            "--xodr", xodr_path,
            "--result", result_path,
        ]
        if do_truncation:
            cmd.append("--truncation")
        proc = subprocess.run(cmd, capture_output=True, text=True)
        try:
            with open(result_path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except (OSError, ValueError):
            raise ProbeFailure(
                f"child produced no valid result file (exit={proc.returncode}); "
                f"stderr tail: {proc.stderr[-400:]!r}"
            )
        if not data.get("ok"):
            raise ProbeFailure(data.get("error") or f"child failed (exit={proc.returncode})")
        return data
    finally:
        try:
            os.remove(result_path)
        except OSError:
            pass


def _expect(cond, msg):
    if not cond:
        raise ProbeFailure(msg)


def _fclose(a, b):
    return abs(float(a) - float(b)) <= FLOAT_TOL


# ---- individual probe assertions -----------------------------------------
def probe_railway_switch(data):
    rr = data["railroad"]
    switches = rr.get("switches", [])
    _expect(len(switches) == 2, f"expected 2 switches, got {len(switches)}")

    s12 = next((s for s in switches if s["id"] == "12"), None)
    _expect(s12 is not None, "switch id '12' not found")
    _expect(s12["road_id"] == "1", f"switch 12 road_id != '1' ({s12['road_id']!r})")
    _expect(s12["position"] == "dynamic", f"switch 12 position != 'dynamic' ({s12['position']!r})")

    mt = s12["main_track"]
    _expect(mt is not None and mt["id"] == "1", f"switch 12 main_track.id != '1' ({mt})")
    _expect(_fclose(mt["s"], 10.0), f"switch 12 main_track.s != 10.0 ({mt['s']})")
    _expect(mt["dir"] == "+", f"switch 12 main_track.dir != '+' ({mt['dir']!r})")

    st = s12["side_track"]
    _expect(st is not None and st["id"] == "2", f"switch 12 side_track.id != '2' ({st})")
    _expect(_fclose(st["s"], 0.0), f"switch 12 side_track.s != 0.0 ({st['s']})")
    _expect(st["dir"] == "+", f"switch 12 side_track.dir != '+' ({st['dir']!r})")

    pr = s12["partner"]
    _expect(pr is not None and pr["name"] == "Switch32" and pr["id"] == "32",
            f"switch 12 partner mismatch ({pr})")

    # audit entries empty (fixture is fully supported), userData non-empty (viPartListRailML).
    audit = data["audit"]
    _expect(audit.get("entries") == [], f"audit entries not empty ({audit.get('entries')})")
    ud = data["user_data"].get("user_data", [])
    _expect(len(ud) > 0, "expected non-empty user_data (viPartListRailML)")


def probe_railway_station(data):
    stations = data["railroad"].get("stations", [])
    _expect(len(stations) == 2, f"expected 2 stations, got {len(stations)}")
    ids = sorted(s["id"] for s in stations)
    _expect(ids == ["12", "13"], f"station ids != ['12','13'] ({ids})")
    for s in stations:
        _expect(s["type"] == "small", f"station {s['id']} type != 'small' ({s['type']!r})")

    s12 = next(s for s in stations if s["id"] == "12")
    plats = s12["platforms"]
    _expect(len(plats) == 1, f"station 12 expected 1 platform, got {len(plats)}")
    p = plats[0]
    _expect(p["id"] == "12-1", f"station 12 platform id != '12-1' ({p['id']!r})")
    segs = p["segments"]
    _expect(len(segs) == 1, f"station 12 platform expected 1 segment, got {len(segs)}")
    seg = segs[0]
    _expect(seg["road_id"] == "2", f"segment road_id != '2' ({seg['road_id']!r})")
    _expect(_fclose(seg["s_start"], 16.5), f"segment s_start != 16.5 ({seg['s_start']})")
    _expect(_fclose(seg["s_end"], 51.0), f"segment s_end != 51.0 ({seg['s_end']})")
    _expect(seg["side"] == "right", f"segment side != 'right' ({seg['side']!r})")


def probe_semantics(data):
    signals = data["signals"].get("signals", [])
    sig = next((s for s in signals if s["road_id"] == "1" and s["signal_id"] == "2001"), None)
    _expect(sig is not None, "signal (road '1', id '2001') not found")
    _expect(sig["has_semantics"] is True, "signal 2001 has_semantics != True")
    speeds = sig["semantics"]["speeds"]
    _expect(len(speeds) == 1, f"signal 2001 expected 1 speed semantic, got {len(speeds)}")
    sp = speeds[0]
    _expect(sp["type"] == "maximum", f"speed type != 'maximum' ({sp['type']!r})")
    _expect(_fclose(sp["value"], 80.0), f"speed value != 80.0 ({sp['value']})")
    _expect(sp["unit"] == "km/h", f"speed unit != 'km/h' ({sp['unit']!r})")


def probe_junction(data):
    junctions = data["junctions"].get("junctions", [])
    j900 = next((j for j in junctions if j["junction_id"] == "900"), None)
    _expect(j900 is not None, "junction '900' not found in priorities")
    prios = j900["priorities"]
    _expect(len(prios) == 1, f"junction 900 expected 1 priority, got {len(prios)}")
    _expect(prios[0]["high"] == "1" and prios[0]["low"] == "2",
            f"junction 900 priority != high '1' low '2' ({prios[0]})")

    cps = data["cross_paths"].get("cross_paths", [])
    cp = next((c for c in cps if c["junction_id"] == "900" and c["id"] == "0"), None)
    _expect(cp is not None, "crossPath (junction '900', id '0') not found")
    _expect(cp["crossing_road"] == "2", f"crossPath crossing_road != '2' ({cp['crossing_road']!r})")
    _expect(cp["road_at_start"] == "1", f"crossPath road_at_start != '1' ({cp['road_at_start']!r})")
    _expect(cp["road_at_end"] == "1", f"crossPath road_at_end != '1' ({cp['road_at_end']!r})")


def probe_truncation(data):
    _expect("trunc_full_len" in data, "truncation probe did not run")
    full = data["trunc_full_len"]
    reported = data["trunc_reported_len"]
    _expect(reported == full,
            f"truncated call reported len {reported} != full len {full}")
    _expect(full > 4, f"expected railroad JSON longer than tiny buffer (full={full})")


# ---- probe registry -------------------------------------------------------
PROBES = [
    {
        "name": "railway-switch",
        "xodr": "GT_esmini/test/odr_fixtures/official/examples/Ex_Railway-Switch/Ex_Railway-Switch.xodr",
        "assert": probe_railway_switch,
        "truncation": True,  # this fixture has railroad data -> good truncation subject
    },
    {
        "name": "railway-station",
        "xodr": "GT_esmini/test/odr_fixtures/official/examples/Ex_Railway-Station/Ex_Railway-Station.xodr",
        "assert": probe_railway_station,
    },
    {
        "name": "signal-semantics",
        "xodr": "GT_esmini/test/odr_fixtures/generated/g7_semantics_19.xodr",
        "assert": probe_semantics,
    },
    {
        "name": "junction-priority+crosspath",
        "xodr": "GT_esmini/test/odr_fixtures/handauthored/21_common_junction_crosspath_19.xodr",
        "assert": probe_junction,
    },
]


def main_parent(dll_path):
    if not os.path.exists(dll_path):
        print(f"FAIL: GT_esminiLib.dll not found at {dll_path}")
        print("      (build GT_esminiLib.dll first -- Protocol A -- then re-run.)")
        return 1

    results = []
    hard_fail = False
    for probe in PROBES:
        xodr = os.path.join(REPO_ROOT, probe["xodr"].replace("/", os.sep))
        if not os.path.exists(xodr):
            results.append((probe["name"], "SKIP", "fixture absent"))
            continue
        try:
            data = _spawn(dll_path, xodr, do_truncation=probe.get("truncation", False))
            probe["assert"](data)
            if probe.get("truncation"):
                probe_truncation(data)
            results.append((probe["name"], "PASS", ""))
        except ProbeFailure as exc:
            results.append((probe["name"], "FAIL", str(exc)))
            hard_fail = True

    # ---- compact table ----
    print()
    print(f"GT ODR JSON roundtrip -- DLL: {dll_path}")
    print("-" * 72)
    width = max(len(n) for n, _, _ in results) if results else 8
    for name, status, detail in results:
        line = f"  {name.ljust(width)}  {status}"
        if detail:
            line += f"  -- {detail}"
        print(line)
    print("-" * 72)
    npass = sum(1 for _, s, _ in results if s == "PASS")
    nfail = sum(1 for _, s, _ in results if s == "FAIL")
    nskip = sum(1 for _, s, _ in results if s == "SKIP")
    print(f"  {npass} passed, {nfail} failed, {nskip} skipped")
    print()
    return 1 if hard_fail else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dll", default=DEFAULT_DLL, help="path to GT_esminiLib.dll")
    ap.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--xodr", help=argparse.SUPPRESS)
    ap.add_argument("--result", help=argparse.SUPPRESS)
    ap.add_argument("--truncation", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()

    if args.child:
        return _run_child(args.dll, args.xodr, args.result, args.truncation)
    return main_parent(args.dll)


if __name__ == "__main__":
    sys.exit(main())
