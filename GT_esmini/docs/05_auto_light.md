# AutoLight機能

このドキュメントでは、GT_esminiの自動ライト制御機能（AutoLight）について説明します。

## AutoLight機能とは

AutoLight機能は、車両の動作に応じて自動的にライトを点灯・消灯する機能です。シナリオファイル（XOSC）に明示的な`LightStateAction`を記述しなくても、リアルな車両挙動を再現できます。

### 自動制御されるライト

AutoLightは、以下の3種類のライトを自動制御します：

| ライトタイプ | 点灯条件 | 消灯条件 |
|------------|---------|---------|
| **ブレーキランプ** | 減速度が-0.1G以下 | 減速度が-0.1Gより大きい |
| **ウインカー（左/右）** | 車線変更開始時、交差点での右左折開始時 | 車線変更完了時、交差点通過時 |
| **バックライト** | 速度が負（後退中） | 速度が0以上（前進または停止） |

## ブレーキランプの自動制御

### 点灯条件

ブレーキランプは、車両の減速度が**-0.1G（約-0.98 m/s²）以下**になると点灯します。

```cpp
const double BRAKE_DECELERATION_THRESHOLD = -0.98; // -0.1G [m/s^2]
```

### 実装の詳細

AutoLightControllerは、毎フレーム車両の速度を監視し、前フレームとの差分から減速度を計算します：

```cpp
void AutoLightController::UpdateBrakeLights()
{
    double currentSpeed = vehicle_->GetSpeed();
    double acceleration = (currentSpeed - prevSpeed_) / dt;
    
    if (acceleration <= BRAKE_DECELERATION_THRESHOLD)
    {
        // ブレーキランプを点灯
        LightState state;
        state.mode = LightState::Mode::ON;
        lightExt_->SetLightState(VehicleLightType::BRAKE_LIGHTS, state);
    }
    else
    {
        // ブレーキランプを消灯
        LightState state;
        state.mode = LightState::Mode::OFF;
        lightExt_->SetLightState(VehicleLightType::BRAKE_LIGHTS, state);
    }
    
    prevSpeed_ = currentSpeed;
}
```

### 使用例

```cpp
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

// シミュレーション実行
// 車両が減速すると、自動的にブレーキランプが点灯
for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
    
    // ブレーキランプの状態を確認
    int brakeLight = GT_GetLightState(0, 6); // BRAKE_LIGHTS
    if (brakeLight == 1)
    {
        std::cout << "Brake light is ON (vehicle is decelerating)" << std::endl;
    }
}

GT_Close();
```

## ウインカーの自動制御

### 点灯条件

ウインカーは、以下の2つの状況で自動的に点灯します：

#### 1. 車線変更時

車両の`laneId`が変化すると、車線変更と判定されます：

- **左車線変更**: `laneId`が増加 → 左ウインカー点灯
- **右車線変更**: `laneId`が減少 → 右ウインカー点灯

```cpp
int currentLaneId = vehicle_->pos_.GetTrackId();

if (currentLaneId != prevLaneId_)
{
    if (currentLaneId > prevLaneId_)
    {
        // 左車線変更 → 左ウインカー点灯
        SetIndicator(VehicleLightType::INDICATOR_LEFT, true);
    }
    else
    {
        // 右車線変更 → 右ウインカー点灯
        SetIndicator(VehicleLightType::INDICATOR_RIGHT, true);
    }
    
    isInLaneChange_ = true;
    laneChangeStartTime_ = currentTime;
}
```

#### 2. 交差点での右左折時

車両がジャンクション内にいる場合、進行方向から右左折を判定します：

- **左折**: 進行方向が左に変化 → 左ウインカー点灯
- **右折**: 進行方向が右に変化 → 右ウインカー点灯

