from logidrivepy import LogitechController
import time

# Singleton instance
_controller = None


def Init() -> bool:
    """
    Logitech SDKを初期化します。

    Returns:
        bool: 初期化に成功した場合はTrue、失敗した場合はFalse。
    """
    global _controller
    if _controller is None:
        _controller = LogitechController()

    # ignore_xinput_controllers=False: XInputデバイスも許可する (G29 PCモードなどはXInputとして認識される場合がある)
    return _controller.steering_initialize(ignore_xinput_controllers=False)


def Shutdown():
    """
    Logitech SDKをシャットダウンします。
    """
    global _controller
    if _controller is not None:
        _controller.steering_shutdown()
        # Singletonインスタンスは破棄しないが、SDKはシャットダウンする


def _ensure_update():
    """
    SDKの状態を更新します。内部で使用されます。
    """
    global _controller
    if _controller:
        return _controller.logi_update()
    return False


def SetSteerAngle(angle: float):
    """
    ステアリングのターゲット角度を設定します（Spring Forceを使用）。

    Args:
        angle (float): ターゲット角度。範囲: -1.0 (右) 〜 1.0 (左)
    """
    global _controller
    if _controller and _ensure_update():
        if _controller.is_connected(0):
            # Input  1.0 (Left)  -> Offset -100
            # Input -1.0 (Right) -> Offset  100

            # クランプ -1.0 ~ 1.0
            angle = max(-1.0, min(1.0, angle))

            # 変換
            offset_pct = int(angle * -100)

            # クランプ -100 ~ 100
            offset_pct = max(-100, min(100, offset_pct))

            # Spring Force適用 (Index 0)
            _controller.LogiPlaySpringForce(0, offset_pct, 70, 70)


def GetSteerAngle() -> float:
    """
    現在のステアリング角度を取得します。

    Returns:
        float: 現在の角度。範囲: -1.0 (右) 〜 1.0 (左)
    """
    global _controller
    if _controller and _ensure_update():
        if _controller.is_connected(0):
            state = _controller.get_state_engines(0)
            raw_val = state.contents.lX

            # Raw: -32768 (Left) ~ 32767 (Right)
            # Normalize to 1.0 (Left) ~ -1.0 (Right)
            norm_val = -(raw_val / 32768.0)

            return max(-1.0, min(1.0, norm_val))
    return 0.0


def GetPedalValue() -> tuple[float, float]:
    """
    現在のペダル入力値を取得します。

    Returns:
        tuple[float, float]: (アクセル, ブレーキ)。範囲: 0.0 (離) 〜 1.0 (踏)
    """
    global _controller
    if _controller and _ensure_update():
        if _controller.is_connected(0):
            state = _controller.get_state_engines(0)

            # アクセル (lY) / ブレーキ (lRz)
            # Logitech SDKのデフォルト挙動に依存するが、通常は
            # -32768 (離) ~ 32767 (全開)
            # もしくは 32767 (離) ~ -32768 (全開) の場合もある
            # ここでは -32768 (離) ~ 32767 (全開) として実装し、必要に応じて調整する

            # Note: G29/G923 Default behavior often treats axes as
            # Released: 32767, Pressed: -32768 (Inverted) OR Released: -32768, Pressed: 32767
            # logidrivepy examples often assume standard direct input mapping.
            # Assuming: Released (-32768) -> Pressed (32767)

            raw_throttle = state.contents.lY
            raw_brake = state.contents.lRz

            # Normalize to 0.0 - 1.0
            # From -32768...32767 to 0.0...1.0
            throttle = (raw_throttle + 32768) / 65535.0
            brake = (raw_brake + 32768) / 65535.0

            return (max(0.0, min(1.0, throttle)), max(0.0, min(1.0, brake)))

    return (0.0, 0.0)


# --- Button Definitions (G29 / G923 PS Mode) ---
# Note: These indices may vary by specific wheel model and switch setting (PS3/PS4).
# Assuming PS4/PC mode standard mapping:
BUTTON_CROSS = 0
BUTTON_SQUARE = 1
BUTTON_CIRCLE = 2
BUTTON_TRIANGLE = 3
BUTTON_R1 = 4
BUTTON_L1 = 5
BUTTON_R2 = 6
BUTTON_L2 = 7
BUTTON_SHARE = 8
BUTTON_OPTION = 9
BUTTON_R3 = 10
BUTTON_L3 = 11
BUTTON_PS = 24


def IsButtonPressed(index: int) -> bool:
    """
    指定されたインデックスのボタンが押されているか判定します。

    Args:
        index (int): ボタンインデックス (例: BUTTON_CIRCLE)

    Returns:
        bool: 押されている場合はTrue
    """
    global _controller
    if _controller and _ensure_update():
        if _controller.is_connected(0):
            # Use the library's helper method if available, or manual check
            if hasattr(_controller, "button_is_pressed"):
                # button_is_pressed internally might rely on unsigned check, so it might fail if ctypes sees signed.
                # Let's fallback to our robust manual check if needed, or trust it.
                # Given user says "reset didn't work" even after switching to button_is_pressed,
                # likely library implementation also expects positive 128.
                pass

            # Robust manual check: Check bit 7 (value & 0x80)
            state = _controller.get_state_engines(0)
            val = state.contents.rgbButtons[index]
            # Convert to unsigned 8-bit to be safe
            u_val = val & 0xFF
            return u_val == 128
    return False


def GetPOV(index: int = 0) -> int:
    """
    POV (D-Pad) の状態を取得します。

    Args:
        index (int): POVインデックス (通常は0)

    Returns:
        int: 角度 (0, 4500, 9000... 31500)。中心は -1 または 65535 (実装による)
             Logitech SDKでは通常:
             -1: Center
             0: Up
             9000: Right
             18000: Down
             27000: Left
    """
    global _controller
    if _controller and _ensure_update():
        if _controller.is_connected(0):
            state = _controller.get_state_engines(0)
            pov = state.contents.rgdwPOV[index]
            # Normalize Center values
            if pov == 4294967295 or pov == 65535:
                return -1
            return pov
    return -1
