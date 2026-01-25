from udp_osi_common import UdpReceiver, OSIReceiver as CommonOSIReceiver
from osi3.osi_groundtruth_pb2 import GroundTruth
from udp_osi_common import UdpReceiver, OSIReceiver as CommonOSIReceiver
from osi3.osi_groundtruth_pb2 import GroundTruth
import struct
import math
import socket

class OSIReceiverWrapper:
    """
    Wrapper around OSIReceiver to easily fetch Ego vehicle data.
    """
    def __init__(self, port=48198):
        self.receiver = CommonOSIReceiver()
        
        # udp_osi_common.OSIReceiver binds to 48198 by default.
        # If user requests a different port, we must close the default one and recreate.
        if port != 48198:
            self.receiver.udp_receiver.close()
            self.receiver.udp_receiver = UdpReceiver(ip='127.0.0.1', port=port)
        
        self.last_msg = None

    def get_ego_state(self, ego_id):
        """
        Receive next OSI message and extract ego vehicle state.
        
        Args:
            ego_id (int): Object ID of the ego vehicle.

        Returns:
            dict: { 'x', 'y', 'z', 'h', 'speed', 'valid' } or None if error/timeout
        """
        try:
            self.last_msg = self.receiver.receive()
            if self.last_msg is None:
                return None
            
            # Find Ego Vehicle
            # Priority:
            # 1. Specified ego_id (if found)
            # 2. host_vehicle_id (if valid and found)
            # 3. First MovingObject (fallback)
            
            ego_obj = None
            
            # 1. Try specified ID
            for obj in self.last_msg.moving_object:
                if obj.id.value == ego_id:
                    ego_obj = obj
                    break
            
            # 2. Try host_vehicle_id if not found
            if ego_obj is None and self.last_msg.HasField('host_vehicle_id'):
                host_id = self.last_msg.host_vehicle_id.value
                for obj in self.last_msg.moving_object:
                    if obj.id.value == host_id:
                        ego_obj = obj
                        # print(f"Using host_vehicle_id: {host_id}")
                        break
            
            # 3. Fallback to first moving object
            if ego_obj is None and len(self.last_msg.moving_object) > 0:
                ego_obj = self.last_msg.moving_object[0]
                # print(f"Ego not found by ID {ego_id}, using first object ID: {ego_obj.id.value}")
            
            if ego_obj:
                pos = ego_obj.base.position
                ori = ego_obj.base.orientation
                vel = ego_obj.base.velocity
                
                # OSI Orientation is Roll/Pitch/Yaw
                # esmini/OpenDRIVE Heading is Yaw (counter-clockwise from East? or North?)
                # OSI Yaw is around Z-axis. ISO 8855: x=forward, y=left, z=up. Yaw is rotation around Z.
                # 0 is along X-axis. 
                
                speed = math.sqrt(vel.x**2 + vel.y**2 + vel.z**2)
                
                return {
                    'x': pos.x,
                    'y': pos.y,
                    'z': pos.z,
                    'h': ori.yaw,
                    'speed': speed,
                    'valid': True
                }
            else:
                # Fallback: Check HostVehicleData if available? 
                # GroundTruth usually contains ALL objects.
                # If ID not found, return None or Invalid
                found_ids = [obj.id.value for obj in self.last_msg.moving_object]
                return {'valid': False, 'error': f'ID {ego_id} not found. Received IDs: {found_ids}'}

        except socket.timeout:
            # Expected timeout if non-blocking
            return None
        except Exception as e:
            print(f"OSI Receive Error: {e}")
            return None

    def close(self):
        self.receiver.close()
