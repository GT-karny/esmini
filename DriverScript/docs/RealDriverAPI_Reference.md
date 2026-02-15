# RealDriver Controller API Reference

`RealDriverClient` は、esminiの `RealDriverController` を搭載した車両をUDP経由で制御するためのPythonライブラリです。

## 概要

- **パッケージ名**: `realdriver`
- **クラス名**: `RealDriverClient`
- **通信方式**: UDP (struct.packによるバイナリパケット送信)
- **パケット構造**: `[LightMask (4 bytes, int32)] + [HostVehicleData (Protobuf Serialized)]`

## クラスとメソッド

### 初期化

```python
from realdriver import RealDriverClient, LightMode, IndicatorMode

client = RealDriverClient(ip="127.0.0.1", port=53995)
```

| 引数 | 型 | デフォルト | 説明 |
| :--- | :--- | :--- | :--- |
| `ip` | `str` | `"127.0.0.1"` | esminiが動作しているホストのIPアドレス |
| `port` | `int` | `53995` | RealDriverController の入力UDPポート (`BasePort`) |

---

### 制御の更新

#### 運転操作の設定

```python
client.set_controls(throttle, brake, steering)
```

| 引数 | 型 | 範囲 | 説明 |
| :--- | :--- | :--- | :--- |
| `throttle` | `float` | `0.0` ～ `1.0` | アクセル開度 (0% - 100%) |
| `brake` | `float` | `0.0` ～ `1.0` | ブレーキ強度 (0% - 100%) |
| `steering` | `float` | `-1.0` ～ `1.0` | ステアリング入力。ラジアン(rad)として扱われます。 |

#### ギアの設定

```python
client.set_gear(gear)
```

| 引数 | 型 | 値 | 説明 |
| :--- | :--- | :--- | :--- |
| `gear` | `int` | `1` | ドライブ (D) - 前進 |
| | | `0` | ニュートラル (N) |
| | | `-1` | リバース (R) - 後退 |

#### エンジンブレーキの設定

```python
client.set_engine_brake(force)
```

| 引数 | 型 | 説明 |
| :--- | :--- | :--- |
| `force` | `float` | エンジンブレーキによる減速度 (m/s²)。(現在はNO-OP実装です。将来的に対応予定) |

### ライトの設定 (高レベルAPI)

#### ヘッドライトの設定

```python
client.set_headlights(mode)
```

| 引数 | 型 | 説明 |
| :--- | :--- | :--- |
| `mode` | `LightMode` | `LightMode.OFF`: 消灯<br>`LightMode.LOW`: ロービーム点灯 (+ナンバー灯ON)<br>`LightMode.HIGH`: ハイビーム点灯 (+ナンバー灯ON) |

#### ウインカーの設定

```python
client.set_indicators(mode)
```

| 引数 | 型 | 説明 |
| :--- | :--- | :--- |
| `mode` | `IndicatorMode` | `IndicatorMode.OFF`: 消灯<br>`IndicatorMode.LEFT`: 左ウインカー<br>`IndicatorMode.RIGHT`: 右ウインカー<br>`IndicatorMode.HAZARD`: ハザード (両側点滅) |

#### フォグランプの設定

```python
client.set_fog_lights(front=None, rear=None)
```

| 引数 | 型 | 説明 |
| :--- | :--- | :--- |
| `front` | `bool` | フロントフォグの点灯状態 (`True`/`False`)。省略(`None`)時は変更なし。 |
| `rear` | `bool` | リアフォグの点灯状態 (`True`/`False`)。省略(`None`)時は変更なし。 |

### ライトの設定 (低レベルAPI)

これらは互換性や微調整のために残されていますが、通常は高レベルAPIの使用を推奨します。

```python
client.set_light_state(light_type, on)
# light_type: 'low', 'high', 'left', 'right', 'hazard', 'fog_front', 'fog_rear'
```

```python
client.set_lights(mask)
# mask: 32-bit integer bitmask
```

---

### ADAS機能の設定

