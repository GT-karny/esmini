#!/usr/bin/env python
"""run_odr_conformance.py -- 3-layer OpenDRIVE conformance harness (plan P0, sec 3.3).

Manifest-driven, CWD-independent, deterministic. Freezes a machine-verifiable "red"
baseline BEFORE any parser change (opendrive_16_19_support_plan.md, phase P0).

LAYERS
------
  1. schema   -- validate_xodr_schema.py over control_set + fixtures; status vs the
                 manifest `expected.schema`  -> PASS / FAIL / XFAIL / XPASS / SKIP.
  2. rm       -- isolated esminiRMLib RM_Init probes -> a deterministic JSON extract
                 (roads/lanes/positions/signs, floats rounded to 1e-6) compared with
                 golden/rm/<slug>.json (abs tol 1e-6 on floats, exact otherwise).
  2b. motion  -- isolated esminiRMLib MOTION-TRAVERSAL probes (P6 S0 oracle upgrade,
                 odr_p6_virtual_junction_design.md sec 6): scripted RM_PositionMoveForward
                 walks (ds=+/-5.0, junctionSelectorAngle=MOTION_JSELECT) across every road's
                 first drivable lane from BOTH ends, freezing {road,lane,s,x,y,h} per step in
                 golden/motion/<slug>.json. Same universe as rm; expected-fail rm entries SKIP.
  3. osi      -- (profile full) control_set + fixtures flagged `osi: true`; isolated GT_esminiLib SE_Init/SE_StepDT/
                 SE_GetOSIGroundTruth probes decoded with esmini's own osi3 bindings
                 -> deterministic JSON extract compared with golden/osi/<slug>.json.

Every probe runs in its OWN subprocess (the DLLs flood stdout and some xodr files
CRASH the process); the worker writes its JSON result to a FILE, never stdout.

USAGE
-----
  run_odr_conformance.py [--profile quick|full] [--update-golden] [--check-matrix]
                         [--only <substr>] [--dll <path>] [--rmdll <path>]
                         [--report-dir <dir>] [--smoke] [--layers schema[,rm,motion,osi]]

  quick (default): schema + rm + motion.   full: adds osi.
  --update-golden: (re)write goldens (sorted keys, indent 1, trailing newline).
  --check-matrix : verify matrix_requirements.yaml coverage; print cluster x fixture table.
  --only <substr>: restrict to manifest entries whose id or path contains <substr>.
  --smoke        : run 3 end-to-end app smokes (esmini / replayer / odrviewer).
  --layers L,... : restrict to a subset of {schema,rm,motion,osi} (intersected with the profile).
                   Default = profile behavior. `--layers schema --check-matrix` runs the
                   DLL-free schema+matrix path (CI-friendly; tolerates absent build/ASAM).

Exit 0 iff zero FAIL and zero XPASS (SKIP / XFAIL are fine). Run under the venv that
has xmlschema/lxml/pyyaml (DriverScript/.venv). OSI decode also needs osi3 + protobuf.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(_THIS_DIR)
if _THIS_DIR not in sys.path:
    sys.path.insert(0, _THIS_DIR)

import yaml  # noqa: E402

# validate_xodr_schema is imported lazily inside the schema layer so --check-matrix /
# --smoke work even if the 1.9 XSD bootstrap path is unavailable.

# ---------------------------------------------------------------------------
# Paths / constants
# ---------------------------------------------------------------------------
FIX_DIR = os.path.join(_REPO_ROOT, "GT_esmini", "test", "odr_fixtures")
MANIFEST = os.path.join(FIX_DIR, "manifest.yaml")
MATRIX = os.path.join(FIX_DIR, "matrix_requirements.yaml")
GOLDEN_RM_DIR = os.path.join(FIX_DIR, "golden", "rm")
GOLDEN_OSI_DIR = os.path.join(FIX_DIR, "golden", "osi")
GOLDEN_MOTION_DIR = os.path.join(FIX_DIR, "golden", "motion")
WORK_DIR = os.path.join(FIX_DIR, "work")
DEFAULT_REPORT_DIR = os.path.join(FIX_DIR, "reports")

DEFAULT_RMDLL = os.path.join(
    _REPO_ROOT, "build", "EnvironmentSimulator", "Libraries", "esminiRMLib", "Release", "esminiRMLib.dll"
)
DEFAULT_DLL = os.path.join(_REPO_ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")

APP_DIR = os.path.join(_REPO_ROOT, "build", "EnvironmentSimulator", "Applications")
ESMINI_EXE = os.path.join(APP_DIR, "esmini", "Release", "esmini.exe")
REPLAYER_EXE = os.path.join(APP_DIR, "replayer", "Release", "replayer.exe")
ODRVIEWER_EXE = os.path.join(APP_DIR, "odrviewer", "Release", "odrviewer.exe")

# Candidate interpreters that MAY have osi3 (the OSI worker needs osi3 + protobuf and must be
# able to ctypes-load GT_esminiLib.dll). Order = preference: the running interpreter first, then
# the web venv (spec-suggested osi3 host). Auto-detected at OSI-layer start.
_WEB_VENV_PY = os.path.join(_REPO_ROOT, "GT_esmini", "web", ".venv", "Scripts", "python.exe")

FLOAT_TOL = 1e-6
PROBE_TIMEOUT = 60  # seconds per isolated probe
# Motion-traversal layer (P6 S0): fixed step, hard step cap, and a wider timeout (a walk
# does up to 2 x roads x 200 MoveAlongS calls on the big official maps).
MOTION_DS = 5.0
MOTION_MAX_STEPS = 200
MOTION_PROBE_TIMEOUT = 120
# Junction selector angle for the walks. NOT 0.0: upstream MoveAlongS tie-breaks candidates
# with EQUAL |angle-diff| randomly (RoadManager.cpp:10147, SE_Rand seeded from random_device
# per process), so 0.0 is nondeterministic at symmetric junction arms (+/-90 deg ties on
# X/T junctions -- observed on multi_intersections, UC_X_Junction, t_junction fixtures).
# A tiny asymmetric offset (0.001 rad, still "straight") makes every comparison strict,
# so the RNG is never consumed and the walk is bit-reproducible.
MOTION_JSELECT = 0.001

# Fork-drift / core-census checks (pure text; run in every profile). Expected [GT_ODR:]
# non-blank line count + budget are sourced from the machine-readable manifest block in
# GT_esmini/docs/gt_roadmanager_patches.md (single source of truth -- no expected values
# embedded here); an unreadable manifest is a hard startup failure.
import gt_patch_manifest  # noqa: E402

try:
    _PATCH_MANIFEST = gt_patch_manifest.load_manifest(_REPO_ROOT)
except ValueError as _e:
    raise SystemExit(f"FATAL: cannot load the GT patch manifest "
                     f"(GT_esmini/docs/gt_roadmanager_patches.md): {_e}")
# check_fork_drift measures fork-vs-CURRENT-pristine-FILE: prefer the dedicated legacy metric key
# (P6 S2: mirrored vj hunks are invisible to the file diff but visible to check_core_census's
# fork-vs-upstream-BLOB census, so the two expectations legitimately differ).
FORK_ODR_EXPECT_LINES = _PATCH_MANIFEST.get("fork_odr_drift_expect_lines", _PATCH_MANIFEST["fork_odr_expect_lines"])
FORK_LINE_BUDGET = _PATCH_MANIFEST["fork_line_budget"]

# Status tags.
PASS, FAIL, XFAIL, XPASS, SKIP = "PASS", "FAIL", "XFAIL", "XPASS", "SKIP"

# Lane snap masks (from rm_lib docstring): ANY_DRIVING = 1966594.
ANY_DRIVING_MASK = 1966594


# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------
def _round(v):
    """Round floats to 1e-6 grid; leave non-floats untouched. Normalises -0.0 -> 0.0."""
    if isinstance(v, float):
        r = round(v, 6)
        return 0.0 if r == 0.0 else r
    return v


def _slug(repo_rel_path: str) -> str:
    """golden slug = repo-root-relative path with / and . replaced by _."""
    return repo_rel_path.replace("\\", "/").replace("/", "_").replace(".", "_")


def _abs(repo_rel: str) -> str:
    return os.path.normpath(os.path.join(_REPO_ROOT, repo_rel))


def _rel(abs_path: str) -> str:
    try:
        return os.path.relpath(abs_path, _REPO_ROOT).replace("\\", "/")
    except ValueError:
        return abs_path.replace("\\", "/")


def _write_json(path: str, obj) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(obj, fh, sort_keys=True, indent=1)
        fh.write("\n")


def _load_json(path: str):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def _norm_expect(v) -> str:
    """Manifest expected values: pass|fail|crash -> pass|fail (crash frozen as fail)."""
    v = str(v).strip().lower()
    return "fail" if v == "crash" else v


# ---------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------
def load_manifest() -> dict:
    with open(MANIFEST, "r", encoding="utf-8") as fh:
        m = yaml.safe_load(fh)
    # Validate: unique ids, unique paths, all paths exist (asam_zips entries skip-eligible).
    ids, paths, problems = set(), set(), []
    for fx in m.get("fixtures", []):
        fid, p = fx["id"], fx["path"]
        if fid in ids:
            problems.append(f"duplicate fixture id: {fid}")
        ids.add(fid)
        if p in paths:
            problems.append(f"duplicate fixture path: {p}")
        paths.add(p)
        if not os.path.exists(_abs(p)) and "asam_zips" not in (fx.get("requires") or []):
            problems.append(f"missing fixture (not asam_zips skip-eligible): {p}")
    for e in m.get("control_set", []):
        p = e["path"]
        if p in paths:
            problems.append(f"control_set path also a fixture: {p}")
        if not os.path.exists(_abs(p)):
            problems.append(f"missing control_set path: {p}")
    if problems:
        raise SystemExit("manifest validation failed:\n  " + "\n  ".join(problems))
    return m


# ---------------------------------------------------------------------------
# Worker source (RM + OSI). Written to a temp file and run as an isolated
# subprocess so a crashing DLL cannot take the harness down.
# ---------------------------------------------------------------------------
_RM_WORKER = r'''
import sys, os, json
REPO_ROOT = REPO_ROOT_LIT
rmdll = RMDLL_LIT
xodr = XODR_LIT
out = OUT_LIT
TOL = 6
sys.path.insert(0, os.path.join(REPO_ROOT, "GT_esmini", "scripts"))
from rm_lib import EsminiRMLib

def r(v):
    if isinstance(v, float):
        x = round(v, TOL)
        return 0.0 if x == 0.0 else x
    return v

res = {"load_ok": False}
try:
    lib = EsminiRMLib(rmdll)
    rc = lib.Init(xodr)
    if rc != 0:
        res = {"load_ok": False, "rc": rc}
        json.dump(res, open(out, "w"))
        sys.exit(0)
    res["load_ok"] = True
    nroads = lib.GetNumberOfRoads()
    res["num_roads"] = nroads
    roads = []
    for ri in range(nroads):
        rid = lib.GetIdOfRoadFromIndex(ri)
        length = float(lib.GetRoadLength(rid))
        rd = {"id": lib.GetRoadIdString(rid), "length": r(length)}
        jstr = lib.GetJunctionIdString(rid)
        # RM_ID_UNDEFINED string is "" -> only record real junction ids
        if jstr:
            rd["junction"] = jstr
        # lane samples
        if length > 0:
            svals = [1.0, length * 0.25, length * 0.5, length * 0.75, max(length - 1.0, 0.0)]
        else:
            svals = [0.0]
        svals = [min(max(s, 0.0), length) for s in svals]
        samples = []
        first_drivable = None
        for s in svals:
            nlanes = lib.GetRoadNumberOfLanes(rid, s, -1)
            lanes = []
            for li in range(nlanes):
                rc2, lid = lib.GetLaneIdByIndex(rid, li, s, -1)
                if rc2 != 0:
                    continue
                lt = lib.GetLaneTypeByRoadId(rid, lid, s)
                rcw, w = lib.GetLaneWidthByRoadId(rid, lid, s)
                lanes.append({"id": int(lid), "type": int(lt), "width": r(float(w)) if rcw == 0 else None})
            lanes.sort(key=lambda x: x["id"])
            samples.append({"s": r(s), "num_lanes": int(nlanes), "lanes": lanes})
        rd["lane_samples"] = samples
        # position probe at first drivable lane, s = L/2
        ndriv = lib.GetRoadNumberOfDrivableLanes(rid, length * 0.5) if length > 0 else 0
        if ndriv > 0:
            rcd, dlane = lib.GetDrivableLaneIdByIndex(rid, 0, length * 0.5)
            if rcd == 0:
                h = lib.CreatePosition()
                pr = lib.SetLanePosition(h, rid, dlane, 0.0, length * 0.5, True)
                if pr == 0:
                    rcp, pd = lib.GetPositionData(h)
                    if rcp == 0:
                        rd["position_probe"] = {
                            "lane": int(dlane), "s": r(length * 0.5),
                            "x": r(pd.x), "y": r(pd.y), "z": r(pd.z), "h": r(pd.h),
                        }
                lib.DeletePosition(h)
        # signs
        signs = []
        ns = lib.GetNumberOfRoadSigns(rid)
        for si in range(ns):
            rcs, sg = lib.GetRoadSign(rid, si)
            if rcs != 0:
                continue
            name = sg.name.decode("utf-8", "replace") if sg.name else ""
            signs.append({
                "id": int(sg.id), "s": r(float(sg.s)), "t": r(float(sg.t)),
                "name": name, "orientation": int(sg.orientation), "z_offset": r(float(sg.z_offset)),
            })
        signs.sort(key=lambda x: (x["s"], x["t"], x["id"], x["name"]))
        rd["signs"] = signs
        roads.append(rd)
    roads.sort(key=lambda x: x["id"])
    res["roads"] = roads
    lib.Close()
except Exception as e:
    res = {"load_ok": False, "error": "%s: %s" % (type(e).__name__, e)}
json.dump(res, open(out, "w"))
'''

_MOTION_WORKER = r'''
import sys, os, json
REPO_ROOT = REPO_ROOT_LIT
rmdll = RMDLL_LIT
xodr = XODR_LIT
out = OUT_LIT
DS = DS_LIT
MAX_STEPS = MAX_STEPS_LIT
JSELECT = JSELECT_LIT
TOL = 6
sys.path.insert(0, os.path.join(REPO_ROOT, "GT_esmini", "scripts"))
from rm_lib import EsminiRMLib

def r(v):
    if isinstance(v, float):
        x = round(v, TOL)
        return 0.0 if x == 0.0 else x
    return v

res = {"load_ok": False}
try:
    lib = EsminiRMLib(rmdll)
    rc = lib.Init(xodr)
    if rc != 0:
        res = {"load_ok": False, "rc": rc}
        json.dump(res, open(out, "w"))
        sys.exit(0)
    res["load_ok"] = True

    def snap(handle):
        rcp, pd = lib.GetPositionData(handle)
        if rcp != 0:
            return None
        return {"road": lib.GetRoadIdString(pd.roadId), "lane": int(pd.laneId),
                "s": r(float(pd.s)), "x": r(pd.x), "y": r(pd.y), "h": r(pd.h)}

    # Stable road order = sort by original string id (same convention as the rm extract).
    roads = []
    for ri in range(lib.GetNumberOfRoads()):
        rid = lib.GetIdOfRoadFromIndex(ri)
        roads.append((lib.GetRoadIdString(rid), rid))
    roads.sort(key=lambda t: t[0])

    walks = []
    for rid_str, rid in roads:
        length = float(lib.GetRoadLength(rid))
        if length <= 0:
            continue
        if lib.GetRoadNumberOfDrivableLanes(rid, length * 0.5) <= 0:
            continue
        rcd, dlane = lib.GetDrivableLaneIdByIndex(rid, 0, length * 0.5)
        if rcd != 0:
            continue
        # Walk the first drivable lane from BOTH ends: "+" = start at s=0, move +DS along
        # heading; "-" = start at s=length, move -DS. junctionSelectorAngle=JSELECT (a hair
        # off straight so upstream's random equal-angle tie-break never fires -- see
        # MOTION_JSELECT); crossing INTO connecting roads is part of the frozen record.
        for dirn, start_s, ds in (("+", 0.0, DS), ("-", length, -DS)):
            h = lib.CreatePosition()
            walk = {"start_road": rid_str, "dir": dirn, "steps": []}
            pr = lib.SetLanePosition(h, rid, dlane, 0.0, start_s, True)
            if pr != 0:
                walk["set_rc"] = int(pr)
                walks.append(walk)
                lib.DeletePosition(h)
                continue
            st = snap(h)
            if st is not None:
                walk["steps"].append(st)
            end_rc = 0
            for _ in range(MAX_STEPS):
                mrc = lib.PositionMoveForward(h, ds, JSELECT)
                st = snap(h)
                if st is not None:
                    walk["steps"].append(st)
                if mrc < 0:
                    end_rc = int(mrc)  # end-of-road / error: frozen as part of the golden
                    break
            walk["end_rc"] = end_rc  # 0 = step cap reached (loops) or clean walk
            walks.append(walk)
            lib.DeletePosition(h)
    res["walks"] = walks
    lib.Close()
except Exception as e:
    res = {"load_ok": False, "error": "%s: %s" % (type(e).__name__, e)}
json.dump(res, open(out, "w"))
'''

_OSI_WORKER = r'''
import sys, os, json, ctypes
REPO_ROOT = REPO_ROOT_LIT
dll = DLL_LIT
xosc = XOSC_LIT
out = OUT_LIT
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))  # esmini's own osi3 bindings

def r(v):
    if isinstance(v, float):
        x = round(v, 6)
        return 0.0 if x == 0.0 else x
    return v

res = {"init_ok": False}
try:
    import osi3.osi_groundtruth_pb2 as gtpb
    lib = ctypes.CDLL(dll)
    lib.SE_Init.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.SE_Init.restype = ctypes.c_int
    lib.SE_StepDT.argtypes = [ctypes.c_double]; lib.SE_StepDT.restype = ctypes.c_int
    lib.SE_GetOSIGroundTruth.argtypes = [ctypes.POINTER(ctypes.c_int)]
    lib.SE_GetOSIGroundTruth.restype = ctypes.c_void_p  # NOT c_char_p (truncates at first NUL)
    lib.SE_Close.restype = None
    rc = lib.SE_Init(xosc.encode("utf-8"), 1, 0, 0, 0)
    if rc != 0:
        json.dump({"init_ok": False, "rc": rc}, open(out, "w"))
        sys.exit(0)
    lib.SE_StepDT(0.05)
    size = ctypes.c_int(0)
    ptr = lib.SE_GetOSIGroundTruth(ctypes.byref(size))
    data = ctypes.string_at(ptr, size.value) if (ptr and size.value > 0) else b""
    lib.SE_Close()
    g = gtpb.GroundTruth()
    g.ParseFromString(data)
    lanes = []
    for L in g.lane:
        c = L.classification
        lanes.append({
            "id": int(L.id.value),
            "type": int(c.type),
            "subtype": int(c.subtype),
            "centerline_points": len(c.centerline),
        })
    lanes.sort(key=lambda x: x["id"])
    stypes = {}
    for so in g.stationary_object:
        t = int(so.classification.type)
        stypes[str(t)] = stypes.get(str(t), 0) + 1
    tl_ids = sorted(int(tl.id.value) for tl in g.traffic_light)
    res = {
        "init_ok": True,
        "lane_count": len(g.lane),
        "lanes": lanes,
        "lane_boundary_count": len(g.lane_boundary),
        "stationary_objects": {"count": len(g.stationary_object), "types": stypes},
        "traffic_sign_count": len(g.traffic_sign),
        "traffic_light_count": len(g.traffic_light),
        "traffic_light_ids": tl_ids,
    }
except Exception as e:
    res = {"init_ok": False, "error": "%s: %s" % (type(e).__name__, e)}
json.dump(res, open(out, "w"))
'''


def _run_worker_script(body: str, out: str, script: str, interp: str | None = None,
                       timeout: int = PROBE_TIMEOUT) -> dict:
    """Run a materialised worker script isolated (timeout); read its JSON result file.

    The DLLs flood stdout and some xodr files crash the process, so every probe runs in
    its own subprocess and communicates via a result FILE, never stdout. `interp` selects
    the interpreter (defaults to the running one; the OSI layer may pick a different osi3 host).
    """
    with open(script, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(body)
    log_text = ""
    try:
        proc = subprocess.run([interp or sys.executable, script], capture_output=True,
                              timeout=timeout, cwd=_REPO_ROOT)
        log_text = (proc.stdout or b"").decode("utf-8", "replace") + (proc.stderr or b"").decode("utf-8", "replace")
        res = _load_json(out) if os.path.exists(out) else {"__worker_failed__": True, "returncode": proc.returncode}
    except subprocess.TimeoutExpired:
        res = {"__worker_failed__": True, "timeout": True}
    finally:
        for pth in (script, out):
            try:
                os.remove(pth)
            except OSError:
                pass
    res["_log"] = log_text
    return res


def _count_markers(log_text: str) -> dict:
    return {
        "ODR-UNSUPPORTED": log_text.count("[ODR-UNSUPPORTED]"),
        "ODR-REMOVED-1.6": log_text.count("[ODR-REMOVED-1.6]"),
    }


# Match one audit log line's payload (the logger prefixes [time] [level] [file::func::line]; the
# payload runs to end-of-line with NOTHING after it -- see logger.cpp AddTimeAndMetaData).
_RE_UNSUP = re.compile(r"\[ODR-UNSUPPORTED\]\s+(?P<body>.+?)\s*$", re.M)
_RE_REMOVED = re.compile(r"\[ODR-REMOVED-1\.6\]\s+(?P<body>.+?)\s*$", re.M)
_RE_ROAD = re.compile(r"\s*\(road=(?P<ctx>[^)]*)\)\s*$")


def _human_to_stored_unsup(body: str) -> str:
    """'road/objects/object/repeat@bT (road=1)' -> 'road/objects/object/repeat@bT|ctx=1'.
    No '(road=..)' suffix -> ctx=''. Works for both element and '<path>@<attr>' bodies."""
    m = _RE_ROAD.search(body)
    if m:
        ctx = m.group("ctx")
        path = body[: m.start()].rstrip()
    else:
        ctx = ""
        path = body.strip()
    return f"{path}|ctx={ctx}"


def _human_to_stored_removed(body: str) -> str:
    """'road/link/neighbor (road=1) (removed in OpenDRIVE 1.6)' ->
    'road/link/neighbor|ctx=1|removed16'."""
    # Strip the trailing removal note first.
    note = " (removed in OpenDRIVE 1.6)"
    if body.endswith(note):
        body = body[: -len(note)]
    base = _human_to_stored_unsup(body)  # yields '<path>|ctx=<ctx>'
    return base + "|removed16"


def _extract_audit_entries(log_text: str) -> set:
    """Parse a worker's captured DLL log into the STORED-format entry set (OdrSideModel.hpp
    contract), so it can be compared against manifest `expected_unsupported_entries`. Deduped."""
    out = set()
    for m in _RE_UNSUP.finditer(log_text):
        out.add(_human_to_stored_unsup(m.group("body")))
    for m in _RE_REMOVED.finditer(log_text):
        out.add(_human_to_stored_removed(m.group("body")))
    return out


# ---------------------------------------------------------------------------
# Layer 1: schema
# ---------------------------------------------------------------------------
def layer_schema(entries: list) -> list:
    """entries: list of dicts with keys path, expected_schema, id/kind. Returns rows."""
    import validate_xodr_schema as vxs

    vxs._register_rev9_mapping()
    # Bootstrap 1.9 XSDs once if any rev-9 file is present and schema19 is missing.
    if not vxs._schema19_available():
        try:
            import odr_fixture_setup
            odr_fixture_setup.ensure_assets(_REPO_ROOT)
        except Exception:
            pass

    rows = []
    for e in entries:
        p = e["path"]
        ap = _abs(p)
        exp = e["expected_schema"]
        if not os.path.exists(ap):
            rows.append({**e, "layer": "schema", "observed": "skip", "status": SKIP,
                         "detail": "file absent (asam_zips skip-eligible)"})
            continue
        tag, errs = vxs.validate_file(ap)
        if tag in (vxs.SKIP_NO_MAPPING, vxs.SKIP_NO_SCHEMA19):
            rows.append({**e, "layer": "schema", "observed": "skip", "status": SKIP,
                         "detail": tag})
            continue
        observed = "pass" if tag == vxs.PASS else "fail"
        status = _cmp_status(exp, observed)
        rows.append({**e, "layer": "schema", "observed": observed, "status": status,
                     "detail": ("" if observed == "pass" else "; ".join(errs[:2]))})
    return rows


def _cmp_status(expected: str, observed: str) -> str:
    """expected/observed in {pass, fail}. Map to PASS/FAIL/XFAIL/XPASS."""
    if expected == "pass":
        return PASS if observed == "pass" else FAIL
    # expected == fail
    return XFAIL if observed == "fail" else XPASS


# ---------------------------------------------------------------------------
# Layer 2: RM probes + goldens
# ---------------------------------------------------------------------------
def layer_rm(entries: list, rmdll: str, update: bool) -> list:
    rows = []
    for e in entries:
        p = e["path"]
        ap = _abs(p)
        if not os.path.exists(ap):
            rows.append({**e, "layer": "rm", "status": SKIP, "detail": "file absent"})
            continue
        # Build worker with a concrete OUT path via mkstemp done inside _run_worker;
        # we pass OUT_LIT as a sentinel that _run_worker replaces.
        res = _run_worker_rm(ap, rmdll)
        markers = _count_markers(res.get("_log", ""))
        extract = None
        if res.get("__worker_failed__"):
            observed_load = "fail"
            detail = "worker crash/timeout: " + json.dumps({k: v for k, v in res.items() if k != "_log"})
        elif res.get("load_ok"):
            observed_load = "pass"
            detail = ""
            extract = {"load_ok": True, **{k: res[k] for k in ("num_roads", "roads") if k in res}}
        else:
            observed_load = "fail"
            detail = f"RM_Init rc={res.get('rc', res.get('error'))}"
        status, gstat = _golden_compare("rm", p, extract, update, e["expected_rm"], observed_load)
        audit_status = _audit_check(e, markers, res.get("_log", ""))
        # An audit prediction mismatch (or control-set warn) fails the row so it counts toward exit.
        if audit_status is not None and audit_status.startswith("FAIL"):
            status = FAIL
            detail = (detail + " | " if detail else "") + "audit " + audit_status
        rows.append({**e, "layer": "rm", "observed": observed_load, "status": status,
                     "golden": gstat, "detail": detail, "markers": markers,
                     "audit_status": audit_status})
    return rows


def _run_worker_rm(abs_xodr: str, rmdll: str) -> dict:
    fd, script = tempfile.mkstemp(suffix="_rm.py", prefix="odrconf_", dir=WORK_DIR)
    os.close(fd)
    out = script + ".json"
    body = (_RM_WORKER
            .replace("REPO_ROOT_LIT", repr(_REPO_ROOT))
            .replace("RMDLL_LIT", repr(rmdll))
            .replace("XODR_LIT", repr(abs_xodr))
            .replace("OUT_LIT", repr(out)))
    return _run_worker_script(body, out, script)


# ---------------------------------------------------------------------------
# Layer 2b: motion-traversal probes + goldens (P6 S0 oracle upgrade)
# ---------------------------------------------------------------------------
def layer_motion(entries: list, rmdll: str, update: bool) -> list:
    """Motion-traversal goldens (odr_p6_virtual_junction_design.md sec 6): for every entry
    whose rm_init is expected to pass, walk each road's first drivable lane from BOTH ends
    (RM_PositionMoveForward, ds=+/-MOTION_DS, junctionSelectorAngle=MOTION_JSELECT,
    MOTION_MAX_STEPS cap) and freeze {road,lane,s,x,y,h} per step in golden/motion/<slug>.json. Junction
    traversal continuation is exactly what P6 will touch, so crossing INTO connecting roads
    is part of the record. Expected-fail rm entries SKIP (no walk on a failed load); the rm
    layer already owns the load XFAIL bookkeeping and the audit checks."""
    rows = []
    for e in entries:
        p = e["path"]
        ap = _abs(p)
        if not os.path.exists(ap):
            rows.append({**e, "layer": "motion", "status": SKIP, "detail": "file absent"})
            continue
        if e["expected_rm"] == "fail":
            rows.append({**e, "layer": "motion", "status": SKIP,
                         "detail": "rm_init expected-fail (no motion walk)"})
            continue
        if e.get("motion_nondeterministic"):
            rows.append({**e, "layer": "motion", "status": SKIP,
                         "detail": "manifest motion_nondeterministic: upstream random "
                                   "equal-angle tie-break -> no stable golden (see manifest note)"})
            continue
        res = _run_worker_motion(ap, rmdll)
        if res.get("__worker_failed__"):
            rows.append({**e, "layer": "motion", "status": FAIL, "observed": "fail",
                         "detail": "worker crash/timeout: " + json.dumps(
                             {k: v for k, v in res.items() if k != "_log"})})
            continue
        if not res.get("load_ok"):
            rows.append({**e, "layer": "motion", "status": FAIL, "observed": "fail",
                         "detail": f"RM_Init rc={res.get('rc', res.get('error'))}"})
            continue
        extract = {"load_ok": True, "walks": res.get("walks", [])}
        status, gstat = _golden_compare("motion", p, extract, update, "pass", "pass")
        rows.append({**e, "layer": "motion", "observed": "pass", "status": status,
                     "golden": gstat, "detail": ""})
    return rows


def _run_worker_motion(abs_xodr: str, rmdll: str) -> dict:
    fd, script = tempfile.mkstemp(suffix="_motion.py", prefix="odrconf_", dir=WORK_DIR)
    os.close(fd)
    out = script + ".json"
    body = (_MOTION_WORKER
            .replace("REPO_ROOT_LIT", repr(_REPO_ROOT))
            .replace("RMDLL_LIT", repr(rmdll))
            .replace("XODR_LIT", repr(abs_xodr))
            .replace("OUT_LIT", repr(out))
            .replace("DS_LIT", repr(MOTION_DS))
            .replace("MAX_STEPS_LIT", repr(MOTION_MAX_STEPS))
            .replace("JSELECT_LIT", repr(MOTION_JSELECT)))
    return _run_worker_script(body, out, script, timeout=MOTION_PROBE_TIMEOUT)


# ---------------------------------------------------------------------------
# Layer 3: OSI probes + goldens (control_set only, profile full)
# ---------------------------------------------------------------------------
_PROBE_XOSC = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <FileHeader revMajor="1" revMinor="1" date="2026-07-02T00:00:00" description="odr-conformance-probe" author="gt"/>
  <ParameterDeclarations/>
  <CatalogLocations/>
  <RoadNetwork>
    <LogicFile filepath="{xodr}"/>
  </RoadNetwork>
  <Entities>
    <ScenarioObject name="Ego">
      <Vehicle name="probe_car" vehicleCategory="car">
        <BoundingBox>
          <Center x="1.4" y="0.0" z="0.9"/>
          <Dimensions width="2.0" length="5.0" height="1.8"/>
        </BoundingBox>
        <Performance maxSpeed="60" maxDeceleration="10" maxAcceleration="10"/>
        <Axles>
          <FrontAxle maxSteering="0.5" wheelDiameter="0.6" trackWidth="1.8" positionX="2.98" positionZ="0.3"/>
          <RearAxle maxSteering="0.0" wheelDiameter="0.6" trackWidth="1.8" positionX="0.0" positionZ="0.3"/>
        </Axles>
        <Properties/>
      </Vehicle>
    </ScenarioObject>
  </Entities>
  <Storyboard>
    <Init>
      <Actions>
        <Private entityRef="Ego">
          <PrivateAction>
            <TeleportAction>
              <Position><WorldPosition x="{x}" y="{y}" h="{h}"/></Position>
            </TeleportAction>
          </PrivateAction>
        </Private>
      </Actions>
    </Init>
    <StopTrigger>
      <ConditionGroup>
        <Condition name="stop" delay="0" conditionEdge="none">
          <ByValueCondition>
            <SimulationTimeCondition value="1.0" rule="greaterThan"/>
          </ByValueCondition>
        </Condition>
      </ConditionGroup>
    </StopTrigger>
  </Storyboard>
</OpenSCENARIO>
"""


