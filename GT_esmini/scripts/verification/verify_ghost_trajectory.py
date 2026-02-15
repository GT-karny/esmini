#!/usr/bin/env python3
"""
OSI Ghost Trajectory Verification Script
Receives OSI GroundTruth messages via UDP and verifies future_trajectory output.
"""

import socket
import sys
import os
import time
import struct

# Add esmini scripts path for osi3 module
script_dir = os.path.dirname(os.path.abspath(__file__))
gt_esmini_dir = os.path.dirname(script_dir)  # GT_esmini directory
esmini_root = os.path.dirname(gt_esmini_dir)  # esmini root
osi_path = os.path.join(esmini_root, 'scripts')
sys.path.insert(0, osi_path)

try:
    from osi3.osi_groundtruth_pb2 import GroundTruth
except ImportError:
    print(f"Error: Could not import osi3 module from {osi_path}")
    sys.exit(1)

def receive_osi_udp(host='127.0.0.1', port=48198, timeout=10):
    """
    Receive OSI messages via UDP and verify future_trajectory.
    Handles esmini's custom header/splitting protocol.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    sock.settimeout(timeout)
    
    print(f"Listening for OSI messages on {host}:{port}")
    print(f"Waiting for data... (timeout: {timeout}s)")
    print("-" * 80)
    
    message_count = 0
    traj_found = False
    
    # buffersize must match esmini's expected MAX size + header
    # udp_osi_common.py uses 8208, but we can be safe with a bit more
    buffersize = 65536 
    
    try:
        next_index = 1
        complete_msg = b''
        
        while True:
            try:
                data, addr = sock.recvfrom(buffersize)
                
                # Unpack header: int counter, uint size
                # 'iI' = signed int, unsigned int. Native byte order.
                header_size = 8
                if len(data) < header_size:
                    continue
                    
                counter, size = struct.unpack('iI', data[:header_size])
                frame = data[header_size:]
                
                # Validate size
                if len(frame) != size:
                    # print(f"Warning: Frame size mismatch {len(frame)} vs {size}")
                    continue
                
                # Logic from udp_osi_common.py
                if counter == 1:
                    complete_msg = b''
                    next_index = 1
                
                if counter == 1 or abs(counter) == next_index:
                    complete_msg += frame
                    next_index += 1
                    
                    if counter < 0: # End of message
                        # Parse complete message
                        gt = GroundTruth()
                        gt.ParseFromString(complete_msg)
                        
                        message_count += 1
                        timestamp = gt.timestamp.seconds + gt.timestamp.nanos * 1e-9
                        
                        if message_count % 10 == 0:
                             print(f"\r[Message #{message_count}] Time: {timestamp:.3f}s - Processing...", end="")

                        for obj in gt.moving_object:
                            if len(obj.future_trajectory) > 0:
                                traj_found = True
                                print(f"\n[Found Trajectory] Msg #{message_count} Time: {timestamp:.3f}s")
                                print(f"  Object ID: {obj.id.value}")
                                print(f"  Points: {len(obj.future_trajectory)}")
                                
                                for i, point in enumerate(obj.future_trajectory[:5]):
                                    t = point.timestamp.seconds + point.timestamp.nanos * 1e-9
                                    x = point.position.x
                                    y = point.position.y
                                    h = point.orientation.yaw
                                    print(f"    Pt {i}: T={t:.3f}, X={x:.2f}, Y={y:.2f}, H={h:.2f}")
                                
                                if len(obj.future_trajectory) > 5:
                                    print("    ...")
                        
                        # Reset for next message? Or assume we are done finding ONE?
                        # User wants verification, let's look for a few frames
                        if traj_found and message_count > 20: 
                             raise KeyboardInterrupt # Break out to success
                else:
                    # Out of sync
                    next_index = 1
                    
            except socket.timeout:
                print(f"\nTimeout after {timeout}s")
                break
            except Exception as e:
                print(f"\nError processing message: {e}")
                import traceback
                traceback.print_exc()
                # reset buffer
                next_index = 1
                complete_msg = b''
                
    except KeyboardInterrupt:
        print("\n\nFinished verification")
    finally:
        sock.close()
        
    print("-" * 80)
    print(f"\nSummary:")
    print(f"  Total messages received: {message_count}")
    print(f"  Future Trajectory data found: {'✓ YES' if traj_found else '✗ NO'}")
    
    return traj_found

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=48198)
    parser.add_argument('--timeout', type=int, default=10)
    args = parser.parse_args()
    
    success = receive_osi_udp(args.host, args.port, args.timeout)
    sys.exit(0 if success else 1)