```cpp
if (vehicle_->pos_.GetJunctionId() >= 0)
{
    // ジャンクション内
    double currentHeading = vehicle_->pos_.GetH();
    double headingChange = currentHeading - prevHeading_;
    
    if (headingChange > TURN_THRESHOLD)
    {
        // 左折 → 左ウインカー点灯
        SetIndicator(VehicleLightType::INDICATOR_LEFT, true);
    }
    else if (headingChange < -TURN_THRESHOLD)
    {
        // 右折 → 右ウインカー点灯
        SetIndicator(VehicleLightType::INDICATOR_RIGHT, true);
    }
}
```

### 消灯条件

ウインカーは、以下の条件で消灯します：

- 車線変更完了後（一定時間経過）
- 交差点通過後（ジャンクションから出た時）

```cpp
if (isInLaneChange_ && (currentTime - laneChangeStartTime_) > LANE_CHANGE_DURATION)
{
    // 車線変更完了 → ウインカー消灯
    SetIndicator(VehicleLightType::INDICATOR_LEFT, false);
    SetIndicator(VehicleLightType::INDICATOR_RIGHT, false);
    isInLaneChange_ = false;
}
```

### 使用例

```cpp
GT_Init("lane_change_scenario.xosc", 0);
GT_EnableAutoLight();

for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
    
    // ウインカーの状態を確認
    int leftInd = GT_GetLightState(0, 8);  // INDICATOR_LEFT
    int rightInd = GT_GetLightState(0, 9); // INDICATOR_RIGHT
    
    if (leftInd == 1)
    {
        std::cout << "Left indicator is ON (turning left or changing to left lane)" << std::endl;
    }
    if (rightInd == 1)
    {
        std::cout << "Right indicator is ON (turning right or changing to right lane)" << std::endl;
    }
}

GT_Close();
```

## バックライトの自動制御

### 点灯条件

バックライト（後退灯）は、車両の速度が**負（後退中）**の場合に点灯します。

```cpp
void AutoLightController::UpdateReversingLights()
{
    double currentSpeed = vehicle_->GetSpeed();
    
    if (currentSpeed < 0.0)
    {
        // バックライトを点灯
        LightState state;
        state.mode = LightState::Mode::ON;
        lightExt_->SetLightState(VehicleLightType::REVERSING_LIGHTS, state);
    }
    else
    {
        // バックライトを消灯
        LightState state;
        state.mode = LightState::Mode::OFF;
        lightExt_->SetLightState(VehicleLightType::REVERSING_LIGHTS, state);
    }
}
```

### 使用例

```cpp
GT_Init("reverse_scenario.xosc", 0);
GT_EnableAutoLight();

for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
    
    // バックライトの状態を確認
    int reverseLight = GT_GetLightState(0, 10); // REVERSING_LIGHTS
    if (reverseLight == 1)
    {
        std::cout << "Reversing light is ON (vehicle is reversing)" << std::endl;
    }
}

GT_Close();
```

## AutoLight機能の有効化

AutoLight機能を使用するには、`GT_EnableAutoLight()`を呼び出します。

### 方法1: GT_Init使用時

```cpp
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight(); // AutoLight有効化
```

### 方法2: SE_Init使用時

```cpp
SE_Init("scenario.xosc", 0, 0, 0, 0);
GT_EnableAutoLight(); // AutoLight有効化
```

> [!NOTE]
> `GT_EnableAutoLight()`は、初期化後、シミュレーション開始前に呼び出してください。

## LightStateActionとの優先順位

AutoLightとLightStateActionを併用する場合、以下の優先順位が適用されます：

### 優先順位ルール

1. **LightStateActionが実行される前**: AutoLightが有効な場合、自動制御が行われる
2. **LightStateActionが実行された時**: AutoLightの状態を**上書き**する
3. **LightStateActionが実行された後**: 次のAutoLight更新まで、LightStateActionの状態が保持される

### 例: ブレーキランプの制御

