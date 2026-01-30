#!/usr/bin/env python3
"""
LogiSteer Deep Inspector

Inspects raw controller structure contents to find button mappings.
"""
import sys
import os
import time
import ctypes

# Add parent directories to path to import realdriver
current_dir = os.path.dirname(os.path.abspath(__file__))
driver_script_dir = os.path.abspath(os.path.join(current_dir, "..", ".."))
if driver_script_dir not in sys.path:
    sys.path.append(driver_script_dir)

try:
    from realdriver import logi_steer
    from logidrivepy import LogitechController
except ImportError:
    print("Error: Could not import realdriver.logi_steer")
    sys.exit(1)

def main():
    print("Initializing LogiSteer...")
    if not logi_steer.Init():
        print("Failed Init.")
        return
    
    # Access internal controller
    controller = logi_steer._controller
    if not controller:
        print("Controller is None")
        return

    print("Controller initialized. Inspecting state...")
    print("Press Ctrl+C to stop.")

    try:
        while True:
            if not controller.logi_update():
               print("Update failed")
            
            if controller.is_connected(0):
                state = controller.get_state_engines(0)
                # state.contents is likely DIJOYSTATE2 or similar
                
                # Print rgbButtons (first 10 bytes)
                raw_buttons = list(state.contents.rgbButtons)
                # Filter to show only pressed buttons (non-zero)
                pressed_indices = [i for i, val in enumerate(raw_buttons) if val != 0]
                
                # Check lButtons (deprecated but maybe used?)
                # DIJOYSTATE2 might not have lButtons in the same way?
                # Logitech SDK wraps it. Structure def:
                # lX, lY, lZ, lRx, lRy, lRz, rglSlider[2], rgdwPOV[4], rgbButtons[128], ...
                
                msg = []
                if pressed_indices:
                   vals = [raw_buttons[i] for i in pressed_indices]
                   msg.append(f"Buttons: {list(zip(pressed_indices, vals))}")
                
                # POV
                pov = state.contents.rgdwPOV[0]
                if pov != 4294967295 and pov != 65535 and pov != -1:
                    msg.append(f"POV: {pov}")

                if msg:
                    print(f"\r{' | '.join(msg):<80}", end="", flush=True)
                else:
                    print(f"\rNo Input...{' '*60}", end="", flush=True)
            
            else:
                print("\rNot Connected(0)", end="", flush=True)

            time.sleep(0.05)
            
    except KeyboardInterrupt:
        print("\nStopping")
    finally:
        logi_steer.Shutdown()

if __name__ == "__main__":
    main()
