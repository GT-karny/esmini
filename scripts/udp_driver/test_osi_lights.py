'''
   OSI receiver script to display light state information.

   Prerequisites:
      Python 3
      pip install protobuf==3.20.2

   To run it:
   1. Start THIS script first:
      python ./scripts/udp_driver/test_osi_lights.py
   2. Then start GT_Sim:
      ./bin/GT_Sim.exe --osc ./scripts/udp_driver/real_driver_test.xosc --osi 127.0.0.1
   3. Finally start RealDriver control:
      python ./scripts/udp_driver/testUDP_RealDriver.py --id 0
   4. Toggle lights in the GUI and observe changes here
'''

from udp_osi_common import *
import time

def print_light_state(msg, counter):
    """Print light state information from OSI message"""

    print("\n" + "="*80)
    print(f"[Frame {counter}] OSI timestamp: {msg.timestamp.seconds + msg.timestamp.nanos * 1e-9:.3f}s")

    # Print moving objects
    print(f"  {len(msg.moving_object)} moving object(s)")

    for i, o in enumerate(msg.moving_object):
        print(f"\n  [Vehicle {i}] ID: {o.id.value}")
        print(f"    Position: x={o.base.position.x:7.2f} y={o.base.position.y:7.2f}")
        print(f"    Velocity: x={o.base.velocity.x:7.2f} y={o.base.velocity.y:7.2f}")

        # Check if vehicle_classification exists
        if not o.HasField('vehicle_classification'):
            print("    No vehicle_classification field")
            continue

        vc = o.vehicle_classification

        # Check if light_state exists
        if not vc.HasField('light_state'):
            print("    No light_state field")
            continue

        ls = vc.light_state
        print("    --- LIGHT STATE ---")

        # Indicator State (OSI v3 enum values)
        indicator_names = {
            0: "UNKNOWN",
            1: "OTHER",
            2: "OFF",
            3: "LEFT",
            4: "RIGHT",
            5: "WARNING/HAZARD"
        }
        indicator = indicator_names.get(ls.indicator_state, f"UNKNOWN({ls.indicator_state})")
        print(f"      Indicator:       {indicator}")

        # Brake Light State (OSI v3 enum values)
        brake_names = {
            0: "UNKNOWN",
            1: "OTHER",
            2: "OFF",
            3: "NORMAL",
            4: "STRONG"
        }
        brake = brake_names.get(ls.brake_light_state, f"UNKNOWN({ls.brake_light_state})")
        print(f"      Brake Light:     {brake}")

        # Generic Light State (OSI v3 enum values - used for head, high beam, reversing, fog)
        generic_light_names = {
            0: "UNKNOWN",
            1: "OTHER",
            2: "OFF",
            3: "ON",
            4: "FLASHING_BLUE",
            5: "FLASHING_BLUE_AND_RED",
            6: "FLASHING_AMBER"
        }
        head = generic_light_names.get(ls.head_light, f"UNKNOWN({ls.head_light})")
        print(f"      Head Light:      {head}")

        # High Beam
        high_beam = generic_light_names.get(ls.high_beam, f"UNKNOWN({ls.high_beam})")
        print(f"      High Beam:       {high_beam}")

        # Reversing Light
        reversing = generic_light_names.get(ls.reversing_light, f"UNKNOWN({ls.reversing_light})")
        print(f"      Reversing Light: {reversing}")

        # Fog Lights
        fog_front = generic_light_names.get(ls.front_fog_light, f"UNKNOWN({ls.front_fog_light})")
        fog_rear = generic_light_names.get(ls.rear_fog_light, f"UNKNOWN({ls.rear_fog_light})")
        print(f"      Front Fog:       {fog_front}")
        print(f"      Rear Fog:        {fog_rear}")

        print("    -------------------")


if __name__ == "__main__":
    print("="*80)
    print("OSI Light State Receiver")
    print("="*80)
    print("\nThis script displays vehicle light states from OSI messages.")
    print("\nSetup:")
    print("  1. Start THIS script first")
    print("  2. Run: bin/GT_Sim.exe --osc scripts/udp_driver/real_driver_test.xosc --osi 127.0.0.1")
    print("  3. Run: python scripts/udp_driver/testUDP_RealDriver.py --id 0")
    print("  4. Toggle lights in the RealDriver GUI")
    print("\nPress Ctrl+C to quit\n")
    print("="*80)

    # Create OSI receiver with timeout
    class OSIReceiverWithTimeout(OSIReceiver):
        def __init__(self, timeout=1.0):
            super().__init__()
            self.udp_receiver.sock.settimeout(timeout)

    osiReceiver = OSIReceiverWithTimeout(timeout=1.0)
    done = False
    counter = 0
    last_msg_time = time.time()

    print("\n[Waiting] Listening on UDP port 48198...\n")

    while not done:
        try:
            msg = osiReceiver.receive()
            counter += 1
            last_msg_time = time.time()

            # Only print every 10th frame to reduce spam
            if counter % 10 == 0:
                print_light_state(msg, counter)

        except timeout:
            elapsed = time.time() - last_msg_time
            if counter == 0:
                print(f'[{elapsed:.1f}s] Waiting for first OSI message...')
            else:
                print(f'\n[Timeout] No OSI messages for {elapsed:.1f}s (last was frame {counter})')
                print('  GT_Sim may have stopped or OSI output disabled')

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
    print(f"\nReceived {counter} total OSI messages.")
    print("="*80)
