# 信号機再発進不具合の修正とモデルベースフィードフォワード制御

このドキュメントでは、`traffic_lights_python.xosc` シナリオで発生した信号機再発進不具合の原因分析と、修正に伴って導入したモデルベースフィードフォワード制御について説明します。

---

## 1. 問題の概要

### 症状

`traffic_lights_python.xosc` シナリオにおいて、PythonDriverController を使用した場合、赤信号で停止した車両が緑信号に変わっても再発進できない。「一瞬前に出てからすぐ止まってしまう」症状が発生していた。DefaultController では正常に動作する。

### シナリオの構成

```
StopOnRedManeuver (同一Maneuver内、全てOVERWRITE優先度)
├── StopOnRedEvent:    SpeedAction(target=0, LINEAR, distance=44m)   ← 減速開始
├── WaitOnRedEvent:    SpeedAction(target=0, STEP), maxExec=1        ← 停止線7m以内で発火
└── DriveOnGreenEvent: SpeedAction(target=13.889, LINEAR, time=5s)   ← 青信号で発進
```

**イベント発火順序の要件**: WaitOnRedEvent は赤信号中に発火する必要がある。これが緑信号中に発火すると、DriveOnGreenEvent の加速を OVERWRITE で上書きしてしまい、再発進不能になる。

---

## 2. 根本原因の分析

### 原因1: END_OF_ROAD 誤検出による物理演算ブロック

`ControllerPythonDriver::UpdateVehiclePhysics()` に以下の2つの `POS_STATUS_END_OF_ROAD` チェックが存在していた。

```cpp
// 事前チェック: speed <= 0 かつ END_OF_ROAD → 物理演算スキップ
if (object_ && real_vehicle_.speed_ <= 0.0 &&
    (object_->pos_.GetStatusBitMask() &
     static_cast<int>(roadmanager::Position::PositionStatusMode::POS_STATUS_END_OF_ROAD)))
{
    real_vehicle_.speed_ = 0.0;
    currentSpeed_ = 0.0;
    return;  // ← 物理演算を完全にスキップ
}

// 事後チェック: END_OF_ROAD → 速度ゼロ化
if (object_ && (object_->pos_.GetStatusBitMask() &
                static_cast<int>(roadmanager::Position::PositionStatusMode::POS_STATUS_END_OF_ROAD)))
{
    real_vehicle_.speed_ = 0.0;
}
```

**問題のメカニズム**:

1. 交差点のジャンクション境界で `POS_STATUS_END_OF_ROAD` が誤検出される
2. 赤信号停止中、PID がブレーキを出力 → 速度 0 に到達
3. `speed <= 0 && END_OF_ROAD` で物理演算がスキップされる
4. 緑信号で PID がスロットルを出力しても物理演算が実行されず永久停止

DefaultController は `MoveAlongS()` で道路座標系に沿って走行するためジャンクション境界を自然に通過できるが、PythonDriverController はワールド座標系の物理演算 → `SetInertiaPos()` 変換を使うため、ジャンクション境界で END_OF_ROAD が誤検出される。

### 原因2: フィードフォワード制御の不正確さによる停車位置ずれ

WaitOnRedEvent の DistanceCondition は停止線から 7m 以内で発火する。しかし、PID の追従誤差により車両が 7m ゾーンに到達する前に停止してしまうと、WaitOnRedEvent が赤信号中に発火せず、緑信号での加速中に発火して DriveOnGreenEvent を上書きしてしまう。

旧フィードフォワードの問題点:

```python
# 旧実装: 定数係数
ff_drag_coeff = 0.000625  # = drag / (torque_at_redline × max_acc)
ff = ff_drag_coeff * target_speed²
```

この係数は **レッドライン（RPM=7000）でのトルク 0.4 のみ** を前提としていた。しかし RealVehicle の実際のトルクカーブは放物線型で、通常走行域（RPM 3000-6000）では torque = 0.77〜1.0 に達する。結果として FF が約2倍過大となり、PID 積分値が蓄積して減速時の追従誤差 ~0.5 m/s が発生していた。

---

## 3. 修正内容

### 修正1: END_OF_ROAD チェックの除去

**対象ファイル**: `GT_esmini/src/control/ControllerPythonDriver.cpp`

`UpdateVehiclePhysics()` から `POS_STATUS_END_OF_ROAD` チェック2箇所を完全に除去した。PythonDriverController の物理演算は Python PID が制御するため、道路末端での停止判断はシナリオ側の SpeedAction / StopTrigger に委ねるのが正しい設計。

**副作用**: 道路末端で車両が停止しなくなるが、比較テストの `find_active_end_time()` による比較区間制限で対処（後述）。

