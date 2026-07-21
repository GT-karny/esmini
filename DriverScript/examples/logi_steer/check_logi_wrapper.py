import time
import math
import sys
import os

# DriverScript/examples/logi_steer から実行されることを想定し、親ディレクトリへのパスを通す
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../../")))

from realdriver import logi_steer


def main():
    print("Testing logi_steer wrapper module...")

    if not logi_steer.Init():
        print("Failed to initialize logi_steer.")
        return

    print("Initialized.")
    print("Press Ctrl+C to exit.")

    try:
        start_time = time.time()
        while True:
            # Test Steering Control
            elapsed = time.time() - start_time
            # 10秒周期
            target_angle = math.sin(elapsed * (2 * math.pi / 10))

            logi_steer.SetSteerAngle(target_angle)

            current_angle = logi_steer.GetSteerAngle()
            throttle, brake = logi_steer.GetPedalValue()

            diff = target_angle - current_angle

            print(
                f"\rTarget: {target_angle:+.2f} | Current: {current_angle:+.2f} | Diff: {diff:+.2f} | Thr: {throttle:.2f} | Brk: {brake:.2f}   ",
                end="",
            )

            time.sleep(1 / 60)

    except KeyboardInterrupt:
        print("\nStopping...")

    finally:
        logi_steer.Shutdown()
        print("Shutdown.")


if __name__ == "__main__":
    main()
