#!/usr/bin/env python3
"""
OSI Light State Verification Script
Receives OSI GroundTruth messages via UDP and verifies light state output.
"""

import socket
import struct
import sys
import os

# Add esmini scripts path for osi3 module
script_dir = os.path.dirname(os.path.abspath(__file__))
gt_esmini_dir = os.path.dirname(script_dir)  # GT_esmini directory
esmini_root = os.path.dirname(gt_esmini_dir)  # esmini root
osi_path = os.path.join(esmini_root, 'scripts')
sys.path.insert(0, osi_path)

from osi3.osi_groundtruth_pb2 import GroundTruth

def receive_osi_udp(host='127.0.0.1', port=48198, timeout=30):
    """
    Receive OSI messages via UDP and verify light states.
    
    Args:
        host: UDP host to bind to
        port: UDP port to listen on
        timeout: Timeout in seconds
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    sock.settimeout(timeout)
    
    print(f"Listening for OSI messages on {host}:{port}")
    print("Waiting for data... (timeout: {timeout}s)")
    print("-" * 80)
    
    message_count = 0
    light_state_found = False
    
    try:
        while True:
            try:
                data, addr = sock.recvfrom(65536)  # Max UDP packet size
                
                # Parse OSI message
                gt = GroundTruth()
                gt.ParseFromString(data)
                
                message_count += 1
                timestamp = gt.timestamp.seconds + gt.timestamp.nanos * 1e-9
                
                print(f"\n[Message #{message_count}] Time: {timestamp:.3f}s")
                
                # Check each moving object
                for obj in gt.moving_object:
                    if obj.type == 2:  # TYPE_VEHICLE
                        obj_id = obj.id.value
                        
                        # Check if vehicle_classification exists
                        if obj.HasField('vehicle_classification'):
                            vc = obj.vehicle_classification
                            
                            # Check if light_state exists
                            if vc.HasField('light_state'):
                                light_state_found = True
                                ls = vc.light_state
                                
                                print(f"  Vehicle ID {obj_id}:")
                                print(f"    Brake Light:     {ls.brake_light_state} ({get_brake_light_name(ls.brake_light_state)})")
                                print(f"    Indicator:       {ls.indicator_state} ({get_indicator_name(ls.indicator_state)})")
                                print(f"    Reversing Light: {ls.reversing_light} ({get_generic_light_name(ls.reversing_light)})")
                                print(f"    Head Light:      {ls.head_light} ({get_generic_light_name(ls.head_light)})")
                                print(f"    High Beam:       {ls.high_beam} ({get_generic_light_name(ls.high_beam)})")
                                print(f"    Front Fog:       {ls.front_fog_light} ({get_generic_light_name(ls.front_fog_light)})")
                                print(f"    Rear Fog:        {ls.rear_fog_light} ({get_generic_light_name(ls.rear_fog_light)})")
                            else:
                                print(f"  Vehicle ID {obj_id}: No light_state field")
                        else:
                            print(f"  Vehicle ID {obj_id}: No vehicle_classification field")
                
                if not light_state_found and message_count == 1:
                    print("  ⚠ Warning: No light state data found in first message")
                
            except socket.timeout:
                print(f"\nTimeout after {timeout}s")
                break
                
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
    finally:
        sock.close()
        
    print("-" * 80)
    print(f"\nSummary:")
    print(f"  Total messages received: {message_count}")
    print(f"  Light state data found: {'✓ YES' if light_state_found else '✗ NO'}")
    
    return light_state_found

def get_brake_light_name(state):
    """Get human-readable brake light state name."""
    names = {
        0: "OFF",
        1: "NORMAL",
        2: "STRONG"
    }
    return names.get(state, f"UNKNOWN({state})")

def get_indicator_name(state):
    """Get human-readable indicator state name."""
    names = {
        0: "OFF",
        1: "LEFT",
        2: "RIGHT",
        3: "WARNING"
    }
    return names.get(state, f"UNKNOWN({state})")

def get_generic_light_name(state):
    """Get human-readable generic light state name."""
    names = {
        0: "OFF",
        1: "ON",
        2: "FLASHING_BLUE",
        3: "FLASHING_BLUE_AND_RED",
        4: "FLASHING_AMBER"
    }
    return names.get(state, f"UNKNOWN({state})")

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Verify OSI light state output via UDP')
    parser.add_argument('--host', default='127.0.0.1', help='UDP host (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=48198, help='UDP port (default: 48198)')
    parser.add_argument('--timeout', type=int, default=30, help='Timeout in seconds (default: 30)')
    
    args = parser.parse_args()
    
    success = receive_osi_udp(args.host, args.port, args.timeout)
    sys.exit(0 if success else 1)
