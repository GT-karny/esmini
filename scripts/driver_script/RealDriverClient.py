import socket
import struct
import time
import sys
import os

# Add parent directory to path to find osi3 module which is in ../osi3 relative to this script
current_dir = os.path.dirname(os.path.abspath(__file__))
scripts_dir = os.path.join(current_dir, '..')
if scripts_dir not in sys.path:
    sys.path.append(scripts_dir)

try:
    from osi3 import osi_hostvehicledata_pb2
    from osi3.osi_hostvehicledata_pb2 import HostVehicleData
except ImportError:
    print("Error: Could not import osi_hostvehicledata_pb2. Make sure it generated and in python path.")
    # Fallback class for IDE linting if needed, but runtime will fail
    HostVehicleData = None

# Enums for high-level API
class LightMode:
    OFF = 0
    LOW = 1
    HIGH = 2

class IndicatorMode:
    OFF = 0
    LEFT = 1
    RIGHT = 2
    HAZARD = 3

class RealDriverClient:
    """
    Client for controlling esmini RealDriverController via UDP using OSI HostVehicleData.
    Sends a 4-byte LightMask followed by serialized HostVehicleData.
    """
    
    def __init__(self, ip="127.0.0.1", port=53995):
        """
        Initialize the RealDriverClient.
        
        Args:
            ip (str): IP address of the esmini host.
            port (int): Port number (default 53995).
        """
        self.ip = ip
        self.port = port
        
        print(f"RealDriverClient: Target IP={self.ip}, Port={self.port}")
        
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        
        # Initialize HostVehicleData
        self.hvd = HostVehicleData()
        
        # Initialize basic fields
        self.hvd.vehicle_basics.operating_state = osi_hostvehicledata_pb2.HostVehicleData.VehicleBasics.OPERATING_STATE_DRIVING
        self.hvd.vehicle_powertrain.gear_transmission = 1
        self.hvd.vehicle_steering.vehicle_steering_wheel.angle = 0.0
        
        self.frame_number = 0
        
        # Light Mask (32-bit integer)
        # Bit 0: Low Beam
        # Bit 1: High Beam
        # Bit 2: Indicator Left
        # Bit 3: Indicator Right
        # Bit 4: Fog Front
        # Bit 5: Fog Rear
        # Bit 8: License Plate (Auto-linked)
        self.light_mask = 0

    def set_controls(self, throttle, brake, steering):
        """
        Set driving controls.
        
        Args:
            throttle (float): 0.0 to 1.0
            brake (float): 0.0 to 1.0
            steering (float): Steering angle in radians.
        """
        self.hvd.vehicle_powertrain.pedal_position_acceleration = float(throttle)
        self.hvd.vehicle_brake_system.pedal_position_brake = float(brake)
        self.hvd.vehicle_steering.vehicle_steering_wheel.angle = float(steering)

    def set_gear(self, gear):
        """
        Set transmission gear.
        
        Args:
            gear (int): 1 for Drive, 0 for Neutral, -1 for Reverse
        """
        self.hvd.vehicle_powertrain.gear_transmission = int(gear)

    def set_engine_brake(self, force):
        """
        Set engine brake deceleration factor.
        For now this is a no-op as it's not mapped to OSI HVD.
        """
        pass

    def set_light_mask_bit(self, bit_index, on):
        """Helper to set/clear a specific bit in light_mask"""
        if on:
            self.light_mask |= (1 << bit_index)
        else:
            self.light_mask &= ~(1 << bit_index)

    def set_headlights(self, mode):
        """
        Set headlight mode.
        
        Args:
            mode (LightMode): OFF, LOW, or HIGH
        """
        # Clear Low/High/License bits first
        self.set_light_mask_bit(0, False) # Low
        self.set_light_mask_bit(1, False) # High
        self.set_light_mask_bit(8, False) # License Plate

        if mode == LightMode.LOW:
            self.set_light_mask_bit(0, True)
            self.set_light_mask_bit(8, True) # License plate linked
        elif mode == LightMode.HIGH:
            self.set_light_mask_bit(1, True)
            self.set_light_mask_bit(8, True) # License plate linked

    def set_indicators(self, mode):
        """
        Set indicator mode.
        
        Args:
            mode (IndicatorMode): OFF, LEFT, RIGHT, HAZARD
        """
        # Clear Indicator bits
        self.set_light_mask_bit(2, False) # Left
        self.set_light_mask_bit(3, False) # Right

        if mode == IndicatorMode.LEFT:
            self.set_light_mask_bit(2, True)
        elif mode == IndicatorMode.RIGHT:
            self.set_light_mask_bit(3, True)
        elif mode == IndicatorMode.HAZARD:
            self.set_light_mask_bit(2, True)
            self.set_light_mask_bit(3, True)

    def set_fog_lights(self, front=None, rear=None):
        """
        Set fog lights state.
        
        Args:
            front (bool, optional): State of front fog lights.
            rear (bool, optional): State of rear fog lights.
        """
        if front is not None:
            self.set_light_mask_bit(4, front)
        if rear is not None:
            self.set_light_mask_bit(5, rear)

    # Deprecated / Legacy support if needed, but updated to use new mask logic
    def set_lights(self, mask):
        """Set raw light mask directly."""
        self.light_mask = mask

    def set_light_state(self, light_type, on):
        """Legacy string-based setter mapped to new logic"""
        if light_type == 'low':
            # This is tricky because string interface implies individual control
            # We'll map 'low' to bit 0 directly
            self.set_light_mask_bit(0, on)
            # Link license plate if turning ON
            if on: self.set_light_mask_bit(8, True)
        elif light_type == 'high':
            self.set_light_mask_bit(1, on)
            if on: self.set_light_mask_bit(8, True)
        elif light_type == 'left':
            self.set_light_mask_bit(2, on)
        elif light_type == 'right':
            self.set_light_mask_bit(3, on)
        elif light_type == 'hazard':
            self.set_indicators(IndicatorMode.HAZARD if on else IndicatorMode.OFF)
        elif light_type == 'fog_front':
            self.set_fog_lights(front=on)
        elif light_type == 'fog_rear':
            self.set_fog_lights(rear=on)

    def set_adas_function(self, function_name, state, custom_name=None):
        """
        Set ADAS function state.
        """
        found = False
        for func in self.hvd.vehicle_automated_driving_function:
            if func.custom_name == function_name:
                func.state = state
                found = True
                break
        
        if not found:
            func = self.hvd.vehicle_automated_driving_function.add()
            func.name = osi_hostvehicledata_pb2.HostVehicleData.VehicleAutomatedDrivingFunction.NAME_OTHER
            func.custom_name = function_name
            func.state = state

    def send_update(self):
        """
        Send the current HostVehicleData to esmini via UDP.
        Prefixes the data with the 4-byte Light Mask (Little Endian).
        """
        try:
            # Serialize HVD
            hvd_data = self.hvd.SerializeToString()
            
            # Create Packet: [LightMask (4 bytes)] + [HVD Data]
            # using 'i' for signed int (matches C++ int), little-endian
            packet = struct.pack('<i', self.light_mask) + hvd_data
            
            # Send
            self.sock.sendto(packet, (self.ip, self.port))
            
            if self.frame_number % 50 == 0:
                print(f"UDP Sent (Frame {self.frame_number}): Port={self.port}, Thr={self.hvd.vehicle_powertrain.pedal_position_acceleration:.2f}, Brk={self.hvd.vehicle_brake_system.pedal_position_brake:.2f}")

            self.frame_number += 1

        except Exception as e:
            print(f"Error sending UDP: {e}")

    def close(self):
        """Close the UDP socket."""
        self.sock.close()