```python
client.set_adas_function(function_name, state)
```

| 引数 | 型 | 説明 |
| :--- | :--- | :--- |
| `function_name` | `str` | ADAS機能名 (例: `'adaptive_cruise_control'`) |
| `state` | `int` | OSI State Enum値 (0=UNKNOWN, 6=ACTIVE 等) |

---

### 送信

```python
client.send_update()
```

現在の設定値（アクセル、ブレーキ、ステアリング、ギア、ライトマスク、ADAS状態）をパケットにまとめ、UDPで送信します。
パケットの先頭4バイトにはライトマスク(Little Endian int32)が付与され、その後にHostVehicleDataのプロトコルバッファデータが続きます。

このメソッドは定期的に（例：50Hz = 20ms毎）呼び出す必要があります。

---

### 終了

```python
client.close()
```

UDPソケットを閉じます。

---

## Appendix: 使用サンプル

以下は、`RealDriverClient` を使用して、Pythonスクリプトから自動的に車両を制御する簡単なサンプルです。

```python
import time
from realdriver import RealDriverClient, LightMode, IndicatorMode

def main():
    # クライアントの作成
    client = RealDriverClient(ip="127.0.0.1", port=53995)
    
    print("Connecting to esmini...")

    try:
        # メインループ (50Hz)
        for i in range(500): # 10秒間実行
            
            # --- ロジック記述 ---
            
            # 最初の2秒間はおとなしく走行
            if i < 100:
                client.set_controls(throttle=0.3, brake=0.0, steering=0.0)
                client.set_gear(1) # Drive
                client.set_headlights(LightMode.LOW) # ヘッドライトON
            
            # 左折準備 (ウインカー)
            elif i < 200:
                client.set_controls(throttle=0.2, brake=0.0, steering=0.0)
                client.set_indicators(IndicatorMode.LEFT) # 左ウインカー
            
            # その後はブレーキで停止
            else:
                client.set_controls(throttle=0.0, brake=0.5, steering=0.0)
                client.set_indicators(IndicatorMode.HAZARD) # ハザード点灯
                
            # --- 送信 ---
            client.send_update()
            
            # 20ms待機
            time.sleep(0.02)
            
    except KeyboardInterrupt:
        print("Stopped by user")
    finally:
        client.close()
        print("Disconnected")

if __name__ == "__main__":
    main()
```

---

## 縦方向プロファイルの受信 (`type=3`)

ControllerRealDriver は単一速度 (`type=1`) ではなく、時系列の縦方向速度プロファイル (`type=3`) を送信します。

### パケット構造 (`type=3`)

| フィールド | サイズ | 型 | 説明 |
| :--- | :--- | :--- | :--- |
| `Type` | 1 byte | `uint8` | パケットタイプ識別子 (値: `3`) |
| `count` | 4 bytes | `uint32` | プロファイル点数 |
| `points` | `count * 32` bytes | struct array | `(t_offset, v_target, a_max, j_max)` を `double` 4つで表現 |

### 設定方法（OpenSCENARIO）

OpenSCENARIOでControllerのプロパティを設定：

```xml
<Controller name="RealDriverController">
    <Properties>
        <Property name="BasePort" value="53995"/>
        <Property name="ClientAddr" value="127.0.0.1"/>
        <Property name="ClientPort" value="54995"/>
    </Properties>
</Controller>
```

### Pythonでの受信例（推奨）

```python
from realdriver.udp_receivers import LongitudinalProfileReceiver

receiver = LongitudinalProfileReceiver(port=54995, host="127.0.0.1")

while True:
    profile = receiver.receive_all()
    if profile:
        # 現在時刻の目標速度
        v_now = profile[0].v_target
        print(f"v_now={v_now:.2f} m/s, points={len(profile)}")
```

### 補間例

```python
from realdriver.protocol.lon_profile import interpolate_speed

v_300ms = interpolate_speed(profile, 0.3)
```

Note:
- `type=1` target speed packet は廃止済みです。
