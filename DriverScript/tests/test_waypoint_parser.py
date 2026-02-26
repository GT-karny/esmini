import struct

from realdriver.waypoint import parse_waypoints_from_udp


def test_parse_waypoints_from_udp_valid():
    packet = bytearray()
    packet += bytes([2])
    packet += struct.pack("<II", 1, 2)
    packet += struct.pack("<dddI4xdi4xd", 1.0, 2.0, 0.1, 5, 10.0, -1, 0.3)
    packet += struct.pack("<dddI4xdi4xd", 3.0, 4.0, 0.2, 6, 20.0, -2, 0.0)

    idx, wps = parse_waypoints_from_udp(bytes(packet))
    assert idx == 1
    assert len(wps) == 2
    assert wps[0].road_id == 5
    assert wps[1].lane_id == -2
