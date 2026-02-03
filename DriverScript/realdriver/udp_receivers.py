"""
UDP Receivers Module

Provides standalone UDP receivers for waypoints and target speed,
decoupled from the controller logic.
"""

import socket
import struct
from typing import Optional, Tuple, List

from .waypoint import Waypoint, parse_waypoints_from_udp


class TargetSpeedReceiver:
    """
    UDP receiver for target speed commands.

    Listens for target speed packets from esmini ControllerRealDriver.

    Packet format:
        - byte 0: packet type (1 = target speed)
        - bytes 1-8: double (little-endian) speed in m/s

    Example:
        receiver = TargetSpeedReceiver(port=54995)

        # In control loop:
        speed = receiver.receive()  # Returns None if no data
        if speed is not None:
            controller.set_target_speed(speed)
    """

    PACKET_TYPE = 1
    PACKET_SIZE = 9

    def __init__(self, port: int = 54995, host: str = "127.0.0.1"):
        """
        Initialize target speed receiver.

        Args:
            port: UDP port to listen on
            host: Host address to bind to
        """
        self.port = port
        self.host = host
        self._sock: Optional[socket.socket] = None
        self._last_speed: Optional[float] = None

        self._setup_socket()

    def _setup_socket(self) -> None:
        """Setup UDP socket."""
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.bind((self.host, self.port))
            self._sock.setblocking(False)
            print(f"[INFO] TargetSpeedReceiver: Listening on {self.host}:{self.port}")
        except Exception as e:
            print(f"[WARN] TargetSpeedReceiver: Failed to setup socket: {e}")
            self._sock = None

    def receive(self) -> Optional[float]:
        """
        Receive target speed from UDP (non-blocking).

        Returns:
            Target speed in m/s if received, None if no data available
        """
        if self._sock is None:
            return None

        try:
            while True:
                data, _ = self._sock.recvfrom(1024)
                if len(data) == self.PACKET_SIZE and data[0] == self.PACKET_TYPE:
                    speed = struct.unpack('<d', data[1:9])[0]
                    self._last_speed = speed
                    return speed
        except BlockingIOError:
            pass  # No data available
        except Exception:
            pass  # Ignore errors

        return None

    def receive_all(self) -> Optional[float]:
        """
        Receive all pending packets and return the latest speed.

        Returns:
            Latest target speed in m/s if any received, None otherwise
        """
        latest_speed = None
        while True:
            speed = self.receive()
            if speed is None:
                break
            latest_speed = speed
        return latest_speed

    @property
    def last_speed(self) -> Optional[float]:
        """Last received speed value."""
        return self._last_speed

    def close(self) -> None:
        """Close the UDP socket."""
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

    Example:
        receiver = WaypointReceiver(port=54996)

        # In control loop:
        result = receiver.receive()
        if result is not None:
            index, waypoints = result
            controller.set_waypoints(waypoints[index:])
    """

    def __init__(self, port: int = 54996, host: str = "127.0.0.1"):
        """
        Initialize waypoint receiver.

        Args:
            port: UDP port to listen on
            host: Host address to bind to
        """
        self.port = port
        self.host = host
        self._sock: Optional[socket.socket] = None
        self._last_data: Optional[bytes] = None
        self._last_index: int = 0
        self._last_waypoints: List[Waypoint] = []

        self._setup_socket()

    def _setup_socket(self) -> None:
        """Setup UDP socket."""
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.bind((self.host, self.port))
            self._sock.setblocking(False)
            print(f"[INFO] WaypointReceiver: Listening on {self.host}:{self.port}")
        except Exception as e:
            print(f"[WARN] WaypointReceiver: Failed to setup socket: {e}")
            self._sock = None

    def receive(self) -> Optional[Tuple[int, List[Waypoint]]]:
        """
        Receive waypoints from UDP (non-blocking).

        Returns:
            Tuple of (current_waypoint_index, waypoints) if received,
            None if no new data available
        """
        if self._sock is None:
            return None

        try:
            while True:
                data, _ = self._sock.recvfrom(8192)

                # Skip if same as last data (avoid reprocessing)
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
            pass  # No data available
        except Exception as e:
            print(f"[WARN] WaypointReceiver: Error receiving: {e}")

        return None

    def receive_all(self) -> Optional[Tuple[int, List[Waypoint]]]:
        """
        Receive all pending packets and return the latest waypoints.

        Returns:
            Latest (index, waypoints) if any received, None otherwise
        """
        latest = None
        while True:
            result = self.receive()
            if result is None:
                break
            latest = result
        return latest

    @property
    def last_index(self) -> int:
        """Last received waypoint index."""
        return self._last_index

    @property
    def last_waypoints(self) -> List[Waypoint]:
        """Last received waypoint list."""
        return self._last_waypoints

    def close(self) -> None:
        """Close the UDP socket."""
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    def __del__(self):
        self.close()
