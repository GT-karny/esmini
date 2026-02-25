import struct

from realdriver.protocol.lon_profile import (
    LON_PROFILE_PACKET_TYPE,
    parse_lon_profile_packet,
    interpolate_speed,
)


def test_parse_lon_profile_packet_valid():
    payload = bytes([LON_PROFILE_PACKET_TYPE]) + struct.pack("<I", 2)
    payload += struct.pack("<dddd", 0.0, 10.0, 3.0, 8.0)
    payload += struct.pack("<dddd", 0.5, 12.0, 3.0, 8.0)

    points = parse_lon_profile_packet(payload)
    assert len(points) == 2
    assert points[0].v_target == 10.0
    assert points[1].t_offset == 0.5


def test_interpolate_speed_linear():
    payload = bytes([LON_PROFILE_PACKET_TYPE]) + struct.pack("<I", 2)
    payload += struct.pack("<dddd", 0.0, 10.0, 3.0, 8.0)
    payload += struct.pack("<dddd", 1.0, 14.0, 3.0, 8.0)
    points = parse_lon_profile_packet(payload)

    assert interpolate_speed(points, 0.25) == 11.0
    assert interpolate_speed(points, 2.0) == 14.0
