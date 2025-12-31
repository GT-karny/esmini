# サンプルシナリオ

このドキュメントでは、GT_esminiの実用的な使用例を紹介します。

## 例1: LightStateActionを使用したシナリオ

### シナリオの説明

車両が走行中に、特定のタイミングでブレーキランプとフォグライトを点灯させるシナリオです。

### XOSCファイル

`test_multiple_lights.xosc`の一部：

```xml
<Event name="MultipleLightsEvent" priority="parallel">
    <Action name="LowBeamAction">
        <PrivateAction>
            <AppearanceAction>
                <LightStateAction transitionTime="0.0">
                    <LightType>
                        <VehicleLight vehicleLightType="lowBeam"/>
                    </LightType>
                    <LightState mode="on"/>
                </LightStateAction>
            </AppearanceAction>
        </PrivateAction>
    </Action>
    <Action name="FogLightsAction">
        <PrivateAction>
            <AppearanceAction>
                <LightStateAction transitionTime="1.0">
                    <LightType>
                        <VehicleLight vehicleLightType="fogLights"/>
                    </LightType>
                    <LightState mode="on" luminousIntensity="2.0"/>
                </LightStateAction>
            </AppearanceAction>
        </PrivateAction>
    </Action>
    <StartTrigger>
        <ConditionGroup>
            <Condition name="StartCondition" delay="0" conditionEdge="rising">
                <ByValueCondition>
                    <SimulationTimeCondition value="2.0" rule="greaterThan"/>
                </ByValueCondition>
            </Condition>
        </ConditionGroup>
    </StartTrigger>
</Event>
```

### C++コード

```cpp
#include "GT_esminiLib.hpp"
#include <iostream>

int main()
{
    // シナリオ読み込み
    if (GT_Init("test_multiple_lights.xosc", 0) != 0)
    {
        std::cerr << "Failed to load scenario" << std::endl;
        return 1;
    }
    
    // シミュレーション実行
    for (int i = 0; i < 200; ++i) // 10秒間
    {
        GT_Step(0.05);
        
        // ライト状態を確認
        if (i % 20 == 0) // 1秒ごと
        {
            int lowBeam = GT_GetLightState(0, 1);  // LOW_BEAM
            int fogLight = GT_GetLightState(0, 3); // FOG_LIGHTS
            
            std::cout << "Time " << (i * 0.05) << "s | "
                      << "LowBeam: " << lowBeam << " | "
                      << "FogLight: " << fogLight << std::endl;
        }
    }
    
    GT_Close();
    return 0;
}
```

### 実行方法

```bash
cd build/bin
./GT_Loader ../../GT_esmini/test/scenarios/test_multiple_lights.xosc
```

### 期待される結果

- 0-2秒: すべてのライトがOFF
- 2秒以降: ロービームとフォグライトがON

## 例2: AutoLightのみを使用

### シナリオの説明

AutoLight機能を使用して、車両の減速時にブレーキランプを自動点灯させるシナリオです。

### XOSCファイル

通常のesminiシナリオ（ライトアクションなし）を使用：

```xml
<!-- cut-in.xosc など、既存のシナリオ -->
```

### C++コード

```cpp
#include "esminiLib.hpp"
#include "GT_esminiLib.hpp"
#include <iostream>

int main()
{
    // esmini本体で初期化
    if (SE_Init("cut-in.xosc", 0, 0, 0, 0) != 0)
    {
        std::cerr << "Failed to load scenario" << std::endl;
        return 1;
    }
    
    // AutoLight有効化
    GT_EnableAutoLight();
    std::cout << "AutoLight enabled" << std::endl;
    
    // シミュレーション実行
    for (int i = 0; i < 600; ++i) // 30秒間
    {
        SE_Step();
        GT_Step(0.05);
        
        // ブレーキランプの状態を確認
        if (i % 20 == 0)
        {
            int brakeLight = GT_GetLightState(0, 6); // BRAKE_LIGHTS
            
            // 車両の速度も取得
            SE_ScenarioObjectState state;
            SE_GetObjectState(0, &state);
            
            std::cout << "Time " << (i * 0.05) << "s | "
                      << "Speed: " << state.speed << " m/s | "
                      << "BrakeLight: " << brakeLight << std::endl;
        }
    }
    
    SE_Close();
    GT_Close();
    return 0;
}
```

### 実行方法

```bash
cd build/bin
./esmini --osc ../../resources/xosc/cut-in.xosc --headless
```

（カスタムアプリケーションの場合は、上記のC++コードをビルドして実行）

### 期待される結果

- 車両が減速すると、ブレーキランプが自動的に点灯
- 加速すると、ブレーキランプが消灯

## 例3: LightStateActionとAutoLightの組み合わせ

### シナリオの説明

AutoLightを有効にしつつ、特定のタイミングでLightStateActionを使用してライトを強制制御するシナリオです。

### XOSCファイル

