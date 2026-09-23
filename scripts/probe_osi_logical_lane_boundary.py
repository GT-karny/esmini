#!/usr/bin/env python3
"""LogicalLaneBoundary probe (spine-work:osi-logical-lane S3, design 2-3).

Everything here is measured against a running binary. What only a real run can
answer:

  1. COVERAGE. Each LogicalLane's left_boundary_id[] and right_boundary_id[] must
     cover [start_s, end_s] "without gap or overlap", in ascending s. A model that
     emits one boundary per lane side and stops has a plausible shape and a hole
     wherever the road marking changes inside a lane section.

  2. CONTACT. "Consecutive boundaries must share a point: the last point of the
     previous boundary must be identical to the first point of the next boundary."
     Measured in world coordinates, not in s.

  3. LATERAL DEVIATION, MEASURED WHERE IT WAS NOT CONSTRUCTED. The proto allows
     5cm laterally and 2cm vertically against the ideal line. The emitted polyline
     is built by bisecting until a HALF-budget is met, so measuring it at its own
     construction s values would only confirm the construction. This samples the
     ideal edge at s values chosen independently of that grid and through a
     SECOND DLL -- esminiRMLib, its own RoadManager -- and reports the distance to
     the emitted polyline. The worst case is reported with the road, lane and
     curvature it came from, because "the maximum is small" says nothing about
     whether the sharpest curve in the map was even visited.

  4. SHARING, AND ITS ONE EXCEPTION. Two lanes either side of an edge must name
     the SAME boundary id. This is the evidence S2's lateral adjacency can be
     recovered from boundaries alone (design 8-alpha), and it is the one property
     a per-lane construction would silently fail while producing identical
     geometry. The proto carves out exactly one exception -- "if two lanes have
     different Z heights (e.g. a driving lane is beside a sidewalk ...) then these
     lanes cannot share a boundary" -- so a pair that does NOT share must instead
     coincide in XY and differ in Z. Both outcomes are required to occur somewhere
     in the fixture set: a build that never split would pass the first branch
     everywhere, and one that always split would pass the second.

  5. PHYSICAL REFERENCE CLOSURE. Every physical_boundary_id must exist in
     GroundTruth.lane_boundary[].

  6. FLAG OFF. GT_OSI_LOGICAL_LANE unset -> logical_lane_boundary_size() == 0.

    DriverScript/.venv/Scripts/python.exe scripts/probe_osi_logical_lane_boundary.py

Exit 0 = PASS. Requires a completed Release build (GT_esminiLib.dll, esminiRMLib.dll).
Output: test_results/osi_logical_lane/boundary_probe.json (+ a table on stdout).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DLL = os.path.join(_REPO_ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")
DEFAULT_RM_DLL = os.path.join(_REPO_ROOT, "build", "GT_esmini", "Release", "esminiRMLib.dll")
OUT_DIR = os.path.join(_REPO_ROOT, "test_results", "osi_logical_lane")

ENV_FLAG = "GT_OSI_LOGICAL_LANE"

# The same five networks the size probe uses, so the boundary counts here and the
# payload numbers there describe one set of maps.
#   e6mini              straight-ish motorway, road marks on every lane
#   fabriksgatan        small urban junction, tight connecting-road curvature
#   multi_intersections 63 roads / 76 connecting-road lanes -- the stress case
#   soderleden          motorway with ramps
#   highway_merge_split lane sections that add and drop lanes
FIXTURES = [
    ("e6mini", "resources/xodr/e6mini.xodr"),
    ("fabriksgatan", "resources/xodr/fabriksgatan.xodr"),
    ("multi_intersections", "resources/xodr/multi_intersections.xodr"),
    ("soderleden", "resources/xodr/soderleden.xodr"),
    ("highway_merge_split", "resources/xodr/highway_example_with_merge_and_split.xodr"),
]

# osi_logicallane.proto, LogicalLaneBoundary.boundary_line.
SPEC_LATERAL_M = 0.05
SPEC_Z_M = 0.02

# How many independent samples per boundary the deviation check draws. Irrational
# fractions of the span, so a sample can only coincide with a construction point by
# accident rather than by construction.
DEVIATION_SAMPLES = 23


_WORKER = r'''
import sys, os, json, math, ctypes
REPO_ROOT = %(repo)r
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))
sys.path.insert(0, os.path.join(REPO_ROOT, "GT_esmini", "scripts"))
sys.path.insert(0, os.path.join(REPO_ROOT, "GT_esmini", "scripts", "verification"))

out       = %(out)r
osi_file  = %(osi_file)r
fixtures  = %(fixtures)r
dll       = %(dll)r
rm_dll    = %(rm_dll)r
n_samples = %(n_samples)d

# A minimal scenario per xodr: the static GroundTruth is what is under test, and it
# is written on the first step regardless of what drives.
XOSC = """<?xml version="1.0"?>
<OpenSCENARIO>
  <FileHeader revMajor="1" revMinor="2" date="2026-09-24T00:00:00" description="boundary probe" author="GT"/>
  <ParameterDeclarations/><CatalogLocations/>
  <RoadNetwork><LogicFile filepath="%%s"/></RoadNetwork>
  <Entities><ScenarioObject name="Ego"><Vehicle name="car" vehicleCategory="car">
    <ParameterDeclarations/>
    <Performance maxSpeed="60" maxDeceleration="10" maxAcceleration="5"/>
    <BoundingBox><Center x="1.4" y="0.0" z="0.75"/><Dimensions width="2.0" length="5.0" height="1.5"/></BoundingBox>
    <Axles><FrontAxle maxSteering="0.5" wheelDiameter="0.6" trackWidth="1.8" positionX="2.8" positionZ="0.3"/>
           <RearAxle maxSteering="0.0" wheelDiameter="0.6" trackWidth="1.8" positionX="0.0" positionZ="0.3"/></Axles>
    <Properties/></Vehicle></ScenarioObject></Entities>
  <Storyboard><Init><Actions><Private entityRef="Ego"><PrivateAction><TeleportAction><Position>
    <LanePosition roadId="%%s" laneId="%%s" offset="0" s="1.0"/>
  </Position></TeleportAction></PrivateAction></Private></Actions></Init>
  <Story name="s"><Act name="a"><ManeuverGroup maximumExecutionCount="1" name="m"><Actors selectTriggeringEntities="false"/></ManeuverGroup>
    <StartTrigger><ConditionGroup><Condition name="c" delay="0" conditionEdge="none">
      <ByValueCondition><SimulationTimeCondition value="0" rule="greaterThan"/></ByValueCondition></Condition></ConditionGroup></StartTrigger>
  </Act></Story>
  <StopTrigger><ConditionGroup><Condition name="stop" delay="0" conditionEdge="none">
    <ByValueCondition><SimulationTimeCondition value="1.0" rule="greaterThan"/></ByValueCondition></Condition></ConditionGroup></StopTrigger>
  </Storyboard>
