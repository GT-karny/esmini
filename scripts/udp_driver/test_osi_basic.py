'''
   Simple OSI receiver to verify basic OSI communication is working.
   This script focuses on displaying basic vehicle state (position, velocity, acceleration)
   to diagnose OSI connectivity before testing light states.

   Prerequisites:
      Python 3

   Python dependencies:
      pip install protobuf==3.20.2

   To run it:
   1. Start THIS script first in terminal 1:
      python ./scripts/udp_driver/test_osi_basic.py
   2. Then start GT_Sim in terminal 2:
      ./bin/GT_Sim.exe --osc ./scripts/udp_driver/real_driver_test.xosc --osi 127.0.0.1
   3. Finally start RealDriver control in terminal 3:
      python ./scripts/udp_driver/testUDP_RealDriver.py --id 0
'''

from udp_osi_common import *
import time

def print_basic_info(msg, counter):
    """Print basic OSI information without light states"""

    print("\n" + "="*80)
    print(f"[Frame {counter}] OSI message timestamp: {msg.timestamp.seconds + msg.timestamp.nanos * 1e-9:.3f} seconds")

    # Print lane info (only in first few messages)
    if counter <= 3 and len(msg.lane) > 0:
        print(f"  {len(msg.lane)} lanes")
        for i, l in enumerate(msg.lane):
            print(f"    [Lane {i}] id={l.id.value} type={l.classification.type}")

    # Print stationary objects (only in first few messages)
    if counter <= 3 and len(msg.stationary_object) > 0:
        print(f"  {len(msg.stationary_object)} stationary objects")
        for i, s in enumerate(msg.stationary_object):
            print(f"    [Obj {i}] id={s.id.value} type={s.classification.type}")

    # Print moving objects (every frame)
    print(f"  {len(msg.moving_object)} moving object(s)")
    for i, o in enumerate(msg.moving_object):
        print(f"\n  [Vehicle {i}] ID: {o.id.value}")
        print(f"    Position:     x={o.base.position.x:7.2f} y={o.base.position.y:7.2f} heading={o.base.orientation.yaw:6.3f}")
        print(f"    Velocity:     x={o.base.velocity.x:7.2f} y={o.base.velocity.y:7.2f} yaw_rate={o.base.orientation_rate.yaw:6.3f}")
        print(f"    Acceleration: x={o.base.acceleration.x:7.2f} y={o.base.acceleration.y:7.2f}")

        # Check if vehicle_classification exists
        has_vehicle_class = o.HasField('vehicle_classification')
        has_light_state = False
        if has_vehicle_class:
            has_light_state = o.vehicle_classification.HasField('light_state')

        print(f"    vehicle_classification: {'YES' if has_vehicle_class else 'NO'}")
        print(f"    light_state field:      {'YES' if has_light_state else 'NO'}")


if __name__ == "__main__":
    print("="*80)
    print("OSI Basic Receiver - Connectivity Test")
    print("="*80)
    print("\nThis script will:")
    print("1. Listen for OSI messages on UDP port 48198")
    print("2. Display basic vehicle state (position, velocity, acceleration)")
    print("3. Check if vehicle_classification and light_state fields exist")
    print("\nMake sure to:")
    print("- START THIS SCRIPT FIRST")
    print("- Then run: bin/GT_Sim.exe --osc scripts/udp_driver/real_driver_test.xosc --osi 127.0.0.1")
    print("- GT_Sim console should show: 'Enabling OSI output to 127.0.0.1'")
    print("\nPress Ctrl+C to quit\n")
    print("="*80)

    # Create OSI receiver
    osiReceiver = OSIReceiver()
    done = False
    counter = 0
    last_timeout_time = time.time()

    print("\n[Waiting] Listening on UDP port 48198...")

    while not done:
        try:
            msg = osiReceiver.receive()
            counter += 1
            print_basic_info(msg, counter)

            # Reset timeout counter
            last_timeout_time = time.time()

        except timeout:
            current_time = time.time()
            elapsed = current_time - last_timeout_time
            print(f'\n[{elapsed:.1f}s] Still waiting for OSI messages...')
            print('  Make sure GT_Sim is running with --osi 127.0.0.1 option')

        except KeyboardInterrupt:
            print('\n\n[Quit] Ctrl+C pressed')
            done = True
        except Exception as e:
            print(f'\n[Error] Unexpected exception: {e}')
            import traceback
            traceback.print_exc()
            done = True

    # Cleanup
    osiReceiver.close()
    print(f"\nOSI receiver closed. Received {counter} messages total.")
    print("="*80)
