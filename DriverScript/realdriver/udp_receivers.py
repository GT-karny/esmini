"""
UDP Receivers Module

Provides standalone UDP receivers for waypoints and longitudinal profiles.
"""

import socket
from typing import Optional, Tuple, List

from .waypoint import Waypoint, parse_waypoints_from_udp
from .protocol.lon_profile import LonProfilePoint, parse_lon_profile_packet, interpolate_speed


class LongitudinalProfileReceiver:
    """
    UDP receiver for longitudinal profile packets (type=3).

    Packet format:
        - byte 0: packet type (3)
        - bytes 1-4: uint32 point count
        - repeated points: (t_offset, v_target, a_max, j_max) as little-endian doubles
    """

    PACKET_TYPE = 3

    def __init__(self, port: int = 54995, host: str = "127.0.0.1"):
        self.port = port
        self.host = host
        self._sock: Optional[socket.socket] = None
        self._last_profile: List[LonProfilePoint] = []

        self._setup_socket()

    def _setup_socket(self) -> None:
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.bind((self.host, self.port))
            self._sock.setblocking(False)
            print(f"[INFO] LongitudinalProfileReceiver: Listening on {self.host}:{self.port}")
        except Exception as e:
            print(f"[WARN] LongitudinalProfileReceiver: Failed to setup socket: {e}")
            self._sock = None

    def receive(self) -> Optional[List[LonProfilePoint]]:
        if self._sock is None:
            return None

        try:
            while True:
                data, _ = self._sock.recvfrom(65535)
                try:
                    profile = parse_lon_profile_packet(data)
                    self._last_profile = profile
                    return profile
                except ValueError:
                    continue
        except BlockingIOError:
            pass
        except Exception:
            pass

        return None

    def receive_all(self) -> Optional[List[LonProfilePoint]]:
        latest = None
        while True:
            profile = self.receive()
            if profile is None:
                break
            latest = profile
        return latest

    @property
    def last_profile(self) -> List[LonProfilePoint]:
        return self._last_profile

    def speed_at(self, t_offset: float = 0.0) -> Optional[float]:
        if not self._last_profile:
            return None
        return interpolate_speed(self._last_profile, t_offset)

    def close(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    def __del__(self):
        self.close()


class WaypointReceiver:
    """
    UDP receiver for waypoint data.

    Listens for waypoint packets from esmini ControllerRealDriver.

    Packet format: See waypoint.parse_waypoints_from_udp()
    """

    def __init__(self, port: int = 54996, host: str = "127.0.0.1"):
        self.port = port
        self.host = host
        self._sock: Optional[socket.socket] = None
        self._last_data: Optional[bytes] = None
        self._last_index: int = 0
        self._last_waypoints: List[Waypoint] = []

        self._setup_socket()

    def _setup_socket(self) -> None:
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.bind((self.host, self.port))
            self._sock.setblocking(False)
            print(f"[INFO] WaypointReceiver: Listening on {self.host}:{self.port}")
        except Exception as e:
            print(f"[WARN] WaypointReceiver: Failed to setup socket: {e}")
            self._sock = None

    def receive(self) -> Optional[Tuple[int, List[Waypoint]]]:
        if self._sock is None:
            return None

        try:
            while True:
                data, _ = self._sock.recvfrom(65535)

                if data == self._last_data:
                    continue

                self._last_data = data

                try:
                    index, waypoints = parse_waypoints_from_udp(data)
                    self._last_index = index
                    self._last_waypoints = waypoints
                    return (index, waypoints)
                except ValueError as e:
                    print(f"[WARN] WaypointReceiver: Parse error: {e}")

        except BlockingIOError:
            pass
        except Exception as e:
            print(f"[WARN] WaypointReceiver: Error receiving: {e}")

        return None

    def receive_all(self) -> Optional[Tuple[int, List[Waypoint]]]:
        latest = None
        while True:
            result = self.receive()
            if result is None:
                break
            latest = result
        return latest

    @property
    def last_index(self) -> int:
        return self._last_index

    @property
    def last_waypoints(self) -> List[Waypoint]:
        return self._last_waypoints

    def close(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    def __del__(self):
        self.close()