```xml
<!-- シナリオファイル: 2秒後にブレーキランプを強制的にOFF -->
<Event name="BrakeLightOffEvent" priority="overwrite">
    <Action name="BrakeLightOffAction">
        <PrivateAction>
            <AppearanceAction>
                <LightStateAction>
                    <LightType>
                        <VehicleLight vehicleLightType="brakeLights"/>
                    </LightType>
                    <LightState mode="off"/>
                </LightStateAction>
            </AppearanceAction>
        </PrivateAction>
    </Action>
    <StartTrigger>
        <ConditionGroup>
            <Condition name="Time2s" delay="0" conditionEdge="rising">
                <ByValueCondition>
                    <SimulationTimeCondition value="2.0" rule="greaterThan"/>
                </ByValueCondition>
            </Condition>
        </ConditionGroup>
    </StartTrigger>
</Event>
```

```cpp
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

// 0-2秒: AutoLightがブレーキランプを制御（減速時に点灯）
// 2秒: LightStateActionでブレーキランプを強制的にOFF
// 2秒以降: AutoLightが再び制御を開始（減速時に点灯）
```

### タイムライン例

```
時刻 | AutoLight | LightStateAction | 最終状態
-----|-----------|------------------|----------
0s   | OFF       | -                | OFF
1s   | ON (減速) | -                | ON
2s   | ON (減速) | OFF (実行)       | OFF ← LightStateActionが優先
3s   | ON (減速) | -                | ON ← AutoLightが再び制御
4s   | OFF (加速)| -                | OFF
```

## 動作パラメータとしきい値

AutoLightControllerは、以下のパラメータを使用します：

| パラメータ | 値 | 説明 |
|-----------|---|------|
| `BRAKE_DECELERATION_THRESHOLD` | -0.98 m/s² | ブレーキランプ点灯のしきい値（-0.1G） |
| `TURN_THRESHOLD` | 0.1 rad | 右左折判定のしきい値（約5.7度） |
| `LANE_CHANGE_DURATION` | 3.0 s | 車線変更完了と判定する時間 |

これらのパラメータは、`AutoLightController.cpp`で定義されています。

## カスタマイズ

しきい値をカスタマイズしたい場合は、`AutoLightController.cpp`を編集してください：

```cpp
// ブレーキランプのしきい値を変更（例: -0.05G）
const double BRAKE_DECELERATION_THRESHOLD = -0.49; // -0.05G [m/s^2]

// 車線変更完了時間を変更（例: 2秒）
const double LANE_CHANGE_DURATION = 2.0; // [s]
```

再ビルド後、新しいしきい値が適用されます。

## デバッグ

AutoLightの動作を確認するには、`GT_GetLightState()`を使用します：

```cpp
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
    
    // すべてのライト状態を表示
    int vehicleId = 0;
    std::cout << "Time: " << (i * 0.05) << "s" << std::endl;
    std::cout << "  Brake:   " << GT_GetLightState(vehicleId, 6) << std::endl;
    std::cout << "  L-Ind:   " << GT_GetLightState(vehicleId, 8) << std::endl;
    std::cout << "  R-Ind:   " << GT_GetLightState(vehicleId, 9) << std::endl;
    std::cout << "  Reverse: " << GT_GetLightState(vehicleId, 10) << std::endl;
}

GT_Close();
```

## 制限事項

現在のバージョンでは、以下の制限があります：

1. **ヘッドライト**: ロービーム、ハイビームは自動制御されません
2. **フォグライト**: フォグライトは自動制御されません
3. **ハザードランプ**: ハザードランプは自動制御されません（LightStateActionで制御可能）
4. **カスタマイズ**: しきい値の変更にはソースコードの編集と再ビルドが必要

## 次のステップ

- [LightStateAction機能](04_light_state_action.md) - シナリオでライトを制御する
- [サンプルシナリオ](07_examples.md) - AutoLightの実用例
- [OSI連携](06_osi_integration.md) - OSI出力でのライト状態

## 関連ドキュメント

- [基本的な使い方](03_basic_usage.md) - GT_esminiの基本
- [APIリファレンス](10_api_reference.md) - 関数の詳細仕様
- [アーキテクチャ](08_architecture.md) - AutoLightControllerの内部構造
