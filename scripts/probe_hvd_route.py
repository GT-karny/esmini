#!/usr/bin/env python3
"""HostVehicleData.route probe (spine-work:osi-logical-lane S4).

What only a running binary can answer -- everything here is measured, nothing is
taken from a design document:

  1. REFERENCE CLOSURE. Every route.route_segment[].lane_segment[].logical_lane_id
     published in HostVehicleData must exist in the static GroundTruth's
     logical_lane[]. A route of ids that resolve to nothing looks perfectly
     well-formed on the wire, so this is the assertion that the field is not
     quietly broken.

  2. DIRECTION, BOTH POLARITIES. osi_route.proto encodes "drive this segment
     against the reference line" as start_s > end_s, while LogicalLane requires
     the opposite ordering for the same two doubles. A build that always emits
     ascending s would pass any single-polarity check, so the probe requires
     BOTH orderings to occur in real data and reports the counts.

  3. ENDPOINTS. The first segment starts at the ego's own s and the last ends at
     the route's destination waypoint s.

  4. route_id STABILITY. The id is bumped in the same branch that rebuilds the
     RouteLanePlan, so a constant route_id across a run IS the evidence that the
     plan cache is holding. A second scenario loaded into the same process must
     then get a HIGHER id (the counter is never reused).

  5. FLAG OFF. With GT_OSI_LOGICAL_LANE unset the HostVehicleData must carry no
     route field at all -- not an empty one -- because there are no logical lanes
     for it to reference.

  6. L2 PROGRESS + ITS NEGATIVE CONTROL. VirtualDriver telemetry route_lane
     gains on_route / s_along_route / route_length. s_along_route must rise
     monotonically while driving.

  7. UDP FRAGMENTATION, END TO END. A HostVehicleData larger than the 8192-byte
     datagram budget must arrive intact at BOTH receivers this repo ships:
     GT_esmini/web/backend/services/osi_bridge.py's _OSIProtocol and
     DriverScript/realdriver/udp_common.py's OSIReceiver reassembly loop. Their
     own code is used, not a re-implementation of it.

    DriverScript/.venv/Scripts/python.exe scripts/probe_hvd_route.py

Exit 0 = PASS. Requires a completed Release build (GT_esminiLib.dll).
Output: test_results/osi_logical_lane/hvd_route_probe.json (+ a table on stdout).
"""
import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DLL = os.path.join(_REPO_ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")
OUT_DIR = os.path.join(_REPO_ROOT, "test_results", "osi_logical_lane")

ENV_FLAG = "GT_OSI_LOGICAL_LANE"
HVD_PORT = 48199

# s of each scenario's LAST route waypoint, read from the scenario/catalog XML -- the
# value the final RouteSegment must end on.
#   routing-test.xosc      -> RoutesAtMultiIntersections "ComplexRoute", last Waypoint
#                             road 197 lane -1 s=20
#   route_valid_off_...    -> last Waypoint road 2 lane -1 s=40
DESTINATION_S = {
    "routing_test_multi_intersections": 20.0,
    "route_lane_exit_ramp": 40.0,
    "long_route_fragmentation": None,   # generated below; filled at runtime
}

# Real repo assets, not probe-authored ones.
#
# routing-test carries the interesting shape: a 4-waypoint catalog route across
# multi_intersections that STARTS on a positive lane (laneId=1, i.e. driving -s
# under right-hand traffic) and ends on a negative one, and its ego has NO
# controller at all. So it exercises both segment polarities and the "a
# DefaultController ego publishes its route too" claim in one run.
FIXTURES = [
    (
        "routing_test_multi_intersections",
        "resources/xosc/routing-test.xosc",
        12.0,
    ),
    (
        "route_lane_exit_ramp",
        "resources/xosc/verification/06_route_lane/route_valid_off_target_lane_for_exit_ramp.xosc",
        10.0,
    ),
]


# --------------------------------------------------------------------------------------
# generated long-route fixture -- the ONLY synthetic asset here, and only for check 7
# --------------------------------------------------------------------------------------
#
# Measured first, generated second: the biggest route this repo's own maps can express is
# ~16 lane segments (routing-test across multi_intersections, 63 roads / 242 logical
# lanes), which puts HostVehicleData at roughly 800 B. Nothing shipped comes near the
# 8192 B datagram budget, so the fragmentation path could not be exercised on a real map
# at all.
#
# This fixture is therefore a deliberate WORST CASE for the transport, not a claim about
# realistic payloads: one long road cut into many lane sections, each of which is exactly
# one RouteSegment. The real-map sizes stay in the same report so the two are never
# confused with each other.
LONG_ROUTE_SECTIONS = 400
LONG_ROUTE_SECTION_LEN = 10.0
LONG_ROUTE_DIR = os.path.join(OUT_DIR, "long_route")


def _write_long_route_fixture():
    os.makedirs(LONG_ROUTE_DIR, exist_ok=True)
    n = LONG_ROUTE_SECTIONS
    seg = LONG_ROUTE_SECTION_LEN
    total = n * seg

    sections = []
    for j in range(n):
        # A lane's link at the road's OUTER end names a lane of the NEIGHBOURING ROAD,
        # not of a neighbouring lane section. The last section therefore still needs a
        # successor (road 2's lane -1, same id) -- without it Road::GetConnectingLaneId
        # finds nothing at the hop and BuildRouteLanePlan reports lane_discontinuity,
        # which is what the first run of this probe actually measured.
        links = '<successor id="-1"/>'
        if j > 0:
            links = '<predecessor id="-1"/>' + links
        sections.append(
            '      <laneSection s="{s:.1f}">\n'
            '        <center><lane id="0" type="none" level="false"><link/></lane></center>\n'
            '        <right>\n'
            '          <lane id="-1" type="driving" level="false"><link>{lk}</link>'
            '<width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/></lane>\n'
            '        </right>\n'
            '      </laneSection>\n'.format(s=j * seg, lk=links)
        )

    xodr = (
        '<?xml version="1.0" encoding="UTF-8"?>\n<OpenDRIVE>\n'
        '  <header revMajor="1" revMinor="7" name="gt_long_route" version="1.00"'
        ' date="2026-09-24T00:00:00" north="0.0" south="0.0" east="0.0" west="0.0"/>\n'
        '  <road name="Long" length="{total:.1f}" id="1" junction="-1">\n'
        '    <link><successor elementType="road" elementId="2" contactPoint="start"/></link>\n'
        '    <planView><geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="{total:.1f}"><line/></geometry></planView>\n'
        '    <lanes>\n{sections}    </lanes>\n  </road>\n'
        '  <road name="Tail" length="20.0" id="2" junction="-1">\n'
        '    <link><predecessor elementType="road" elementId="1" contactPoint="end"/></link>\n'
        '    <planView><geometry s="0.0" x="{total:.1f}" y="0.0" hdg="0.0" length="20.0"><line/></geometry></planView>\n'
        '    <lanes>\n'
        '      <laneSection s="0.0">\n'
        '        <center><lane id="0" type="none" level="false"><link/></lane></center>\n'
        '        <right>\n'
        '          <lane id="-1" type="driving" level="false"><link><predecessor id="-1"/></link>'
        '<width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/></lane>\n'
        '        </right>\n'
        '      </laneSection>\n'
        '    </lanes>\n  </road>\n</OpenDRIVE>\n'
    ).format(total=total, sections="".join(sections))

    with open(os.path.join(LONG_ROUTE_DIR, "long_route.xodr"), "w") as fh:
        fh.write(xodr)

    xosc = LONG_ROUTE_XOSC
    xosc_path = os.path.join(LONG_ROUTE_DIR, "long_route.xosc")
    with open(xosc_path, "w") as fh:
        fh.write(xosc)
    return os.path.relpath(xosc_path, _REPO_ROOT).replace(os.sep, "/")


LONG_ROUTE_XOSC = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <FileHeader revMajor="1" revMinor="1" date="2026-09-24T00:00:00"
              description="HVD route UDP fragmentation worst case (spine-work:osi-logical-lane S4)" author="gt"/>
  <ParameterDeclarations/>
  <CatalogLocations/>
  <RoadNetwork><LogicFile filepath="long_route.xodr"/></RoadNetwork>
  <Entities>
    <ScenarioObject name="Ego">
      <Vehicle name="probe_car" vehicleCategory="car">
        <BoundingBox><Center x="1.4" y="0.0" z="0.9"/><Dimensions width="2.0" length="5.0" height="1.8"/></BoundingBox>
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
          <PrivateAction><TeleportAction><Position>
            <LanePosition roadId="1" laneId="-1" offset="0" s="5"/>
          </Position></TeleportAction></PrivateAction>
          <PrivateAction><RoutingAction><AssignRouteAction>
            <Route name="long_route" closed="false">
              <Waypoint routeStrategy="shortest"><Position>
                <LanePosition roadId="1" laneId="-1" offset="0" s="5"/>
              </Position></Waypoint>
              <Waypoint routeStrategy="shortest"><Position>
                <LanePosition roadId="2" laneId="-1" offset="0" s="15"/>
              </Position></Waypoint>
            </Route>
          </AssignRouteAction></RoutingAction></PrivateAction>
          <PrivateAction><LongitudinalAction><SpeedAction>
            <SpeedActionDynamics dynamicsShape="step" value="0.0" dynamicsDimension="time"/>
            <SpeedActionTarget><AbsoluteTargetSpeed value="20"/></SpeedActionTarget>
          </SpeedAction></LongitudinalAction></PrivateAction>
        </Private>
      </Actions>
    </Init>
    <StopTrigger><ConditionGroup><Condition name="stop" delay="0" conditionEdge="none">
      <ByValueCondition><SimulationTimeCondition value="60.0" rule="greaterThan"/></ByValueCondition>
    </Condition></ConditionGroup></StopTrigger>
  </Storyboard>
</OpenSCENARIO>
"""


# --------------------------------------------------------------------------------------
# in-process worker: drives GT_esminiLib and dumps per-frame route facts to JSON
# --------------------------------------------------------------------------------------
_WORKER = r'''
import sys, os, json
REPO_ROOT = %(repo)r
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))          # esmini osi3 bindings
sys.path.insert(0, os.path.join(REPO_ROOT, "GT_esmini", "scripts", "verification"))

out      = %(out)r
osi_file = %(osi_file)r
runs     = %(runs)r      # [[label, xosc, max_time], ...] -- all in ONE process
dll      = %(dll)r
send_udp = %(send_udp)r

res = {"runs": [], "error": None}
try:
    import osi3.osi_groundtruth_pb2 as gtpb
    import osi3.osi_hostvehicledata_pb2 as hvpb
    from gt_lib import GtLib

    gt = GtLib(dll_path=dll)

    for run_idx, (label, xosc, max_time) in enumerate(runs):
        r = {"label": label, "xosc": xosc, "init_rc": None, "frames": [],
             "logical_lane_ids": 0, "static_record_bytes": 0, "error": None}
        args = ["--osc", os.path.join(REPO_ROOT, xosc), "--headless",
                "--fixed_timestep", "0.05", "--disable_stdout"]
        rc = gt.init_with_args(args)
        r["init_rc"] = rc
        if rc != 0:
            res["runs"].append(r)
            continue

        if send_udp:
            # Opens the GroundTruth socket AND sets OSI frequency 1. HVD goes out on
            # 48199 from GT_Step regardless; this just makes the run representative of
            # a live streaming session.
            gt.open_osi_socket()

        # The static GroundTruth exists in exactly one place: the FIRST record of the
        # .osi file. SE_GetOSIGroundTruth returns dynamic_gt only after init, so
        # reading lanes from it would measure ~2 KB of moving objects (S0 handover 4).
        this_osi = osi_file + ".%%d" %% run_idx
        gt.lib.SE_EnableOSIFile(this_osi.encode("utf-8"))
        gt.step(0.05)
        gt.lib.SE_FlushOSIFile()

        logical_ids = set()
        try:
            with open(this_osi, "rb") as fh:
                raw = fh.read()
            rec_len = int.from_bytes(raw[0:4], "little")
            data = raw[4:4 + rec_len]
            g = gtpb.GroundTruth()
            g.ParseFromString(data)
            logical_ids = {ll.id.value for ll in g.logical_lane}
            r["static_record_bytes"] = rec_len
            r["osi_lane_count"] = len(g.lane)
        except Exception as exc:                      # noqa: BLE001
            r["error"] = "static read failed: %%s" %% exc
        r["logical_lane_ids"] = len(logical_ids)

        t = 0.05
        while t < max_time:
            gt.step(0.05)
            t += 0.05
            blob = gt.get_osi_host_vehicle_data()
            if not blob:
                continue
            hv = hvpb.HostVehicleData()
            hv.ParseFromString(blob)
            tel = gt.get_vd_telemetry() or {}
            rl = tel.get("route_lane", {})
            ego = tel.get("ego", {})

            f = {"t": round(t, 3), "hvd_bytes": len(blob),
                 "has_route": hv.HasField("route"),
                 "route_id": hv.route.route_id.value if hv.HasField("route") else None,
                 "n_segments": len(hv.route.route_segment) if hv.HasField("route") else 0,
                 "n_lane_segments": 0, "dangling": 0, "asc": 0, "desc": 0,
                 "first_start_s": None, "last_end_s": None,
                 "ego_track": ego.get("track"), "ego_s": ego.get("s"),
                 "tel_valid": rl.get("valid"), "tel_reason": rl.get("reason"),
                 "tel_on_route": rl.get("on_route"),
                 "tel_s_along_route": rl.get("s_along_route"),
                 "tel_route_length": rl.get("route_length")}
            if hv.HasField("route"):
                for si, seg in enumerate(hv.route.route_segment):
                    for ls in seg.lane_segment:
                        f["n_lane_segments"] += 1
                        if ls.logical_lane_id.value not in logical_ids:
                            f["dangling"] += 1
                        if ls.start_s < ls.end_s:
                            f["asc"] += 1
                        elif ls.start_s > ls.end_s:
                            f["desc"] += 1
                    if si == 0 and seg.lane_segment:
                        f["first_start_s"] = seg.lane_segment[0].start_s
                    if si == len(hv.route.route_segment) - 1 and seg.lane_segment:
                        f["last_end_s"] = seg.lane_segment[0].end_s
            r["frames"].append(f)

        gt.close()
        res["runs"].append(r)
except Exception as exc:                              # noqa: BLE001
    import traceback
    res["error"] = traceback.format_exc()

with open(out, "w") as fh:
    json.dump(res, fh)
'''


def _run_worker(dll, runs, flag_on, send_udp=False):
    """One subprocess, so the GT_OSI_LOGICAL_LANE latch is fresh."""
    out_fd, out_path = tempfile.mkstemp(suffix=".json")
    os.close(out_fd)
    osi_path = os.path.join(tempfile.gettempdir(), "gt_hvd_route_probe.osi")
    src = _WORKER % {
        "repo": _REPO_ROOT,
        "out": out_path,
        "osi_file": osi_path,
        "runs": runs,
        "dll": dll,
        "send_udp": send_udp,
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
        timeout=900,
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


# --------------------------------------------------------------------------------------
# UDP: capture the real datagrams, then replay them through BOTH shipped receivers
# --------------------------------------------------------------------------------------
_UDP_WORKER = r'''
import sys, os, json, socket, threading
REPO_ROOT = %(repo)r
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))
sys.path.insert(0, os.path.join(REPO_ROOT, "GT_esmini", "scripts", "verification"))
out   = %(out)r
xosc  = %(xosc)r
dll   = %(dll)r
port  = %(port)d
max_t = %(max_time)f

res = {"error": None, "datagrams": [], "init_rc": None}
try:
    from gt_lib import GtLib

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 22)
    sock.bind(("127.0.0.1", port))
    sock.settimeout(0.5)

    captured = []
    stop = threading.Event()

    def rx():
        while not stop.is_set():
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                break
            captured.append(data)

    th = threading.Thread(target=rx, daemon=True)
    th.start()

    gt = GtLib(dll_path=dll)
    rc = gt.init_with_args(["--osc", os.path.join(REPO_ROOT, xosc), "--headless",
                            "--fixed_timestep", "0.05", "--disable_stdout"])
    res["init_rc"] = rc
    if rc == 0:
        # Required, and measured to be required: the logical-lane index is filled by the
        # static-GroundTruth post-pass, which only runs once OSI GroundTruth output is
        # actually being produced. Without this the route comes out with zero segments
        # and the HVD stays at ~384 B -- which is exactly what the first run of this
        # probe recorded before the call was added.
        gt.open_osi_socket()
        t = 0.0
        while t < max_t:
            gt.step(0.05)
            t += 0.05
        gt.close()

    import time
    time.sleep(0.6)
    stop.set()
    th.join(timeout=2.0)
    sock.close()

    res["datagrams"] = [d.hex() for d in captured]
except Exception:                                     # noqa: BLE001
    import traceback
    res["error"] = traceback.format_exc()

with open(out, "w") as fh:
    json.dump(res, fh)
'''


def _capture_udp(dll, xosc, max_time, flag_on=True):
    out_fd, out_path = tempfile.mkstemp(suffix=".json")
    os.close(out_fd)
    src = _UDP_WORKER % {
        "repo": _REPO_ROOT,
        "out": out_path,
        "xosc": xosc,
        "dll": dll,
        "port": HVD_PORT,
        "max_time": max_time,
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
        timeout=900,
    )
    try:
        with open(out_path) as fh:
            result = json.load(fh)
    except Exception:  # noqa: BLE001
        result = {"error": "udp worker produced no JSON\n%s\n%s" % (proc.stdout[-4000:], proc.stderr[-4000:]),
                  "datagrams": []}
    for path in (out_path, py_path):
        try:
            os.unlink(path)
        except OSError:
            pass
    return result


def _replay_through_osi_bridge(datagrams):
    """Feed the captured datagrams into the web backend's own _OSIProtocol."""
    # osi_bridge.py imports `GT_esmini.web.backend.config`, so BOTH the repo root and
    # the web package root have to be importable.
    sys.path.insert(0, _REPO_ROOT)
    sys.path.insert(0, os.path.join(_REPO_ROOT, "GT_esmini", "web"))
    from backend.services import osi_bridge  # noqa: PLC0415

    class _Sink:
        def __init__(self):
            self.messages = []

    stream = osi_bridge._StreamState()  # noqa: SLF001
    received = []

    class _Q:
        def put_nowait(self, msg):
            received.append(msg)

    stream.subscribers = {"probe": _Q()}
    proto = osi_bridge._OSIProtocol(stream, "hvd")  # noqa: SLF001
    for d in datagrams:
        proto.datagram_received(d, ("127.0.0.1", HVD_PORT))
    return received


def _replay_through_udp_common(datagrams):
    """Run DriverScript's own reassembly loop over the captured datagrams.

    udp_common.OSIReceiver owns a socket bound to the GroundTruth port and parses
    GroundTruth. Only the transport underneath it and the message type are swapped;
    the reassembly loop that is under test is the shipped one, called unmodified.
    """
    sys.path.insert(0, os.path.join(_REPO_ROOT, "DriverScript"))
    from osi3.osi_hostvehicledata_pb2 import HostVehicleData  # noqa: PLC0415
    from realdriver import udp_common  # noqa: PLC0415

    class _Replay:
        def __init__(self, packets):
            self._packets = list(packets)

        def receive(self):
            if not self._packets:
                raise StopIteration
            return self._packets.pop(0)

        def close(self):
            pass

    receiver = udp_common.OSIReceiver.__new__(udp_common.OSIReceiver)
    receiver.udp_receiver = _Replay(datagrams)
    receiver.osi_msg = HostVehicleData()

    messages = []
    while True:
        try:
            msg = receiver.receive()
        except StopIteration:
            break
        if msg is not None:
            messages.append(msg.ByteSize())
    return messages


# --------------------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=DEFAULT_DLL)
    ap.add_argument("--skip-udp", action="store_true", help="skip the fragmentation check")
    args = ap.parse_args()

    if not os.path.isfile(args.dll):
        print("FAIL: DLL not found: %s (build Release first)" % args.dll)
        return 2

    os.makedirs(OUT_DIR, exist_ok=True)
    report = {"dll": args.dll, "checks": [], "on": None, "off": None, "udp": None}
    failures = []

    def check(name, ok, detail=""):
        report["checks"].append({"name": name, "pass": bool(ok), "detail": detail})
        if not ok:
            failures.append("%s: %s" % (name, detail))
        print("  [%s] %-42s %s" % ("PASS" if ok else "FAIL", name, detail))

    long_xosc = _write_long_route_fixture()
    DESTINATION_S["long_route_fragmentation"] = 15.0
    runs = [[name, path, t] for name, path, t in FIXTURES]
    # The generated worst case runs in the SAME process as the real fixtures, which also
    # makes it the third distinct route -> the third route_id.
    runs.append(["long_route_fragmentation", long_xosc, 2.0])

    print("== GT_OSI_LOGICAL_LANE=1 ==")
    on = _run_worker(args.dll, runs, flag_on=True)
    report["on"] = _summarise(on)
    if on.get("error"):
        check("worker_on", False, on["error"][-600:])
        _write(report)
        return 1

    prev_max_route_id = 0
    for r in on["runs"]:
        label = r["label"]
        if r["init_rc"] != 0:
            check("%s.init" % label, False, "init rc=%s" % r["init_rc"])
            continue
        frames = [f for f in r["frames"] if f["has_route"]]
        check("%s.route_present" % label, bool(frames),
              "%d/%d frames carry route (logical_lane=%d)" % (len(frames), len(r["frames"]), r["logical_lane_ids"]))
        if not frames:
            continue

        dangling = sum(f["dangling"] for f in frames)
        total = sum(f["n_lane_segments"] for f in frames)
        check("%s.reference_closure" % label, dangling == 0,
              "%d/%d lane_segments reference a live logical_lane" % (total - dangling, total))

        asc = sum(f["asc"] for f in frames)
        desc = sum(f["desc"] for f in frames)
        report.setdefault("polarity", {})[label] = {"asc": asc, "desc": desc}
        print("      polarity: start_s<end_s %d / start_s>end_s %d" % (asc, desc))

        # First segment starts at the ego's own s. Scoped to frames where the ego is
        # actually ON a road of the plan: route_lane.valid is exactly that predicate, and
        # for an ego that has left the plan the expansion deliberately falls back to the
        # route's own first waypoint instead (route_valid_off_target_lane_for_exit_ramp
        # drives off the plan on purpose, so an unscoped check measures the fallback).
        # A DefaultController ego publishes no telemetry at all, hence the None guard.
        endpoint_frames = [f for f in frames
                           if f["ego_s"] is not None and f["first_start_s"] is not None
                           and f["tel_on_route"] is True]
        if endpoint_frames:
            worst = max(abs(f["first_start_s"] - f["ego_s"]) for f in endpoint_frames)
            check("%s.first_segment_at_ego_s" % label, worst < 1e-6,
                  "max |first_start_s - ego_s| = %.3g m over %d on-route frames" % (worst, len(endpoint_frames)))
        # ... and the measured reason the scope is on_route and not simply "on a plan
        # road": where the route's own lane does not EXIST yet, the route legitimately
        # starts downstream of the ego. Reported, not asserted -- it is a property of the
        # map, not of the build.
        gap = [f for f in frames
               if f["ego_s"] is not None and f["first_start_s"] is not None and f["tel_on_route"] is False]
        if gap:
            worst_gap = max(abs(f["first_start_s"] - f["ego_s"]) for f in gap)
            print("      route starts downstream of the ego on %d off-route frames "
                  "(max %.1f m): the planned lane does not exist at the ego yet"
                  % (len(gap), worst_gap))
            report.setdefault("downstream_start", {})[label] = {"frames": len(gap), "max_gap_m": worst_gap}

        # Last segment ends at the destination waypoint s, and stays there: the
        # destination does not move while the route does not change.
        dest = DESTINATION_S.get(label)
        last_vals = sorted({round(f["last_end_s"], 6) for f in frames if f["last_end_s"] is not None})
        if dest is not None and last_vals:
            check("%s.last_segment_at_destination_s" % label,
                  len(last_vals) == 1 and abs(last_vals[0] - dest) < 1e-6,
                  "last_end_s values seen %s, scenario destination waypoint s = %s" % (last_vals, dest))

        # off_route frames must carry -1, never 0 (0 reads as "at the start").
        off = [f for f in frames if f["tel_on_route"] is False]
        if off:
            bad = [f["tel_s_along_route"] for f in off if f["tel_s_along_route"] != -1.0]
            check("%s.L2_off_route_is_minus_one" % label, not bad,
                  "%d off-route frames, %d with a value other than -1" % (len(off), len(bad)))

        ids = sorted({f["route_id"] for f in frames})
        check("%s.route_id_stable" % label, len(ids) == 1,
              "route_id values seen: %s (constant == the plan cache held)" % ids)
        if ids:
            check("%s.route_id_monotonic" % label, ids[0] > prev_max_route_id,
                  "%s > previous max %d" % (ids[0], prev_max_route_id))
            prev_max_route_id = max(ids)

        # L2, where a VirtualDriver publishes telemetry.
        prog = [(f["t"], f["tel_s_along_route"]) for f in frames
                if f["tel_on_route"] is True and f["tel_s_along_route"] is not None]
        if prog:
            non_monotonic = sum(1 for i in range(1, len(prog)) if prog[i][1] < prog[i - 1][1] - 1e-9)
            check("%s.L2_monotonic" % label, non_monotonic == 0,
                  "s_along_route %.2f -> %.2f m over %d on-route frames, %d regressions"
                  % (prog[0][1], prog[-1][1], len(prog), non_monotonic))

        sizes = [f["hvd_bytes"] for f in frames]
        print("      HVD bytes: min %d max %d | lane_segments/frame max %d"
              % (min(sizes), max(sizes), max(f["n_lane_segments"] for f in frames)))
        report.setdefault("sizes", {})[label] = {"min": min(sizes), "max": max(sizes)}

    pol = report.get("polarity", {})
    tot_asc = sum(v["asc"] for v in pol.values())
    tot_desc = sum(v["desc"] for v in pol.values())
    check("direction_both_polarities", tot_asc > 0 and tot_desc > 0,
          "ascending %d, descending %d (either at 0 would pass a one-sided check)" % (tot_asc, tot_desc))

    print("== GT_OSI_LOGICAL_LANE unset (default OFF) ==")
    off = _run_worker(args.dll, runs, flag_on=False)
    report["off"] = _summarise(off)
    if off.get("error"):
        check("worker_off", False, off["error"][-600:])
    else:
        for r in off["runs"]:
            with_route = sum(1 for f in r["frames"] if f["has_route"])
            check("%s.no_route_when_off" % r["label"], with_route == 0 and r["frames"],
                  "%d/%d frames carry a route field" % (with_route, len(r["frames"])))
            check("%s.no_logical_lanes_when_off" % r["label"], r["logical_lane_ids"] == 0,
                  "logical_lane[] = %d" % r["logical_lane_ids"])

    if not args.skip_udp:
        print("== UDP transport, both shipped receivers ==")
        report["udp"] = {}

        # (a) THE SPLIT PATH -- the one this stage had to make work. Long route, every
        #     frame over the 8192 B datagram budget.
        cap = _capture_udp(args.dll, long_xosc, 2.0)
        if cap.get("error"):
            check("udp_capture_split", False, cap["error"][-600:])
        else:
            datagrams = [bytes.fromhex(h) for h in cap["datagrams"]]
            counters = [struct.unpack_from("iI", d)[0] for d in datagrams if len(d) >= 8]
            singles = [c for c in counters if c == 0]
            bridge_msgs = _replay_through_osi_bridge(datagrams)
            common_msgs = _replay_through_udp_common(datagrams)
            biggest = max((len(m) for m in bridge_msgs), default=0)
            report["udp"]["split"] = {
                "datagrams": len(datagrams), "single_packet_datagrams": len(singles),
                "max_datagram": max((len(d) for d in datagrams), default=0),
                "osi_bridge_messages": len(bridge_msgs),
                "udp_common_messages": len(common_msgs),
                "max_reassembled_bytes": biggest,
            }
            check("udp_capture_split", bool(datagrams),
                  "%d datagrams on %d, largest single datagram %d B"
                  % (len(datagrams), HVD_PORT, report["udp"]["split"]["max_datagram"]))
            check("udp_over_8192", biggest > 8192,
                  "largest reassembled HostVehicleData = %d B from %d-packet frames"
                  % (biggest, len(datagrams) // max(len(bridge_msgs), 1)))
            check("osi_bridge_reassembly_split", len(bridge_msgs) > 0,
                  "%d complete messages" % len(bridge_msgs))
            check("udp_common_reassembly_split", len(common_msgs) == len(bridge_msgs) and len(common_msgs) > 0,
                  "%d complete messages (osi_bridge: %d)" % (len(common_msgs), len(bridge_msgs)))

        # (b) THE UNSPLIT PATH -- a real map, whose HostVehicleData fits one datagram and
        #     therefore still goes out as counter == 0, exactly as before this stage.
        #     Measured rather than assumed, because the two receivers do NOT agree here:
        #     see the printout below.
        cap1 = _capture_udp(args.dll, FIXTURES[0][1], 3.0)
        if cap1.get("error"):
            check("udp_capture_single", False, cap1["error"][-600:])
        else:
            d1 = [bytes.fromhex(h) for h in cap1["datagrams"]]
            c1 = [struct.unpack_from("iI", d)[0] for d in d1 if len(d) >= 8]
            b1 = _replay_through_osi_bridge(d1)
            u1 = _replay_through_udp_common(d1)
            report["udp"]["single"] = {
                "datagrams": len(d1), "all_counter_zero": all(c == 0 for c in c1),
                "osi_bridge_messages": len(b1), "udp_common_messages": len(u1),
            }
            check("udp_single_packet_unchanged", bool(d1) and all(c == 0 for c in c1),
                  "%d datagrams, all counter == 0 (pre-existing behaviour preserved)" % len(d1))
            check("osi_bridge_reassembly_single", len(b1) == len(d1),
                  "%d complete messages from %d datagrams" % (len(b1), len(d1)))
            # NOT a gate. udp_common.OSIReceiver only ends a message on a NEGATIVE
            # counter, so a counter == 0 single packet leaves it waiting for a
            # continuation that never comes. Latent for its own use (the GroundTruth
            # sender numbers from 1 and always negates the last packet, so it never
            # emits 0), and it does not affect the split path above.
            print("      udp_common on counter==0 datagrams: %d/%d messages completed "
                  "(it terminates only on a NEGATIVE counter -- see the report)"
                  % (len(u1), len(d1)))
            report["udp"]["single"]["udp_common_note"] = (
                "udp_common.OSIReceiver never completes a counter==0 single-packet message; "
                "osi_bridge special-cases counter==0 and does.")

    _write(report)
    print()
    if failures:
        print("FAIL (%d):" % len(failures))
        for f in failures:
            print("  - " + f)
        return 1
    print("PASS -- report: %s" % os.path.join(OUT_DIR, "hvd_route_probe.json"))
    return 0


def _summarise(result):
    """Frames are large; keep counts and the first/last frame per run."""
    out = {"worker_rc": result.get("worker_rc"), "error": result.get("error"), "runs": []}
    for r in result.get("runs", []):
        frames = r.get("frames", [])
        out["runs"].append({
            "label": r["label"], "init_rc": r["init_rc"],
            "logical_lane_ids": r.get("logical_lane_ids"),
            "static_record_bytes": r.get("static_record_bytes"),
            "osi_lane_count": r.get("osi_lane_count"),
            "n_frames": len(frames),
            "first_frame": frames[0] if frames else None,
            "last_frame": frames[-1] if frames else None,
        })
    return out


def _write(report):
    path = os.path.join(OUT_DIR, "hvd_route_probe.json")
    with open(path, "w") as fh:
        json.dump(report, fh, indent=2)


if __name__ == "__main__":
    sys.exit(main())
