#!/usr/bin/env python
"""check_route_waypoints.py — lint OpenSCENARIO Route Waypoints for skipped
OpenDRIVE junction connecting roads.

Rule (confirmed root cause for issue #31, see
GT_esmini/docs/virtualdriver/design/scenario_authoring_foundation.md §10):
when a Route's Waypoint list crosses a junction, every road on the path --
source Arm -> ConnectingRoad -> destination Arm -- must carry its own
Waypoint. Skipping the ConnectingRoad makes esmini
RoadManager::Route::AddWaypoint fail path resolution ("Skip waypoint for
scenario routes since path not found"), silently truncating the route.

Directionality matters: a junction typically carries one connecting road per
direction of travel (e.g. one road for arm A->B, a different road for B->A,
each with its own lane linkage), so a naive undirected road-adjacency search
can pick the wrong one. This checker builds, per junction, a table of exact
directed lane-level transitions:
    (predRoad, predLaneId) -> (succRoad, succLaneId) via connectingRoad
read from each connecting road's own <lane><link> predecessor/successor
entries, and matches each Waypoint->Waypoint hop against that table using the
laneId carried by LanePosition Waypoints.

Known, intentional exception: `resources/xosc/verification/p6_virtual_junction/`
exercises the (still-experimental) OpenDRIVE *virtual junction* mechanism,
where a branch road links to a mid-road anchor (elementS) on the main road
instead of an ordinary end-to-end predecessor/successor link. There is no
OpenDRIVE connecting road to add as a Waypoint there, and the scenarios
deliberately prove that ControllerFollowRoute's anchor-aware router resolves
the route without one. This checker reports those as `no-connecting-road`
(informational) and the default `--exclude` in the CLI skips that directory;
do not "fix" them by adding a Waypoint.

Usage:
    DriverScript/.venv/Scripts/python.exe scripts/check_route_waypoints.py
    DriverScript/.venv/Scripts/python.exe scripts/check_route_waypoints.py --root resources/xosc --json

Exit code is non-zero iff any `skipped-connecting-road` or
`ambiguous-connecting-road` finding was reported.
"""
from __future__ import annotations

import argparse
import collections
import json
import pathlib
import sys
from xml.etree import ElementTree as ET

DEFAULT_EXCLUDE = ["verification/p6_virtual_junction/"]


def parse_xodr(xodr_path):
    """Return (junction_of, adjacent, transitions, road_pair_candidates, road_length) for xodr_path."""
    tree = ET.parse(xodr_path)
    root = tree.getroot()

    junction_of = {}
    road_length = {}
    adjacent = collections.defaultdict(set)  # ANY direct road-level predecessor/successor link
    # transitions[(predRoad, predLane, succRoad, succLane)] = (connectingRoadId, connectingRoadOwnLaneId)
    transitions = {}
    # road_pair_candidates[(predRoad, succRoad)] = set of connectingRoadIds (any lane)
    road_pair_candidates = collections.defaultdict(set)

    for road in root.findall("road"):
        rid = road.get("id")
        junction_of[rid] = road.get("junction", "-1")
        road_length[rid] = float(road.get("length"))

    for road in root.findall("road"):
        rid = road.get("id")
        link = road.find("link")
        if link is None:
            continue
        pred = link.find("predecessor")
        succ = link.find("successor")
        pred_road = pred.get("elementId") if pred is not None and pred.get("elementType") == "road" else None
        succ_road = succ.get("elementId") if succ is not None and succ.get("elementType") == "road" else None

        if pred_road is not None:
            adjacent[rid].add(pred_road)
            adjacent[pred_road].add(rid)
        if succ_road is not None:
            adjacent[rid].add(succ_road)
            adjacent[succ_road].add(rid)

        if junction_of.get(rid, "-1") == "-1":
            continue  # plain road: adjacency above already covers it, no lane-transition table needed

        # connecting road: build directed lane transitions predRoad -> succRoad via rid
        if pred_road is None or succ_road is None:
            continue
        lanes_el = road.find("lanes")
        if lanes_el is None:
            continue
        for ls in lanes_el.findall("laneSection"):
            for side in ("left", "right"):
                side_el = ls.find(side)
                if side_el is None:
                    continue
                for lane in side_el.findall("lane"):
                    if lane.get("type") != "driving":
                        continue
                    lane_id = lane.get("id")
                    lane_link = lane.find("link")
                    if lane_link is None:
                        continue
                    lp = lane_link.find("predecessor")
                    ls_ = lane_link.find("successor")
                    if lp is None or ls_ is None:
                        continue
                    pred_lane = lp.get("id")
                    succ_lane = ls_.get("id")
                    transitions[(pred_road, pred_lane, succ_road, succ_lane)] = (rid, lane_id)
                    road_pair_candidates[(pred_road, succ_road)].add(rid)

    return junction_of, adjacent, transitions, road_pair_candidates, road_length


def extract_route_waypoints(route_el):
    """Return list of (road_id, lane_id or None) for a <Route>, None entries for non-road positions."""
    out = []
    for wp in route_el.findall("Waypoint"):
        pos = wp.find("Position")
        entry = None
        if pos is not None:
            lp = pos.find("LanePosition")
            if lp is not None:
                entry = (lp.get("roadId"), lp.get("laneId"))
            else:
                rp = pos.find("RoadPosition")
                if rp is not None:
                    entry = (rp.get("roadId"), None)
        out.append(entry)
    return out


def find_logic_file(osc_root, xosc_path):
    rn = osc_root.find("RoadNetwork")
    if rn is None:
        return None
    lf = rn.find("LogicFile")
    if lf is None:
        return None
    filepath = lf.get("filepath")
    if not filepath:
        return None
    candidate = (xosc_path.parent / filepath).resolve()
    return candidate if candidate.is_file() else None


