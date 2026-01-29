# RealDriver Module Documentation

このドキュメントでは、`DriverScript/realdriver` フォルダ内のモジュールについて解説します。

## 1. モジュール一覧

| モジュール名 | 説明 |
| :--- | :--- |
| `acc_controller.py` | 先行車との車間距離を維持しながら走行するACC (Adaptive Cruise Control) 機能を提供します。 |
| `lane_change_controller.py` | 安全確認を行いながら車線変更を実行するイベント駆動型のコントローラです。 |
| `lateral_controller.py` | ウェイポイント追従のための操舵（ステアリング）制御を行うコントローラです。 |
| `longitudinal_controller.py` | 目標速度に従ってスロットルとブレーキを制御するPIDベースのコントローラです。 |
| `scenario_drive.py` | 横方向制御と縦方向制御を統合し、シナリオに従って走行するハイレベルなコントローラです。 |
| `lkas.py` | RoadManagerを使用して車線維持支援 (LKAS) を行う独立したコントローラです。 |
| `pid_controller.py` | 汎用的なPID制御の実装を提供するユーティリティモジュールです。 |
| `client.py` | esmini本体に対してUDP経由で制御コマンド（HVD）を送信するクライアントです。 |
| `udp_receivers.py` | 外部からのウェイポイントや目標速度のUDPパケットを受信するモジュールです。 |
| `osi_receiver.py` | esminiから送られてくるOSI GroundTruthデータを受信・パースするラッパクラスです。 |
| `vehicle_state.py` | OSI GroundTruthから自車状態（位置、速度など）を抽出し、道路座標データで拡張するクラスを提供します。 |
| `waypoint.py` | ウェイポイントのデータ構造および、複数ソース（ユーザー指定、計算、UDP）からのウェイポイント管理を行います。 |
| `simplified_router.py` | RoadManagerのネットワーク情報を使用して、始点から終点までの経路（ウェイポイント列）を計算します。 |
| `rm_lib.py` | esminiRMLib (RoadManager) のC++関数をPythonから利用するためのラッパーです。 |
| `gt_rm_lib.py` | 道路の接続情報などを取得する GT_esminiRMLib のPythonラッパーです。 |
| `udp_common.py` | UDP通信のための基本的な送信・受信クラスを提供します。 |

---

## 2. API リスト

主要なクラスと関数の一覧です。

### `acc_controller.py`

**`class ACCController`**
*   `__init__(ego_id=0, config=None, rm_lib=None)`
    *   コントローラの初期化。`rm_lib`を指定するとレーンベースの先行車認識を行います。
*   `set_target_speed(speed: float)`
    *   目標巡航速度 [m/s] を設定します。
*   `update(ground_truth, dt: float) -> LongitudinalOutput`
    *   OSIデータを基に制御量（スロットル、ブレーキ）を計算します。
*   `update_from_speed(current_speed: float, dt: float) -> LongitudinalOutput`
    *   (デバッグ用) 現在速度のみを入力として制御量を計算します。先行車認識は行われません。

### `lane_change_controller.py`

**`class LaneChangeController`**
*   `__init__(rm_lib, ego_id=0, config=None)`
    *   初期化。`rm_lib` は必須です。
*   `check_safety(ground_truth, direction: str) -> SafetyCheckResult`
    *   指定方向('left'/'right')への車線変更が安全かどうかを確認します。
*   `trigger_lane_change(direction: str) -> bool`
    *   車線変更を開始します。
*   `update(ground_truth, dt: float) -> LaneChangeOutput`
    *   制御ループ内で呼び出し、ステアリング等の制御出力を得ます。状態遷移（待機→確認→実行→完了）を管理します。

### `lateral_controller.py`

**`class LateralController`**
*   `__init__(rm_lib=None, ego_id=0, config=None, waypoint_mgr=None)`
    *   初期化。
*   `set_waypoints(waypoints: List[Waypoint])`
    *   追従するウェイポイントリストを設定します。
*   `update(ground_truth, dt: float) -> float`
    *   現在の車両状態とウェイポイントから、ステアリング量 [-1.0, 1.0] を計算します。

### `longitudinal_controller.py`

**`class LongitudinalController`**
*   `__init__(ego_id=0, config=None)`
    *   初期化。RoadManagerに依存せず、速度のみで制御可能です。
*   `set_target_speed(speed: float)`
    *   目標速度を設定します。
*   `update(ground_truth, dt: float) -> LongitudinalOutput`
    *   OSIデータから速度を取得し、PID制御を行います。

### `scenario_drive.py`

**`class ScenarioDriveController`**
*   `__init__(lib_path, xodr_path, ego_id=0, ...)`
    *   RoadManagerの初期化や各サブコントローラのセットアップを一括で行います。
*   `set_waypoints(waypoints: List[Waypoint])`
    *   ユーザー定義のウェイポイントを設定します。
*   `set_target(target: Waypoint)`
    *   目的地を設定し、自動的に経路計算を行います。
*   `update(ground_truth, dt: float) -> Tuple[float, float, float]`
    *   メインループ用関数。ステアリング、スロットル、ブレーキの3つの値を返します。

### `client.py`

**`class RealDriverClient`**
*   `__init__(ip="127.0.0.1", port=53995)`
    *   esminiへのUDP接続を初期化します。
*   `set_controls(throttle, brake, steering)`
    *   基本制御量を設定します。
*   `set_gear(gear: int)`
    *   ギアを設定します (1: Drive, 0: Neutral, -1: Reverse)。
*   `set_indicators(mode: IndicatorMode)`
    *   ウィンカーの状態を設定します。
*   `set_headlights(mode: LightMode)`
    *   ヘッドライトの状態を設定します。
*   `send_update()`
    *   現在の設定値をUDPパケットとして送信します。

### `vehicle_state.py`

**`class VehicleState`** (データクラス)
*   `x`, `y`, `z`, `h` (Heading), `speed` を保持します。
*   `enrich_with_road_data` により `road_id`, `lane_id`, `s` 等が追加されます。

**`class VehicleStateExtractor`**
*   `extract(ground_truth) -> VehicleState`
    *   OSI GroundTruthメッセージから自車情報を抽出します。
*   `enrich_with_road_data(state, rm_lib) -> VehicleState`
    *   RoadManagerを使用して、座標情報を道路情報（RoadID/LaneID/S）で補完します。

### `waypoint.py`

**`class WaypointManager`**
*   `set_waypoints(waypoints)`
    *   ユーザー指定のウェイポイントを設定します。
*   `receive_from_udp(data)`
    *   UDP経由で受信したパケットからウェイポイントを解析・設定します。
*   `get_current_waypoint() -> Waypoint`
    *   現在目指しているウェイポイントを取得します。
*   `is_complete() -> bool`
    *   すべてのウェイポイントを通過したか確認します。