</OpenSCENARIO>
"""


def seg_distance(px, py, pz, ax, ay, az, bx, by, bz):
    """Distance from P to segment AB, split into the XY part and the Z part.

    The two are reported separately because the proto's budgets are different
    numbers (5cm lateral, 2cm vertical) and collapsing them into one 3D distance
    would let a Z error hide inside the larger lateral allowance."""
    dx, dy = bx - ax, by - ay
    den = dx * dx + dy * dy
    u = 0.0 if den <= 0.0 else ((px - ax) * dx + (py - ay) * dy) / den
    u = max(0.0, min(1.0, u))
    cx, cy, cz = ax + u * dx, ay + u * dy, az + u * (bz - az)
    return math.hypot(px - cx, py - cy), abs(pz - cz)


res = {"fixtures": [], "error": None}
try:
    import osi3.osi_groundtruth_pb2 as gtpb
    from gt_lib import GtLib
    from rm_lib import EsminiRMLib

    gt = GtLib(dll_path=dll)

    for fx_idx, (label, xodr) in enumerate(fixtures):
        r = {"label": label, "xodr": xodr, "init_rc": None, "error": None,
             "logical_lanes": 0, "boundaries": 0, "boundary_points": 0,
             "physical_ids": 0, "physical_dangling": 0,
             "lanes_checked": 0, "coverage_bad": [], "contact_bad": [],
             "contact_max_m": 0.0, "contact_pairs": 0,
             "shared_pairs": 0, "shared_bad": [], "shared_identical": 0,
             "height_split_pairs": 0, "height_split_dz_max": 0.0, "height_split_examples": [],
             "dev_samples": 0, "dev_max_xy": 0.0, "dev_max_z": 0.0, "dev_worst": [], "dev_worst_z": [],
             "dev_skipped": 0, "s_not_monotonic": 0, "rm_init_rc": None,
             "curv_max_seen": 0.0}

        # An entry lane for the teleport: first road, its first negative driving lane.
        rm = None
        try:
            rm = EsminiRMLib(rm_dll)
            r["rm_init_rc"] = rm.Init(os.path.join(REPO_ROOT, xodr))
        except Exception as exc:                      # noqa: BLE001
            r["error"] = "rm init failed: %%s" %% exc
            res["fixtures"].append(r)
            continue
        if r["rm_init_rc"] != 0:
            r["error"] = "RM_Init rc=%%s" %% r["rm_init_rc"]
            res["fixtures"].append(r)
            continue

        entry_road, entry_lane = None, None
        for ri in range(rm.GetNumberOfRoads()):
            rid = rm.GetIdOfRoadFromIndex(ri)
            n = rm.GetRoadNumberOfDrivableLanes(rid, 1.0)
            for li in range(n):
                # (rc, value) -- rm_lib returns the out-parameter alongside the rc.
                lrc, lid = rm.GetDrivableLaneIdByIndex(rid, li, 1.0)
                if lrc == 0 and lid < 0:
                    entry_road, entry_lane = rid, lid
                    break
            if entry_road is not None:
                break
        if entry_road is None:
            r["error"] = "no drivable lane found"
            rm.Close()
            res["fixtures"].append(r)
            continue

        xosc_path = os.path.join(os.path.dirname(out), "gt_ll_boundary_%%d.xosc" %% fx_idx)
        with open(xosc_path, "w") as fh:
            fh.write(XOSC %% (os.path.join(REPO_ROOT, xodr).replace("\\", "/"), entry_road, entry_lane))

        rc = gt.init_with_args(["--osc", xosc_path, "--headless", "--fixed_timestep", "0.05", "--disable_stdout"])
        r["init_rc"] = rc
        if rc != 0:
            rm.Close()
            res["fixtures"].append(r)
            continue
        gt.set_osi_frequency(1)
        this_osi = osi_file + ".%%d" %% fx_idx
        gt.lib.SE_EnableOSIFile(this_osi.encode("utf-8"))
        gt.step(0.05)
        gt.lib.SE_FlushOSIFile()

        g = gtpb.GroundTruth()
        with open(this_osi, "rb") as fh:
            raw = fh.read()
        rec_len = int.from_bytes(raw[0:4], "little")
        g.ParseFromString(raw[4:4 + rec_len])
        r["static_record_bytes"] = rec_len

        bnd = {b.id.value: b for b in g.logical_lane_boundary}
        r["boundaries"] = len(bnd)
        r["logical_lanes"] = len(g.logical_lane)
        r["boundary_points"] = sum(len(b.boundary_line) for b in g.logical_lane_boundary)
        physical_ids = {lb.id.value for lb in g.lane_boundary}

        for b in g.logical_lane_boundary:
            for pid in b.physical_boundary_id:
                r["physical_ids"] += 1
                if pid.value not in physical_ids:
                    r["physical_dangling"] += 1
            prev_s = None
            for pt in b.boundary_line:
                if prev_s is not None and pt.s_position < prev_s - 1e-9:
                    r["s_not_monotonic"] += 1
                prev_s = pt.s_position

        # Logical lanes keyed by their OpenDRIVE address, so sharing between
        # neighbours can be checked by lane id rather than by geometry.
        by_addr = {}
        for ll in g.logical_lane:
            road_id, road_s, lane_id = None, None, None
            for sr in ll.source_reference:
                for ident in sr.identifier:
                    if ident.startswith("road_id:"):
                        road_id = int(ident.split(":", 1)[1])
                    elif ident.startswith("road_s:"):
                        road_s = float(ident.split(":", 1)[1])
                    elif ident.startswith("lane_id:"):
                        lane_id = int(ident.split(":", 1)[1])
            if road_id is None or lane_id is None:
                continue
            by_addr[(road_id, round(road_s, 6), lane_id)] = ll

        handle = rm.CreatePosition()

        for (road_id, road_s, lane_id), ll in sorted(by_addr.items()):
            r["lanes_checked"] += 1
            for side, ids in (("left", list(ll.left_boundary_id)), ("right", list(ll.right_boundary_id))):
                if not ids:
                    r["coverage_bad"].append([road_id, road_s, lane_id, side, "empty"])
                    continue
                spans = []
                ok = True
                for bid in ids:
                    b = bnd.get(bid.value)
                    if b is None or len(b.boundary_line) < 2:
                        r["coverage_bad"].append([road_id, road_s, lane_id, side, "missing/short"])
                        ok = False
                        break
                    spans.append((b.boundary_line[0].s_position, b.boundary_line[-1].s_position, b))
                if not ok:
                    continue
                # 1. COVERAGE: first starts at start_s, last ends at end_s, and the
                #    joins are exact.
                if abs(spans[0][0] - ll.start_s) > 1e-6 or abs(spans[-1][1] - ll.end_s) > 1e-6:
                    r["coverage_bad"].append([road_id, road_s, lane_id, side,
                                              "ends %%.6f..%%.6f vs lane %%.6f..%%.6f"
                                              %% (spans[0][0], spans[-1][1], ll.start_s, ll.end_s)])
                for k in range(1, len(spans)):
                    if abs(spans[k][0] - spans[k - 1][1]) > 1e-6:
                        r["coverage_bad"].append([road_id, road_s, lane_id, side,
                                                  "gap/overlap at %%.6f -> %%.6f" %% (spans[k - 1][1], spans[k][0])])
                    # 2. CONTACT, in world coordinates.
                    p = spans[k - 1][2].boundary_line[-1].position
                    q = spans[k][2].boundary_line[0].position
                    d = math.sqrt((p.x - q.x) ** 2 + (p.y - q.y) ** 2 + (p.z - q.z) ** 2)
                    r["contact_pairs"] += 1
                    r["contact_max_m"] = max(r["contact_max_m"], d)
                    if d > 1e-6:
                        r["contact_bad"].append([road_id, road_s, lane_id, side, round(d, 6)])

            # 4. SHARING: the boundary on the side facing the centre must be the very
            #    same id as the neighbour's boundary on the side facing outwards.
            inner_neighbour = lane_id - (1 if lane_id > 0 else -1)
            if inner_neighbour != 0:
                nb = by_addr.get((road_id, road_s, inner_neighbour))
                if nb is not None:
                    mine = [i.value for i in (ll.right_boundary_id if lane_id > 0 else ll.left_boundary_id)]
                    theirs = [i.value for i in (nb.left_boundary_id if lane_id > 0 else nb.right_boundary_id)]
                    r["shared_pairs"] += 1
                    if mine == theirs:
                        r["shared_identical"] += 1
                    else:
                        # The proto's curb exception, or a defect. Told apart by
                        # geometry: the two must trace the same XY line and be apart
                        # in Z. Compared polyline-to-polyline rather than point by
                        # point, because the two are refined independently and need
                        # not end up with the same number of points.
                        max_xy, max_dz = 0.0, 0.0
                        ok = True
                        for a_id, b_id in zip(mine, theirs):
                            ba, bb = bnd.get(a_id), bnd.get(b_id)
                            if ba is None or bb is None or len(bb.boundary_line) < 2:
                                ok = False
                                break
                            for pt in ba.boundary_line:
                                best = None
                                for k in range(1, len(bb.boundary_line)):
                                    u, v = bb.boundary_line[k - 1].position, bb.boundary_line[k].position
                                    e = seg_distance(pt.position.x, pt.position.y, pt.position.z,
                                                     u.x, u.y, u.z, v.x, v.y, v.z)
                                    if best is None or e[0] < best[0]:
                                        best = e
                                if best is not None:
                                    max_xy = max(max_xy, best[0])
                                    max_dz = max(max_dz, best[1])
                        if ok and len(mine) == len(theirs) and max_xy <= 1e-6 and max_dz > 0.0:
                            r["height_split_pairs"] += 1
                            r["height_split_dz_max"] = max(r["height_split_dz_max"], max_dz)
                            r["height_split_examples"].append(
                                [road_id, road_s, lane_id, inner_neighbour, round(max_dz, 4)])
                        else:
                            r["shared_bad"].append([road_id, road_s, lane_id, inner_neighbour,
                                                    mine[:3], theirs[:3], round(max_xy, 6), round(max_dz, 6)])

        # 3. DEVIATION, measured through the OTHER DLL at independent s values.
        #
        # The ideal edge is rebuilt from RM's own lane widths: the outer edge of lane
        # k is its centre displaced by half its width, which is exactly where esmini
        # itself puts a lane boundary (SetLaneBoundaryPos). The centre edge (lane 0)
        # is lane 0's own centre. Neither value comes from the message under test.
        # boundary id -> (road, the lane that reads it, which of that lane's edges).
        # Recorded from BOTH sides: where the curb exception split an edge, the second
        # geometry is nobody's outer boundary and would otherwise go unmeasured.
        owner_of = {}
        for (road_id, road_s, lane_id), ll in by_addr.items():
            outer = ll.left_boundary_id if lane_id > 0 else ll.right_boundary_id
            inner = ll.right_boundary_id if lane_id > 0 else ll.left_boundary_id
            for bid in outer:
                owner_of.setdefault(bid.value, (road_id, lane_id, True))
            for bid in inner:
                owner_of.setdefault(bid.value, (road_id, lane_id, False))

        for bid, b in bnd.items():
            own = owner_of.get(bid)
            if own is None or len(b.boundary_line) < 2:
                r["dev_skipped"] += 1
                continue
            road_id, owner_lane, is_outer = own
            s0 = b.boundary_line[0].s_position
            s1 = b.boundary_line[-1].s_position
            if not (s1 > s0 + 1e-6):
                r["dev_skipped"] += 1
                continue
            for i in range(n_samples):
                # Irrational-ish spacing: a sample lands on a construction point only
                # by accident, never by construction.
                frac = ((i + 1) * 0.6180339887498949) %% 1.0
                sv = s0 + frac * (s1 - s0)
                if owner_lane == 0:
                    off = 0.0
                else:
                    wrc, w = rm.GetLaneWidthByRoadId(road_id, owner_lane, sv)
                    if wrc != 0:
                        r["dev_skipped"] += 1
                        continue
                    # The ideal edge: the lane centre displaced half a width to the
                    # requested side. This is where esmini itself puts a lane boundary
                    # (SetLaneBoundaryPos), and it carries the lane's own <height>.
                    off = (1.0 if is_outer else -1.0) * (-1.0 if owner_lane < 0 else 1.0) * w / 2.0
                if rm.SetLanePosition(handle, road_id, owner_lane, off, sv) != 0:
                    r["dev_skipped"] += 1
                    continue
                rc2, d = rm.GetPositionData(handle)
                if rc2 != 0:
                    r["dev_skipped"] += 1
                    continue
                # Curvature at the very sample, from the same DLL. "The maximum
                # deviation is small" is worth nothing unless the sharpest curve in
                # the map was among the places measured, and the only way to say that
                # is to carry the curvature alongside the error.
                _, li = rm.GetLaneInfo(handle, 0.0, 0)
                curv = abs(li.curvature)
                r["curv_max_seen"] = max(r["curv_max_seen"], curv)
                best_xy, best_z = None, None
                for k in range(1, len(b.boundary_line)):
                    a, c = b.boundary_line[k - 1].position, b.boundary_line[k].position
                    exy, ez = seg_distance(d.x, d.y, d.z, a.x, a.y, a.z, c.x, c.y, c.z)
                    if best_xy is None or exy < best_xy:
                        best_xy, best_z = exy, ez
                if best_xy is None:
                    continue
                r["dev_samples"] += 1
                r["dev_max_xy"] = max(r["dev_max_xy"], best_xy)
                r["dev_max_z"] = max(r["dev_max_z"], best_z)
                row = [int(road_id), int(owner_lane), "outer" if is_outer else "inner",
                       round(sv, 3), len(b.boundary_line), round(curv, 5)]
                r["dev_worst"].append([round(best_xy, 6)] + row)
                # Kept separately: the two budgets are different numbers, and a list
                # sorted by the lateral error never shows the worst vertical one.
                r["dev_worst_z"].append([round(best_z, 6)] + row)
            r["dev_worst"] = sorted(r["dev_worst"], reverse=True)[:5]
            r["dev_worst_z"] = sorted(r["dev_worst_z"], reverse=True)[:5]

        rm.DeletePosition(handle)
        rm.Close()
        gt.close()
        r["coverage_bad"] = r["coverage_bad"][:10]
        r["contact_bad"] = r["contact_bad"][:10]
        r["shared_bad"] = r["shared_bad"][:10]
        r["height_split_examples"] = r["height_split_examples"][:5]
        res["fixtures"].append(r)
except Exception as exc:                              # noqa: BLE001
    import traceback
    res["error"] = traceback.format_exc()

with open(out, "w") as fh:
    json.dump(res, fh)
'''


def _run_worker(dll, rm_dll, fixtures, flag_on, n_samples):
    """One subprocess per polarity, so the GT_OSI_LOGICAL_LANE latch is fresh."""
    out_fd, out_path = tempfile.mkstemp(suffix=".json")
    os.close(out_fd)
    src = _WORKER % {
        "repo": _REPO_ROOT,
        "out": out_path,
        "osi_file": os.path.join(tempfile.gettempdir(), "gt_ll_boundary_probe.osi"),
        "fixtures": fixtures,
        "dll": dll,
        "rm_dll": rm_dll,
        "n_samples": n_samples,
    }
    py_fd, py_path = tempfile.mkstemp(suffix=".py")
    with os.fdopen(py_fd, "w") as fh:
        fh.write(src)

    # The two polarities are "unset" and "0", NOT "1" and "unset": since S3 the
    # variable is an OPT-OUT, so leaving it unset is what exercises the default and
    # setting it to 0 is what exercises the escape hatch. Written the other way round,
    # both runs would come out ON and every OFF assertion below would pass vacuously.
    env = dict(os.environ)
    if flag_on:
        env.pop(ENV_FLAG, None)
    else:
        env[ENV_FLAG] = "0"

    proc = subprocess.run([sys.executable, py_path], env=env, capture_output=True, text=True,
                          cwd=_REPO_ROOT, timeout=3600)
    try:
        with open(out_path) as fh:
            result = json.load(fh)
    except Exception:  # noqa: BLE001
        result = {"fixtures": [], "error": "worker produced no JSON\n%s\n%s"
                  % (proc.stdout[-4000:], proc.stderr[-4000:])}
    result["worker_rc"] = proc.returncode
    for path in (out_path, py_path):
        try:
            os.unlink(path)
        except OSError:
            pass
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=DEFAULT_DLL)
    ap.add_argument("--rm-dll", default=DEFAULT_RM_DLL)
    ap.add_argument("--samples", type=int, default=DEVIATION_SAMPLES)
    args = ap.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)
    for path in (args.dll, args.rm_dll):
        if not os.path.exists(path):
            print("FAIL: no library at %s -- build Release first" % path)
            return 1

    failures = []
    report = {"dll": args.dll, "rm_dll": args.rm_dll, "samples_per_boundary": args.samples}

    def check(name, ok, detail=""):
        print("  [%s] %s%s" % ("PASS" if ok else "FAIL", name, (" -- " + detail) if detail else ""))
        if not ok:
            failures.append("%s: %s" % (name, detail))

    fixtures = [[label, xodr] for (label, xodr) in FIXTURES]

    print("== GT_OSI_LOGICAL_LANE unset (default ON) ==")
    on = _run_worker(args.dll, args.rm_dll, fixtures, True, args.samples)
    report["on"] = on
    if on.get("error"):
        check("worker_on", False, on["error"][-2000:])
        _write(report)
        return 1

    totals = {"shared_identical": 0, "height_split": 0}
    for r in on["fixtures"]:
        label = r["label"]
        print("  -- %s" % label)
        if r.get("error") or r["init_rc"] != 0:
            check("%s.init" % label, False, "%s (init rc=%s)" % (r.get("error"), r["init_rc"]))
            continue
        check("%s.boundaries_present" % label, r["boundaries"] > 0,
              "%d boundaries / %d points over %d logical lanes"
              % (r["boundaries"], r["boundary_points"], r["logical_lanes"]))
        if r["boundaries"] == 0:
            continue

        check("%s.coverage" % label, not r["coverage_bad"],
              "%d lanes checked, %d side(s) not covering [start_s, end_s] without gap%s"
              % (r["lanes_checked"], len(r["coverage_bad"]),
                 ("; first: %s" % r["coverage_bad"][0]) if r["coverage_bad"] else ""))
        check("%s.contact_points_match" % label, not r["contact_bad"],
              "%d consecutive-boundary joins, max separation %.3g m%s"
              % (r["contact_pairs"], r["contact_max_m"],
                 ("; first bad: %s" % r["contact_bad"][0]) if r["contact_bad"] else ""))
        check("%s.adjacent_lanes_share_a_boundary" % label, not r["shared_bad"],
              "%d neighbour pairs: %d share one id, %d split by height (max dz %.3f m, e.g. %s), "
              "%d neither%s"
              % (r["shared_pairs"], r["shared_identical"], r["height_split_pairs"],
                 r["height_split_dz_max"], r["height_split_examples"][:1], len(r["shared_bad"]),
                 ("; first bad: %s" % r["shared_bad"][0]) if r["shared_bad"] else ""))
        totals["shared_identical"] += r["shared_identical"]
        totals["height_split"] += r["height_split_pairs"]
        check("%s.physical_reference_closure" % label, r["physical_dangling"] == 0,
              "%d/%d physical_boundary_id resolve in lane_boundary[]"
              % (r["physical_ids"] - r["physical_dangling"], r["physical_ids"]))
        check("%s.s_monotonic" % label, r["s_not_monotonic"] == 0,
              "%d boundary points with decreasing s" % r["s_not_monotonic"])
        check("%s.lateral_deviation_within_spec" % label,
              r["dev_samples"] > 0 and r["dev_max_xy"] <= SPEC_LATERAL_M and r["dev_max_z"] <= SPEC_Z_M,
              "max %.4f m lateral / %.4f m vertical over %d independent samples "
              "(spec %.2f / %.2f; %d skipped); sharpest curvature sampled %.5f 1/m (R=%.1f m); "
              "worst lateral [err, road, lane, edge, s, pts, curv]: %s; worst vertical: %s"
              % (r["dev_max_xy"], r["dev_max_z"], r["dev_samples"], SPEC_LATERAL_M, SPEC_Z_M,
                 r["dev_skipped"], r["curv_max_seen"],
                 (1.0 / r["curv_max_seen"]) if r["curv_max_seen"] > 1e-9 else float("inf"),
                 r["dev_worst"][:2], r["dev_worst_z"][:2]))

    # Both branches of the sharing rule must be present in real data, or the check
    # above only ever exercised one of them.
    report["sharing_totals"] = totals
    check("sharing_both_outcomes_observed", totals["shared_identical"] > 0 and totals["height_split"] > 0,
          "%d neighbour pairs share one boundary id, %d are split because the lanes sit at "
          "different heights (the proto's only exception)" % (totals["shared_identical"], totals["height_split"]))

    print("== GT_OSI_LOGICAL_LANE=0 ==")
    off = _run_worker(args.dll, args.rm_dll, fixtures, False, 0)
    report["off"] = off
    if off.get("error"):
        check("worker_off", False, off["error"][-2000:])
    else:
        for r in off["fixtures"]:
            check("%s.no_boundaries_when_off" % r["label"],
                  r["boundaries"] == 0 and r["logical_lanes"] == 0,
                  "logical_lane_boundary[] = %d, logical_lane[] = %d" % (r["boundaries"], r["logical_lanes"]))

    _write(report)
    print()
    if failures:
        print("FAIL (%d):" % len(failures))
        for f in failures:
            print("  - " + f)
        return 1
    print("PASS -- report: %s" % os.path.join(OUT_DIR, "boundary_probe.json"))
    return 0


def _write(report):
    with open(os.path.join(OUT_DIR, "boundary_probe.json"), "w") as fh:
        json.dump(report, fh, indent=1)


if __name__ == "__main__":
    sys.exit(main())
