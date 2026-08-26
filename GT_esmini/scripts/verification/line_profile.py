"""Lateral profile OF THE REPORTED LINE ITSELF, point by point.

Every earlier instrument measured where the VEHICLE went. These three defects are about
the shape of the LINE, so the line is what has to be measured: for one frame, convert each
future_trajectory point to a lane-relative lateral offset against a fixed reference lane,
so "the first N points are in the next lane, the rest snap back" is directly visible.

usage: line_profile.py <osi file> <ref lane id> <t> [t ...]
"""

import math
import os
import struct
import sys

ROOT = r"E:\Repository\GT_esmini\esmini"
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "GT_esmini", "scripts"))
from osi3.osi_groundtruth_pb2 import GroundTruth  # noqa: E402
from rm_lib import EsminiRMLib  # noqa: E402

SP = os.path.dirname(os.path.abspath(__file__))


def osi_frames(path):
    with open(path, "rb") as f:
        while True:
            hdr = f.read(4)
            if len(hdr) < 4:
                break
            n = struct.unpack("I", hdr)[0]
            buf = f.read(n)
            if len(buf) < n:
                break
            gt = GroundTruth()
            gt.ParseFromString(buf)
            yield gt


name = sys.argv[1]
odr = sys.argv[2]
ref_lane = int(sys.argv[3])
times = [float(x) for x in sys.argv[4:]]

rm = EsminiRMLib(os.path.join(ROOT, "bin", "esminiRMLib.dll"))
assert rm.Init(os.path.join(ROOT, "resources", "xodr", odr)) == 0, "odr load failed"
h_pt = rm.CreatePosition()
h_ref = rm.CreatePosition()


def lateral(x, y, h):
    """Signed offset from the centre of `ref_lane` at this point's own s. + = left."""
    rm.SetWorldXYHPosition(h_pt, x, y, h)
    _rc, d = rm.GetPositionData(h_pt)
    rm.SetLanePosition(h_ref, d.roadId, ref_lane, 0.0, d.s)
    _rc2, ref = rm.GetPositionData(h_ref)
    dx, dy = x - ref.x, y - ref.y
    return d.s, d.laneId, -dx * math.sin(ref.h) + dy * math.cos(ref.h)


for gt in osi_frames(os.path.join(SP, name)):
    t = gt.timestamp.seconds + gt.timestamp.nanos * 1e-9
    if not any(abs(t - tt) < 0.026 for tt in times):
        continue
    if not gt.moving_object:
        continue
    e = gt.moving_object[0]
    b = e.base
    es, elane, elat = lateral(b.position.x, b.position.y, b.orientation.yaw)
    print(
        "=== t=%.2f  ego: lane=%d s=%.1f lat=%+.2f  (ref lane %d)  n_points=%d"
        % (t, elane, es, elat, ref_lane, len(e.future_trajectory))
    )
    for i, p in enumerate(e.future_trajectory):
        pt = p.timestamp.seconds + p.timestamp.nanos * 1e-9
        s, lane, lat = lateral(p.position.x, p.position.y, p.orientation.yaw)
        print(
            "   [%2d] +%5.2fs  lane=%-3d s=%6.1f  lat=%+.2f" % (i, pt - t, lane, s, lat)
        )
    print()

rm.Close()
