#!/usr/bin/env python
"""Every OSI field the engine populates must reach the web layer.

The web projections (`_gt_to_json`, `_hvd_to_json`,
`_logical_lane_network_to_json`) are hand-written whitelists. That is a
reasonable design -- forwarding raw protobuf to a browser is not -- but a
whitelist silently drops anything added later, and from the browser a dropped
field is indistinguishable from one the engine never filled. That ambiguity is
not hypothetical: the logical lane layer shipped in v0.18.0 and nobody noticed
it was invisible in the GUI until someone asked.

So this probe does not check "are the fields we listed present". It walks the
fields a REAL frame actually set, and requires every one of them to be either

  * mapped to a key that is genuinely present in the projected JSON, or
  * named in OMITTED with a reason.

A field added to the emitter and to nothing else lands in neither and fails.
That is the whole point: the probe goes red for the thing that used to go
unnoticed.

Run:  DriverScript/.venv/Scripts/python.exe scripts/probe_osi_web_field_coverage.py
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for _p in (
    _REPO_ROOT,
    os.path.join(_REPO_ROOT, "scripts"),
    os.path.join(_REPO_ROOT, "GT_esmini", "scripts", "verification"),
):
    if _p not in sys.path:
        sys.path.insert(0, _p)

DEFAULT_DLL = os.path.join(
    _REPO_ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll"
)
OUT_DIR = os.path.join(_REPO_ROOT, "test_results", "osi_logical_lane")

# Scenarios chosen for field breadth, not behaviour: cut-in has two vehicles and
# lights, routing-test drives a junction network with a route.
RUNS = [
    ("cut_in", "resources/xosc/cut-in.xosc", 2.0),
    ("routing_test", "resources/xosc/routing-test.xosc", 3.0),
]

# --- deliberately not forwarded, with the reason -----------------------------
#
# Anything here is a decision, not an oversight. Delete an entry and the probe
# starts demanding the field, which is the correct way to change one's mind.

# --- forwarded under a different name, and VERIFIED present ------------------
#
# FLATTENED is an assertion, not an excuse: the probe requires the named key to
# exist in the payload. OMITTED is the excuse, and carries a reason.

FLAT_GT_DYNAMIC = {
    "timestamp": "sim_time",
    "moving_object": "objects",
    "traffic_light": "traffic_lights",
    "host_vehicle_id": "host_vehicle_id",
}
OMITTED_GT_DYNAMIC = {
    "version": "protocol version; on the static REST payload instead",
}

FLAT_GT_STATIC = {
    "version": "osi_version",
    "lane": "physical_lanes",
    "lane_boundary": "physical_boundaries",
    "stationary_object": "stationary_objects",
    "traffic_sign": "traffic_signs",
    "logical_lane": "lanes",
    "logical_lane_boundary": "boundaries",
    "reference_line": "reference_lines",
    "host_vehicle_id": "host_vehicle_id",
    "proj_string": "proj_string",
    "map_reference": "map_reference",
    "model_reference": "model_reference",
}
OMITTED_GT_STATIC = {
    "timestamp": "the static frame's timestamp is t=0, carries no information",
    "moving_object": "dynamic; forwarded on the WebSocket",
    "traffic_light": "dynamic (phase changes every frame); on the WebSocket",
}

FLAT_MOVING_OBJECT = {
    "id": "id",
    "type": "obj_type",
    "base.dimension.length": "length",
    "base.dimension.width": "width",
    "base.dimension.height": "height",
    "base.position": "x",
    "base.orientation": "h",
    "base.velocity": "vel",
    "base.acceleration": "acc",
    "base.orientation_rate": "orientation_rate",
    "base.orientation_acceleration": "orientation_acc",
    "vehicle_classification.type": "vehicle_class",
    "vehicle_classification.light_state": "head_light",
    "vehicle_classification.role": "role",
    "vehicle_attributes.bbcenter_to_rear": "bbcenter_to_rear",
    "vehicle_attributes.bbcenter_to_front": "bbcenter_to_front",
    "vehicle_attributes.number_wheels": "number_wheels",
    "source_reference": "name",
    "color_description.rgb": "rgb",
    "model_reference": "model_reference",
    "moving_object_classification.assigned_lane_id": "assigned_lane_id",
    "moving_object_classification.logical_lane_assignment": "logical_lanes",
    "assigned_lane_id": "assigned_lane_id",
    "future_trajectory": "future_trajectory",
}
OMITTED_MOVING_OBJECT = {
    "vehicle_attributes.driver_id": "internal id, no consumer",
    "vehicle_attributes.wheel_data": (
        "per-wheel rotation/steer; the ManualDrive panel reads these from HVD, "
        "and forwarding them would multiply the per-frame payload by the wheel count"
    ),
}

FLAT_HVD = {
    "timestamp": "sim_time",
    "host_vehicle_id": "host_vehicle_id",
    "location": "location",
    "vehicle_motion": "vehicle_motion",
    "vehicle_localization": "vehicle_localization",
    "vehicle_basics.operating_state": "operating_state",
    "vehicle_powertrain.pedal_position_acceleration": "throttle",
    "vehicle_powertrain.gear_transmission": "gear",
    "vehicle_powertrain.motor": "rpm",
    "vehicle_brake_system.pedal_position_brake": "brake",
    "vehicle_steering.vehicle_steering_wheel": "steering_angle",
    "vehicle_automated_driving_function": "adas_functions",
    "route": "route",
}
OMITTED_HVD: dict[str, str] = {}


def populated_paths(msg, prefix="", depth=0, max_depth=2):
    """Field paths this message actually SET, one level into submessages.

    Stops at `max_depth` because the interesting question is "is this branch
    represented at all", not "is every leaf spelled the same way".
    """
    out = []
    for fd, val in msg.ListFields():
        name = prefix + fd.name
        if hasattr(val, "add"):  # repeated
            out.append(name)
        elif hasattr(val, "ListFields") and depth < max_depth:
            sub = populated_paths(val, name + ".", depth + 1, max_depth)
            out.extend(sub if sub else [name])
        else:
            out.append(name)
    return out


def _ancestors(path):
    """'a.b.c' -> ['a.b.c', 'a.b', 'a'] so a decision recorded at any level counts."""
    parts = path.split(".")
    return [".".join(parts[: i + 1]) for i in range(len(parts))][::-1]


def check(kind, paths, flattened, omitted, present_keys, failures):
    """Every populated path is forwarded (and verified), or omitted (and stated).

    The two maps are deliberately NOT one map. The first version of this probe
    put "flattened to vel[]" in OMITTED, which made the entry excuse the field
    instead of asserting it -- deleting the forwarding line still passed. A
    FLATTENED entry now names the key it becomes and the probe REQUIRES that key
    to be present, so removing the forwarding turns the probe red.
    """
    covered = 0
    for p in sorted(set(paths)):
        anc = _ancestors(p)

        target = next((flattened[a] for a in anc if a in flattened), None)
        if target is not None:
            if target in present_keys:
                covered += 1
            else:
                failures.append(
                    "%s: %r is declared as flattened into %r, but %r is missing "
                    "from the payload -- the forwarding was removed or renamed"
                    % (kind, p, target, target)
                )
            continue

        if any(a in omitted for a in anc):
            covered += 1
            continue

        if any(a in present_keys for a in anc):
            covered += 1
            continue

        failures.append(
            "%s: populated field %r reaches nothing in the web layer and is "
            "declared neither FLATTENED nor OMITTED -- forward it, or say why not"
            % (kind, p)
        )
    return covered


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=DEFAULT_DLL)
    ap.add_argument("--out-dir", default=OUT_DIR)
    args = ap.parse_args()

    if not os.path.exists(args.dll):
        print("MISSING DLL: %s" % args.dll)
        return 2

    import osi3.osi_groundtruth_pb2 as gtpb
    import osi3.osi_hostvehicledata_pb2 as hvdpb
    from gt_lib import GtLib

    from GT_esmini.web.backend.api import osi_stream

    failures: list[str] = []
    report = {"runs": []}

    for label, xosc, seconds in RUNS:
        r = {
            "label": label,
            "xosc": xosc,
            "gt_fields": 0,
            "mo_fields": 0,
            "hvd_fields": 0,
            "static_keys": 0,
        }
        osi_path = os.path.join(tempfile.gettempdir(), "field_cov_%s.osi" % label)

        gt = GtLib(dll_path=args.dll)
        rc = gt.init_with_args(
            [
                "--osc",
                os.path.join(_REPO_ROOT, xosc),
                "--headless",
                "--fixed_timestep",
                "0.05",
                "--disable_stdout",
            ]
        )
        if rc != 0:
            failures.append("%s: init rc=%s" % (label, rc))
            report["runs"].append(r)
            continue

        gt.lib.SE_EnableOSIFile(osi_path.encode("utf-8"))
        gt.step(0.05)
        gt.lib.SE_FlushOSIFile()
        raw = open(osi_path, "rb").read()
        n = int.from_bytes(raw[0:4], "little")
        static = gtpb.GroundTruth()
        static.ParseFromString(raw[4 : 4 + n])

        steps = int(seconds / 0.05)
        for _ in range(steps):
            gt.step(0.05)
        dyn_raw = gt.get_osi_ground_truth()
        hvd_raw = gt.get_osi_host_vehicle_data()
        gt.close()

        dyn = gtpb.GroundTruth()
        dyn.ParseFromString(dyn_raw)

        ws = osi_stream._gt_to_json(dyn_raw)
        net = osi_stream._logical_lane_network_to_json(raw[4 : 4 + n])
        if ws is None or net is None:
            failures.append("%s: projection returned None" % label)
            report["runs"].append(r)
            continue

        # The static REST payload counts as "reached" for static GroundTruth.
        static_keys = set(net.keys())
        r["static_keys"] = len(static_keys)

        # Static and dynamic go to DIFFERENT places, so they are checked against
        # different payloads. Merging them was the first version of this probe and
        # it reported the static-only fields as dropped.
        dyn_paths = populated_paths(dyn, max_depth=0)
        static_paths = populated_paths(static, max_depth=0)
        r["gt_fields"] = len(set(dyn_paths) | set(static_paths))
        check(
            "GroundTruth/dynamic",
            dyn_paths,
            FLAT_GT_DYNAMIC,
            OMITTED_GT_DYNAMIC,
            set(ws.keys()),
            failures,
        )
        check(
            "GroundTruth/static",
            static_paths,
            FLAT_GT_STATIC,
            OMITTED_GT_STATIC,
            static_keys,
            failures,
        )

        if dyn.moving_object:
            mo_paths = populated_paths(dyn.moving_object[0])
            r["mo_fields"] = len(set(mo_paths))
            check(
                "MovingObject",
                mo_paths,
                FLAT_MOVING_OBJECT,
                OMITTED_MOVING_OBJECT,
                set(ws["objects"][0].keys()),
                failures,
            )

        if hvd_raw:
            h = hvdpb.HostVehicleData()
            h.ParseFromString(hvd_raw)
            hj = osi_stream._hvd_to_json(hvd_raw)
            if hj is None:
                failures.append("%s: HVD projection returned None" % label)
            else:
                hvd_paths = populated_paths(h)
                r["hvd_fields"] = len(set(hvd_paths))
                check(
                    "HostVehicleData",
                    hvd_paths,
                    FLAT_HVD,
                    OMITTED_HVD,
                    set(hj.keys()),
                    failures,
                )

        print(
            "  %-14s GroundTruth %2d fields / MovingObject %2d / HVD %2d "
            "/ static payload %d keys"
            % (label, r["gt_fields"], r["mo_fields"], r["hvd_fields"], r["static_keys"])
        )
        report["runs"].append(r)

    report["failures"] = failures
    report["pass"] = not failures
    os.makedirs(args.out_dir, exist_ok=True)
    out = os.path.join(args.out_dir, "web_field_coverage.json")
    with open(out, "w") as fh:
        json.dump(report, fh, indent=1)

    if failures:
        print()
        for f in failures:
            print("  FAIL %s" % f)
        print("\nFAIL -- report: %s" % out)
        return 1
    print("\nPASS -- every populated field is forwarded or documented as omitted")
    print("report: %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
