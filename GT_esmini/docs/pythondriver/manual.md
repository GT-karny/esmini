# PythonDriverController マニュアル

PythonDriverController の設定、Python スクリプトインターフェース、車両物理モデル、シナリオアクション連携などを包括的に解説するリファレンスマニュアルです。

---

## 目次

1. [概要とクイックスタート](#1-概要とクイックスタート)
2. [XOSC設定](#2-xosc設定)
3. [Pythonコントローラーインターフェース](#3-pythonコントローラーインターフェース)
4. [フレーム実行フロー](#4-フレーム実行フロー)
5. [車両物理モデル](#5-車両物理モデル)
6. [ウェイポイントシステム](#6-ウェイポイントシステム)
7. [シナリオアクション連携](#7-シナリオアクション連携)
8. [ライト制御システム](#8-ライト制御システム)
9. [ADAS状態](#9-adas状態)
10. [トレース・デバッグ](#10-トレースデバッグ)
11. [エラーハンドリング](#11-エラーハンドリング)
12. [トラブルシューティング](#12-トラブルシューティング)
13. [関連ドキュメント](#13-関連ドキュメント)

---

## 1. 概要とクイックスタート

### 1.1 PythonDriverControllerとは

PythonDriverController は GT_esmini に組み込まれた Python コントローラーです。C++ プロセス内で Python インタープリターを起動し、シミュレーションの各フレームで Python スクリプトの `step()` メソッドを**同期的**に呼び出します。

主な特徴:

- **同期実行**: 1フレーム = 1回の `step()` 呼び出し。UDP 遅延なし
- **双方向データ**: C++ から OSI GroundTruth、ウェイポイント、速度プロファイルを受け取り、Python からスロットル・ブレーキ・ステアリング・ライト制御を返す
- **シナリオアクション連携**: SpeedAction、LaneChangeAction 等の OpenSCENARIO アクションを自動検出し、ウェイポイント再生成や目標速度変更として Python に伝達
- **非推奨の RealDriverController (UDP方式) の後継**

### 1.2 前提条件

- GT_esmini ビルド済み (`GT_ENABLE_EMBEDDED_PYTHON` は常時有効)
- Python 3.12 ランタイム (組み込み版が `thirdparty/python-embed/` に同梱)

### 1.3 最小動作例

#### XOSC ファイル

```xml
<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <FileHeader revMajor="1" revMinor="1" date="2026-01-01" description="Minimal PythonDriver example" author="GT_esmini"/>
  <ParameterDeclarations/>
  <CatalogLocations>
    <VehicleCatalog>
      <Directory path="../../resources/xosc/Catalogs/Vehicles"/>
    </VehicleCatalog>
  </CatalogLocations>
  <RoadNetwork>
    <LogicFile filepath="../../resources/xodr/fabriksgatan.xodr"/>
  </RoadNetwork>
  <Entities>
    <ScenarioObject name="Ego">
      <CatalogReference catalogName="VehicleCatalog" entryName="car_white"/>
      <ObjectController>
        <Controller name="PythonDriverController">
          <Properties>
            <Property name="esminiController" value="PythonDriverController"/>
            <Property name="PythonScript" value="my_controller.py"/>
            <Property name="PythonClass" value="MyController"/>
            <Property name="PythonHome" value=""/>
          </Properties>
        </Controller>
      </ObjectController>
    </ScenarioObject>
  </Entities>
  <Storyboard>
    <Init>
      <Actions>
        <Private entityRef="Ego">
          <PrivateAction>
            <TeleportAction>
              <Position>
                <LanePosition roadId="2" laneId="-1" offset="0" s="20"/>
              </Position>
            </TeleportAction>
          </PrivateAction>
          <PrivateAction>
            <LongitudinalAction>
              <SpeedAction>
                <SpeedActionDynamics dynamicsShape="step" dynamicsDimension="time" value="0.0"/>
                <SpeedActionTarget>
                  <AbsoluteTargetSpeed value="10.0"/>
                </SpeedActionTarget>
              </SpeedAction>
            </LongitudinalAction>
          </PrivateAction>
          <!-- ActivateControllerAction は必須 -->
          <PrivateAction>
            <ActivateControllerAction longitudinal="true" lateral="true"/>
          </PrivateAction>
        </Private>
      </Actions>
    </Init>
    <Story name="EmptyStory">
      <Act name="Act1">
        <ManeuverGroup maximumExecutionCount="1" name="MG1">
          <Actors selectTriggeringEntities="false">
            <EntityRef entityRef="Ego"/>
          </Actors>
        </ManeuverGroup>
        <StartTrigger/>
      </Act>
    </Story>
    <StopTrigger>
      <ConditionGroup>
        <Condition name="Stop" delay="0" conditionEdge="rising">
          <ByValueCondition>
            <SimulationTimeCondition value="10.0" rule="greaterThan"/>
          </ByValueCondition>
        </Condition>
      </ConditionGroup>
    </StopTrigger>
  </Storyboard>
</OpenSCENARIO>
```

#### Python スクリプト (`my_controller.py`)

```python
class MyController:
    def init(self, config):
        """シナリオ開始時に1回呼ばれる"""
        self.dt = config.get("dt", 0.01)

    def step(self, frame_data):
        """毎フレーム呼ばれる。制御入力dictを返す"""
        dt = frame_data.get("dt", 0.0)
        if dt <= 0:
            # 初期化フレーム (dt=0): PID計算をスキップ
            return self._make_output(0.0, 0.0, 0.0)

        # 定速走行の例: スロットル0.3、ステアリングなし
        return self._make_output(throttle=0.3, brake=0.0, steering=0.0)

    def close(self):
        """シナリオ終了時に呼ばれる（オプション）"""
        pass

    def _make_output(self, throttle, brake, steering):
        return {
            "throttle": throttle,
            "brake": brake,
            "steering": steering,
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": [],
        }
```

#### 実行

```bash
GT_Sim.exe --window 60 60 800 400 --osc minimal_example.xosc
```

> **注意**: `PythonScript` には相対パスを指定可能です。GT_Sim の起動ディレクトリから解決されます（詳細は [2.2 PythonScriptパス解決](#22-pythonscriptパス解決) を参照）。

---

## 2. XOSC設定

### 2.1 コントローラープロパティ一覧

XOSC の `<Controller>` 内 `<Properties>` で以下のプロパティを設定します。

| プロパティ名 | 型 | 必須 | デフォルト値 | 説明 |
|---|---|---|---|---|
| `esminiController` | string | Yes | — | `"PythonDriverController"` 固定 |
| `PythonScript` | string | Yes | — | Python スクリプトファイルのパス |
| `PythonClass` | string | No | `"EmbeddedController"` | スクリプト内のクラス名 |
| `PythonHome` | string | No | ビルド時マクロ `GT_EMBEDDED_PYTHON_HOME` | 組み込み Python ランタイムのパス |
| `PythonTrace` | string | No | `"off"` | `"on"`, `"1"`, `"true"`, `"TRUE"` でトレース有効化 |
| `PythonTraceDir` | string | No | カレントディレクトリ | トレースファイルの出力先 |

**XOSC記述例:**

```xml
<ObjectController>
  <Controller name="PythonDriverController">
    <Properties>
      <Property name="esminiController" value="PythonDriverController"/>
      <Property name="PythonScript" value="DriverScript/pythondriver/scenario_drive_embedded.py"/>
      <Property name="PythonClass" value="EmbeddedController"/>
      <Property name="PythonHome" value=""/>
      <Property name="PythonTrace" value="on"/>
      <Property name="PythonTraceDir" value="test_results/trace_output"/>
    </Properties>
  </Controller>
</ObjectController>
```

### 2.2 PythonScriptパス解決

`PythonScript` に相対パスを指定した場合、以下の順序で解決されます。

1. **カレントディレクトリ** (GT_Sim の起動場所) からの相対パス
2. **実行ファイルのディレクトリ** (`GT_Sim.exe` と同じディレクトリ)
3. **実行ファイルの親ディレクトリを最大6階層上まで探索**
4. 見つからない場合はカレントディレクトリの絶対パスにフォールバック

**典型的な運用**: リポジトリルートから GT_Sim を起動し、`DriverScript/pythondriver/scenario_drive_embedded.py` のような相対パスを指定すれば自動的に解決されます。

> **ソース**: `ControllerPythonDriver.cpp` 220-269行

### 2.3 ActivateControllerAction

PythonDriverController を有効にするには、Init の PrivateAction に `ActivateControllerAction` が**必須**です。

```xml
<PrivateAction>
  <ActivateControllerAction longitudinal="true" lateral="true"/>
</PrivateAction>
```

`longitudinal` と `lateral` の両方を `"true"` に設定すると、コントローラーは MODE_OVERRIDE で両ドメインを制御します。これにより:

- esmini のデフォルトコントローラーは無効化される
- SpeedAction は `End()` が即座に呼ばれる（コントローラーが検出して目標速度を取得）
- LaneChangeAction 等の横方向アクションも同様にインターセプトされる

---

## 3. Pythonコントローラーインターフェース

### 3.1 基底クラス

`DriverScript/pythondriver/controller_base.py` に抽象基底クラスが定義されています。

```python
from abc import ABC, abstractmethod
from typing import Any, Dict

class EmbeddedControllerBase(ABC):
    """C++ PythonDriverBridge が期待するコントラクト"""

    @abstractmethod
    def init(self, config: Dict[str, Any]) -> None:
        """シナリオ開始時に1回呼ばれる"""

    @abstractmethod
    def step(self, frame_data: Dict[str, Any]) -> Dict[str, Any]:
        """毎フレーム呼ばれる。制御入力dictを返す"""

    def close(self) -> None:
        """シャットダウン時に呼ばれる（オプション）"""
        return None
```

> **注意**: 基底クラスの継承は必須ではありません。上記3メソッドを持つクラスであれば動作します。

### 3.2 `init(config)` メソッド

シナリオ開始時に1回呼ばれます。`config` dict に以下のキーが渡されます。

| キー | 型 | 説明 |
|---|---|---|
| `xodr_path` | str | OpenDRIVE (.xodr) ファイルの絶対パス |
| `dt` | float | 公称タイムステップ [秒] (通常 0.01) |
| `script_dir` | str | Python スクリプトの親ディレクトリの絶対パス |
| `ego_id` | int | 自車両のオブジェクト ID |
| `trace_enabled` | bool | トレースが有効かどうか |
| `trace_dir` | str | トレース出力ディレクトリ (空文字列の場合あり) |

**使用例:**

```python
def init(self, config):
    self.dt = float(config.get("dt", 0.01))
    self.ego_id = int(config.get("ego_id", 0))
    xodr_path = config.get("xodr_path", "")
    # xodr_path を使ってルート情報を読み込む等の初期化処理
```

### 3.3 `step(frame_data)` メソッド — 入力

毎フレーム呼ばれます。`frame_data` dict に以下のキーが渡されます。

#### スカラー値

| キー | 型 | 説明 |
|---|---|---|
| `frame_id` | int | フレーム番号 (0始まり、毎フレーム+1) |
| `dt` | float | タイムステップ [秒]。**初期化フレームでは 0** |
| `current_speed` | float | 現在の車速 [m/s] (RealVehicle の物理演算値) |
| `set_speed` | float | 目標速度 [m/s] (LonProfilePlanner の平滑化済み値) |

> **重要**: `dt=0` の初期化フレームでは PID 制御を**実行してはいけません**。OSI GroundTruth の速度が 0 のため、PID の微分項が暴走します。ウェイポイント読み込み等の初期化処理のみ行ってください。

#### ウェイポイント

| キー | 型 | 説明 |
|---|---|---|
| `waypoints` | list[dict] | ウェイポイントのリスト (後述) |
| `waypoint_index` | int | 現在の最近接ウェイポイントのインデックス |
| `waypoint_generation` | dict | `{"version": int}` — 世代番号。ルート変更時にインクリメント |

各ウェイポイント dict のキー:

| キー | 型 | 単位 | 説明 |
|---|---|---|---|
| `x` | float | m | グローバル X 座標 |
| `y` | float | m | グローバル Y 座標 |
| `h` | float | rad | ヘディング角 (0=+X方向, π/2=+Y方向) |
| `road_id` | int | — | OpenDRIVE 道路 ID |
| `s` | float | m | 道路上の縦断位置 |
| `lane_id` | int | — | レーン ID (負=走行方向左側) |
| `lane_offset` | float | m | レーン中心からの横方向オフセット |

#### 縦速度プロファイル

| キー | 型 | 説明 |
|---|---|---|
| `lon_profile` | list[dict] | 20点の速度プロファイル (3秒先まで、0.15秒間隔) |

各プロファイル点 dict のキー:

| キー | 型 | 単位 | 説明 |
|---|---|---|---|
| `t_offset` | float | s | 現在時刻からの時間オフセット |
| `v_target` | float | m/s | この時点での目標速度 |
| `a_max` | float | m/s^2 | 最大加速度 (デフォルト 3.0) |
| `j_max` | float | m/s^3 | 最大ジャーク (デフォルト 8.0) |

#### シナリオアクションフラグ

| キー | 型 | 説明 |
|---|---|---|
| `actions` | dict | 現在実行中のシナリオアクション情報 |

`actions` dict のキー:

| キー | 型 | 説明 |
|---|---|---|
| `assign_route` | bool | AssignRouteAction 実行中 |
| `lane_change` | bool | LaneChangeAction 実行中 |
| `lane_change_target_lane` | int / None | 車線変更のターゲットレーン ID |
| `lane_offset` | bool | LaneOffsetAction 実行中 |
| `lane_offset_target_m` | float / None | レーンオフセットのターゲット値 [m] |
| `follow_trajectory` | bool | FollowTrajectoryAction 実行中 |
| `longitudinal_distance` | bool | LongDistanceAction 実行中 |
| `speed_profile` | bool | SpeedProfileAction 実行中 |
| `synchronize` | bool | SynchronizeAction 実行中 |

#### OSI GroundTruth

| キー | 型 | 説明 |
|---|---|---|
| `ground_truth_bytes` | bytes / None | シリアライズ済み OSI GroundTruth protobuf バイナリ |

`ground_truth_bytes` のデシリアライズ例:

```python
from osi3.osi_groundtruth_pb2 import GroundTruth

gt = GroundTruth()
gt.ParseFromString(frame_data["ground_truth_bytes"])
# gt.moving_object, gt.traffic_sign, gt.traffic_light 等にアクセス
```

### 3.4 `step(frame_data)` メソッド — 戻り値

`step()` は制御入力の dict を**必ず返す**必要があります。

| キー | 型 | 範囲 | デフォルト | 説明 |
|---|---|---|---|---|
| `throttle` | float | [0.0, 1.0] | 0.0 | アクセル開度 |
| `brake` | float | [0.0, 1.0] | 0.0 | ブレーキ圧 |
| `steering` | float | rad | 0.0 | ステアリング角 (正=左旋回) |
| `gear` | int | — | 1 | ギア (1=D, -1=R) |
| `lights` | dict | — | `{}` | ライトパッチ ([セクション8](#8-ライト制御システム) 参照) |
| `engine_brake` | float | [0.0, 1.0] | 0.49 | エンジンブレーキ係数 |
| `adas_states` | list[int] | — | `[]` | ADAS 状態 ([セクション9](#9-adas状態) 参照) |

**必須キー**: `throttle`, `brake`, `steering`, `gear`, `lights` の5つは**必ず含める**必要があります。欠如するとエラーになります。

**戻り値の例:**

```python
return {
    "throttle": 0.3,
    "brake": 0.0,
    "steering": 0.05,
    "gear": 1,
    "lights": {"left_indicator": "on"},
    "engine_brake": 0.49,
    "adas_states": [],
}
```

### 3.5 `close()` メソッド

シミュレーション終了時に呼ばれるオプションのクリーンアップメソッドです。ファイルのクローズやリソース解放に使用します。メソッドが存在しない場合はスキップされます。

---

## 4. フレーム実行フロー

`PythonDriverCoordinator::RunFrame()` が各フレームの処理を以下の順序で実行します。

```
[ScenarioEngine::step()]
    ↓
[Controller::Step(timeStep)]
    ↓
[PythonDriverCoordinator::RunFrame()]
    |
    |-- (1) UpdateSetSpeedFromScenarioObject
    |       外部アクションによる速度変更を検出
    |
    |-- (2) DetectSpeedActionTarget
    |       SpeedAction のターゲット速度と TransitionDynamics を取得
    |
    |-- (3) EvaluateScenarioActions
    |       LaneChange/LaneOffset/FollowTrajectory/AssignRoute を検出
    |       → アクションを End() して、ウェイポイントを再生成
    |
    |-- (4) EnsureWaypointsExtracted
    |       初回のみ: ルートからウェイポイントを生成
    |
    |-- (5) UpdateCurrentWaypointIndex
    |       車両位置に最も近いウェイポイントを探索
    |
    |-- (6) LonProfilePlanner::Advance + BuildProfile
    |       目標速度を平滑化し、20点の速度プロファイルを生成
    |
    |-- (7) SE_GetOSIGroundTruth
    |       OSI GroundTruth バイナリを取得
    |
    |-- (8) PythonDriverBridge::CallStep  ←──── Python step() 呼び出し
    |       frame_data dict を構築 → Python 呼び出し → 戻り値を解析
    |
    |-- (9) Apply result to input_
    |       throttle/brake/steering/gear/lights を内部状態に反映
    |
    |-- (10) UpdateVehiclePhysics
    |        RealVehicle でスロットル/ブレーキを物理演算
    |
    |-- (11) UpdateCachedPowertrain + UpdateHostVehicleReporter
    |        RPM/トルクを OSI HVD にキャッシュ
    |
    |-- (12) SyncGatewayObjectState + UpdateVehicleLights
    |        Gateway と esmini オブジェクトに位置・速度を書き込み
    |
    |-- (13) Controller::Step + RefreshWaypointsOnRoutePointerChange
    |        基底クラスの Step() とルートポインタ変更検出
```

### 4.1 初期化フレーム (dt=0)

最初のフレームでは C++ が `dt=0` を送信します。このフレームの特徴:

- OSI GroundTruth の速度が 0 (まだ物理演算が走っていない)
- ウェイポイントと目標速度は利用可能
- **Python 側で PID 制御を実行すると、微分項が暴走する** (初期エラーが大きく、次フレームで急ブレーキが発生)

推奨される対処:

```python
def step(self, frame_data):
    dt = frame_data.get("dt", 0.0)
    if dt <= 0:
        # ウェイポイントや目標速度の初期化は行う
        self._load_waypoints(frame_data)
        # 制御出力はゼロ (物理演算なし)
        return self._make_output(throttle=0.0, brake=0.0, steering=0.0)
    # 通常の制御ロジック
    ...
```

---

## 5. 車両物理モデル

PythonDriverController は `RealVehicle` クラスによる独自の車両物理モデルを使用します。Python が返した throttle/brake/steering を物理演算に通して、位置・速度・加速度を計算します。

### 5.1 エンジンモデル

```
engine_force = GetTorque(rpm) × throttle × max_acc
```

トルクカーブ (正規化 RPM `n` に対して):

```
torque = 0.4 + 0.6 × (4 × n × (1 - n))
```

- `n = 0` (アイドル): torque = 0.4
- `n = 0.5` (ピーク): torque = 1.0
- `n = 1.0` (レッドライン): torque = 0.4

RPM パラメータ:

- アイドル回転数: 800 RPM
- 最大回転数: 7000 RPM
- RPM 慣性: 2000 RPM/s

### 5.2 抵抗モデル

```
drag_force = 0.005 × speed²
```

スロットルが 5% 未満の場合、エンジンブレーキが追加されます:

```
total_drag = drag_force + engine_brake_factor
```

`engine_brake_factor` のデフォルトは 0.49 (Python から変更可能)。

後退速度は 20 m/s でクランプされます。

### 5.3 操舵モデル

```
target_wheel_angle = -steering × steer_gain
```

- `steer_gain` デフォルト: 0.7 (ステアリング入力→車輪角変換係数)
- ステアリングレートリミット: 5 rad/s

`critical_speed` を超えるとアンダーステア特性が発生します:

```
understeer_coeff = understeer_factor × (speed_ratio² - 1)
grip_factor = 1.0 / (1.0 + understeer_coeff)
actual_steering = requested_steering × grip_factor
```

### 5.4 サスペンションモデル

ピッチとロールの動的挙動をバネ-ダンパ系でモデル化します:

```
pitch_acc = -stiffness × dynamic_pitch - damping × pitch_rate + forcing
```

外力 (forcing):
- ピッチ: `-mass_height × longitudinal_acceleration` (加速→ノーズアップ)
- ロール: `mass_height × lateral_acceleration` (左旋回→右ロール)

最終姿勢 = 地形勾配 (OpenDRIVE) + 動的姿勢 (サスペンション)

### 5.5 設定ファイル `real_vehicle_params.json`

**パス**: `<GT_Sim.exe ディレクトリ>/config/real_vehicle_params.json`

ファイルが存在しない場合は C++ のハードコードデフォルト値が使用されます (エラーは発生しません)。

| パラメータ | デフォルト | 単位 | 説明 |
|---|---|---|---|
| `pitch_stiffness` | 15.0 | — | ピッチ方向のバネ定数 |
| `pitch_damping` | 6.0 | — | ピッチ方向の減衰係数 |
| `roll_stiffness` | 20.0 | — | ロール方向のバネ定数 |
| `roll_damping` | 8.0 | — | ロール方向の減衰係数 |
| `mass_height` | 0.02 | m | 重心高さ係数 |
| `center_of_rotation_z_offset` | 0.4 | m | ピッチ/ロール回転中心の Z オフセット |
| `max_pitch_deg` | 6.0 | deg | ピッチ角の上限 |
| `max_roll_deg` | 6.0 | deg | ロール角の上限 |
| `steer_gain` | 0.7 | — | ステアリングゲイン |
| `max_acc` | 12.0 | m/s^2 | 最大加速度 (フルスロットル時) |
| `max_speed` | 60.0 | m/s | 最大速度 (216 km/h) |
| `reverse_gear_ratio` | 1.5 | — | 後退ギア比 |
| `understeer_factor` | 0.0015 | — | アンダーステア係数 |
| `critical_speed` | 30.0 | m/s | アンダーステア臨界速度 |
| `max_understeer_reduction` | 0.6 | — | 最大アンダーステア低減量 |

### 5.6 フィードフォワード係数

縦方向 PID 制御のフィードフォワード項として、定常走行に必要なスロットル開度を事前計算できます:

```
ff_throttle = 0.005 / (torque(rpm) × max_acc) × speed²
```

RPM = 7000 (torque = 0.4), max_acc = 12.0 の場合: `ff_coeff ≈ 0.00104`

---

## 6. ウェイポイントシステム

### 6.1 生成方式

ウェイポイントは車両前方約 500m を密にサンプリングした経路点のリストです。2つのモードがあります。

**ルートベース** (AssignRouteAction が設定済みの場合):
- `route->SetPathS(rs)` で道路座標を辿り、ジャンクション内のルート追従を正しく処理
- ルートウェイポイントの direction を参照してレーン ID とオフセットを反転

**前方パス** (ルートなしの場合):
- `Position::MoveAlongS()` で道路サクセッサーリンクを辿って前方にステップ

### 6.2 定数

| 定数 | 値 | 説明 |
|---|---|---|
| `kWaypointStep` | 5.0 m | 標準ウェイポイント間隔 |
| `kWaypointTotalDistance` | 500.0 m | 前方生成距離 |

### 6.3 適応的ステップサイズ

カーブの曲率に基づいてウェイポイント間隔を動的に調整します。

| 条件 (1m先→2m先の heading 変化) | ステップサイズ |
|---|---|
| > 8 deg | 1.0 m |
| > 3 deg | 2.0 m |
| それ以下 | 5.0 m (デフォルト) |

### 6.4 ウェイポイントインデックス更新

毎フレーム、車両位置に最も近いウェイポイントを前回位置の [-20, +120] 範囲で探索します。

### 6.5 再生成トリガー

以下のイベントでウェイポイントが全面再生成されます:

| トリガー | 処理 |
|---|---|
| **LaneChangeAction 検出** | SmootherStep 補間で現在レーン→ターゲットレーンへの遷移パスを生成 |
| **LaneOffsetAction 検出** | SmootherStep 補間で現在オフセット→ターゲットオフセットへの遷移パスを生成 |
| **FollowTrajectoryAction 検出** | トラジェクトリ形状を評価してウェイポイントに変換 |
| **AssignRouteAction 検出** | ルートから新しいウェイポイントを抽出 |
| **ルートポインタ変更** | ルートオブジェクトが変わった場合に再抽出 |

### 6.6 世代バージョン管理

`waypoint_generation.version` (frame_data 内) はウェイポイント再生成のたびにインクリメントされます。Python 側でこの値を追跡することで、ルート変更を検出できます。

```python
def step(self, frame_data):
    gen = frame_data["waypoint_generation"]["version"]
    if gen != self._last_generation:
        self._on_waypoints_changed(frame_data["waypoints"])
        self._last_generation = gen
```

---

## 7. シナリオアクション連携

PythonDriverController は MODE_OVERRIDE で動作するため、esmini のデフォルトアクション処理とは異なる方式でシナリオアクションを処理します。

### 7.1 SpeedAction

1. SpeedAction は MODE_OVERRIDE 下で `Start()` 直後に `End()` される (esmini の仕様)
2. コントローラーは `DetectSpeedActionTarget()` で RUNNING/COMPLETE 状態の SpeedAction を走査
3. ターゲット速度と TransitionDynamics (形状・期間) を抽出
4. `LonProfilePlanner::SetTargetWithDynamics()` に渡して平滑化された速度プロファイルを生成

**TransitionDynamics の形状:**

| DynamicsShape | 対応 |
|---|---|
| `linear` | 線形遷移 |
| `sinusoidal` | 正弦波遷移 |
| `cubic` | 3次曲線遷移 |
| `step` | 即座に目標速度へジャンプ |

**DynamicsDimension の変換:**

| Dimension | 処理 |
|---|---|
| `time` | duration をそのまま使用 |
| `distance` | `duration = distance / ((current_speed + target_speed) / 2)` |
| `rate` | `duration = |target - current| / rate` |

### 7.2 LaneChangeAction

1. `HandlePathActions()` が新規 LaneChangeAction を検出 (エッジ検出: 前フレームでは非アクティブ)
2. ターゲットレーン ID と遷移時間を取得
3. `RegenerateWaypointsForLaneChange()` で SmootherStep 補間による遷移パスを生成
4. `action->End()` でアクションを強制完了
5. Python 側にはウェイポイント変更として通知される

### 7.3 LaneOffsetAction

1. LaneChangeAction と同様のエッジ検出
2. `ResolveLaneOffsetTarget()` で ABSOLUTE/RELATIVE ターゲットを解決
3. `RegenerateWaypointsForLaneOffset()` で遷移パスを生成
4. `action->End()` で強制完了

### 7.4 FollowTrajectoryAction

1. トラジェクトリ形状 (`shape_->Evaluate()`) を s パラメータで評価
2. 各点を `(x, y, h)` のウェイポイントに変換
3. `action->End()` で強制完了

### 7.5 AssignRouteAction

1. ルートからウェイポイントを再抽出 (`ExtractWaypoints()`)
2. `action->End()` で強制完了
3. ルートポインタの変更も別途監視 (`RefreshWaypointsOnRoutePointerChange()`)

### 7.6 フラグのみ伝達されるアクション

以下のアクションは C++ 側でインターセプト/完了されず、`actions` dict のブールフラグとして Python に伝達されます。

| アクション | Python での利用例 |
|---|---|
| `longitudinal_distance` | ACC (車間距離制御) の有効化 |
| `speed_profile` | 速度プロファイル追従 |
| `synchronize` | 他車両との同期 |

Python 側でこれらのフラグを参照して独自の制御ロジックを実装します。

### 7.7 アクション検出の仕組み

C++ 側では「前フレームの状態 (`wasXxx_`)」と「現フレームの状態」を比較するエッジ検出パターンを使用しています。これにより、アクション開始の瞬間のみでウェイポイント再生成等の処理が実行されます。

---

## 8. ライト制御システム

### 8.1 ライトスロット一覧

7つのライトスロットがあり、Python の `lights` dict のキーと対応しています。

| インデックス | スロット名 | Python key | 説明 |
|---|---|---|---|
| 0 | LOW_BEAM | `low_beam` | ロービーム |
| 1 | HIGH_BEAM | `high_beam` | ハイビーム |
| 2 | LEFT_INDICATOR | `left_indicator` | 左ウインカー |
| 3 | RIGHT_INDICATOR | `right_indicator` | 右ウインカー |
| 4 | FOG | `fog` | フォグランプ |
| 5 | BRAKE | `brake` | ブレーキランプ |
| 6 | REVERSE | `reverse` | バックランプ |

### 8.2 制御モード

各ライトスロットに対して3つのモードを指定できます。

| 値 | モード | 動作 |
|---|---|---|
| `"auto"` | 自動 | AutoLightController に委譲 |
| `"on"` | 手動 ON | 強制点灯 |
| `"off"` | 手動 OFF | 強制消灯 |

### 8.3 パッチセマンティクス

`lights` dict はパッチ (差分) として適用されます:

- **空の dict `{}`**: 何も変更しない (前フレームの状態を維持)
- **特定キーのみ含む**: 指定されたスロットのみ変更
- **各フレームで消費**: C++ はパッチ適用後に内部状態をクリアする

**例: 左ウインカーのみ ON にする**

```python
return {
    ...,
    "lights": {"left_indicator": "on"},
}
```

**例: 全ライトを自動モードに戻す**

```python
return {
    ...,
    "lights": {
        "low_beam": "auto",
        "high_beam": "auto",
        "left_indicator": "auto",
        "right_indicator": "auto",
        "fog": "auto",
        "brake": "auto",
        "reverse": "auto",
    },
}
```

### 8.4 AutoLightController との連携

`"auto"` モードの場合、GT_esmini の AutoLightController が以下の自動制御を行います:

- **ブレーキランプ**: 減速度 > 1.2 m/s^2 で点灯
- **ウインカー**: 車線変更やジャンクション手前で自動点灯
- **バックランプ**: リバースギアで自動点灯

### 8.5 OSI ライトマスク

ライト状態は OSI 出力に以下のビットマスクとして反映されます:

| ビット | ライト |
|---|---|
| bit 0 (1) | ロービーム |
| bit 1 (2) | ハイビーム |
| bit 2 (4) | 左ウインカー |
| bit 3 (8) | 右ウインカー |
| bit 4 (16) | フォグランプ |

---

## 9. ADAS状態

### 9.1 ADAS 機能一覧

24個の ADAS 機能が定義されています。`adas_states` リストの各インデックスが対応する機能の状態を表します。

| Index | 機能名 | 説明 |
|---|---|---|
| 0 | BLIND_SPOT_WARNING | 死角警告 |
| 1 | FORWARD_COLLISION_WARNING | 前方衝突警告 |
| 2 | LANE_DEPARTURE_WARNING | 車線逸脱警告 |
| 3 | PARKING_COLLISION_WARNING | 駐車衝突警告 |
| 4 | REAR_CROSS_TRAFFIC_WARNING | 後方交差交通警告 |
| 5 | AUTOMATIC_EMERGENCY_BRAKING | 自動緊急ブレーキ |
| 6 | AUTOMATIC_EMERGENCY_STEERING | 自動緊急操舵 |
| 7 | REVERSE_AUTOMATIC_EMERGENCY_BRAKING | 後退自動緊急ブレーキ |
| 8 | ADAPTIVE_CRUISE_CONTROL | ACC |
| 9 | LANE_KEEPING_ASSIST | LKAS |
| 10 | ACTIVE_DRIVING_ASSISTANCE | ADA |
| 11 | BACKUP_CAMERA | バックカメラ |
| 12 | SURROUND_VIEW_CAMERA | サラウンドビュー |
| 13 | NIGHT_VISION | ナイトビジョン |
| 14 | HEAD_UP_DISPLAY | HUD |
| 15 | ACTIVE_PARKING_ASSISTANCE | 自動駐車支援 |
| 16 | REMOTE_PARKING_ASSISTANCE | リモート駐車 |
| 17 | TRAILER_ASSISTANCE | トレーラー支援 |
| 18 | AUTOMATIC_HIGH_BEAMS | 自動ハイビーム |
| 19 | DRIVER_MONITORING | ドライバーモニタリング |
| 20 | URBAN_DRIVING | 都市走行自動運転 |
| 21 | HIGHWAY_AUTOPILOT | 高速道路自動運転 |
| 22 | CRUISE_CONTROL | クルーズコントロール |
| 23 | SPEED_LIMIT_CONTROL | 速度制限制御 |

### 9.2 状態値

| 値 | 意味 |
|---|---|
| 0 | 無効/利用不可 |
| 1 | 利用可能だが非アクティブ |
| 2 | アクティブ/作動中 |

### 9.3 使用方法

```python
# ACC をアクティブ、LKAS をアクティブに設定する例
adas = [0] * 24
adas[8] = 2   # ADAPTIVE_CRUISE_CONTROL = Active
adas[9] = 2   # LANE_KEEPING_ASSIST = Active

return {
    ...,
    "adas_states": adas,
}
```

`adas_states` が空リスト `[]` の場合、前回の状態が維持されます。

---

## 10. トレース・デバッグ

### 10.1 トレースの有効化

XOSC で以下のプロパティを設定します:

```xml
<Property name="PythonTrace" value="on"/>
<Property name="PythonTraceDir" value="path/to/output"/>
```

`PythonTraceDir` が空の場合、カレントディレクトリに出力されます。

### 10.2 トレースファイル

3つの JSONL (1行1JSON) ファイルが生成されます。

#### `cpp_to_py_trace.jsonl` — C++ → Python への入力

各フレームで Python に渡されたデータの概要です。

```json
{"frame_id":0,"gt_size":12345,"waypoint_count":100,"waypoint_index":0,"waypoint_generation_version":1,"actions":{"assign_route":false,"lane_change":false,"lane_offset":false,"follow_trajectory":false,"longitudinal_distance":false,"speed_profile":false,"synchronize":false},"lon_profile_count":20,"set_speed":10.0,"current_speed":0.0,"dt":0.0}
```

主なフィールド:

| フィールド | 説明 |
|---|---|
| `frame_id` | フレーム番号 |
| `gt_size` | OSI GroundTruth のバイト数 |
| `waypoint_count` | ウェイポイント数 |
| `waypoint_index` | 現在の最近接インデックス |
| `waypoint_generation_version` | ウェイポイント世代 |
| `actions` | シナリオアクションフラグ |
| `lon_profile_count` | 速度プロファイル点数 |
| `set_speed` | 目標速度 |
| `current_speed` | 現在速度 |
| `dt` | タイムステップ |

#### `py_to_cpp_trace.jsonl` — Python → C++ への出力

Python から返された制御値の解析結果です。

```json
{"frame_id":1,"result_keys":["throttle","brake","steering","gear","lights","engine_brake","adas_states"],"required_keys":{"throttle":true,"brake":true,"steering":true,"gear":true,"lights":true},"parsed":{"throttle":0.25,"brake":0.0,"steering":0.05,"gear":1,"lights":{"low_beam":"auto","high_beam":"unset","left_indicator":"unset","right_indicator":"unset","fog":"unset","brake":"unset","reverse":"unset"},"engine_brake":0.49,"adas_count":0},"valid":true}
```

主なフィールド:

| フィールド | 説明 |
|---|---|
| `error` | エラーがある場合の種別 (`"step_exception"`, `"invalid_result"`) |
| `result_keys` | Python が返した dict の全キー |
| `required_keys` | 必須キーの有無チェック |
| `parsed` | C++ が解析した制御値 |
| `valid` | 全体のバリデーション結果 |

#### `python_trace.jsonl` — Python 内部のトレース

Python コントローラー内で記録される処理ログです (実装は各 Python スクリプトに依存)。

```json
{"frame_id":1,"recv":{"gt_bytes_len":12345,"waypoint_count":100,"waypoint_index":5,"waypoint_generation":1,"lon_profile_count":20,"set_speed":10.0,"current_speed":5.2,"dt":0.01,"actions":{}},"control_mode":"scenario_longitudinal","send":{"throttle":0.25,"brake":0.0,"steering":0.05,"gear":1,"lights":{},"engine_brake":0.49}}
```

### 10.3 デバッグ手順

1. 問題のフレーム番号を特定する (ログやプロット等から)
2. `cpp_to_py_trace.jsonl` で該当フレームの入力データを確認
3. `py_to_cpp_trace.jsonl` で Python の出力と `valid` フラグを確認
4. `python_trace.jsonl` で Python 内部の制御モードと計算結果を確認
5. 3ファイルを `frame_id` で結合して時系列分析

---

## 11. エラーハンドリング

### 11.1 致命的エラーポリシー

PythonDriverController では**すべてのエラーが致命的**です。エラーが発生すると `fatal_error_` フラグが立ち、`SE_Close()` によりシミュレーションが停止します。回復はできません。

### 11.2 初期化失敗パターン

| 段階 | エラー条件 | ログメッセージ |
|---|---|---|
| モジュールインポート | スクリプトが見つからない / 構文エラー | `Failed to import module 'xxx'` |
| クラス取得 | クラスが存在しない / callable でない | `Class 'xxx' not found or not callable` |
| インスタンス生成 | コンストラクタが例外 | `Failed to instantiate class 'xxx'` |
| `init()` 呼び出し | `init()` が例外を送出 | `init() raised exception` |

### 11.3 実行時失敗パターン

| エラー条件 | 動作 | ログ |
|---|---|---|
| `step()` が例外 | `fatal_error_ = true`、即座に停止 | `step() raised exception` |
| `step()` が dict 以外を返す | `valid = false`、停止 | `step() did not return a dict` |
| `lights` キーが欠如 | `valid = false`、停止 | `missing required dict key 'lights'` |
| ライト値が不正文字列 | 警告ログ、`valid = false` | `lights.xxx has invalid value 'yyy'` |

### 11.4 Python 例外のスタックトレース

Python 例外が発生した場合、`PyErr_Print()` により stderr にスタックトレースが出力されます。GT_Sim の `stderr.txt` で確認できます。

---

## 12. トラブルシューティング

### 12.1 `python312.dll not found`

**原因**: 組み込み Python ランタイムが GT_Sim.exe と同じディレクトリにない。

**対策**: `thirdparty/python-embed/python-3.12.10-embed-amd64/` から `python312.dll` を GT_Sim.exe のディレクトリにコピーする。

### 12.2 `ModuleNotFoundError: No module named 'encodings'`

**原因**: `python312.zip` が見つからない。`Py_SetPythonHome()` を使用すると `python312._pth` の処理が無効化される。

**対策**: `PythonHome` プロパティが正しいパスを指していること、そのディレクトリに `python312.zip` が存在することを確認する。

### 12.3 Python スクリプトが見つからない

**原因**: `PythonScript` パスの解決に失敗。

**対策**:
1. GT_Sim をリポジトリルートから起動しているか確認
2. 絶対パスで指定してテスト
3. パス解決順序 (CWD → exe dir → 6階層上まで探索) を参照

### 12.4 `import osi3` / `import google.protobuf` が失敗

**原因**: 組み込み Python に protobuf / osi3 パッケージがない。

**対策**: `DriverScript/` ディレクトリが sys.path に含まれていること、`DriverScript/osi3/` パッケージが存在することを確認。`SetupSysPath()` がスクリプトディレクトリから `DriverScript/` を自動検出する。

### 12.5 DLL コピーが失敗する

**原因**: GT_Sim.exe プロセスが DLL をロック中。

**対策**: ビルド前に GT_Sim プロセスを停止する。

### 12.6 初期フレームで急ブレーキ (PID 微分キック)

**原因**: `dt=0` の初期化フレームで PID を実行すると、OSI 速度が 0 のため微分項が暴走する。

**対策**: `dt <= 0` のフレームでは PID 計算をスキップし、制御出力を 0 にする。

### 12.7 LaneChangeAction 後のウェイポイント不整合

**原因**: コントローラーが LaneChangeAction を `End()` で強制完了し、ウェイポイントを再生成する。

**対策**: Python 側で `waypoint_generation.version` を監視し、変更時にウェイポイントインデックスを再初期化する。

### 12.8 SpeedAction が即座に End() になる

**原因**: MODE_OVERRIDE 下では esmini が SpeedAction の `Start()` 直後に `End()` を呼ぶ。これは仕様通りの動作。

**対策**: コントローラーは `DetectSpeedActionTarget()` で完了した SpeedAction からターゲット速度を抽出する。Python 側での対処は不要。

### 12.9 ステアリング符号の不一致

**原因**: Python 制御ロジックのステアリング符号と RealVehicle の符号が逆の場合がある。

**対策**: RealVehicle は `wheel_angle = -steering × steer_gain` で変換する。正の steering → 左旋回。Python スクリプト内で符号を確認すること。

---

## 13. 関連ドキュメント

| ドキュメント | 内容 |
|---|---|
| [システム構造](system_structure.md) | GT_esmini 全体のアーキテクチャとモジュール構成 |
| [PythonDriver検証テスト](validation_tests.md) | 4段階の検証テスト構造と実行方法 |
| [シミュレーションデータ形式](simulation_data_format.md) | .dat / .csv の形式仕様 |
| [AutoLight機能](../features/auto_light.md) | 自動ライト制御の詳細 |
| [OSI連携](../integration/osi_integration.md) | OSI GroundTruth / HostVehicleData の仕様 |
| [DriverScript README](../../../DriverScript/README.md) | Python パッケージの概要と API コントラクト |
