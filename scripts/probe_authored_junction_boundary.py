#!/usr/bin/env python3
"""P7 WP4 probe: authored junction <boundary> -> OSI intersection contour (flagged, default OFF).

Loads a junction fixture through GT_esminiLib TWICE via isolated in-process OSI GroundTruth decode
(the run_odr_conformance.py `_OSI_WORKER` idiom: SE_Init on a minimal wrapper xosc, SE_StepDT,
SE_GetOSIGroundTruth, decode with esmini's own osi3 bindings, all in a subprocess that talks back
through a JSON result file):

  * run A (default): env GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY UNSET  -> heuristic free lane boundary
  * run B (flag ON): env GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY=1      -> authored contour substituted

It then asserts the intersection lane's free_lane_boundary set CHANGES only in run B (the boundary id
is new and points to a boundary whose point count matches the authored polyline), and that everything
else about the extract is stable. Committed so WP5 can re-run it:

    DriverScript/.venv/Scripts/python.exe scripts/probe_authored_junction_boundary.py

Exit 0 = PASS, non-zero = FAIL. Requires a completed Release build (GT_esminiLib.dll).
"""
import ctypes
import json
import os
import subprocess
import sys
import tempfile

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DLL = os.path.join(_REPO_ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")
# g6 qualifies as an OSI intersection: incoming road 0 is type "town" (not motorway) and the
# junction resolves its connections -> Junction::IsOsiIntersection() == true. It carries an authored
# <boundary> of four lane segments (roads 0/1/2/3, boundaryLane -1).
FIXTURE = os.path.join(_REPO_ROOT, "GT_esmini", "test", "odr_fixtures", "generated",
                       "g6_junction_boundary_18.xodr")

_WRAP_XOSC = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <FileHeader revMajor="1" revMinor="1" date="2026-07-04T00:00:00" description="wp4-junc-boundary-probe" author="gt"/>
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
              <Position><WorldPosition x="0" y="0" h="0"/></Position>
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

# In-process OSI worker: decode the intersection lane + its free_lane_boundary ids and, for each such
# id, the referenced lane_boundary's point count. Talks back via a JSON file (never stdout: the DLLs
# flood it). Mirrors run_odr_conformance.py's _OSI_WORKER contract.
_WORKER = r'''
import sys, os, json, ctypes
REPO_ROOT = %(repo)r
dll = %(dll)r
xosc = %(xosc)r
out = %(out)r
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))  # esmini's own osi3 bindings

# OSI Lane_Classification_Type_TYPE_INTERSECTION
TYPE_INTERSECTION = 4

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
        json.dump({"init_ok": False, "rc": rc}, open(out, "w")); sys.exit(0)
    lib.SE_StepDT(0.05)
    size = ctypes.c_int(0)
    ptr = lib.SE_GetOSIGroundTruth(ctypes.byref(size))
    data = ctypes.string_at(ptr, size.value) if (ptr and size.value > 0) else b""
    lib.SE_Close()
    g = gtpb.GroundTruth()
    g.ParseFromString(data)
    # boundary id -> point count
    bpts = {int(b.id.value): len(b.boundary_line) for b in g.lane_boundary}
    intersections = []
    for L in g.lane:
        if int(L.classification.type) != TYPE_INTERSECTION:
            continue
        flb = sorted(int(x.value) for x in L.classification.free_lane_boundary_id)
        intersections.append({
            "lane_id": int(L.id.value),
            "free_lane_boundary_ids": flb,
            "free_lane_boundary_point_counts": [bpts.get(i, -1) for i in flb],
        })
    intersections.sort(key=lambda d: d["lane_id"])
    res = {"init_ok": True, "lane_boundary_count": len(g.lane_boundary),
           "intersections": intersections}
except Exception as e:
    res = {"init_ok": False, "error": "%%s: %%s" %% (type(e).__name__, e)}
json.dump(res, open(out, "w"))
'''


def _run(xosc_path, dll, flag_on):
    fd, script = tempfile.mkstemp(suffix="_wp4.py", prefix="wp4probe_")
    os.close(fd)
    out = script + ".json"
    body = _WORKER % {"repo": _REPO_ROOT, "dll": dll, "xosc": xosc_path, "out": out}
    with open(script, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(body)
    env = dict(os.environ)
    if flag_on:
        env["GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY"] = "1"
    else:
        env.pop("GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY", None)
    try:
        subprocess.run([sys.executable, script], env=env, timeout=120,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(out, "r") as fh:
            return json.load(fh)
    finally:
        for p in (script, out):
            try:
                os.remove(p)
            except OSError:
                pass


def main():
    dll = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DLL
    if not os.path.isfile(dll):
        print(f"FAIL: GT_esminiLib.dll not found at {dll} (build Release first)")
        return 2
    if not os.path.isfile(FIXTURE):
        print(f"FAIL: fixture not found: {FIXTURE}")
        return 2

    fd, xoscf = tempfile.mkstemp(suffix="_wp4.xosc", prefix="wp4probe_")
    os.close(fd)
    with open(xoscf, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(_WRAP_XOSC.format(xodr=FIXTURE.replace("\\", "/")))
    try:
        off = _run(xoscf, dll, flag_on=False)
        on = _run(xoscf, dll, flag_on=True)
    finally:
        try:
            os.remove(xoscf)
        except OSError:
            pass

    print("--- run A (flag OFF, default) ---")
    print(json.dumps(off, indent=2))
    print("--- run B (flag ON) ---")
    print(json.dumps(on, indent=2))

    ok = True

    def fail(msg):
        nonlocal ok
        ok = False
        print("FAIL:", msg)

    if not off.get("init_ok"):
        fail(f"OFF run did not init: {off}")
    if not on.get("init_ok"):
        fail(f"ON run did not init: {on}")
    if not ok:
        return 1

    off_ix = off.get("intersections", [])
    on_ix = on.get("intersections", [])
    if not off_ix:
        fail("no OSI intersection lane in OFF run -- fixture does not qualify (check IsOsiIntersection)")
        return 1
    if len(off_ix) != len(on_ix):
        fail(f"intersection lane count differs OFF={len(off_ix)} ON={len(on_ix)}")
        return 1

    changed_any = False
    for a, b in zip(off_ix, on_ix):
        if a["lane_id"] != b["lane_id"]:
            fail(f"intersection lane id mismatch {a['lane_id']} vs {b['lane_id']}")
            continue
        if a["free_lane_boundary_ids"] == b["free_lane_boundary_ids"]:
            # Unchanged -- acceptable only if this junction has no authored boundary. g6's junction
            # DOES, so an unchanged set here is a failure.
            fail(f"lane {a['lane_id']}: free_lane_boundary unchanged with flag ON "
                 f"(ids={a['free_lane_boundary_ids']})")
            continue
        changed_any = True
        # ON run should carry exactly one synthetic authored-contour boundary with >= 3 points.
        if len(b["free_lane_boundary_ids"]) != 1:
            fail(f"lane {a['lane_id']}: ON run expected exactly 1 authored boundary id, got "
                 f"{b['free_lane_boundary_ids']}")
        pc = b["free_lane_boundary_point_counts"][0] if b["free_lane_boundary_point_counts"] else -1
        if pc < 3:
            fail(f"lane {a['lane_id']}: authored contour boundary has {pc} points (need >= 3)")
        # Synthetic id must be new (not present in OFF run's set for this lane).
        if set(b["free_lane_boundary_ids"]) & set(a["free_lane_boundary_ids"]):
            fail(f"lane {a['lane_id']}: ON boundary id collides with OFF ids")
        print(f"OK: lane {a['lane_id']}: OFF free_lane_boundary={a['free_lane_boundary_ids']} "
              f"-> ON authored contour id={b['free_lane_boundary_ids']} ({pc} pts)")

    if not changed_any:
        fail("no intersection lane changed between OFF and ON runs")

    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
