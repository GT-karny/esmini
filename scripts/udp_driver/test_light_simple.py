"""
Simple test script to send light commands to RealDriver
"""
import time
from RealDriverClient import RealDriverClient

def main():
    print("=== RealDriver Light Test ===")
    print("Connecting to RealDriver on 127.0.0.1:53995 (Object ID 0)")

    client = RealDriverClient(ip="127.0.0.1", port=53995, object_id=0)

    print("\nSending initial state (all lights OFF)...")
    client.set_controls(throttle=0.0, brake=0.0, steering=0.0)
    client.set_gear(1)  # Drive
    client.set_lights(0)  # All lights OFF
    client.send_update()
    time.sleep(1)

    print("\n--- Test 1: Low Beam ON ---")
    client.set_lights(1)  # Bit 0: Low Beam
    for i in range(10):
        client.send_update()
        time.sleep(0.02)  # 50Hz

    print("\n--- Test 2: High Beam ON ---")
    client.set_lights(2)  # Bit 1: High Beam
    for i in range(10):
        client.send_update()
        time.sleep(0.02)

    print("\n--- Test 3: Left Indicator ON ---")
    client.set_lights(4)  # Bit 2: Left Indicator
    for i in range(10):
        client.send_update()
        time.sleep(0.02)

    print("\n--- Test 4: Right Indicator ON ---")
    client.set_lights(8)  # Bit 3: Right Indicator
    for i in range(10):
        client.send_update()
        time.sleep(0.02)

    print("\n--- Test 5: Hazard ON ---")
    client.set_lights(16)  # Bit 4: Hazard
    for i in range(10):
        client.send_update()
        time.sleep(0.02)

    print("\n--- Test 6: All Lights ON ---")
    client.set_lights(127)  # All bits
    for i in range(10):
        client.send_update()
        time.sleep(0.02)

    print("\n--- Test 7: Brake Light (via brake input) ---")
    client.set_lights(0)  # Clear manual lights
    client.set_controls(throttle=0.0, brake=0.8, steering=0.0)  # Brake pressed
    for i in range(10):
        client.send_update()
        time.sleep(0.02)

    print("\n--- Test 8: Reversing Light (via gear) ---")
    client.set_controls(throttle=0.0, brake=0.0, steering=0.0)
    client.set_gear(-1)  # Reverse
    for i in range(10):
        client.send_update()
        time.sleep(0.02)

    print("\n--- Test Complete ---")
    print("Check OSI output for light state changes")

    # Keep sending for a bit longer
    print("\nKeeping connection alive for 5 seconds...")
    client.set_gear(1)
    client.set_lights(0)
    for i in range(250):  # 5 seconds at 50Hz
        client.send_update()
        time.sleep(0.02)

    client.close()
    print("Done!")

if __name__ == "__main__":
    main()
