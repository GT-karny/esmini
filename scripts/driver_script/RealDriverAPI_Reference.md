# RealDriver Controller API Reference

`RealDriverClient` は、esminiの `RealDriverController` を搭載した車両をUDP経由で制御するためのPythonライブラリです。

## 概要

- **ファイル名**: `RealDriverClient.py`
- **クラス名**: `RealDriverClient`
- **通信方式**: UDP (struct.packによるバイナリパケット送信)
- **パケット構造**: `[LightMask (4 bytes, int32)] + [HostVehicleData (Protobuf Serialized)]`

## クラスとメソッド

### 初期化

```python
from RealDriverClient import RealDriverClient, LightMode, IndicatorMode

client = RealDriverClient(ip="127.0.0.1", port=53995, object_id=0)
```

| 引数 | 型 | デフォルト | 説明 |
| :--- | :--- | :--- | :--- |
| `ip` | `str` | `"127.0.0.1"` | esminiが動作しているホストのIPアドレス |
| `port` | `int` | `53995` | ベースポート番号。実際の送信ポートは `port + object_id` になります。 |
| `object_id` | `int` | `0` | 制御対象の車両ID (OSI/OpenSCENARIO上のID) |

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
from RealDriverClient import RealDriverClient, LightMode, IndicatorMode

def main():
    # クライアントの作成
    client = RealDriverClient(ip="127.0.0.1", port=53995, object_id=0)
    
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