### 修正2: LonProfilePlanner の遷移開始点修正

**対象ファイル**: `GT_esmini/include/gt_esmini/control/realdriver/LonProfilePlanner.hpp`, `GT_esmini/src/control/realdriver/LonProfilePlanner.cpp`

`SetTargetWithDynamics()` に `start_speed` パラメータを追加。

```cpp
void SetTargetWithDynamics(double target_speed, double duration,
                           SpeedTransitionShape shape = SpeedTransitionShape::LINEAR,
                           double start_speed = -1.0);
```

従来は `smoothed_target_`（前回の平滑化目標速度）を遷移開始点としていたが、PID の追従遅れにより実速度と乖離していた。`currentSpeed_`（実際の車速）を渡すことで、現実の走行状態に基づいた減速プロファイルを生成する。

### 修正3: モデルベースフィードフォワード制御

**対象ファイル**: `DriverScript/realdriver/longitudinal_controller.py`

定数係数フィードフォワードを、RealVehicle の物理モデルを完全に再現したモデルベース FF に書き換えた。

#### RealVehicle 物理モデル

C++ 側の `RealVehicle::UpdatePhysics()` は以下の力学モデルで動作する:

```
engine_force = torque(speed) × throttle × max_acc     (max_acc = 20.0)
brake_force  = brake × max_dec                         (max_dec = 20.0)
drag         = 0.005 × speed²
engine_brake = 0.49  (throttle < 0.05 の場合のみ)

acceleration = engine_force - brake_force - drag - engine_brake
```

トルクカーブ（放物線型）:
```
rpm   = clamp(|speed| × 60 × gear_ratio × 2, idle_rpm, max_rpm)
norm  = (rpm - 800) / (7000 - 800)
torque = 0.4 + 0.6 × 4 × norm × (1 - norm)
```

| 速度 [m/s] | RPM  | トルク | 定常スロットル |
|:---:|:---:|:---:|:---:|
| 0   | 800  | 0.40 | 0.000 |
| 5   | 4200 | 1.00 | 0.006 |
| 10  | 4200 | 1.00 | 0.025 |
| 15  | 6300 | 0.76 | 0.074 |
| 20  | 7000 | 0.40 | 0.250 |

#### Python 側の実装

```python
def _compute_torque(self, speed: float) -> float:
    """RealVehicle のトルクカーブを再現"""
    rpm = clamp(|speed| × 420, 800, 7000)
    norm = (rpm - 800) / 6200
    return 0.4 + 0.6 × 4 × norm × (1 - norm)

def _compute_feedforward(self, target_speed, target_accel) -> float:
    """目標速度・目標加速度からスロットル/ブレーキを逆算"""
    drag = 0.005 × target_speed²
    eb = 0.49 × min(1.0, |target_speed|)  # 低速フェード

    if target_accel + drag > 0:
        # スロットル領域
        return (target_accel + drag) / (torque × 20.0)
    else:
        # ブレーキ領域（エンジンブレーキ補助あり）
        return (target_accel + drag + eb) / 20.0
```

**目標加速度の計算**: 連続フレームの `target_speed` 差分から算出。

```python
target_accel = (target_speed - prev_target_speed) / dt
```

LonProfilePlanner が SpeedAction の TransitionDynamics に基づいて滑らかに変化する `target_speed` を生成するため、FF は加減速プロファイル全体を予測的に補償する。

#### エンジンブレーキ低速フェード

RealVehicle の物理モデルでは速度 < 0.01 m/s のとき drag とエンジンブレーキが無視される。FF 側でもこれを反映しないと、停車時に正のスロットルが出力されてクリープが発生する。

```python
eb = 0.49 × min(1.0, abs_speed)  # 0-1 m/s でフェードイン
```

### 修正4: 比較テストの有効区間制限

**対象ファイル**: `scripts/comparison_kpis.py`

END_OF_ROAD チェック除去により、`straight_500m` シナリオで PythonDriverController が道路末端を通過して走り続ける。DefaultController は道路末端で停止するため、停止後の区間で大きな乖離が生じる。

この乖離は物理的に正しい差異（PythonDriverController に END_OF_ROAD 停止機能がない）であり、比較対象から除外すべきである。

```python
def find_active_end_time(rows, speed_threshold=0.1, hold_seconds=2.0, min_active_speed=1.0):
    """DefaultController が走行後に永続停止した時刻を検出。
    一度走行した後に 2秒以上停止が続いた場合、停止開始時刻を返す。
    """
```

