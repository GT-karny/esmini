#!/usr/bin/env python3
"""Size and flag-neutrality probe for the OSI logical-lane layer (spine-work:osi-logical-lane).

Two jobs, both measured -- nothing here is taken from a design document:

  1. SIZE. For each fixture, load it in-process through GT_esminiLib and decode
     the static OSI GroundTruth, then break the serialized bytes down per
     GroundTruth field by re-serializing each field on its own. Since S1 the
     logical layer's own cost (reference_line + logical_lane, with the
     boundaries still to come in S3) is reported as a MEASURED increment; the S0
     run of this probe could only project it, and the projection bundled the
     boundaries -- the bulk of the layer -- with the lane bodies. Design section
     7-3 flips the default to ON at S3 on the strength of these numbers.

  2. FLAG NEUTRALITY, WITH BOTH POLARITIES. Every fixture is loaded twice in
     separate processes, once with GT_OSI_LOGICAL_LANE unset and once with it
     set to 1. With the flag ON the record legitimately grows, so the invariant
     is not "identical bytes" any more but "nothing that already existed moved":
     each pre-existing static field is compared by its own digest, and the lane
     and lane_boundary ID SETS are compared element by element. That is the
     acceptance criterion for the id-numbering discipline of design section 3 --
     a renumbered lane silently breaks the ODR conformance goldens and the
     lane_map join behind signal:ego_lane, and nothing else would report it.
     The comparison is vacuous on its own (it also holds for a flag that is
     never read), so the probe additionally requires the post-pass's enabled log
     line to appear in the ON run and to be absent from the OFF run.

Also checked against the xodr itself: one reference line per road, one logical
lane per non-centre OpenDRIVE lane -- junction connecting roads included, which
is where the logical layer carries strictly more than osi_lane does.

    DriverScript/.venv/Scripts/python.exe scripts/probe_osi_logical_lane_size.py

Exit 0 = PASS. Requires a completed Release build (GT_esminiLib.dll).
Output: test_results/osi_logical_lane/size_probe.json (+ a table on stdout).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DLL = os.path.join(_REPO_ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")
OUT_DIR = os.path.join(_REPO_ROOT, "test_results", "osi_logical_lane")

ENV_FLAG = "GT_OSI_LOGICAL_LANE"
# Emitted by BuildOsiLogicalLanes() when the flag is ON. The negative control for
# "the flag is wired at all" rather than merely inert; see the module docstring.
ENABLED_MARK = "[GT_OSI:logical-lane] post-pass enabled"

# Fixtures. e6mini is the anchor: cut-in.xosc drives it and it is what upstream's
# 185928-byte assertion measures (1 road / 15 lanes / 1 laneSection). The rest
# widen the shape of the road network, which is what actually drives the logical
# lane count -- junction connecting roads are fused into ONE osi_lane today but
# become one logical lane each.
FIXTURES = [
    ("e6mini", "resources/xodr/e6mini.xodr"),
    ("fabriksgatan", "resources/xodr/fabriksgatan.xodr"),
    ("multi_intersections", "resources/xodr/multi_intersections.xodr"),
    ("soderleden", "resources/xodr/soderleden.xodr"),
    ("highway_merge_split", "resources/xodr/highway_example_with_merge_and_split.xodr"),
]

_WRAP_XOSC = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <FileHeader revMajor="1" revMinor="1" date="2026-09-24T00:00:00" description="s0-logical-lane-size-probe" author="gt"/>
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

# In-process OSI worker. Same contract as scripts/probe_authored_junction_boundary.py
# and run_odr_conformance.py's _OSI_WORKER: a subprocess that writes its result to a
# JSON file, never to stdout (the DLLs flood it -- and here stdout is the channel the
# parent greps for the enabled-marker, so it has to stay the DLL's).
_WORKER = r'''
import sys, os, json, ctypes, hashlib
REPO_ROOT = %(repo)r
dll = %(dll)r
xosc = %(xosc)r
out = %(out)r
osi_file = %(osi_file)r
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))  # esmini's own osi3 bindings

res = {"init_ok": False}
try:
    import osi3.osi_groundtruth_pb2 as gtpb
    lib = ctypes.CDLL(dll)
    lib.SE_Init.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.SE_Init.restype = ctypes.c_int
    lib.SE_StepDT.argtypes = [ctypes.c_double]; lib.SE_StepDT.restype = ctypes.c_int
    lib.SE_SetOSIFrequency.argtypes = [ctypes.c_int]; lib.SE_SetOSIFrequency.restype = ctypes.c_int
    lib.SE_EnableOSIFile.argtypes = [ctypes.c_char_p]; lib.SE_EnableOSIFile.restype = None
    lib.SE_FlushOSIFile.restype = None
    lib.SE_GetOSIGroundTruth.argtypes = [ctypes.POINTER(ctypes.c_int)]
    lib.SE_GetOSIGroundTruth.restype = ctypes.c_void_p  # NOT c_char_p (truncates at first NUL)
    lib.SE_Close.restype = None

    rc = lib.SE_Init(xosc.encode("utf-8"), 1, 0, 0, 0)
    if rc != 0:
        json.dump({"init_ok": False, "rc": rc}, open(out, "w")); sys.exit(0)
    # Reproduce the upstream GetOSIRoadLaneTest.lane_no_obj measurement (the one that
    # asserts st_size == 185928 on pristine upstream): frequency 1, OSI file opened,
    # flushed -- the first record carries the static content.
    lib.SE_SetOSIFrequency(1)
    lib.SE_EnableOSIFile(osi_file.encode("utf-8"))
    lib.SE_StepDT(0.05)
    lib.SE_FlushOSIFile()
    osi_file_bytes = os.path.getsize(osi_file) if os.path.isfile(osi_file) else -1

    # Per-frame payload as a live consumer sees it. NOT the static network: after
    # initialisation UpdateOSIGroundTruth only copies dynamic_gt into the external
    # message, so this is ~2 KB of moving objects and says nothing about lanes.
    size = ctypes.c_int(0)
    ptr = lib.SE_GetOSIGroundTruth(ctypes.byref(size))
    frame_data = ctypes.string_at(ptr, size.value) if (ptr and size.value > 0) else b""
    lib.SE_Close()

    # The static network comes from the FIRST RECORD of the .osi file, which is the
    # only place it is serialized (SerializeDynamicAndStaticData: static_gt then
    # dynamic_gt appended; concatenating two GroundTruth messages parses as one).
    # Record framing is a 4-byte little-endian unsigned size then the payload
    # (OSIReporter::WriteOSIFile). This is the same record upstream's
    # `st_size == 185928` assertion stats.
    with open(osi_file, "rb") as fh:
        raw = fh.read()
    rec_len = int.from_bytes(raw[0:4], "little")
    data = raw[4:4 + rec_len]
    if len(data) != rec_len:
        raise RuntimeError("short .osi record: want %%d got %%d" %% (rec_len, len(data)))

    g = gtpb.GroundTruth()
    g.ParseFromString(data)

    # Per-field wire bytes: copy ONE repeated field into an otherwise empty
    # GroundTruth and serialize it. Counts the field tags and length prefixes the
    # way the real message does, which a sum of element ByteSize() would not.
    def field_bytes(name):
        m = gtpb.GroundTruth()
        getattr(m, name).extend(getattr(g, name))
        return len(m.SerializeToString())

    def field_sha(name):
        m = gtpb.GroundTruth()
        getattr(m, name).extend(getattr(g, name))
        return hashlib.sha256(m.SerializeToString()).hexdigest()

    LOGICAL_FIELDS = ["reference_line", "logical_lane_boundary", "logical_lane"]
    PREEXISTING_STATIC_FIELDS = ["lane", "lane_boundary", "traffic_sign", "traffic_light",
                                 "road_marking", "stationary_object"]
    STATIC_FIELDS = PREEXISTING_STATIC_FIELDS + LOGICAL_FIELDS
    DYNAMIC_FIELDS = ["moving_object"]
    per_field = {n: field_bytes(n) for n in STATIC_FIELDS + DYNAMIC_FIELDS}
    counts = {n: len(getattr(g, n)) for n in STATIC_FIELDS + DYNAMIC_FIELDS}
    # Per-field digests of everything that existed BEFORE the logical layer. Once
    # the layer emits, the whole-record digest necessarily differs between OFF and
    # ON, so "nothing else moved" has to be asserted field by field. These digests
    # are what make the id-stability claim (design 3) measurable end to end: a
    # renumbered lane changes lane's bytes even though its count is unchanged.
    pre_sha = {n: field_sha(n) for n in PREEXISTING_STATIC_FIELDS}

    centerline_points = sum(len(L.classification.centerline) for L in g.lane)
    boundary_points = sum(len(b.boundary_line) for b in g.lane_boundary)
    reference_line_points = sum(len(r.poly_line) for r in g.reference_line)

    res = {
        "init_ok": True,
        "gt_serialized_bytes": len(data),          # first .osi record = static + dynamic
        "frame_payload_bytes": len(frame_data),    # per-frame external GT = dynamic only
        "osi_file_bytes": osi_file_bytes,          # record length + the 4-byte prefix
        "record_bytes": rec_len,
        "counts": counts,
        "per_field_bytes": per_field,
        "pre_existing_field_sha256": pre_sha,
        "static_subtotal_bytes": sum(per_field[n] for n in STATIC_FIELDS),
        "logical_layer_bytes": sum(per_field[n] for n in LOGICAL_FIELDS),
        "centerline_points": centerline_points,
        "lane_boundary_points": boundary_points,
        "reference_line_points": reference_line_points,
        # The id sets the ODR conformance goldens and the lane_map join are keyed
        # on. Compared OFF vs ON directly, because a shifted id is the one failure
        # mode of this feature that nothing downstream would report out loud.
        "lane_ids": sorted(L.id.value for L in g.lane),
        "lane_boundary_ids": sorted(b.id.value for b in g.lane_boundary),
        "logical_lane_ids": sorted(L.id.value for L in g.logical_lane),
        "reference_line_ids": sorted(r.id.value for r in g.reference_line),
        # the byte-identity check compares this, not just the length
        "gt_sha256": hashlib.sha256(data).hexdigest(),
        "frame_sha256": hashlib.sha256(frame_data).hexdigest(),
    }
except Exception as e:
    res = {"init_ok": False, "error": "%%s: %%s" %% (type(e).__name__, e)}
json.dump(res, open(out, "w"))
'''


def _run(xosc_path, dll, flag_on, workdir):
    """One fixture, one flag polarity, one fresh process."""
    fd, script = tempfile.mkstemp(suffix="_s0ll.py", prefix="s0llprobe_", dir=workdir)
    os.close(fd)
    out = script + ".json"
    osi_file = script + ".osi"
    body = _WORKER % {
        "repo": _REPO_ROOT,
        "dll": dll,
        "xosc": xosc_path,
        "out": out,
        "osi_file": osi_file,
    }
    with open(script, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(body)
    env = dict(os.environ)
    if flag_on:
        env[ENV_FLAG] = "1"
    else:
        env.pop(ENV_FLAG, None)
    try:
        # stdout captured, NOT discarded: the enabled-marker log line is the
        # evidence that the ON run actually entered the post-pass.
        proc = subprocess.run(
            [sys.executable, script],
            env=env,
            timeout=600,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        console = proc.stdout.decode("utf-8", "replace")
        try:
            with open(out, "r") as fh:
                res = json.load(fh)
        except (OSError, ValueError):
            res = {"init_ok": False, "error": "worker produced no result file"}
        res["post_pass_log_seen"] = ENABLED_MARK in console
        return res
    finally:
        for p in (script, out, osi_file):
            try:
                os.remove(p)
            except OSError:
                pass


def _xodr_shape(path):
    """Road / laneSection / lane counts straight from the xodr.

    These fix the logical-lane population, which the OSI GroundTruth cannot show
    today: osi_lane drops centre lanes and fuses every junction connecting road
    into one TYPE_INTERSECTION lane, while logical lanes are 1:1 with OpenDRIVE
    lanes everywhere, junctions included.
    """
    root = ET.parse(path).getroot()
    roads = root.findall("road")
    n_section = 0
    n_lane = 0
    n_lane_nocenter = 0
    n_junction_road = 0
    n_junction_lane = 0
    for r in roads:
        in_junction = r.get("junction", "-1") not in ("-1", "", None)
        if in_junction:
            n_junction_road += 1
        for ls in r.findall("./lanes/laneSection"):
            n_section += 1
            for side in ("left", "center", "right"):
                for _ in ls.findall("./%s/lane" % side):
                    n_lane += 1
                    if side != "center":
                        n_lane_nocenter += 1
                        if in_junction:
                            n_junction_lane += 1
    return {
        "roads": len(roads),
        "lane_sections": n_section,
        "lanes_incl_center": n_lane,
        "lanes_excl_center": n_lane_nocenter,
        "junction_roads": n_junction_road,
        "junction_lanes_excl_center": n_junction_lane,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=DEFAULT_DLL)
    ap.add_argument("--out-dir", default=OUT_DIR)
    args = ap.parse_args()

    if not os.path.isfile(args.dll):
        print("FAIL: GT_esminiLib.dll not found at %s (build Release first)" % args.dll)
        return 2

    workdir = tempfile.mkdtemp(prefix="s0ll_")
    results = {}
    ok = True

    def fail(msg):
        nonlocal ok
        ok = False
        print("FAIL:", msg)

    for name, rel in FIXTURES:
        xodr = os.path.join(_REPO_ROOT, rel)
        if not os.path.isfile(xodr):
            fail("fixture missing: %s" % rel)
            continue
        fd, xoscf = tempfile.mkstemp(suffix="_s0ll.xosc", prefix="s0ll_", dir=workdir)
        os.close(fd)
        with open(xoscf, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(_WRAP_XOSC.format(xodr=xodr.replace("\\", "/")))
        try:
            off = _run(xoscf, args.dll, flag_on=False, workdir=workdir)
            on = _run(xoscf, args.dll, flag_on=True, workdir=workdir)
        finally:
            try:
                os.remove(xoscf)
            except OSError:
                pass

        entry = {"xodr": rel, "shape": _xodr_shape(xodr), "off": off, "on": on}
        results[name] = entry
        print("ran %-22s OFF init=%s ON init=%s" % (name, off.get("init_ok"), on.get("init_ok")))

        if not off.get("init_ok"):
            fail("%s: OFF run did not init: %s" % (name, off))
            continue
        if not on.get("init_ok"):
            fail("%s: ON run did not init: %s" % (name, on))
            continue

        # A record with no lanes in it would make every comparison below vacuous.
        if off["counts"]["lane"] == 0:
            fail("%s: the measured record carries 0 lanes -- the probe is not "
                 "looking at the static network" % name)

        # (1) NOTHING THAT ALREADY EXISTED MOVED. From S1 on the whole-record
        # digest differs by construction (the logical layer is new bytes), so the
        # invariant is asserted field by field instead -- including the two id
        # sets that the ODR conformance goldens and the lane_map join are keyed
        # on. This is the acceptance criterion for design section 3.
        for f, sha in off["pre_existing_field_sha256"].items():
            if on["pre_existing_field_sha256"].get(f) != sha:
                fail(
                    "%s: pre-existing static field '%s' changed OFF vs ON (%d vs %d bytes)"
                    % (name, f, off["per_field_bytes"][f], on["per_field_bytes"][f])
                )
        for f in ("lane_ids", "lane_boundary_ids"):
            if off[f] != on[f]:
                moved = sorted(set(off[f]) ^ set(on[f]))
                fail("%s: %s differ OFF vs ON (%d symmetric-difference entries, e.g. %s)"
                     % (name, f, len(moved), moved[:8]))
        if off["frame_sha256"] != on["frame_sha256"]:
            fail(
                "%s: per-frame GroundTruth payload differs OFF vs ON (%d vs %d bytes)"
                % (name, off["frame_payload_bytes"], on["frame_payload_bytes"])
            )

        # (2) the gate demonstrably fired -- without this, (1) is also true of a dead flag
        if off.get("post_pass_log_seen"):
            fail(
                "%s: post-pass reported ENABLED with %s unset -- the default is not OFF"
                % (name, ENV_FLAG)
            )
        if not on.get("post_pass_log_seen"):
            fail(
                "%s: post-pass did not report enabled with %s=1 -- the flag is not wired"
                % (name, ENV_FLAG)
            )

        # (3) OFF is inert
        for f in ("reference_line", "logical_lane_boundary", "logical_lane"):
            if off["counts"][f] != 0:
                fail("%s: flag OFF still emitted %d %s" % (name, off["counts"][f], f))
        if off["logical_layer_bytes"] != 0:
            fail("%s: flag OFF still spent %d bytes on the logical layer" % (name, off["logical_layer_bytes"]))

        # (4) ON emits exactly the S1 model: one reference line per road, one
        # logical lane per non-centre OpenDRIVE lane (junction connecting roads
        # included -- that is the whole point of the layer), and NO boundaries,
        # which arrive in S3.
        want_lanes = entry["shape"]["lanes_excl_center"]
        if on["counts"]["logical_lane"] != want_lanes:
            fail(
                "%s: logical_lane=%d but the xodr has %d non-centre lanes"
                % (name, on["counts"]["logical_lane"], want_lanes)
            )
        if on["counts"]["logical_lane"] <= on["counts"]["lane"] and entry["shape"]["junction_lanes_excl_center"] > 0:
            fail(
                "%s: logical_lane=%d does not exceed osi lane=%d although the network has "
                "%d connecting-road lanes -- junction lanes are not individually emitted"
                % (name, on["counts"]["logical_lane"], on["counts"]["lane"], entry["shape"]["junction_lanes_excl_center"])
            )
        if on["counts"]["reference_line"] != entry["shape"]["roads"]:
            fail(
                "%s: reference_line=%d but the xodr has %d roads"
                % (name, on["counts"]["reference_line"], entry["shape"]["roads"])
            )
        if on["counts"]["logical_lane_boundary"] != 0:
            fail("%s: S1 emitted %d logical_lane_boundary (boundaries are S3)" % (name, on["counts"]["logical_lane_boundary"]))
        # Freshly drawn ids, disjoint from every id that already existed.
        existing = set(off["lane_ids"]) | set(off["lane_boundary_ids"])
        clash = existing & (set(on["logical_lane_ids"]) | set(on["reference_line_ids"]))
        if clash:
            fail("%s: %d logical-layer ids collide with existing lane/boundary ids, e.g. %s"
                 % (name, len(clash), sorted(clash)[:8]))

    # ---------------- report ----------------
    print()
    print("=== size baseline, flag OFF (every pre-existing field byte-identical with flag ON) ===")
    hdr = (
        "fixture", "record_B", "static_B", "frame_B", "lane", "laneB",
        "clPts", "bPts", "roads", "sects", "lanes*", "juncLanes*",
    )
    print("%-22s %10s %10s %10s %6s %6s %8s %8s %6s %6s %7s %10s" % hdr)
    for name, e in results.items():
        o = e["off"]
        if not o.get("init_ok"):
            print("%-22s  (init failed)" % name)
            continue
        s = e["shape"]
        print(
            "%-22s %10d %10d %10d %6d %6d %8d %8d %6d %6d %7d %10d"
            % (
                name,
                o["record_bytes"],
                o["static_subtotal_bytes"],
                o["frame_payload_bytes"],
                o["counts"]["lane"],
                o["counts"]["lane_boundary"],
                o["centerline_points"],
                o["lane_boundary_points"],
                s["roads"],
                s["lane_sections"],
                s["lanes_excl_center"],
                s["junction_lanes_excl_center"],
            )
        )
    print("  lanes* / juncLanes* = OpenDRIVE lanes excluding centre lanes (from the xodr),")
    print("  i.e. how many LOGICAL lanes the network has vs. how many of them sit inside")
    print("  junctions -- the ones osi_lane fuses into a single TYPE_INTERSECTION lane today.")

    print()
    print("=== S1 measured increment: reference_line + logical_lane only (NO boundaries) ===")
    print("  This is the middle value design section 7-3 asks for before flipping the default")
    print("  ON at S3: the S0 table could only project the whole layer, and the projection")
    print("  bundled the boundaries -- which are the bulk of it -- with the lane bodies.")
    print("  'static x' is measured against the consumer's whole static payload, 'roadnet x'")
    print("  against the road-network part alone (lane + lane_boundary). They diverge wherever")
    print("  a fixture carries many stationary objects, which the logical layer never touches.")
    hdr2 = ("fixture", "logLanes", "refLines", "refPts", "refLine_B", "logLane_B", "added_B", "static x", "roadnet x")
    print("  %-22s %8s %8s %7s %10s %10s %9s %9s %10s" % hdr2)
    for name, e in results.items():
        o, n = e["off"], e["on"]
        if not o.get("init_ok") or not n.get("init_ok") or o["counts"]["lane"] == 0:
            continue
        added = n["logical_layer_bytes"]
        roadnet = o["per_field_bytes"]["lane"] + o["per_field_bytes"]["lane_boundary"]
        print(
            "  %-22s %8d %8d %7d %10d %10d %9d %8.2fx %9.2fx"
            % (
                name,
                n["counts"]["logical_lane"],
                n["counts"]["reference_line"],
                n["reference_line_points"],
                n["per_field_bytes"]["reference_line"],
                n["per_field_bytes"]["logical_lane"],
                added,
                (o["static_subtotal_bytes"] + added) / float(o["static_subtotal_bytes"]),
                (roadnet + added) / float(roadnet),
            )
        )

    os.makedirs(args.out_dir, exist_ok=True)
    out_json = os.path.join(args.out_dir, "size_probe.json")
    with open(out_json, "w", encoding="utf-8") as fh:
        json.dump({"dll": args.dll, "fixtures": results, "pass": ok}, fh, indent=2)
    print()
    print("wrote", os.path.relpath(out_json, _REPO_ROOT))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