`autolight_test.xosc`（AutoLightテスト用シナリオ）

### C++コード

```cpp
#include "GT_esminiLib.hpp"
#include <iostream>

int main()
{
    // GT_Initでシナリオ読み込み（LightStateAction対応）
    if (GT_Init("autolight_test.xosc", 0) != 0)
    {
        std::cerr << "Failed to load scenario" << std::endl;
        return 1;
    }
    
    // AutoLight有効化
    GT_EnableAutoLight();
    
    // シミュレーション実行
    for (int i = 0; i < 300; ++i) // 15秒間
    {
        GT_Step(0.05);
        
        if (i % 10 == 0)
        {
            int brake = GT_GetLightState(0, 6);    // BRAKE_LIGHTS
            int leftInd = GT_GetLightState(0, 8);  // INDICATOR_LEFT
            int rightInd = GT_GetLightState(0, 9); // INDICATOR_RIGHT
            
            std::cout << "Time " << (i * 0.05) << "s | "
                      << "Brake: " << brake << " | "
                      << "L-Ind: " << leftInd << " | "
                      << "R-Ind: " << rightInd << std::endl;
        }
    }
    
    GT_Close();
    return 0;
}
```

### 期待される結果

- AutoLightによる自動制御とLightStateActionによる明示的制御が組み合わされる
- LightStateActionが実行された時点で、AutoLightの状態が上書きされる

## 例4: OSI出力を使用したライト状態の監視

### シナリオの説明

OSI UDPソケットを使用して、ライト状態を外部ツールに送信するシナリオです。

### C++コード（送信側）

```cpp
#include "esminiLib.hpp"
#include "GT_esminiLib.hpp"
#include <iostream>

int main()
{
    // GT_esmini初期化
    GT_Init("autolight_test.xosc", 0);
    GT_EnableAutoLight();
    
    // OSIソケットを開く
    if (SE_OpenOSISocket("127.0.0.1:48198") == 0)
    {
        std::cout << "OSI socket opened on 127.0.0.1:48198" << std::endl;
    }
    else
    {
        std::cerr << "Failed to open OSI socket" << std::endl;
        return 1;
    }
    
    // シミュレーション実行
    std::cout << "Running simulation..." << std::endl;
    for (int i = 0; i < 300; ++i)
    {
        GT_Step(0.05);
        // OSI出力は自動的に送信される
    }
    
    // クリーンアップ
    SE_CloseOSISocket();
    GT_Close();
    
    std::cout << "Simulation completed" << std::endl;
    return 0;
}
```

### Pythonコード（受信側）

```python
#!/usr/bin/env python3
import socket
from osi3.osi_groundtruth_pb2 import GroundTruth

# UDPソケットを開く
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('127.0.0.1', 48198))
sock.settimeout(1.0)

print("Waiting for OSI messages on 127.0.0.1:48198...")

try:
    while True:
        try:
            data, addr = sock.recvfrom(65536)
            
            # OSIメッセージをパース
            gt = GroundTruth()
            gt.ParseFromString(data)
            
            # ライト状態を表示
            for obj in gt.moving_object:
                if obj.HasField('vehicle_classification'):
                    light = obj.vehicle_classification.light_state
                    print(f"Time: {gt.timestamp.seconds}.{gt.timestamp.nanos:09d}")
                    print(f"  Vehicle ID: {obj.id.value}")
                    print(f"  Brake Light: {light.brake_light_state}")
                    print(f"  Indicator: {light.indicator_state}")
                    print(f"  Reverse Light: {light.reverse_light_state}")
                    print()
        except socket.timeout:
            continue
except KeyboardInterrupt:
    print("\nStopped by user")
finally:
    sock.close()
```

### 実行方法

1. 受信側を起動：
```bash
python osi_receiver.py
```

2. 送信側を起動：
```bash
./my_gt_esmini_app
```

### 期待される結果

- 受信側のコンソールに、リアルタイムでライト状態が表示される

## テストシナリオ一覧

GT_esminiには、以下のテストシナリオが含まれています：

| ファイル | 説明 |
|---------|------|
| `test_brake_lights.xosc` | ブレーキランプの点灯・消灯テスト |
| `test_indicators.xosc` | ウインカーの点滅テスト |
| `test_multiple_lights.xosc` | 複数のライトを同時制御 |
| `autolight_test.xosc` | AutoLight機能の総合テスト |

これらは`GT_esmini/test/scenarios/`ディレクトリにあります。

## 次のステップ

- [LightStateAction機能](04_light_state_action.md) - XOSCファイルの詳細
- [AutoLight機能](05_auto_light.md) - 自動制御の詳細
- [OSI連携](06_osi_integration.md) - OSI出力の詳細

## 関連ドキュメント

- [基本的な使い方](03_basic_usage.md) - GT_esminiの基本
- [APIリファレンス](10_api_reference.md) - 関数の詳細仕様
