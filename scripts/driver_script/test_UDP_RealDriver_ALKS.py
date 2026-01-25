import time
import argparse
import math
import os
import sys

# Add parent directory to path to find modules
current_dir = os.path.dirname(os.path.abspath(__file__))
scripts_dir = os.path.join(current_dir, '..')
udp_driver_dir = os.path.join(scripts_dir, 'udp_driver')

if scripts_dir not in sys.path:
    sys.path.append(scripts_dir)
if udp_driver_dir not in sys.path:
    sys.path.append(udp_driver_dir)

from RealDriverClient import RealDriverClient
from ALKS_module import LKASController
from esminiRMLib import EsminiRMLib
from OSIReceiver import OSIReceiverWrapper

def main():
    parser = argparse.ArgumentParser(description="Test script for ALKS LKAS Module with RealDriver & OSI")
    parser.add_argument("--ip", type=str, default="127.0.0.1", help="esmini Host IP")
    parser.add_argument("--port", type=int, default=53995, help="RealDriver Base Port")
    parser.add_argument("--osi_port", type=int, default=48198, help="OSI Port")
    parser.add_argument("--id", type=int, default=0, help="Object ID (Ego)")
    parser.add_argument("--lib_path", type=str, default="./bin/esminiRMLib.dll", help="Path to esminiRMLib.dll")
    parser.add_argument("--xodr_path", type=str, required=True, help="Path to OpenDRIVE map file (.xodr)")
    
    args = parser.parse_args()

    # 1. Initialize RealDriverClient (OSI HostVehicleData + LightMask protocol)
    print(f"Connecting to RealDriver via UDP at {args.ip}:{args.port} for Object ID {args.id}")
    client = RealDriverClient(args.ip, args.port)
    
    # 2. Initialize EsminiRMLib
    print(f"Loading esminiRMLib from {args.lib_path}")
    if not os.path.exists(args.lib_path):
        print(f"Error: DLL not found at {args.lib_path}")
        return

    try:
        rm_lib = EsminiRMLib(args.lib_path)
    except Exception as e:
        print(f"Failed to load EsminiRMLib: {e}")
        return

    print(f"Initializing RoadManager with map: {args.xodr_path}")
    if rm_lib.Init(args.xodr_path) < 0:
        print("Failed to initialize RoadManager with provided map.")
        return

    # 3. Initialize OSI Receiver
    print(f"Initializing OSI Receiver on port {args.osi_port}")
    # Set a short timeout (e.g. 0.1s) to allow loop to continue if no data
    osi_rx = OSIReceiverWrapper(port=args.osi_port)
    osi_rx.receiver.udp_receiver.sock.settimeout(0.1)

    # 4. Initialize LKAS Controller
    print("Initializing LKAS Controller (ReferenceDriver)...")
    # Tuning PID: kp may need adjustment depending on steering ratio and units
    lkas = LKASController(rm_lib, model_type='ReferenceDriver', kp=1.5, ki=0.01, kd=0.1)

    # No specific gear setting packet in this simple mode, usually handled by 'driverInput' mode automatically or assumed D.

    print("\nStarting control loop. Press Ctrl+C to stop.")
    
    try:
        # Initial control values
        throttle = 0.0
        brake = 0.0
        steering_angle = 0.0
        
        last_time = time.time()
        frame_number = 0

        while True:
            # --- 1. Get Sensor Data (OSI) ---
            # This call blocks until message received (or timeout)
            ego_state = osi_rx.get_ego_state(args.id)
            
            current_time = time.time()
            dt = current_time - last_time
            last_time = current_time
            if dt <= 0: dt = 0.001

            if ego_state and ego_state['valid']:
                ego_x = ego_state['x']
                ego_y = ego_state['y']
                ego_z = ego_state['z']
                ego_h = ego_state['h']
                speed = ego_state['speed']
                
                # --- 2. Update LKAS ---
                # Only update if we have valid speed (avoid steering at standstill if noise)
                if speed > 0.1:
                    steering_angle = lkas.update(ego_x, ego_y, ego_z, ego_h, dt)
                
                # --- 3. Simple Speed Control (Cruise) ---
                target_speed = 10.0 # m/s (36km/h)
                
                # Forcing throttle for testing
                if speed < target_speed:
                    throttle = 0.5 
                    brake = 0.0
                else:
                    throttle = 0.0
                    brake = 0.0

                # Print status
                if frame_number % 20 == 0:
                    print(f"Pos: ({ego_x:.2f}, {ego_y:.2f}) | Speed: {speed:.2f} | Steer: {steering_angle:.3f} | Thr: {throttle:.2f} | Brk: {brake:.2f}")

            else:
                # OSI Timeout or Invalid ID
                if ego_state and 'error' in ego_state:
                    print(f"Waiting for valid OSI data... {ego_state['error']}")
                else:
                    print("Waiting for valid OSI data... (sending keep-alive controls)")
                
                # Send keep-alive controls (zero throttle) to keep connection active
                throttle = 0.0
                brake = 0.0
                steering_angle = 0.0

            # --- 4. Send Controls via RealDriverClient ---
            # Uses OSI HostVehicleData + LightMask protocol
            # Note: Negate steering_angle to compensate for sign inversion in RealVehicle.cpp
            # (see RealVehicle.cpp:260 - target_wheel_angle = -steering * steer_max)
            client.set_controls(throttle, brake, -steering_angle)
            client.set_gear(1)  # Drive
            client.send_update()
            frame_number += 1
            
            # Avoid busy loop if OSI is fast (though OSI receive blocks usually)
            # time.sleep(0.001)

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        print("Closing connections...")
        osi_rx.close()
        rm_lib.Close()
        if 'client' in locals():
            client.close()
        print("Done.")

if __name__ == "__main__":
    main()
