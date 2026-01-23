'''
   HostVehicleData UDP Receiver Test Script

   This script verifies that GT_HostVehicleReporter is correctly
   sending OSI HostVehicleData via UDP.

   Prerequisites:
      Python 3
      pip install protobuf==3.20.2

   Usage:
   1. Start THIS script first:
      python GT_esmini/scripts/test_hostvehicledata.py

   2. Start GT_Sim with a RealDriver scenario:
      ./bin/GT_Sim.exe --osc resources/xosc/test_real_driver_lights.xosc

   3. (Optional) Start RealDriverClient to send control inputs:
      python scripts/udp_driver/testUDP_RealDriver.py --id 0
'''

from socket import AF_INET, SOCK_DGRAM, socket, timeout
import struct
import os
import sys
import argparse
import math

# Add scripts root directory to module search path for osi3
SCRIPTS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', 'scripts'))
sys.path.append(SCRIPTS_DIR)

try:
    from osi3.osi_hostvehicledata_pb2 import HostVehicleData
    PROTOBUF_AVAILABLE = True
except ImportError:
    PROTOBUF_AVAILABLE = False
    print("[WARNING] osi_hostvehicledata_pb2 not found. Running in raw mode.")
    print("          To enable full parsing, add osi_hostvehicledata_pb2.py to scripts/osi3/")


DEFAULT_PORT = 48199
BUFFER_SIZE = 8208  # MAX OSI data size + header (two ints)


class HostVehicleDataReceiver:
    def __init__(self, ip='127.0.0.1', port=DEFAULT_PORT, timeout_sec=-1):
        self.sock = socket(AF_INET, SOCK_DGRAM)
        if timeout_sec >= 0:
            self.sock.settimeout(timeout_sec)
        self.sock.bind((ip, port))
        self.port = port
        if PROTOBUF_AVAILABLE:
            self.hv_data = HostVehicleData()

    def receive(self):
        """Receive and parse HostVehicleData message"""
        done = False
        next_index = 1
        complete_msg = b''

        # Large messages might be split into multiple parts
        while not done:
            msg = self.sock.recvfrom(BUFFER_SIZE)[0]

            # Extract message parts: counter(int) + size(unsigned int)
            header_size = 4 + 4
            if len(msg) < header_size:
                print(f"[ERROR] Message too short: {len(msg)} bytes")
                return None, 0

            counter, size = struct.unpack('iI', msg[:header_size])
            frame = msg[header_size:]

            if len(frame) != size:
                print(f"[ERROR] Size mismatch: header says {size}, got {len(frame)}")
                return None, 0

            if counter == 1:  # New message
                complete_msg = b''
                next_index = 1

            # Compose complete message
            if counter == 1 or abs(counter) == next_index:
                complete_msg += frame
                next_index += 1
                if counter < 0 or counter == 0:  # Negative or 0 indicates end/single
                    done = True
            else:
                next_index = 1  # Out of sync, reset

        # Parse protobuf if available
        if PROTOBUF_AVAILABLE:
            try:
                self.hv_data.ParseFromString(complete_msg)
                return self.hv_data, len(complete_msg)
            except Exception as e:
                print(f"[ERROR] Failed to parse protobuf: {e}")
                return None, len(complete_msg)
        else:
            return complete_msg, len(complete_msg)

    def close(self):
        self.sock.close()


def rad_to_deg(rad):
    """Convert radians to degrees"""
    return rad * 180.0 / math.pi


def gear_to_string(gear):
    """Convert gear value to string"""
    if gear == -1:
        return "R (Reverse)"
    elif gear == 0:
        return "N (Neutral)"
    elif gear == 1:
        return "D (Drive)"
    else:
        return f"{gear}"


def operating_state_to_string(state):
    """Convert operating state enum to string"""
    states = {
        0: "UNKNOWN",
        1: "OTHER",
        2: "SLEEP",
        3: "STANDBY",
        4: "BOARDING",
        5: "ENTERTAINMENT",
        6: "DRIVING",
        7: "DIAGNOSTIC"
    }
    return states.get(state, f"UNKNOWN({state})")


def motor_type_to_string(motor_type):
    """Convert motor type enum to string"""
    types = {
        0: "UNKNOWN",
        1: "OTHER",
        2: "OTTO",
        3: "DIESEL",
        4: "ELECTRIC"
    }
    return types.get(motor_type, f"UNKNOWN({motor_type})")


def adas_state_to_string(state):
    """Convert ADAS function state enum to string"""
    states = {
        0: "UNKNOWN",
        1: "OTHER",
        2: "UNAVAILABLE",
        3: "AVAILABLE",
        4: "ACTIVE",
        5: "SUSPENDED"
    }
    return states.get(state, f"UNKNOWN({state})")


