from dataclasses import dataclass
import struct
from typing import List


LON_PROFILE_PACKET_TYPE = 3
LON_PROFILE_POINT_FORMAT = "<dddd"
LON_PROFILE_POINT_SIZE = struct.calcsize(LON_PROFILE_POINT_FORMAT)


@dataclass(frozen=True)
class LonProfilePoint:
    t_offset: float
    v_target: float
    a_max: float
    j_max: float


def parse_lon_profile_packet(data: bytes) -> List[LonProfilePoint]:
    if len(data) < 5:
        raise ValueError(f"Packet too small: {len(data)} bytes")

    packet_type = data[0]
    if packet_type != LON_PROFILE_PACKET_TYPE:
        raise ValueError(f"Invalid packet type: {packet_type}, expected {LON_PROFILE_PACKET_TYPE}")

    count = struct.unpack("<I", data[1:5])[0]
    expected = 5 + count * LON_PROFILE_POINT_SIZE
    if len(data) < expected:
        raise ValueError(f"Packet size mismatch: got {len(data)}, expected {expected}")

    points: List[LonProfilePoint] = []
    offset = 5
    for _ in range(count):
        t_offset, v_target, a_max, j_max = struct.unpack(
            LON_PROFILE_POINT_FORMAT,
            data[offset:offset + LON_PROFILE_POINT_SIZE],
        )
        points.append(LonProfilePoint(t_offset=t_offset, v_target=v_target, a_max=a_max, j_max=j_max))
        offset += LON_PROFILE_POINT_SIZE

    return points


def interpolate_speed(points: List[LonProfilePoint], query_t: float) -> float:
    if not points:
        return 0.0

    if query_t <= points[0].t_offset:
        return points[0].v_target

    for i in range(len(points) - 1):
        a = points[i]
        b = points[i + 1]
        if a.t_offset <= query_t <= b.t_offset:
            dt = max(1e-9, b.t_offset - a.t_offset)
            alpha = (query_t - a.t_offset) / dt
            return a.v_target + alpha * (b.v_target - a.v_target)

    return points[-1].v_target
