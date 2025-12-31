# OSI連携

このドキュメントでは、GT_esminiとOSI (Open Simulation Interface) の連携について説明します。

## OSIとは

OSI (Open Simulation Interface) は、ADAS/AD開発のための標準化されたインターフェースです。シミュレーター間でセンサーデータや車両状態を交換するために使用されます。

GT_esminiは、OSI v3.5.0に対応しており、ライト状態をOSI出力に含めることができます。

## ライト状態のOSI出力

GT_esminiは、`MovingObject`メッセージの`VehicleClassification.LightState`フィールドにライト状態を出力します。

### サポートされているOSIフィールド

| GT_esmini ライトタイプ | OSI フィールド |
|----------------------|---------------|
| BRAKE_LIGHTS | `brake_light_state` |
| INDICATOR_LEFT | `indicator_state` (LEFT) |
| INDICATOR_RIGHT | `indicator_state` (RIGHT) |
| REVERSING_LIGHTS | `reverse_light_state` |
| FOG_LIGHTS_FRONT | `front_fog_light` |
| FOG_LIGHTS_REAR | `rear_fog_light` |
| WARNING_LIGHTS | `emergency_vehicle_illumination` |

### OSI出力の値

| GT_esmini モード | OSI 値 |
|-----------------|--------|
| OFF | `*_STATE_OFF` または `*_STATE_OTHER` |
| ON | `*_STATE_NORMAL` または `*_STATE_ON` |
| FLASHING | `*_STATE_FLASHING` |

## GT_OSIReporterの役割

GT_esminiは、`GT_OSIReporter`を使用してOSI出力にライト状態を追加します。

### 実装方法

`GT_OSIReporter`は、esmini本体の`OSIReporter`を置き換える形で実装されています：

1. `OSIReporter.cpp`をコピーして`GT_OSIReporter.cpp`を作成
2. `UpdateOSIMovingObject`関数にライト状態の出力処理を追加
3. ビルドシステムで`OSIReporter.cpp`の代わりに`GT_OSIReporter.cpp`を使用

詳細は[004_build_modifications_traceability.md](file:///e:/Repository/GT_esmini/esmini/plan/004_build_modifications_traceability.md)を参照してください。

## OSI出力の使用方法

### 基本的な使用方法

```cpp
#include "esminiLib.hpp"
#include "GT_esminiLib.hpp"

int main()
{
    // GT_esminiの初期化
    GT_Init("scenario.xosc", 0);
    GT_EnableAutoLight();
    
    // OSIソケットを開く（UDP）
    if (SE_OpenOSISocket("127.0.0.1:48198") == 0)
    {
        std::cout << "OSI socket opened" << std::endl;
    }
    else
    {
        std::cerr << "Failed to open OSI socket" << std::endl;
    }
    
    // シミュレーションループ
    for (int i = 0; i < 1000; ++i)
    {
        GT_Step(0.05);
        
        // OSI出力は自動的に更新・送信される
    }
    
    // クリーンアップ
    SE_CloseOSISocket();
    GT_Close();
    
    return 0;
}
```

### OSIファイル出力

UDPソケットの代わりに、ファイルに出力することもできます：

```cpp
// OSIファイル出力を有効化
SE_EnableOSIFile("output.osi");

// シミュレーション実行
for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
}

// ファイルは自動的にクローズされる
```

## OSI出力の確認

### Python OSIライブラリを使用

```python
from osi3.osi_groundtruth_pb2 import GroundTruth
import socket

# UDPソケットを開く
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('127.0.0.1', 48198))

while True:
    data, addr = sock.recvfrom(65536)
    
    # OSIメッセージをパース
    gt = GroundTruth()
    gt.ParseFromString(data)
    
    # ライト状態を表示
    for obj in gt.moving_object:
        if obj.HasField('vehicle_classification'):
            light = obj.vehicle_classification.light_state
            print(f"Vehicle {obj.id.value}:")
            print(f"  Brake: {light.brake_light_state}")
            print(f"  Indicator: {light.indicator_state}")
            print(f"  Reverse: {light.reverse_light_state}")
```

### OSI Visualizerを使用

OSI Visualizerなどのツールを使用して、OSI出力を可視化できます。

## 統合テスト例

GT_esminiには、OSI出力を確認する統合テストが含まれています：

```bash
# GT_Loaderを使用してOSI出力を確認
cd build/bin
./GT_Loader ../../GT_esmini/test/scenarios/autolight_test.xosc --osi_udp 127.0.0.1:48198
```

別のターミナルでOSI受信スクリプトを実行：

```bash
python osi_receiver.py
```

## トラブルシューティング

### OSI出力にライト状態が含まれない

**原因:** GT_OSIReporterが正しくビルドされていない

**解決策:**
1. `ScenarioEngine/CMakeLists.txt`で`GT_OSIReporter.cpp`が使用されていることを確認
2. クリーンビルドを実行

### UDPソケットが開けない

**原因:** ポートが既に使用されている、またはファイアウォールがブロックしている

**解決策:**
1. 別のポート番号を試す
2. ファイアウォール設定を確認

## 次のステップ

- [サンプルシナリオ](07_examples.md) - OSI使用例
- [基本的な使い方](03_basic_usage.md) - GT_esminiの基本

## 関連ドキュメント

- [Open Simulation Interface](https://github.com/OpenSimulationInterface/open-simulation-interface) - OSI公式リポジトリ
- [アーキテクチャ](08_architecture.md) - GT_OSIReporterの内部構造