def _first_road_probe(rm_extract: dict):
    """Return (x, y, h) from road[0]'s position_probe, or None if no drivable lane."""
    roads = rm_extract.get("roads") or []
    if not roads:
        return None
    pp = roads[0].get("position_probe")
    if not pp:
        return None
    return pp["x"], pp["y"], pp["h"]


def _interp_has_osi3(py: str) -> bool:
    """True if interpreter `py` can import esmini's osi3 bindings + protobuf."""
    if not os.path.exists(py):
        return False
    probe = (
        "import sys, os; sys.path.insert(0, os.path.join(%r, 'scripts'));"
        "import google.protobuf, osi3.osi_groundtruth_pb2" % _REPO_ROOT
    )
    try:
        r = subprocess.run([py, "-c", probe], capture_output=True, timeout=30, cwd=_REPO_ROOT)
        return r.returncode == 0
    except Exception:
        return False


def detect_osi_interpreter() -> str | None:
    """Return the first interpreter (running, then web venv) that has osi3, else None."""
    for py in (sys.executable, _WEB_VENV_PY):
        if _interp_has_osi3(py):
            return py
    return None


def layer_osi(entries: list, dll: str, update: bool, osi_py: str, rmdll: str) -> list:
    """OSI layer: control_set + fixtures flagged `osi: true` in the manifest (P3: fixture-scoped
    OSI observables, e.g. traffic_light presence for the demote fixture). Needs a fresh RM probe
    for the teleport pose."""
    rows = []
    for e in entries:
        p = e["path"]
        ap = _abs(p)
        if not os.path.exists(ap):
            rows.append({**e, "layer": "osi", "status": SKIP, "detail": "file absent"})
            continue
        # RM probe (fresh) to get road[0] midpoint pose.
        rm = _run_worker_rm(ap, rmdll)
        pose = None
        if rm.get("load_ok"):
            pose = _first_road_probe({"roads": rm.get("roads", [])})
        if pose is None:
            rows.append({**e, "layer": "osi", "status": SKIP,
                         "detail": "no drivable lane at road[0] midpoint (cannot host probe)"})
            continue
        x, y, h = pose
        xosc = _PROBE_XOSC.format(xodr=ap.replace("\\", "/"), x=_round(x), y=_round(y), h=_round(h))
        fd, xoscf = tempfile.mkstemp(suffix="_probe.xosc", prefix="odrconf_", dir=WORK_DIR)
        os.close(fd)
        with open(xoscf, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(xosc)
        res = _run_worker_osi(xoscf, dll, osi_py)
        try:
            os.remove(xoscf)
        except OSError:
            pass
        markers = _count_markers(res.get("_log", ""))
        if res.get("__worker_failed__"):
            rows.append({**e, "layer": "osi", "status": FAIL, "observed": "init_fail",
                         "detail": "OSI worker crash/timeout", "markers": markers})
            continue
        if not res.get("init_ok"):
            rows.append({**e, "layer": "osi", "status": FAIL, "observed": "INIT_FAIL",
                         "detail": f"SE_Init rc={res.get('rc', res.get('error'))}", "markers": markers})
            continue
        extract = {k: v for k, v in res.items() if not k.startswith("_") and k != "init_ok"}
        status, gstat = _golden_compare("osi", p, extract, update, "pass", "pass")
        detail = ""
        # Opt-in OSI content checks (manifest-driven, generic). Each records the FIRST failure
        # into `detail`; any one flips the row to FAIL so it counts toward the harness exit code.
        # OSI Lane classification enum (osi3): UNKNOWN=0 OTHER=1 DRIVING=2 NONDRIVING=3 INTERSECTION=4;
        # Subtype SIDEWALK=4. StationaryObject CROSSWALK -> TYPE_OTHER (see GT_OSIReporter).
        lanes = extract.get("lanes", [])
        checks = []
        # P2 acceptance (i): opt-in zero-TYPE_UNKNOWN lane classification check.
        if e.get("osi_expect_no_unknown"):
            unknown = [l["id"] for l in lanes if l.get("type") == 0]
            if unknown:
                checks.append(f"osi_expect_no_unknown: TYPE_UNKNOWN lanes {unknown}")
        # P5 acceptance (i)/(#3,#4): the IsOsiIntersection empty-connection guard suppresses the ghost
        # TYPE_INTERSECTION lane emitted for a junction with ZERO resolvable connections. Set on the
        # crossPath fixtures whose (virtual/crossing) junctions carry no <connection> -> the OSI ground
        # truth must contain NO intersection lane at all. (Junctions WITH connections legitimately emit
        # an intersection lane whose centerline is empty by upstream design, so we do NOT flag empty
        # centerlines globally -- see fixture 21 / multi_intersections control.)
        if e.get("osi_expect_no_intersection_lane"):
            inter = [l["id"] for l in lanes if l.get("type") == 4]
            if inter:
                checks.append(f"osi_expect_no_intersection_lane: ghost TYPE_INTERSECTION lanes {inter}")
        # P5 acceptance (#3): the walking lane(s) survive as OSI NONDRIVING/SIDEWALK (P2 lane-type map).
        min_sw = e.get("osi_expect_lane_type_sidewalk_min")
        if min_sw is not None:
            nsw = sum(1 for l in lanes if l.get("subtype") == 4)
            if nsw < int(min_sw):
                checks.append(f"osi_expect_lane_type_sidewalk_min={min_sw}: only {nsw} SIDEWALK lane(s)")
        # P5 acceptance (#4): the crossPath-synthesized CROSSWALK is present as a StationaryObject.
        min_stat = e.get("osi_expect_stationary_min")
        if min_stat is not None:
            nstat = int(extract.get("stationary_objects", {}).get("count", 0))
            if nstat < int(min_stat):
                checks.append(f"osi_expect_stationary_min={min_stat}: only {nstat} stationary object(s)")
        if checks:
            status = FAIL
            detail = " | ".join(checks)
        rows.append({**e, "layer": "osi", "observed": "ok", "status": status,
                     "golden": gstat, "detail": detail, "markers": markers})
    return rows


def _run_worker_osi(abs_xosc: str, dll: str, osi_py: str) -> dict:
    fd, script = tempfile.mkstemp(suffix="_osi.py", prefix="odrconf_", dir=WORK_DIR)
    os.close(fd)
    out = script + ".json"
    body = (_OSI_WORKER
            .replace("REPO_ROOT_LIT", repr(_REPO_ROOT))
            .replace("DLL_LIT", repr(dll))
            .replace("XOSC_LIT", repr(abs_xosc))
            .replace("OUT_LIT", repr(out)))
    return _run_worker_script(body, out, script, interp=osi_py)


# ---------------------------------------------------------------------------
# Golden compare / update
# ---------------------------------------------------------------------------
def _golden_path(kind: str, repo_rel: str) -> str:
    base = {"rm": GOLDEN_RM_DIR, "osi": GOLDEN_OSI_DIR, "motion": GOLDEN_MOTION_DIR}[kind]
    return os.path.join(base, _slug(repo_rel) + ".json")


def _diff_json(a, b, path=""):
    """Return list of human diff strings. abs tol 1e-6 on floats, exact otherwise."""
    diffs = []
    if isinstance(a, float) or isinstance(b, float):
        try:
            if abs(float(a) - float(b)) > FLOAT_TOL:
                diffs.append(f"{path}: {a} != {b}")
        except (TypeError, ValueError):
            diffs.append(f"{path}: {a!r} != {b!r}")
        return diffs
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                diffs.append(f"{path}.{k}: missing in observed")
            elif k not in b:
                diffs.append(f"{path}.{k}: missing in golden")
            else:
                diffs += _diff_json(a[k], b[k], f"{path}.{k}")
        return diffs
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            diffs.append(f"{path}: list len {len(a)} != {len(b)}")
        for i, (x, y) in enumerate(zip(a, b)):
            diffs += _diff_json(x, y, f"{path}[{i}]")
        return diffs
    if a != b:
        diffs.append(f"{path}: {a!r} != {b!r}")
    return diffs


def _golden_compare(kind, repo_rel, extract, update, expected_load, observed_load):
    """Returns (status, golden_status_str).

    For fixtures whose expected load is fail (rm), no extract is produced; status is the
    XFAIL/XPASS/PASS/FAIL of the LOAD comparison and golden is 'n/a (load-fail expected)'.
    """
    load_status = _cmp_status(_norm_expect(expected_load), observed_load)
    if extract is None:
        # No serialisable extract (load failed). Load comparison is the whole story.
        return load_status, "n/a"
    gpath = _golden_path(kind, repo_rel)
    if update:
        _write_json(gpath, extract)
        return (load_status if load_status in (PASS, XFAIL) else load_status), "written"
    if not os.path.exists(gpath):
        return FAIL, "MISSING (run --update-golden)"
    golden = _load_json(gpath)
    diffs = _diff_json(extract, golden, "")
    if diffs:
        return FAIL, "DIFF: " + " | ".join(diffs[:5]) + (" ..." if len(diffs) > 5 else "")
    return load_status, "match"


# ---------------------------------------------------------------------------
# Audit plumbing (P1-ready, inert in P0)
# ---------------------------------------------------------------------------
def _audit_check(entry: dict, markers: dict, log_text: str = ""):
    """Compare the DLL's audit output to the manifest expectations for this entry.

    Enforcement (returns None if nothing to enforce, "ok", or "FAIL: ..."):
      * control_set  -> ALWAYS enforce audit-count == 0 (the 1.4/1.5 zero-warn acceptance,
        plan sec 3.3). Any [ODR-UNSUPPORTED]/[ODR-REMOVED-1.6] on a repo asset is a failure.
      * expected_unsupported_entries (list) -> exact SET comparison of stored-format entries
        parsed from the log against the predicted list (attribute-granular; the primary check).
      * expected_unsupported (dict {elements, attributes}) -> total ODR-UNSUPPORTED count check
        (kept for back-compat; only used if no entry list is given).
    """
    problems = []

    # control_set: hard zero-warn (independent of any explicit field).
    if entry.get("kind") == "control_set":
        got = markers.get("ODR-UNSUPPORTED", 0) + markers.get("ODR-REMOVED-1.6", 0)
        if got != 0:
            observed = sorted(_extract_audit_entries(log_text))
            problems.append(f"control_set must be audit-zero but observed {got}: {observed[:8]}")
        return "ok" if not problems else "FAIL: " + "; ".join(problems)

    exp = entry.get("expected_unsupported")
    exp_entries = entry.get("expected_unsupported_entries")
    if exp is None and exp_entries is None:
        return None

    if isinstance(exp_entries, list):
        want = set(exp_entries)
        got = _extract_audit_entries(log_text)
        missing = sorted(want - got)
        extra = sorted(got - want)
        if missing:
            problems.append(f"missing {len(missing)}: {missing[:6]}")
        if extra:
            problems.append(f"unexpected {len(extra)}: {extra[:6]}")
    elif isinstance(exp, dict):
        want_n = int(exp.get("elements", 0)) + int(exp.get("attributes", 0))
        got_n = markers.get("ODR-UNSUPPORTED", 0)
        if want_n != got_n:
            problems.append(f"expected_unsupported total {want_n} != observed {got_n}")

    return "ok" if not problems else "FAIL: " + "; ".join(problems)


def run_fork_drift() -> dict:
    """Run the pure-text fork-drift check (check_fork_drift.check_drift).

    Returns a dict: {ran: bool, ok: bool, odr_lines: int, summary: str, unattributed: [...]}.
    Enforces the FORK_ODR_EXPECT_LINES budget in addition to no-unattributed-drift.
    """
    try:
        import check_fork_drift as cfd
    except Exception as e:  # pragma: no cover - import guard
        return {"ran": False, "ok": False, "odr_lines": 0,
                "summary": f"fork-drift: ERROR (cannot import check_fork_drift: {e})", "unattributed": []}
    res = cfd.check_drift()
    ok = bool(res.get("ok")) and res.get("odr_lines") == FORK_ODR_EXPECT_LINES
    summary = cfd.format_summary(res, FORK_LINE_BUDGET)
    if res.get("ok") and res.get("odr_lines") != FORK_ODR_EXPECT_LINES:
        summary += f"  [BUDGET MISMATCH: {res.get('odr_lines')} != expected {FORK_ODR_EXPECT_LINES}]"
    return {"ran": True, "ok": ok, "odr_lines": res.get("odr_lines", 0),
            "summary": summary, "unattributed": res.get("unattributed", [])}


def run_core_census() -> dict:
    """Run the AUTHORITATIVE manifest-driven census checker (check_core_census) + its selftest.

    Both must pass. Returns {ran, ok, summary, failures}. Pure text, runs in every profile
    (quick / full / schema-only), like run_fork_drift().
    """
    try:
        import check_core_census as ccc
    except Exception as e:  # pragma: no cover - import guard
        return {"ran": False, "ok": False, "failures": [],
                "summary": f"core-census: ERROR (cannot import check_core_census: {e})"}
    try:
        selftest_ok = ccc.run_selftest(quiet=True)
        res = ccc.run_check(_REPO_ROOT)
    except (RuntimeError, ValueError) as e:
        return {"ran": True, "ok": False, "failures": [str(e)],
                "summary": f"core-census: ERROR ({e})"}
    ok = selftest_ok and res["ok"]
    summary = ccc.format_summary(res)
    if not selftest_ok:
        summary += "  [SELFTEST FAILED]"
    return {"ran": True, "ok": ok, "summary": summary, "failures": res.get("failures", [])}


def _selftest_audit() -> bool:
    """Unit-smoke the audit mechanism with a fake log text (no fixture carries it yet)."""
    fake_log = "prefix [ODR-UNSUPPORTED] road/surface/CRG\n[ODR-UNSUPPORTED] road/surface/CRG@xOffset\n[ODR-REMOVED-1.6] road/link/neighbor\n"
    m = _count_markers(fake_log)
    ok = (m["ODR-UNSUPPORTED"] == 2 and m["ODR-REMOVED-1.6"] == 1)
    e_ok = _audit_check({"expected_unsupported": {"elements": 1, "attributes": 1}}, m) == "ok"
    e_bad = _audit_check({"expected_unsupported": {"elements": 5, "attributes": 0}}, m).startswith("FAIL")
    e_none = _audit_check({}, m) is None
    return ok and e_ok and e_bad and e_none


# ---------------------------------------------------------------------------
# Matrix check
# ---------------------------------------------------------------------------
def _pattern_matches(pattern: str, feature: str) -> bool:
    """substring OR prefix match (see matrix_requirements.yaml header)."""
    return pattern in feature or feature.startswith(pattern)


def check_matrix(manifest: dict) -> tuple:
    """Returns (ok, table_lines, fail_lines)."""
    with open(MATRIX, "r", encoding="utf-8") as fh:
        mx = yaml.safe_load(fh)
    clusters = mx["clusters"]
    # feature index: cluster -> {fixture_id -> [features]}
    fx_by_cluster = {}
    for fx in manifest.get("fixtures", []):
        for c in (fx.get("clusters") or []):
            fx_by_cluster.setdefault(c, {})[fx["id"]] = fx.get("features") or []
    all_features = [(fx["id"], f) for fx in manifest.get("fixtures", []) for f in (fx.get("features") or [])]

    table, fails = [], []
    covered, deferred = 0, 0
    for cid in sorted(clusters.keys()):
        spec = clusters[cid] or {}
        title = spec.get("title", "")
        if "deferred" in spec:
            deferred += 1
            table.append(f"  [{cid:>2}] DEFERRED   {spec['deferred']}")
            continue
        if "satisfied_by" in spec:
            covered += 1
            table.append(f"  [{cid:>2}] {spec['satisfied_by'].upper():<10} (no feature pattern) {title[:48]}")
            continue
        patterns = spec.get("patterns") or []
        if not patterns:
            fails.append(f"cluster {cid} has no patterns and is not deferred/satisfied_by")
            table.append(f"  [{cid:>2}] EMPTY-ROW  !! no patterns")
            continue
        covered += 1
        for pat in patterns:
            hits = [fid for (fid, f) in all_features if _pattern_matches(pat, f)]
            hit_ids = sorted(set(hits))
            if not hit_ids:
                fails.append(f"cluster {cid} pattern {pat!r} matched NO fixture feature")
                table.append(f"  [{cid:>2}] MISS  {pat}")
            else:
                table.append(f"  [{cid:>2}] hit   {pat}  <- {', '.join(hit_ids[:4])}")
    header = [
        f"Matrix coverage: {covered} clusters covered, {deferred} deferred "
        f"(total {len(clusters)} of plan clusters 0-22).",
        "",
    ]
    return (len(fails) == 0), header + table, fails


# ---------------------------------------------------------------------------
# Smoke
# ---------------------------------------------------------------------------
def run_smoke() -> list:
    os.makedirs(WORK_DIR, exist_ok=True)
    results = []

    # (a) esmini headless -> record .dat  (needs a probe xosc on a known-good road)
    straight = _abs("resources/xodr/straight_500m.xodr")
    smoke_xosc = os.path.join(WORK_DIR, "smoke_probe.xosc")
    with open(smoke_xosc, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(_PROBE_XOSC.format(xodr=straight.replace("\\", "/"), x=250.0, y=1.535, h=3.14159))
    # --logfile_path keeps each app's log inside work/ (gitignored) instead of the repo root.
    log = lambda n: os.path.join(WORK_DIR, n)
    dat = os.path.join(WORK_DIR, "smoke.dat")
    a = _smoke_cmd([ESMINI_EXE, "--osc", smoke_xosc, "--headless",
                    "--fixed_timestep", "0.05", "--record", dat, "--disable_controllers",
                    "--logfile_path", log("smoke_esmini_log.txt")],
                   ok_extra=lambda: os.path.exists(dat) and os.path.getsize(dat) > 0)
    results.append(("esmini --record", a))

    # (b) replayer on the .dat
    if os.path.exists(dat) and os.path.getsize(dat) > 0:
        b = _smoke_cmd([REPLAYER_EXE, "--file", dat, "--headless", "--time_scale", "10",
                        "--quit_at_end", "--res_path", _abs("resources"),
                        "--logfile_path", log("smoke_replayer_log.txt")])
    else:
        b = (False, "skipped: no .dat produced by (a)")
    results.append(("replayer --file", b))

    # (c) odrviewer headless on straight_500m
    c = _smoke_cmd([ODRVIEWER_EXE, "--odr", _abs("resources/xodr/straight_500m.xodr"),
                    "--headless", "--density", "0", "--duration", "0.5",
                    "--logfile_path", log("smoke_odrviewer_log.txt")])
    results.append(("odrviewer --odr", c))
    return results


def _smoke_cmd(cmd: list, ok_extra=None) -> tuple:
    if not os.path.exists(cmd[0]):
        return (False, f"missing exe: {_rel(cmd[0])}")
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=120, cwd=_REPO_ROOT)
        rc = proc.returncode
        ok = (rc == 0)
        if ok and ok_extra is not None:
            ok = bool(ok_extra())
        tail = (proc.stderr or proc.stdout or b"").decode("utf-8", "replace").strip().splitlines()
        tail = tail[-1] if tail else ""
        return (ok, f"exit={rc}" + (f" | {tail[:80]}" if not ok else ""))
    except subprocess.TimeoutExpired:
        return (False, "TIMEOUT")


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------
def _tally(rows: list) -> dict:
    t = {PASS: 0, FAIL: 0, XFAIL: 0, XPASS: 0, SKIP: 0}
    for r in rows:
        t[r["status"]] = t.get(r["status"], 0) + 1
    return t


def _print_layer(name: str, rows: list) -> None:
    t = _tally(rows)
    print(f"\n=== Layer: {name} ===")
    print(f"  {PASS}={t[PASS]} {FAIL}={t[FAIL]} {XFAIL}={t[XFAIL]} {XPASS}={t[XPASS]} {SKIP}={t[SKIP]} (n={len(rows)})")
    for r in rows:
        if r["status"] in (FAIL, XPASS):
            g = r.get("golden", "")
            print(f"    {r['status']:<6} {r.get('id', r['path'])}  {r.get('detail','')}  {('golden='+g) if g else ''}")


def write_reports(report_dir: str, profile: str, layers: dict, matrix_res, smoke_res, audit_selftest,
                  fork_drift=None, core_census=None) -> None:
    os.makedirs(report_dir, exist_ok=True)
    summary = {name: _tally(rows) for name, rows in layers.items()}
    jreport = {
        "profile": profile,
        "summary": summary,
        "audit_selftest": audit_selftest,
        "layers": {name: [{k: v for k, v in r.items() if k not in ("_log",)} for r in rows]
                   for name, rows in layers.items()},
    }
    if fork_drift is not None:
        jreport["fork_drift"] = {"ok": fork_drift["ok"], "odr_lines": fork_drift["odr_lines"],
                                 "summary": fork_drift["summary"]}
    if core_census is not None:
        jreport["core_census"] = {"ok": core_census["ok"], "summary": core_census["summary"],
                                  "failures": core_census["failures"]}
    if matrix_res is not None:
        jreport["matrix"] = {"ok": matrix_res[0], "fails": matrix_res[2]}
    if smoke_res is not None:
        jreport["smoke"] = [{"name": n, "ok": ok, "detail": d} for (n, (ok, d)) in
                            [(n, r) for (n, r) in smoke_res]]
    _write_json(os.path.join(report_dir, "conformance_report.json"), jreport)

    # Markdown
    md = ["# OpenDRIVE conformance report", "",
          f"- profile: **{profile}**", f"- audit self-test: **{'PASS' if audit_selftest else 'FAIL'}**"]
    if fork_drift is not None:
        md.append(f"- {fork_drift['summary']}")
    if core_census is not None:
        md.append(f"- {core_census['summary']}")
    md.append("")
    for name, rows in layers.items():
        t = _tally(rows)
        md.append(f"## Layer: {name}")
        md.append("")
        md.append(f"`{PASS}={t[PASS]} {FAIL}={t[FAIL]} {XFAIL}={t[XFAIL]} {XPASS}={t[XPASS]} {SKIP}={t[SKIP]}` (n={len(rows)})")
        md.append("")
        bad = [r for r in rows if r["status"] in (FAIL, XPASS)]
        if bad:
            md.append("| status | id | detail | golden |")
            md.append("|---|---|---|---|")
            for r in bad:
                md.append(f"| {r['status']} | {r.get('id', r['path'])} | {r.get('detail','')} | {r.get('golden','')} |")
            md.append("")
    if matrix_res is not None:
        md.append("## Matrix")
        md.append("")
        md.append(f"ok: **{matrix_res[0]}**")
        md.append("")
        md.append("```")
        md += matrix_res[1]
        md.append("```")
        md.append("")
    if smoke_res is not None:
        md.append("## Smoke")
        md.append("")
        for (n, (ok, d)) in smoke_res:
            md.append(f"- {'PASS' if ok else 'FAIL'} `{n}` -- {d}")
        md.append("")
    with open(os.path.join(report_dir, "conformance_report.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(md) + "\n")


# ---------------------------------------------------------------------------
# Entry list assembly
# ---------------------------------------------------------------------------
def _assemble(manifest: dict, only: str):
    """Build the schema/rm/osi entry lists with normalized expected values."""
    control, fixtures = [], []
    for e in manifest.get("control_set", []):
        exp = e.get("expected", {})
        control.append({
            "id": e["path"], "path": e["path"], "kind": "control_set",
            "expected_schema": _norm_expect(exp.get("schema", "pass")),
            "expected_rm": _norm_expect(exp.get("rm_init", "pass")),
        })
    for fx in manifest.get("fixtures", []):
        exp = fx.get("expected", {})
        fixtures.append({
            "id": fx["id"], "path": fx["path"], "kind": "fixture",
            "expected_schema": _norm_expect(exp.get("schema", "pass")),
            "expected_rm": _norm_expect(exp.get("rm_init", "pass")),
            "requires": fx.get("requires") or [],
            "expected_unsupported": fx.get("expected_unsupported"),
            "expected_unsupported_entries": fx.get("expected_unsupported_entries"),
            # P2/P3: fixtures may opt into the OSI layer (manifest `osi: true`); optionally with the
            # zero-TYPE_UNKNOWN lane-classification acceptance check (`osi_expect_no_unknown: true`).
            "osi": bool(fx.get("osi")),
            "osi_expect_no_unknown": bool(fx.get("osi_expect_no_unknown")),
            # P5: opt-in OSI content checks for the crossPath fixtures (acceptance i / breakage #3,#4).
            "osi_expect_no_intersection_lane": bool(fx.get("osi_expect_no_intersection_lane")),
            "osi_expect_lane_type_sidewalk_min": fx.get("osi_expect_lane_type_sidewalk_min"),
            "osi_expect_stationary_min": fx.get("osi_expect_stationary_min"),
            # P6 S0: opt-out for maps whose MoveAlongS walk is nondeterministic by upstream
            # design (random equal-angle tie-break on geometrically-parallel connections).
            "motion_nondeterministic": bool(fx.get("motion_nondeterministic")),
        })
    if only:
        control = [e for e in control if only in e["id"] or only in e["path"]]
        fixtures = [e for e in fixtures if only in e["id"] or only in e["path"]]
    return control, fixtures


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="3-layer OpenDRIVE conformance harness (plan P0).")
    ap.add_argument("--profile", choices=["quick", "full"], default="quick")
    ap.add_argument("--update-golden", action="store_true")
    ap.add_argument("--check-matrix", action="store_true")
    ap.add_argument("--only", metavar="SUBSTR", default="")
    ap.add_argument("--dll", default=DEFAULT_DLL, help="GT_esminiLib.dll (OSI layer)")
    ap.add_argument("--rmdll", default=DEFAULT_RMDLL, help="esminiRMLib.dll (RM layer)")
    ap.add_argument("--report-dir", default=DEFAULT_REPORT_DIR)
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument(
        "--layers", metavar="L1[,L2,...]", default="",
        help="restrict to a comma-separated subset of {schema,rm,motion,osi}. Default: derived "
             "from --profile (quick=schema,rm,motion; full=schema,rm,motion,osi). Use e.g. "
             "'--layers schema --check-matrix' to run the schema layer only (no DLLs), "
             "the CI-friendly path that tolerates absent build tree / ASAM zips.",
    )
    args = ap.parse_args(argv)

    # Resolve the active layer set. Empty --layers keeps the profile behavior verbatim;
    # an explicit list intersects with the profile so 'osi' still requires --profile full.
    profile_layers = ["schema", "rm", "motion"] + (["osi"] if args.profile == "full" else [])
    if args.layers.strip():
        requested = [x.strip().lower() for x in args.layers.split(",") if x.strip()]
        unknown = [x for x in requested if x not in ("schema", "rm", "motion", "osi")]
        if unknown:
            ap.error("--layers: unknown layer(s) %s (choose from schema, rm, motion, osi)" % unknown)
        active_layers = [x for x in profile_layers if x in requested]
    else:
        active_layers = profile_layers

    os.makedirs(WORK_DIR, exist_ok=True)
    manifest = load_manifest()

    audit_selftest = _selftest_audit()
    if not audit_selftest:
        print("WARNING: audit self-test FAILED (marker counting mechanism broken)", file=sys.stderr)

    # Fork-drift + core-census checks (pure text, no DLLs -- run in every profile / layer subset).
    fork_drift = run_fork_drift()
    core_census = run_core_census()

    control, fixtures = _assemble(manifest, args.only)

    layers = {}
    matrix_res = None
    smoke_res = None
    exit_bad = 0

    # --- Layer 1: schema (control_set + all fixtures) ---
    if "schema" in active_layers:
        schema_rows = layer_schema(control + fixtures)
        layers["schema"] = schema_rows

    # --- Layer 2: rm (control_set + every fixture) ---
    # Fixtures whose expected rm_init is pass get a golden; expected-fail fixtures are still
    # probed so the abort/degradation is frozen as an XFAIL of the LOAD comparison (no golden).
    # asam_zips fixtures that are absent are skipped inside layer_rm.
    if "rm" in active_layers:
        rm_rows = layer_rm(list(control) + list(fixtures), args.rmdll, args.update_golden)
        layers["rm"] = rm_rows

    # --- Layer 2b: motion (same universe as rm; expected-fail rm entries SKIP) ---
    if "motion" in active_layers:
        motion_rows = layer_motion(list(control) + list(fixtures), args.rmdll, args.update_golden)
        layers["motion"] = motion_rows

    # --- Layer 3: osi (control_set + manifest `osi: true` fixtures, profile full) ---
    if "osi" in active_layers:
        osi_py = detect_osi_interpreter()
        if osi_py is None:
            print("\nNOTICE: OSI layer SKIPPED -- no interpreter with osi3+protobuf found "
                  f"(tried: {_rel(sys.executable)}, {_rel(_WEB_VENV_PY)}). "
                  "Install osi3 or use the web venv to enable layer 3.", file=sys.stderr)
        else:
            print(f"\nOSI worker interpreter: {_rel(osi_py)}")
            osi_entries = list(control) + [f for f in fixtures if f.get("osi")]
            osi_rows = layer_osi(osi_entries, args.dll, args.update_golden, osi_py, args.rmdll)
            layers["osi"] = osi_rows

    # --- Matrix ---
    if args.check_matrix:
        matrix_res = check_matrix(manifest)

    # --- Smoke ---
    if args.smoke:
        smoke_res = run_smoke()

    # --- Print ---
    for name, rows in layers.items():
        _print_layer(name, rows)
        t = _tally(rows)
        exit_bad += t[FAIL] + t[XPASS]

    if not audit_selftest:
        exit_bad += 1

    # --- Fork drift + core census (always) ---
    print("\n=== Fork drift / core census ===")
    print("  " + fork_drift["summary"])
    if not fork_drift["ok"]:
        for b in fork_drift["unattributed"]:
            print(f"    UNATTRIBUTED  fork L{b['fork_span'][0]}-{b['fork_span'][1]}  {b.get('sample','')}")
        exit_bad += 1
    print("  " + core_census["summary"])
    if not core_census["ok"]:
        for f in core_census["failures"]:
            print(f"    CENSUS  {f}")
        exit_bad += 1

    if matrix_res is not None:
        print("\n=== Matrix check ===")
        for line in matrix_res[1]:
            print(line)
        if not matrix_res[0]:
            print("  MATRIX FAILURES:")
            for f in matrix_res[2]:
                print("    -", f)
            exit_bad += len(matrix_res[2])
        else:
            print("  matrix OK (no empty rows)")

    if smoke_res is not None:
        print("\n=== Smoke ===")
        for (n, (ok, d)) in smoke_res:
            print(f"  {'PASS' if ok else 'FAIL'}  {n}  {d}")
            if not ok:
                exit_bad += 1

    write_reports(args.report_dir, args.profile, layers, matrix_res, smoke_res, audit_selftest,
                  fork_drift, core_census)

    print("\n" + ("=" * 60))
    total = {PASS: 0, FAIL: 0, XFAIL: 0, XPASS: 0, SKIP: 0}
    for rows in layers.values():
        for k, v in _tally(rows).items():
            total[k] += v
    print(f"TOTAL  {PASS}={total[PASS]} {FAIL}={total[FAIL]} {XFAIL}={total[XFAIL]} "
          f"{XPASS}={total[XPASS]} {SKIP}={total[SKIP]}")
    print(f"Report: {_rel(os.path.join(args.report_dir, 'conformance_report.md'))}")
    if exit_bad == 0:
        print("RESULT: OK (no FAIL / XPASS)")
        return 0
    print(f"RESULT: NOT OK ({exit_bad} failing conditions)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