def audit_file(xosc_path, cache):
    """Audit one .xosc file. Returns (findings, error_or_None).

    Each finding is a dict with keys: route, from, to, reason, detail, xodr,
    and (for `skipped-connecting-road`) a `fix` dict describing the missing
    Waypoint (roadId/laneId/s/insert_after_waypoint_index/route_strategy).
    `reason` is one of: skipped-connecting-road, ambiguous-connecting-road,
    no-connecting-road (informational -- see module docstring).
    """
    try:
        tree = ET.parse(xosc_path)
    except ET.ParseError as e:
        return [], f"parse-error: {e}"
    root = tree.getroot()

    xodr_path = find_logic_file(root, xosc_path)
    if xodr_path is None:
        return [], "no-xodr"

    if xodr_path not in cache:
        try:
            cache[xodr_path] = parse_xodr(xodr_path)
        except (ET.ParseError, OSError) as e:
            cache[xodr_path] = (None, str(e), None, None, None)
    junction_of, adjacent, transitions, road_pair_candidates, road_length = cache[xodr_path]
    if junction_of is None:
        return [], f"xodr-error: {adjacent}"

    findings = []
    for route_el in root.iter("Route"):
        route_name = route_el.get("name", "<unnamed>")
        wps = extract_route_waypoints(route_el)
        if any(e is None for e in wps):
            continue  # non-road-anchored waypoint (World/Relative) -- not checkable this way
        wp_road_set = {rid for rid, _ in wps}

        wp_elements = route_el.findall("Waypoint")
        for i, ((roadA, laneA), (roadB, laneB)) in enumerate(zip(wps, wps[1:])):
            if roadA == roadB:
                continue
            if roadB in adjacent.get(roadA, ()):
                continue  # directly linked at the road level (arm-arm or arm-connectingRoad hop)

            candidates = road_pair_candidates.get((roadA, roadB), set())
            if not candidates:
                findings.append(
                    {
                        "route": route_name, "from": f"{roadA}/{laneA}", "to": f"{roadB}/{laneB}",
                        "reason": "no-connecting-road", "detail": None, "xodr": str(xodr_path),
                    }
                )
                continue

            exact = transitions.get((roadA, laneA, roadB, laneB)) if laneA and laneB else None
            if exact is not None:
                connecting_road, own_lane = exact
            elif len(candidates) == 1:
                connecting_road = next(iter(candidates))
                # best-effort own lane: any driving lane transition recorded for this connecting road
                own_lane = next(
                    v[1] for (_, _, _, _), v in transitions.items() if v[0] == connecting_road
                )
            else:
                findings.append(
                    {
                        "route": route_name, "from": f"{roadA}/{laneA}", "to": f"{roadB}/{laneB}",
                        "reason": "ambiguous-connecting-road", "detail": sorted(candidates),
                        "xodr": str(xodr_path),
                    }
                )
                continue

            if connecting_road not in wp_road_set:
                length = road_length.get(connecting_road)
                mid_s = round(length / 2.0, 2) if length else 0.0
                findings.append(
                    {
                        "route": route_name, "from": f"{roadA}/{laneA}", "to": f"{roadB}/{laneB}",
                        "reason": "skipped-connecting-road", "detail": connecting_road,
                        "xodr": str(xodr_path),
                        "fix": {
                            "insert_after_waypoint_index": i,
                            "roadId": connecting_road,
                            "laneId": own_lane,
                            "s": mid_s,
                            "route_strategy": wp_elements[i].get("routeStrategy", "shortest"),
                        },
                    }
                )
    return findings, None


_HARD_FAIL_REASONS = {"skipped-connecting-road", "ambiguous-connecting-road"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=None, help="Directory to scan for .xosc (default: <repo>/resources/xosc)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument(
        "--exclude", action="append", default=None,
        help=f"path substring to skip (repeatable); default: {DEFAULT_EXCLUDE}",
    )
    args = ap.parse_args()

    here = pathlib.Path(__file__).resolve().parent
    repo_root = here.parent  # scripts/ is a direct child of the repo root
    scan_root = pathlib.Path(args.root).resolve() if args.root else repo_root / "resources" / "xosc"
    exclude = args.exclude if args.exclude is not None else DEFAULT_EXCLUDE

    xosc_files = sorted(
        p for p in scan_root.rglob("*.xosc") if ".gt_sim_temp" not in p.parts
    )
    cache = {}
    total_hard_findings = 0
    json_out = []
    for xosc_path in xosc_files:
        rel = xosc_path.relative_to(repo_root)
        rel_posix = str(rel).replace("\\", "/")
        if any(ex in rel_posix for ex in exclude):
            continue
        findings, err = audit_file(xosc_path, cache)
        if err and err not in ("no-xodr",):
            print(f"[WARN] {rel_posix}: {err}", file=sys.stderr)
        for f in findings:
            if f["reason"] in _HARD_FAIL_REASONS:
                total_hard_findings += 1
            if args.json:
                json_out.append({**f, "file": rel_posix})
            else:
                tag = "FAIL" if f["reason"] in _HARD_FAIL_REASONS else "info"
                print(f"[{tag}] {rel_posix} :: route={f['route']!r} {f['from']}->{f['to']} [{f['reason']}] {f['detail']} (xodr={f['xodr']})")
    if args.json:
        print(json.dumps(json_out, indent=2))
    else:
        print(f"\n{len(xosc_files)} xosc scanned, {total_hard_findings} FAIL finding(s)")
    return 1 if total_hard_findings else 0


if __name__ == "__main__":
    sys.exit(main())
