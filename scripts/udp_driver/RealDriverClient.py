import socket
import struct
import time

class RealDriverClient:
    """
    Client for controlling esmini RealDriverController via UDP.
    """
    
    def __init__(self, ip="127.0.0.1", port=53995, object_id=0):
        """
        Initialize the RealDriverClient.
        
        Args:
            ip (str): IP address of the esmini host.
            port (int): Base port number (default 53995). The actual port will be base_port + object_id.
            object_id (int): ID of the object to control.
        """
        self.ip = ip
        self.port = port + object_id
        self.object_id = object_id
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.frame_number = 0
        
        # Control state
        self.throttle = 0.0
        self.brake = 0.0
        self.steering = 0.0
        self.gear = 1  # 1: Drive, 0: Neutral, -1: Reverse
        
        # Light state mask (Bitmask)
        # Bit 0: Low Beam
        # Bit 1: High Beam
        # Bit 2: Left Indicator
        # Bit 3: Right Indicator
        # Bit 4: Hazard
        # Bit 5: Fog Front
        # Bit 6: Fog Rear
        self.light_mask = 0
        
        # Engine Brake (Default 2.0 -> now 0.49/0.05G requested, stick to API default or user controlled)
        # We start with 0.49 to match C++ default if not set.
        self.engine_brake = 0.49
        

    def set_controls(self, throttle, brake, steering):
        """
        Set driving controls.
        
        Args:
            throttle (float): 0.0 to 1.0
            brake (float): 0.0 to 1.0
            steering (float): -1.0 (Right) to 1.0 (Left)
        """
        self.throttle = float(throttle)
        self.brake = float(brake)
        self.steering = float(steering)

    def set_gear(self, gear):
        """
        Set transmission gear.
        
        Args:
            gear (int): 1 for Drive, 0 for Neutral, -1 for Reverse
        """
        self.gear = int(gear)

    def set_engine_brake(self, force):
        """
        Set engine brake deceleration factor.
        
        Args:
            force (float): Deceleration force factor (approx m/s^2). Default 0.49 (0.05G).
        """
        self.engine_brake = float(force)

    def set_lights(self, mask):
        """
        Set light mask directly.
        
        Args:
            mask (int): Integer bitmask for lights.
        """
        self.light_mask = int(mask)

    def set_light_state(self, light_type, on):
        """
        Set individual light state.
        
        Args:
            light_type (str): 'low', 'high', 'left', 'right', 'hazard', 'fog_front', 'fog_rear'
            on (bool): True to turn on, False to turn off
        """
        bit_map = {
            'low': 1,
            'high': 2,
            'left': 4,
            'right': 8,
            'hazard': 16,
            'fog_front': 32,
            'fog_rear': 64
        }
        
        if light_type in bit_map:
            bit = bit_map[light_type]
            if on:
                self.light_mask |= bit
            else:
                self.light_mask &= ~bit
        else:
            print(f"Unknown light type: {light_type}")

    def send_update(self):
        """
        Send the current control state to esmini via UDP.
        Should be called periodically (e.g., 50Hz).
        """
        try:
            # Format: <iiiiddddId
            # int: version (1)
            # int: inputMode (0 = driverInput)
            # int: objectId
            # int: frameNumber
            # double: throttle
            # double: brake
            # double: steering 
            # double: gear
            # uint: lightMask
            # double: engineBrake
            
            packed_data = struct.pack('<iiiiddddId', 
                                      1,                  # Version
                                      0,                  # InputMode (0=Driver)
                                      self.object_id, 
                                      self.frame_number, 
                                      self.throttle, 
                                      self.brake, 
                                      self.steering,      
                                      float(self.gear),
                                      self.light_mask,
                                      self.engine_brake
                                      )
            
            self.sock.sendto(packed_data, (self.ip, self.port))
            self.frame_number += 1
            
        except Exception as e:
            print(f"Error sending UDP: {e}")

    def close(self):
        """Close the UDP socket."""
        self.sock.close()
