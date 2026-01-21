'''
   This script shows how to fetch and parse OSI message on UDP socket from esmini
   with LIGHT STATE information display.

   Prerequisites:
      Python 3

   Python dependencies:
      pip install protobuf==3.20.2

   To run it:
   1. Open two terminals
   2. From terminal 1, run: python ./scripts/udp_driver/testUDPDriver-print-osi-info-with-lights.py
   3. From terminal 2, run: ./bin/GT_Sim --osc <scenario.xosc> --osi 127.0.0.1
   4. From terminal 3, run: python ./scripts/udp_driver/testUDP_RealDriver.py --id 0

   For complete driver control definitions, see esmini/EnvironmentSimulator/Modules/Controllers/ControllerUDPDriver.hpp
'''

from udp_osi_common import *

def print_osi_stuff(msg):

    print("\n" + "="*80)
    print("OSI message timestamp: {:.2f} seconds".format(msg.timestamp.seconds + msg.timestamp.nanos * 1e-9))

    # Print some static content typically only available in first message
    print("{} lanes".format(len(msg.lane)))
    for i, l in enumerate(msg.lane):
        clf = l.classification
        print("  [{}] id {} type: {}".format(i, l.id.value, clf.type))

    print('{} stationary objects'.format(len(msg.stationary_object)))
    for i, s in enumerate(msg.stationary_object):
        print('  [{}] id {} type {}'.format(i, s.id.value, s.classification.type))
        print('    pos.x {:.2f} pos.y {:.2f} rot.h {:.2f}'.format(s.base.position.x, s.base.position.y, s.base.orientation.yaw))

    # Print dynamic content from the message
    print('{} moving objects'.format(len(msg.moving_object)))
    for i, o in enumerate(msg.moving_object):
        print('\n  [{}] id {}'.format(i, o.id.value))
        print('    pos.x {:.2f} pos.y {:.2f} rot.h {:.2f}'.format(o.base.position.x, o.base.position.y, o.base.orientation.yaw))
        print('    vel.x {:.2f} vel.y {:.2f} rot_rate.h {:.2f}'.format(o.base.velocity.x, o.base.velocity.y, o.base.orientation_rate.yaw))
        print('    acc.x {:.2f} acc.y {:.2f} rot_acc.h {:.2f}'.format(o.base.acceleration.x, o.base.acceleration.y, o.base.orientation_acceleration.yaw))

        lane_id = o.assigned_lane_id[0].value if len(msg.lane) > 0 and len(o.assigned_lane_id) > 0 else -1
        left_lane_id = -1
        right_lane_id = -1
        for l in msg.lane:
            if l.id.value == o.assigned_lane_id[0].value:
                left_lane_id = l.classification.left_adjacent_lane_id[0].value if len(l.classification.left_adjacent_lane_id) > 0 else -1
                right_lane_id = l.classification.right_adjacent_lane_id[0].value if len(l.classification.right_adjacent_lane_id) > 0 else -1
                break
        print('    lane id {} left adj lane id {} right adj lane id {}'.format(lane_id, left_lane_id, right_lane_id))

        # ========== LIGHT STATE DISPLAY ==========
        if o.HasField('vehicle_classification'):
            vc = o.vehicle_classification
            if vc.HasField('light_state'):
                ls = vc.light_state
                print('    --- LIGHT STATE ---')

                # Indicator State
                indicator_names = {
                    0: "OFF",
                    1: "LEFT",
                    2: "RIGHT",
                    3: "WARNING"
                }
                indicator = indicator_names.get(ls.indicator_state, f"UNKNOWN({ls.indicator_state})")
                print(f'      Indicator: {indicator}')

                # Brake Light State
                brake_names = {
                    0: "OFF",
                    1: "NORMAL",
                    2: "STRONG"
                }
                brake = brake_names.get(ls.brake_light_state, f"UNKNOWN({ls.brake_light_state})")
                print(f'      Brake Light: {brake}')

                # Head Light
                head_names = {
                    0: "OFF",
                    1: "ON",
                    2: "FLASHING"
                }
                head = head_names.get(ls.head_light, f"UNKNOWN({ls.head_light})")
                print(f'      Head Light: {head}')

                # High Beam
                high_beam = head_names.get(ls.high_beam, f"UNKNOWN({ls.high_beam})")
                print(f'      High Beam: {high_beam}')

                # Reversing Light
                reversing = head_names.get(ls.reversing_light, f"UNKNOWN({ls.reversing_light})")
                print(f'      Reversing Light: {reversing}')

                # Fog Lights
                fog_front = head_names.get(ls.front_fog_light, f"UNKNOWN({ls.front_fog_light})")
                fog_rear = head_names.get(ls.rear_fog_light, f"UNKNOWN({ls.rear_fog_light})")
                print(f'      Fog Light (Front): {fog_front}')
                print(f'      Fog Light (Rear): {fog_rear}')

                print('    -------------------')
            else:
                print('    Light state: NOT AVAILABLE')
        else:
            print('    Not a vehicle (no light state)')


if __name__ == "__main__":

    # Create UDP socket objects
    osiReceiver = OSIReceiver()
    done = False
    counter = 0

    print("Waiting for OSI messages on port 48198...")
    print("Start GT_Sim with --osi 127.0.0.1 option")
    print("Press Ctrl+C to quit\n")

    while not done:
        # Read OSI
        try:
            msg = osiReceiver.receive()
            print_osi_stuff(msg)
        except timeout:
            print('osiReceive Timeout (waiting for messages...)')
            # Don't quit on timeout, keep waiting
        except KeyboardInterrupt:
            print('\nCtrl+C pressed, quit')
            done = True

        counter += 1

        # Optional: limit iterations for testing
        # if counter > 100:
        #     done = True

    # Close and quit
    osiReceiver.close()
    print("OSI receiver closed.")