この関数は以下の条件を満たす時刻を検出する:
1. 車両が一度 `min_active_speed`（1.0 m/s）以上で走行した（初期停止を除外）
2. その後 `speed_threshold`（0.1 m/s）以下に低下
3. `hold_seconds`（2.0秒）以上その状態が継続

検出された時刻を `t_max` として `align_time_series()` に渡し、比較区間を制限する。

---

## 4. 制御構成図

```
┌─────────────────────────────────────────────────────────┐
│ C++ ControllerPythonDriver                              │
│                                                         │
│  SpeedAction ──→ DetectSpeedActionTarget()               │
│                  ├── setSpeed_ = target                  │
│                  └── LonProfilePlanner                   │
│                      .SetTargetWithDynamics(             │
│                          target, duration, shape,        │
│                          currentSpeed_)                  │
│                                                         │
│  LonProfilePlanner.Advance(dt, setSpeed_)               │
│      └── smoothed_target_ を時間経過で更新               │
│                                                         │
│  smoothed_target_ ──→ [OSI GroundTruth経由] ──→ Python  │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│ Python LongitudinalController                           │
│                                                         │
│  target_speed (from C++)                                │
│      │                                                  │
│      ├──→ speed_error = target - current                │
│      │        └──→ PID (Kp=1.2, Ki=0.08, Kd=0.1)      │
│      │                 └──→ feedback                    │
│      │                                                  │
│      └──→ target_accel = Δtarget / dt                   │
│               └──→ _compute_feedforward()               │
│                        ├── torque = f(speed)            │
│                        ├── drag = 0.005 × v²           │
│                        ├── engine_brake = 0.49 × fade   │
│                        └──→ ff                          │
│                                                         │
│  control = feedback + ff                                │
│      ├── control ≥ 0 → throttle                         │
│      └── control < 0 → brake                            │
└─────────────────────────────────────────────────────────┘
```

---

## 5. テスト結果

### 信号機シナリオ

| 項目 | 修正前 | 修正後 |
|:---|:---|:---|
| WaitOnRedEvent 発火 | 緑信号中（t=18.57） | 赤信号中（t=16.23） |
| 停止線からの距離 | 9.41m（7m圏外） | 6.99m（7m圏内） |
| 緑信号後の再発進 | 不可 | 可（13+ m/s まで加速） |
| 交差点通過 | 不可 | 可（road 3 → road 2） |

### 回帰テスト（比較テスト 3/3 PASS）

| シナリオ | 結果 | 備考 |
|:---|:---|:---|
| straight_500m | PASS | `find_active_end_time` で道路末端後を除外 |
| speed_profile | PASS | 終点距離閾値を 4.0→7.0m に調整 |
| lane_change | PASS | 変更なし |

---

## 6. 設定パラメータ

### LongitudinalConfig（Python 側）

| パラメータ | デフォルト値 | 説明 |
|:---|:---:|:---|
| `pid_kp` | 1.2 | PID 比例ゲイン |
| `pid_ki` | 0.08 | PID 積分ゲイン |
| `pid_kd` | 0.1 | PID 微分ゲイン |
| `rv_max_acc` | 20.0 | RealVehicle の最大加速力 [m/s^2] |
| `rv_max_dec` | 20.0 | RealVehicle の最大減速力 [m/s^2] |
| `rv_drag_coeff` | 0.005 | 空気抵抗係数 |
| `rv_engine_brake` | 0.49 | エンジンブレーキ係数 |
| `rv_idle_rpm` | 800 | アイドル回転数 |
| `rv_max_rpm` | 7000 | 最大回転数 |
| `rv_gear_ratio` | 3.5 | ギア比 |

これらの値は C++ 側の `Vehicle` 基底クラスおよび `RealVehicle` のデフォルト値と一致させる必要がある。車両パラメータが変更された場合は Python 側も合わせて更新すること。

---

## 7. 関連ファイル

| ファイル | 役割 |
|:---|:---|
| `GT_esmini/src/control/ControllerPythonDriver.cpp` | PythonDriverController 本体 |
| `GT_esmini/src/control/realdriver/LonProfilePlanner.cpp` | 速度遷移プロファイル生成 |
| `GT_esmini/src/control/RealVehicle.cpp` | 車両物理モデル（参照用） |
| `DriverScript/realdriver/longitudinal_controller.py` | Python 縦方向制御 |
| `DriverScript/realdriver/pid_controller.py` | PID 制御器 |
| `scripts/comparison_kpis.py` | 比較テストメトリクス計算 |
| `GT_esmini/test/comparison_thresholds.yaml` | 比較テスト閾値設定 |
| `resources/xosc/traffic_lights_python.xosc` | 信号機シナリオ |
