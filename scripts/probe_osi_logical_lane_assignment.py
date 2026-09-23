#!/usr/bin/env python3
"""LogicalLaneAssignment probe (spine-work:osi-logical-lane S2.5, design 2-6-1).

Everything here is measured against a running binary; nothing is taken from the
design document. What only a real run can answer:

  1. THE REFERENCE POINT (S2.5b). osi_common.proto puts BaseMoving.position at
     "the center (x,y,z) of the bounding box" and LogicalLaneAssignment.s_position
     at "the object reference point", so the assignment's ST must be the ST of the
     BOX CENTRE -- not of the entity origin, which esmini places at the rear axle,
     1.4 m behind it for the shipped catalogue car.

     Measured as a round trip through a SECOND DLL: esminiRMLib, its own RoadManager
     instance, converts (roadId, s_position, t_position) back to world XY, and the
     result is compared against moving_object.base.position, which the reporter
     wrote from a different code path (Position::GetOsiX/Y). Three numbers come out
     of the same loop:

       rt_floor   the INSTRUMENT'S OWN residual at the very same point: esminiRMLib
                  forward-maps base.position to road coordinates and converts that
                  straight back to XY. ST->XY is exact geometry while XY->ST walks
                  the OSI polyline, so the two do not close perfectly, and the gap
                  has to be measured rather than assumed to be zero. (The obvious
                  alternative floor -- round-tripping the entity origin's own s/t --
                  is a tautology: the origin's XY was DERIVED from that s/t, so it
                  closes at 0 and measures nothing.)
       rt_centre  the claim. Must be no worse than rt_floor.
       rt_cross   the NEGATIVE CONTROL: the origin's s/t against the box centre's
                  XY. Must come out at roughly centerOffsetX, i.e. the check has to
                  be able to tell the two points apart at all.
       rt_ds      esminiRMLib's OWN s for the box centre against s_position. Two
                  RoadManager instances resolving the same world point.

  2. THE JUNCTION CLAIM, BOTH FACES AT ONCE. On an OSI-intersection connecting
     road the PHYSICAL assigned_lane_id is the fused junction id -- that is why
     signal:ego_lane cannot join there (171/15800 frames). The logical assignment
     must instead name the connecting road's own lane, and the physical field must
     still be the junction id. Fixing only one of the two would break replayer's
     osi_receiver and the upstream UDP samples.

  3. L1-b, BOTH POLARITIES. osi_common.proto requires every lane the body overlaps
     by more than 5 cm to be assigned. A vehicle changing lanes must therefore go
     1 -> 2 -> 1, and one driving straight must stay at 1 for the whole run. All
     frames at 1 would pass a one-sided check while the straddle detector was dead.

  4. REFERENCE CLOSURE. Every assigned_lane_id must exist in the static
     GroundTruth's logical_lane[]. A dangling id serialises perfectly well.

  5. FLAG OFF. GT_OSI_LOGICAL_LANE unset -> not one assignment anywhere.

  6. DYNAMIC GROUND-TRUTH INCREMENT, MEASURED. The design projected "~35 B per
     object, ~70 B while straddling" (current-state 5-5a). This runs a
     many-vehicle scene ON and OFF and reports the real per-frame delta, the real
     bytes per assignment, and the ratio -- so the projection can be replaced by
     numbers.

    DriverScript/.venv/Scripts/python.exe scripts/probe_osi_logical_lane_assignment.py

Exit 0 = PASS. Requires a completed Release build (GT_esminiLib.dll).
Output: test_results/osi_logical_lane/assignment_probe.json (+ a table on stdout).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DLL = os.path.join(_REPO_ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")
OUT_DIR = os.path.join(_REPO_ROOT, "test_results", "osi_logical_lane")

ENV_FLAG = "GT_OSI_LOGICAL_LANE"

# Real repo assets. Each one is here because it is the only shape that answers a
# question above; none of them were authored for this probe.
#
#   cut_in            e6mini, 2 vehicles. OverTaker performs a LaneChangeAction
#                     while the ego holds its lane, so ONE run carries both
#                     polarities of check 3: an entity that must straddle and an
#                     entity that must never straddle.
#   routing_test      multi_intersections, ego routed across four junctions with
#                     no controller at all. The only fixture that spends frames on
#                     OSI-intersection connecting roads (check 2).
#   highway_driver    e6mini, 10 vehicles, 45 s. The scene for check 6: the
#                     per-object cost only becomes readable with enough objects.
FIXTURES = [
    ("cut_in", "resources/xosc/cut-in.xosc", 12.0),
    ("routing_test", "resources/xosc/routing-test.xosc", 14.0),
    ("highway_driver", "resources/xosc/highway_driver.xosc", 20.0),
]

# The independent instrument for check 1. A different DLL with its own RoadManager,
# so nothing in the comparison shares state with the reporter that produced the
# message. Built by the same Protocol A run as GT_esminiLib.
DEFAULT_RM_DLL = os.path.join(_REPO_ROOT, "build", "GT_esmini", "Release", "esminiRMLib.dll")

# Entities of cut-in.xosc, and what each one must do. Named explicitly: "some
# entity straddled somewhere" is not the claim -- the one that changes lanes must,
# and the one that does not must not.
CUT_IN_EXPECT = {"OverTaker": "straddles", "Ego": "never straddles"}


_WORKER = r'''
import sys, os, json, math, ctypes
REPO_ROOT = %(repo)r
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))          # esmini osi3 bindings
sys.path.insert(0, os.path.join(REPO_ROOT, "GT_esmini", "scripts", "verification"))

out      = %(out)r
osi_file = %(osi_file)r
runs     = %(runs)r      # [[label, xosc, max_time], ...] -- each in ONE process
dll      = %(dll)r
rm_dll   = %(rm_dll)r


class SE_ScenarioObjectState(ctypes.Structure):
    """esminiLib.hpp SE_ScenarioObjectState, field for field. id_t is uint32."""
    _fields_ = [
        ("id", ctypes.c_int), ("model_id", ctypes.c_int), ("ctrl_type", ctypes.c_int),
        ("timestamp", ctypes.c_double),
        ("x", ctypes.c_double), ("y", ctypes.c_double), ("z", ctypes.c_double),
        ("h", ctypes.c_double), ("p", ctypes.c_double), ("r", ctypes.c_double),
        ("roadId", ctypes.c_uint32), ("junctionId", ctypes.c_uint32),
        ("t", ctypes.c_double), ("laneId", ctypes.c_int), ("laneOffset", ctypes.c_double),
        ("s", ctypes.c_double), ("speed", ctypes.c_double),
        ("centerOffsetX", ctypes.c_double), ("centerOffsetY", ctypes.c_double),
        ("centerOffsetZ", ctypes.c_double),
        ("width", ctypes.c_double), ("length", ctypes.c_double), ("height", ctypes.c_double),
        ("objectType", ctypes.c_int), ("objectCategory", ctypes.c_int),
        ("wheel_angle", ctypes.c_double), ("wheel_rot", ctypes.c_double),
        ("visibilityMask", ctypes.c_int),
    ]


def rle(seq):
    """[1,1,2,2,2,1] -> [[1,2],[2,3],[1,1]] -- the straddle history, readably short."""
    out = []
    for v in seq:
        if out and out[-1][0] == v:
            out[-1][1] += 1
        else:
            out.append([v, 1])
    return out


res = {"runs": [], "error": None}
try:
    import osi3.osi_groundtruth_pb2 as gtpb
    import osi3.osi_lane_pb2 as lanepb
    from gt_lib import GtLib

    sys.path.insert(0, os.path.join(REPO_ROOT, "GT_esmini", "scripts"))
    from rm_lib import EsminiRMLib

    gt = GtLib(dll_path=dll)
    gt.lib.SE_GetODRFilename.restype = ctypes.c_char_p
    gt.lib.SE_GetObjectState.argtypes = [ctypes.c_int, ctypes.POINTER(SE_ScenarioObjectState)]
    gt.lib.SE_GetObjectState.restype = ctypes.c_int
    gt.lib.SE_GetNumberOfObjects.restype = ctypes.c_int
    gt.lib.SE_GetId.argtypes = [ctypes.c_int]
    gt.lib.SE_GetId.restype = ctypes.c_int

    for run_idx, (label, xosc, max_time) in enumerate(runs):
        r = {"label": label, "xosc": xosc, "init_rc": None, "error": None,
             "frames": 0, "object_frames": 0,
             "static_logical_lanes": 0, "static_record_bytes": 0,
             "osi_intersection_lanes": 0,
             "assignments": 0, "dangling": 0,
             "st_compared": 0, "st_max_ds": 0.0, "st_max_dt": 0.0,
             "rt_compared": 0, "rt_skipped_no_rm": 0,
             "rt_centre_max": 0.0, "rt_floor_max": 0.0, "rt_cross_min": None,
             "rt_clamped_frames": 0, "rt_clamped_max": 0.0,
             "rt_centre_max_unclamped": 0.0, "rt_negative_control_frames": 0,
             "rt_ds_compared": 0, "rt_ds_max": 0.0, "rt_worst": [],
             "odr": None, "rm_init_rc": None,
             "angle_min": None, "angle_max": None,
             "jct_object_frames": 0, "jct_logical_is_junction_id": 0,
             "jct_logical_road_matches": 0, "jct_logical_resolved": 0,
             "per_entity": {}, "dyn_bytes": []}
        args = ["--osc", os.path.join(REPO_ROOT, xosc), "--headless",
                "--fixed_timestep", "0.05", "--disable_stdout"]
        rc = gt.init_with_args(args)
        r["init_rc"] = rc
        if rc != 0:
            res["runs"].append(r)
            continue
        gt.set_osi_frequency(1)

        # The second RoadManager. Init'd from the scenario's own xodr, resolved out
        # of the running DLL rather than hard-coded per fixture.
        rm = None
        rm_handle = -1
        road_len = {}
        try:
            odr = gt.lib.SE_GetODRFilename()
            r["odr"] = odr.decode("utf-8") if odr else None
            if r["odr"]:
                rm = EsminiRMLib(rm_dll)
                r["rm_init_rc"] = rm.Init(r["odr"])
                if r["rm_init_rc"] == 0:
                    rm_handle = rm.CreatePosition()
                    for ri in range(rm.GetNumberOfRoads()):
                        rid = rm.GetIdOfRoadFromIndex(ri)
                        road_len[rid] = rm.GetRoadLength(rid)
                else:
                    rm = None
        except Exception as exc:                      # noqa: BLE001
            r["error"] = "rm init failed: %%s" %% exc
            rm = None

        def round_trip(road_id, s_val, t_val):
            """(road, s, t) -> world XY through the OTHER DLL. None when unusable."""
            if rm is None or rm_handle < 0:
                return None
            if rm.SetRoadPosition(rm_handle, road_id, s_val, t_val) != 0:
                return None
            rc, data = rm.GetPositionData(rm_handle)
            if rc != 0:
                return None
            return (data.x, data.y)

        def forward(x, y, z, hdg):
            """world XY -> road coords -> world XY, both legs inside the OTHER DLL.

            The return trip goes through the LANE form (road, lane, laneOffset, s),
            which is the complete road-frame description RM_GetPositionData hands
            back -- RM_PositionData carries no t, so the (road, s, t) form cannot be
            used to close this particular loop.
            """
            if rm is None or rm_handle < 0:
                return None, None
            if rm.SetWorldXYZHPosition(rm_handle, x, y, z, hdg) != 0:
                return None, None
            rc, fwd = rm.GetPositionData(rm_handle)
            if rc != 0:
                return None, None
            if rm.SetLanePosition(rm_handle, fwd.roadId, fwd.laneId, fwd.laneOffset, fwd.s) != 0:
                return None, fwd
            rc2, back = rm.GetPositionData(rm_handle)
            if rc2 != 0:
                return None, fwd
            return (back.x, back.y), fwd

        # The static GroundTruth lives in exactly one place: the FIRST record of the
        # .osi file (S0 hand-off 4 -- SE_GetOSIGroundTruth returns dynamic_gt only).
        this_osi = osi_file + ".%%d" %% run_idx
        gt.lib.SE_EnableOSIFile(this_osi.encode("utf-8"))
        gt.step(0.05)
        gt.lib.SE_FlushOSIFile()

        logical = {}          # logical lane id -> road_id from its source_reference
        intersection_ids = set()
        try:
            with open(this_osi, "rb") as fh:
                raw = fh.read()
            rec_len = int.from_bytes(raw[0:4], "little")
            g = gtpb.GroundTruth()
            g.ParseFromString(raw[4:4 + rec_len])
            r["static_record_bytes"] = rec_len
            for ll in g.logical_lane:
                road_id = None
                for sr in ll.source_reference:
                    for ident in sr.identifier:
                        if ident.startswith("road_id:"):
                            road_id = ident.split(":", 1)[1]
                logical[ll.id.value] = road_id
            for ln in g.lane:
                if ln.classification.type == lanepb.Lane.Classification.TYPE_INTERSECTION:
                    intersection_ids.add(ln.id.value)
        except Exception as exc:                      # noqa: BLE001
            r["error"] = "static read failed: %%s" %% exc
        r["static_logical_lanes"] = len(logical)
        r["osi_intersection_lanes"] = len(intersection_ids)

        counts = {}           # entity name -> [assignment count per frame]
        t = 0.05
        while t < max_time:
            gt.step(0.05)
            t += 0.05
            blob = gt.get_osi_ground_truth()
            if not blob:
                continue
            g = gtpb.GroundTruth()
            g.ParseFromString(blob)
            r["frames"] += 1
            r["dyn_bytes"].append(len(blob))

            # entity id -> road/lane/s/t straight out of the DLL, a different path
            # from the one that filled the message being checked.
            se = {}
            n = gt.lib.SE_GetNumberOfObjects()
            for i in range(n):
                st = SE_ScenarioObjectState()
                oid = gt.lib.SE_GetId(i)
                if gt.lib.SE_GetObjectState(oid, ctypes.byref(st)) == 0:
                    se[oid] = st

            for mo in g.moving_object:
                ent_id, ent_name = None, None
                for sr in mo.source_reference:
                    for ident in sr.identifier:
                        if ident.startswith("entity_id:"):
                            ent_id = int(ident.split(":", 1)[1])
                        elif ident.startswith("entity_name:"):
                            ent_name = ident.split(":", 1)[1]
                cls = mo.moving_object_classification
                assigns = list(cls.logical_lane_assignment)
                r["object_frames"] += 1
                key = ent_name or ("id_%%s" %% ent_id)
                counts.setdefault(key, []).append(len(assigns))

                phys = [i.value for i in cls.assigned_lane_id]
                in_junction = any(p in intersection_ids for p in phys)
                if in_junction:
                    r["jct_object_frames"] += 1

                for a in assigns:
                    r["assignments"] += 1
                    lid = a.assigned_lane_id.value
                    if lid not in logical:
                        r["dangling"] += 1
                    ang = a.angle_to_lane
                    r["angle_min"] = ang if r["angle_min"] is None else min(r["angle_min"], ang)
                    r["angle_max"] = ang if r["angle_max"] is None else max(r["angle_max"], ang)
                    st = se.get(ent_id)
                    if st is not None:
                        r["st_compared"] += 1
                        # No longer an equality: this is the measured shift from the
                        # entity origin to the reference point.
                        r["st_max_ds"] = max(r["st_max_ds"], abs(a.s_position - st.s))
                        r["st_max_dt"] = max(r["st_max_dt"], abs(a.t_position - st.t))

                # ROUND TRIP -- once per object-frame, on the anchor entry (every
                # entry of a frame carries the same s/t by construction).
                st = se.get(ent_id)
                if assigns and st is not None:
                    a0 = assigns[0]
                    if rm is None:
                        r["rt_skipped_no_rm"] += 1
                    else:
                        bx, by, bz = mo.base.position.x, mo.base.position.y, mo.base.position.z
                        centre = round_trip(st.roadId, a0.s_position, a0.t_position)
                        origin = round_trip(st.roadId, st.s, st.t)
                        floor_xy, fwd = forward(bx, by, bz, st.h)
                        if centre is not None and origin is not None and floor_xy is not None:
                            r["rt_compared"] += 1
                            e_centre = math.hypot(centre[0] - bx, centre[1] - by)
                            e_floor = math.hypot(floor_xy[0] - bx, floor_xy[1] - by)
                            e_cross = math.hypot(origin[0] - bx, origin[1] - by)
                            r["rt_centre_max"] = max(r["rt_centre_max"], e_centre)
                            r["rt_floor_max"] = max(r["rt_floor_max"], e_floor)
                            # Saturation at a road end is the one place the reference
                            # point is deliberately NOT where the box centre is
                            # (design 10-15). Counted, never quietly excluded.
                            rl = road_len.get(st.roadId)
                            clamped = rl is not None and (a0.s_position >= rl - 1e-6 or a0.s_position <= 1e-6)
                            if clamped:
                                r["rt_clamped_frames"] += 1
                                r["rt_clamped_max"] = max(r["rt_clamped_max"], e_centre)
                            else:
                                r["rt_centre_max_unclamped"] = max(r["rt_centre_max_unclamped"], e_centre)
                                # Keep the worst handful with enough context to
                                # attribute them, rather than only a maximum.
                                r["rt_worst"].append(
                                    [round(e_centre, 6), round(e_floor, 6), int(st.roadId),
                                     round(a0.s_position, 3), round(rl, 3) if rl else None,
                                     round(t, 2)])
                                r["rt_worst"] = sorted(r["rt_worst"], reverse=True)[:5]
                            # Two RoadManagers resolving the same world point.
                            if fwd is not None and fwd.roadId == st.roadId:
                                r["rt_ds_compared"] += 1
                                r["rt_ds_max"] = max(r["rt_ds_max"], abs(fwd.s - a0.s_position))
                            # Negative control only where the two points differ at all.
                            if abs(st.centerOffsetX) >= 1.0:
                                r["rt_negative_control_frames"] += 1
                                r["rt_cross_min"] = e_cross if r["rt_cross_min"] is None \
                                    else min(r["rt_cross_min"], e_cross)

                if in_junction and assigns:
                    lid = assigns[0].assigned_lane_id.value
                    if lid in intersection_ids:
                        r["jct_logical_is_junction_id"] += 1
                    if lid in logical:
                        r["jct_logical_resolved"] += 1
                        st = se.get(ent_id)
                        if st is not None and logical[lid] is not None and str(st.roadId) == logical[lid]:
                            r["jct_logical_road_matches"] += 1

        for k, v in counts.items():
            r["per_entity"][k] = {"frames": len(v), "min": min(v) if v else 0,
                                  "max": max(v) if v else 0, "rle": rle(v)[:40]}
        if rm is not None:
            try:
                rm.Close()
            except Exception:                         # noqa: BLE001
                pass
        gt.close()
        res["runs"].append(r)
except Exception as exc:                              # noqa: BLE001
    import traceback
    res["error"] = traceback.format_exc()

with open(out, "w") as fh:
    json.dump(res, fh)
'''


def _run_worker(dll, runs, flag_on, rm_dll=DEFAULT_RM_DLL):
    """One subprocess per polarity, so the GT_OSI_LOGICAL_LANE latch is fresh."""
    out_fd, out_path = tempfile.mkstemp(suffix=".json")
    os.close(out_fd)
    osi_path = os.path.join(tempfile.gettempdir(), "gt_ll_assignment_probe.osi")
    src = _WORKER % {
        "repo": _REPO_ROOT,
        "out": out_path,
        "osi_file": osi_path,
        "runs": runs,
        "dll": dll,
        "rm_dll": rm_dll,
    }
    py_fd, py_path = tempfile.mkstemp(suffix=".py")
    with os.fdopen(py_fd, "w") as fh:
        fh.write(src)

    env = dict(os.environ)
    if flag_on:
        env[ENV_FLAG] = "1"
    else:
        env.pop(ENV_FLAG, None)

    proc = subprocess.run(
        [sys.executable, py_path],
        env=env,
        capture_output=True,
        text=True,
        cwd=_REPO_ROOT,
        timeout=1800,
    )
    try:
        with open(out_path) as fh:
            result = json.load(fh)
    except Exception:  # noqa: BLE001
        result = {"runs": [], "error": "worker produced no JSON\n%s\n%s" % (proc.stdout[-4000:], proc.stderr[-4000:])}
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
    args = ap.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)
    if not os.path.exists(args.dll):
        print("FAIL: no DLL at %s -- build Release first" % args.dll)
        return 1

    failures = []
    report = {"dll": args.dll, "fixtures": [f[0] for f in FIXTURES]}

    def check(name, ok, detail=""):
        print("  [%s] %s%s" % ("PASS" if ok else "FAIL", name, (" -- " + detail) if detail else ""))
        if not ok:
            failures.append("%s: %s" % (name, detail))

    runs = [[label, xosc, max_time] for (label, xosc, max_time) in FIXTURES]

    print("== GT_OSI_LOGICAL_LANE=1 ==")
    on = _run_worker(args.dll, runs, flag_on=True)
    report["on"] = _summarise(on)
    if on.get("error"):
        check("worker_on", False, on["error"][-1500:])
        _write(report)
        return 1

    by_label = {}
    for r in on["runs"]:
        label = r["label"]
        by_label[label] = r
        print("  -- %s (%s)" % (label, r["xosc"]))
        if r["init_rc"] != 0:
            check("%s.init" % label, False, "init rc=%s" % r["init_rc"])
            continue
        check("%s.assignments_present" % label, r["assignments"] > 0,
              "%d assignments over %d object-frames (%d frames, %d static logical lanes)"
              % (r["assignments"], r["object_frames"], r["frames"], r["static_logical_lanes"]))
        if r["assignments"] == 0:
            continue

        check("%s.reference_closure" % label, r["dangling"] == 0,
              "%d/%d assigned_lane_id resolve in logical_lane[]"
              % (r["assignments"] - r["dangling"], r["assignments"]))

        # 1. The reference point, measured through esminiRMLib against base.position.
        if r["rt_compared"] == 0:
            check("%s.reference_point" % label, False,
                  "no round trips completed (rm_init_rc=%s odr=%s skipped=%d)"
                  % (r["rm_init_rc"], r["odr"], r["rt_skipped_no_rm"]))
        else:
            # The claim: resolving the BOX CENTRE costs no more than the instrument's
            # own XY -> ST -> XY residual at the same point.
            budget = r["rt_floor_max"] + 1e-3
            check("%s.reference_point_is_the_box_centre" % label,
                  r["rt_centre_max_unclamped"] <= budget,
                  "round trip (roadId, s_position, t_position) -> XY vs base.position: "
                  "max %.4g m over %d comparisons (instrument's own XY->ST->XY residual at the "
                  "same point: %.4g m; budget %.4g m); %d frames saturated at a road end, "
                  "max %.3g m there; worst unclamped [err, floor, road, s, road_len, t]: %s"
                  % (r["rt_centre_max_unclamped"], r["rt_compared"], r["rt_floor_max"],
                     budget, r["rt_clamped_frames"], r["rt_clamped_max"], r["rt_worst"][:3]))
            check("%s.two_roadmanagers_agree_on_s" % label,
                  r["rt_ds_compared"] > 0 and r["rt_ds_max"] <= 1e-6,
                  "max |esminiRMLib.s(base.position) - s_position| = %.3g m over %d frames"
                  % (r["rt_ds_max"], r["rt_ds_compared"]))
            # The negative control, in the same units, from the same loop.
            if r["rt_negative_control_frames"] == 0:
                check("%s.origin_would_fail" % label, False,
                      "no entity with a non-zero centerOffsetX -- the check cannot "
                      "distinguish the two points on this fixture")
            else:
                check("%s.origin_would_fail" % label,
                      r["rt_cross_min"] is not None and r["rt_cross_min"] > 10.0 * budget,
                      "same round trip fed the ORIGIN's s/t misses base.position by "
                      "at least %.4g m over %d frames (must exceed 10x the budget, %.4g m)"
                      % (r["rt_cross_min"] or 0.0, r["rt_negative_control_frames"], 10.0 * budget))
            print("     shift origin -> reference point: max |ds| = %.3f m, max |dt| = %.3f m over %d assignments"
                  % (r["st_max_ds"], r["st_max_dt"], r["st_compared"]))

        # angle_to_lane must be wrapped; an unwrapped Position value would show up
        # here as a maximum near 2*pi.
        if r["angle_max"] is not None:
            check("%s.angle_wrapped" % label, -3.1416 <= r["angle_min"] and r["angle_max"] <= 3.1416,
                  "angle_to_lane in [%.4f, %.4f] rad" % (r["angle_min"], r["angle_max"]))

    # 2. The junction claim. Only routing_test spends frames there, so it is asserted
    #    on that fixture and merely reported elsewhere.
    print("== inside OSI intersections ==")
    jr = by_label.get("routing_test", {})
    check("junction_frames_exist", jr.get("jct_object_frames", 0) > 0,
          "%d object-frames on a fused-junction physical lane (%d OSI intersections in the map)"
          % (jr.get("jct_object_frames", 0), jr.get("osi_intersection_lanes", 0)))
    if jr.get("jct_object_frames", 0) > 0:
        check("junction_logical_is_not_the_junction_id", jr["jct_logical_is_junction_id"] == 0,
              "%d/%d logical assignments named the fused junction id instead of a connecting lane"
              % (jr["jct_logical_is_junction_id"], jr["jct_object_frames"]))
        check("junction_logical_names_the_connecting_road", jr["jct_logical_road_matches"] == jr["jct_object_frames"],
              "%d/%d logical assignments resolve to a lane of the road the object is actually on"
              % (jr["jct_logical_road_matches"], jr["jct_object_frames"]))
    report["junction"] = {k: jr.get(k) for k in
                          ("jct_object_frames", "jct_logical_is_junction_id",
                           "jct_logical_road_matches", "osi_intersection_lanes")}

    # 3. L1-b, both polarities, by name.
    print("== L1-b straddle, both polarities ==")
    ci = by_label.get("cut_in", {})
    per = ci.get("per_entity", {})
    report["straddle"] = per
    for name, expectation in CUT_IN_EXPECT.items():
        e = per.get(name)
        if e is None:
            check("straddle.%s" % name, False, "entity not found in the run (entities: %s)" % sorted(per))
            continue
        seq = [c for c, _ in e["rle"]]
        if expectation == "straddles":
            went_up_and_back = any(seq[i] == 1 and seq[i + 1] == 2 for i in range(len(seq) - 1)) and \
                               any(seq[i] == 2 and seq[i + 1] == 1 for i in range(len(seq) - 1))
            check("straddle.%s_goes_1_2_1" % name, went_up_and_back,
                  "assignment counts %s over %d frames" % (seq, e["frames"]))
        else:
            check("straddle.%s_stays_1" % name, e["min"] == 1 and e["max"] == 1,
                  "assignment counts %s over %d frames (min %d max %d)" % (seq, e["frames"], e["min"], e["max"]))

    # 5. Flag OFF.
    print("== GT_OSI_LOGICAL_LANE unset (default OFF) ==")
    off = _run_worker(args.dll, runs, flag_on=False)
    report["off"] = _summarise(off)
    if off.get("error"):
        check("worker_off", False, off["error"][-1500:])
    else:
        for r in off["runs"]:
            check("%s.no_assignments_when_off" % r["label"],
                  r["assignments"] == 0 and r["object_frames"] > 0,
                  "%d assignments over %d object-frames; static logical_lane[] = %d"
                  % (r["assignments"], r["object_frames"], r["static_logical_lanes"]))

    # 6. The measured dynamic increment, replacing the design's projection.
    print("== dynamic GroundTruth increment (measured) ==")
    report["dynamic_increment"] = {}
    off_by_label = {r["label"]: r for r in off.get("runs", [])}
    for label, r_on in by_label.items():
        r_off = off_by_label.get(label)
        if not r_off or not r_on["dyn_bytes"] or not r_off["dyn_bytes"]:
            continue
        n = min(len(r_on["dyn_bytes"]), len(r_off["dyn_bytes"]))
        deltas = [r_on["dyn_bytes"][i] - r_off["dyn_bytes"][i] for i in range(n)]
        objs = r_on["object_frames"] / max(r_on["frames"], 1)
        per_assign = (sum(deltas) / r_on["assignments"]) if r_on["assignments"] else 0.0
        base_mean = sum(r_off["dyn_bytes"][:n]) / n
        on_mean = sum(r_on["dyn_bytes"][:n]) / n
        entry = {
            "frames": n,
            "objects_per_frame": round(objs, 2),
            "assignments_total": r_on["assignments"],
            "assignments_per_frame": round(r_on["assignments"] / max(r_on["frames"], 1), 2),
            "dyn_off_mean_B": round(base_mean, 1),
            "dyn_on_mean_B": round(on_mean, 1),
            "delta_mean_B": round(sum(deltas) / n, 1),
            "delta_max_B": max(deltas),
            "bytes_per_assignment": round(per_assign, 2),
            "ratio": round(on_mean / base_mean, 4) if base_mean else None,
        }
        report["dynamic_increment"][label] = entry
        print("  %-16s objects/frame %5.2f  assignments/frame %5.2f  dynamic GT %d -> %d B "
              "(+%.1f B, x%.3f)  %.2f B per assignment"
              % (label, entry["objects_per_frame"], entry["assignments_per_frame"],
                 round(base_mean), round(on_mean), entry["delta_mean_B"],
                 entry["ratio"] or 0.0, entry["bytes_per_assignment"]))
    check("dynamic_increment_measured", bool(report["dynamic_increment"]),
          "%d fixtures measured ON vs OFF" % len(report["dynamic_increment"]))

    _write(report)
    print()
    if failures:
        print("FAIL (%d):" % len(failures))
        for f in failures:
            print("  - " + f)
        return 1
    print("PASS -- report: %s" % os.path.join(OUT_DIR, "assignment_probe.json"))
    return 0


def _summarise(result):
    out = {"worker_rc": result.get("worker_rc"), "error": result.get("error"), "runs": []}
    for r in result.get("runs", []):
        keep = {k: v for k, v in r.items() if k != "dyn_bytes"}
        keep["dyn_bytes_min"] = min(r["dyn_bytes"]) if r.get("dyn_bytes") else None
        keep["dyn_bytes_max"] = max(r["dyn_bytes"]) if r.get("dyn_bytes") else None
        out["runs"].append(keep)
    return out


def _write(report):
    path = os.path.join(OUT_DIR, "assignment_probe.json")
    with open(path, "w") as fh:
        json.dump(report, fh, indent=2)


if __name__ == "__main__":
    sys.exit(main())
