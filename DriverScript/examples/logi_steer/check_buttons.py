#!/usr/bin/env python3
"""
LogiSteer Button Checker

This script checks all button inputs (0-127) and POV status
from the connected Logitech wheel and prints active inputs.
Use this to identify button IDs for mapping.
"""

import sys
import os
import time

# Add parent directories to path to import realdriver
current_dir = os.path.dirname(os.path.abspath(__file__))
driver_script_dir = os.path.abspath(os.path.join(current_dir, "..", ".."))
if driver_script_dir not in sys.path:
    sys.path.append(driver_script_dir)

try:
    from realdriver import logi_steer
except ImportError:
    print("Error: Could not import realdriver.logi_steer")
    print(f"PYTHONPATH: {sys.path}")
    sys.exit(1)


def main():
    print("Initializing Logitech Steering Wheel...")
    if not logi_steer.Init():
        print("Failed to initialize Logitech SDK.")
        return 1

    print("\nLogiSteer Button Checker running.")
    print("Press Ctrl+C to exit.")
    print("-" * 40)

    try:
        last_print_time = 0

        while True:
            # Update SDK state
            # Note: IsButtonPressed calls _ensure_update() internally,
            # but calling it once per frame explicitly is good practice if we accessed raw data.
            # Here we just use the API.

            active_buttons = []

            # Check all 128 possible buttons
            for i in range(128):
                if logi_steer.IsButtonPressed(i):
                    active_buttons.append(i)

            # Check POV
            pov = logi_steer.GetPOV(0)

            # Check Pedals and Steering for completeness
            steer = logi_steer.GetSteerAngle()
            throttle, brake = logi_steer.GetPedalValue()

            # Compile output string
            status_parts = []

            if active_buttons:
                status_parts.append(f"Buttons: {active_buttons}")
            else:
                status_parts.append("Buttons: None")

            if pov == 4294967295 or pov == 65535 or pov == -1:
                status_parts.append("POV: Center")
            else:
                status_parts.append(f"POV: {pov}")

            # Always show axes to prove it's alive
            status_parts.append(f"St: {steer:.2f} Th: {throttle:.2f} Br: {brake:.2f}")

            # Print
            # \r overwrites the line, usually works in terminals
            # Padding with spaces to clear previous text
            msg = " | ".join(status_parts)
            print(f"\r{msg:<80}", end="", flush=True)
            current_time = time.time()
            last_print_time = current_time

            time.sleep(0.05)

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        logi_steer.Shutdown()
        print("\nDone.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
