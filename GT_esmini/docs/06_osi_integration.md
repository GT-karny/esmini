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

## OSI入力からのライト制御

GT_OSMP_FMUは、OSI `TrafficUpdate` メッセージを介して外部からライト状態の制御を受け取ることができます。これにより、外部のコントローラーやシミュレーターがesmini内の車両のライトを操作可能になります。

### サポートされているOSI入力フィールド

`MovingObject.vehicle_classification.light_state` 内の以下のフィールドがサポートされています：

| OSI フィールド | 型 | 動作 |
|---------------|---|------|
| `indicator_state` | `IndicatorState` | `LEFT`/`RIGHT` で点滅、`WARNING` でハザード点滅 |
| `brake_light_state` | `BrakeLightState` | `NORMAL` または `STRONG` で点灯 |
| `reversing_light` | `GenericLightState` | `ON` で点灯 |
| `head_light` | `GenericLightState` | `ON` でロービーム点灯 |
| `high_beam` | `GenericLightState` | `ON` でハイビーム点灯 |

### TrafficUpdateによる位置・回転の更新

OSI `TrafficUpdate` メッセージを受信すると、以下の処理が行われます：

1.  **6自由度 (6DOF) 対応**:
    -   位置 (Position): `x`, `y`, `z`
    -   回転 (Orientation): `yaw` (Heading), `pitch`, `roll`
    -   これら全ての要素が `esmini` の車両状態に反映されます。

2.  **座標系の補正 (Reference Point Adjustment)**:
    -   OSIの位置は通常「Bounding Boxの中心」ですが、`esmini` は「Reference Point (後車軸中心の路面投影点など)」を使用します。
    -   FMU内部で、車両固有のオフセット (`centerOffset`) と回転角を用いて、OSIの中心座標から `esmini` のReference Pointを逆算・補正して適用します。
    -   式 (簡易版):
        -   `X_ref = X_osi - Rotated(Offset).x`
        -   `Y_ref = Y_osi - Rotated(Offset).y`
        -   `Z_ref = Z_osi - Offset.z`

3.  **Road Snap (自動吸着) の無効化**:
    -   `TrafficUpdate` で位置が更新される際、`esmini` 本来の機能である「路面への自動吸着 (Zスナップ)」は **無効化** されます。
    -   これにより、外部物理シミュレータが計算した正確な3D挙動 (ピッチング、ロール、ジャンプ等) がそのまま再現されます。
    -   **注意**: 外部シミュレータが適切なZ座標と姿勢を提供する必要があります。Z=0などが送られると、車両が地下に埋まる等の表示不正が発生します。


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

## デュアル軌道ロジック (Dual Trajectory Logic)

GT_esminiは、FMU連携強化のために、OSIの`future_trajectory`フィールドを使用したデュアル軌道レポート機能を実装しています。これにより、理想的な経路（ゴースト）と実際の制御計画（自車）の両方を外部システムに送信できます。

1. **ゴーストオブジェクト（理想軌道）**
   - **ソース**: シナリオで定義された自身の `trail_` (PolyLineBase)。
   - **内容**: シナリオ通りの理想的な将来位置。
   - **生成方法**: 現在時刻から一定時間先（例：10秒先）までの点をサンプリングして予測。

2. **自車（Ego）オブジェクト（リカバリ軌道）**
   - **ソース**: 自車位置からゴースト位置への動的スプライン。
   - **内容**: ゴーストの軌道に復帰するための経路。
   - **生成方法**: **3次エルミートスプライン (Cubic Hermite Spline)** を使用して、現在の自車位置・方位と、ゴーストの現在位置・方位を滑らかに接続。

### future_trajectory フィールドの詳細

`osi3::MovingObject` メッセージの `future_trajectory` (repeated `osi3::StatePoint`) に以下の値が格納されます。

| フィールド | 型 | 説明 |
|------------|----|------|
| `timestamp` | `osi3::Timestamp` | 予測点の時刻（シミュレーション時刻 + 先読み時間）。ナノ秒単位まで設定。 |
| `position` | `osi3::Vector3d` | **x, y**: 軌道上の座標<br>**z**: 現在の路面高さ (Ego) または トレイルの高さ (Ghost) |
| `orientation` | `osi3::Orientation3d` | **yaw**: 軌道の接線方向（Heading）<br>**roll, pitch**: 0 (現在は計算簡略化のため0固定) |

**サンプリング仕様:**
- **Ghost**: 0.5秒間隔で20点（10秒先まで）
- **Ego**: 自車からゴーストまでの区間を20点で分割（可変dt）

## 関連ドキュメント

- [Open Simulation Interface](https://github.com/OpenSimulationInterface/open-simulation-interface) - OSI公式リポジトリ
- [アーキテクチャ](08_architecture.md) - GT_OSIReporterの内部構造
