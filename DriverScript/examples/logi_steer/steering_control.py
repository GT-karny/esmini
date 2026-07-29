import time
import math
from logidrivepy import LogitechController


def main():
    # 1. 初期化
    controller = LogitechController()

    print("Initializing Logitech SDK...")
    # ignore_xinput_controllers=True: XInputデバイス(ゲームパッドなど)を無視
    if not controller.steering_initialize(ignore_xinput_controllers=True):
        print("Failed to initialize Logitech SDK.")
        return

    print("Logitech SDK Initialized.")

    try:
        # デモ用ループ
        print("Starting steering control demo...")
        print("Press Ctrl+C to exit.")
        print("Range: 1.0 (Left) <-> -1.0 (Right)")

        start_time = time.time()

        while True:
            # 2. SDKの状態更新 (毎フレーム呼び出す必要がある)
            if not controller.logi_update():
                print("Logi update failed (disconnected?)")
                break

            # デモ: サイン波で左右に自動操舵
            # 周期約10秒
            elapsed = time.time() - start_time
            # sinは -1 ~ 1 を返す
            target_val = math.sin(elapsed * (2 * math.pi / 10))

            # 入力値 (1.0 = 左, -1.0 = 右) を SpringForce の Offset % (-100 ~ 100) に変換
            # 一般的に Logitech SDK では:
            # -100% (-32768) が 左
            # 100% (32767) が 右
            # したがって、Input 1.0 (Left) -> Offset -100
            #             Input -1.0 (Right) -> Offset 100

            # 変換式: offset = target_val * -100
            offset_pct = int(target_val * -100)

            # クランプ (-100 ~ 100)
            offset_pct = max(-100, min(100, offset_pct))

            # 3. スプリングフォースの適用
            # index: 0 (最初のコントローラー)
            # offsetPercentage: 中心位置指定 (-100 ~ 100)
            # saturationPercentage: 強さ (0 ~ 100) - 50%くらいが適当か
            # coefficientPercentage: 係数 (0 ~ 100) - バネ定数的なもの

            # 接続確認
            if controller.is_connected(0):
                controller.LogiPlaySpringForce(0, offset_pct, 50, 50)

                # 現在の値を取得
                state = controller.get_state_engines(0)
                current_steer_raw = state.contents.lX  # -32768 (Left) ~ 32767 (Right)

                # 正規化 (-32768 -> 1.0, 32767 -> -1.0)
                current_val = -(current_steer_raw / 32768.0)

                # 差分計算
                diff = target_val - current_val

                # 現在の状態を表示 (カーソル位置を戻して上書き表示っぽくする)
                print(
                    f"\rTarget: {target_val:+.2f} | Current: {current_val:+.2f} | Diff: {diff:+.2f} | Offset: {offset_pct:4d}%   ",
                    end="",
                )

            time.sleep(1 / 60)

    except KeyboardInterrupt:
        print("\nStopping...")

    finally:
        # 4. 終了処理
        print("Shutting down Logitech SDK...")
        controller.LogiStopSpringForce(0)
        controller.steering_shutdown()
        print("Done.")


if __name__ == "__main__":
    main()