def print_hostvehicledata(msg, frame_count, data_size):
    """Print HostVehicleData message in formatted output"""

    print("\n" + "=" * 80)
    print(f"[Frame {frame_count}] HostVehicleData received (size: {data_size} bytes)")
    print("=" * 80)

    if not PROTOBUF_AVAILABLE:
        print(f"  Raw data: {data_size} bytes")
        print("  (Install osi_hostvehicledata_pb2.py for full parsing)")
        return

    # Location
    if msg.HasField('location'):
        loc = msg.location
        print("\n  Location:")

        if loc.HasField('position'):
            p = loc.position
            print(f"    Position:     x={p.x:10.2f} y={p.y:10.2f} z={p.z:10.2f}")

        if loc.HasField('velocity'):
            v = loc.velocity
            print(f"    Velocity:     x={v.x:10.2f} y={v.y:10.2f} z={v.z:10.2f}")

        if loc.HasField('orientation'):
            o = loc.orientation
            yaw_deg = rad_to_deg(o.yaw)
            print(f"    Orientation:  yaw={o.yaw:6.3f} pitch={o.pitch:6.3f} roll={o.roll:6.3f} ({yaw_deg:.1f} deg)")

    # Vehicle Controls
    print("\n  Vehicle Controls:")

    # Steering
    if msg.HasField('vehicle_steering'):
        vs = msg.vehicle_steering
        if vs.HasField('vehicle_steering_wheel'):
            angle = vs.vehicle_steering_wheel.angle
            angle_deg = rad_to_deg(angle)
            print(f"    Steering:     {angle:6.3f} rad ({angle_deg:.1f} deg)")

    # Powertrain (throttle, gear)
    throttle = 0.0
    gear = 1
    if msg.HasField('vehicle_powertrain'):
        vp = msg.vehicle_powertrain
        throttle = vp.pedal_position_acceleration
        gear = vp.gear_transmission
    print(f"    Throttle:     {throttle:.2f}")
    print(f"    Gear:         {gear_to_string(gear)}")

    # Brake
    brake = 0.0
    if msg.HasField('vehicle_brake_system'):
        vb = msg.vehicle_brake_system
        brake = vb.pedal_position_brake
    print(f"    Brake:        {brake:.2f}")

    # Powertrain Motor details
    if msg.HasField('vehicle_powertrain'):
        vp = msg.vehicle_powertrain
        if len(vp.motor) > 0:
            print("\n  Powertrain Motor:")
            for i, motor in enumerate(vp.motor):
                print(f"    [{i}] Type: {motor_type_to_string(motor.type)}, "
                      f"RPM: {motor.rpm:.1f}, Torque: {motor.torque:.1f} Nm")

    # Operating State
    if msg.HasField('vehicle_basics'):
        vb = msg.vehicle_basics
        print(f"\n  Operating State: {operating_state_to_string(vb.operating_state)}")

    # ADAS Functions
    if len(msg.vehicle_automated_driving_function) > 0:
        print(f"\n  ADAS Functions: {len(msg.vehicle_automated_driving_function)}")
        for i, func in enumerate(msg.vehicle_automated_driving_function):
            name = func.custom_name if func.custom_name else f"NAME_{func.name}"
            state = adas_state_to_string(func.state)
            print(f"    [{i}] {name} - {state}")

    print("=" * 80)


def print_raw_data(data, frame_count, data_size):
    """Print raw data when protobuf is not available"""
    print("\n" + "=" * 80)
    print(f"[Frame {frame_count}] HostVehicleData received (size: {data_size} bytes)")
    print("=" * 80)
    print(f"  Raw data received: {data_size} bytes")
    if data_size > 0:
        # Show first 64 bytes as hex
        preview = data[:min(64, data_size)]
        hex_str = ' '.join(f'{b:02x}' for b in preview)
        print(f"  First bytes: {hex_str}...")
    print("\n  [NOTE] Install osi_hostvehicledata_pb2.py in scripts/osi3/ for full parsing")
    print("=" * 80)


def main():
    parser = argparse.ArgumentParser(description='HostVehicleData UDP Receiver Test')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT,
                        help=f'UDP port to listen on (default: {DEFAULT_PORT})')
    parser.add_argument('--timeout', type=float, default=5.0,
                        help='Socket timeout in seconds (default: 5.0)')
    args = parser.parse_args()

    print("=" * 80)
    print("HostVehicleData UDP Receiver Test")
    print("=" * 80)
    print(f"\nListening on UDP port {args.port}")
    print(f"Protobuf parsing: {'ENABLED' if PROTOBUF_AVAILABLE else 'DISABLED (raw mode)'}")
    print("\nMake sure to:")
    print("  1. START THIS SCRIPT FIRST")
    print("  2. Run GT_Sim with a RealDriver scenario")
    print("     Example: bin/GT_Sim.exe --osc resources/xosc/test_real_driver_lights.xosc")
    print("  3. (Optional) Run RealDriverClient to send inputs")
    print("\nPress Ctrl+C to quit\n")
    print("=" * 80)

    receiver = HostVehicleDataReceiver(port=args.port, timeout_sec=args.timeout)
    frame_count = 0

    print(f"\n[Waiting] Listening on UDP port {args.port}...")

    try:
        while True:
            try:
                msg, data_size = receiver.receive()
                frame_count += 1

                if PROTOBUF_AVAILABLE and msg is not None:
                    print_hostvehicledata(msg, frame_count, data_size)
                elif msg is not None:
                    print_raw_data(msg, frame_count, data_size)

            except timeout:
                print(f"[Timeout] No data received for {args.timeout}s, still waiting...")

    except KeyboardInterrupt:
        print("\n\n[Quit] Ctrl+C pressed")

    finally:
        receiver.close()
        print(f"\nReceiver closed. Received {frame_count} messages total.")
        print("=" * 80)


if __name__ == "__main__":
    main()
