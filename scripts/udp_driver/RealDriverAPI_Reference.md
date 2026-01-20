# RealDriver Controller API Reference

`RealDriverClient` は、esminiの `RealDriverController` を搭載した車両をUDP経由で制御するためのPythonライブラリです。

## 概要

- **ファイル名**: `RealDriverClient.py`
- **クラス名**: `RealDriverClient`
- **通信方式**: UDP (struct.packによるバイナリパケット送信)

## クラスとメソッド

### 初期化

```python
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
| `steering` | `float` | `-1.0` ～ `1.0` | ステアリング入力。通常、正の値が左旋回、負の値が右旋回に対応します。 |

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
| `force` | `float` | エンジンブレーキによる減速度 (m/s²)。デフォルトは `0.49` (約0.05G) です。<br>大きくするとアクセルOFF時の減速が強くなります。 |

#### ライトの設定 (個別)

```python
client.set_light_state(light_type, on)
```

| 引数 | 型 | 説明 |
| :--- | :--- | :--- |
| `light_type` | `str` | 設定したいライトの種類を指定します。以下の文字列が使用可能です。 |
| | | `'low'` (ロービーム), `'high'` (ハイビーム) |
| | | `'left'` (左ウィンカー), `'right'` (右ウィンカー), `'hazard'` (ハザード) |
| | | `'fog_front'` (前フォグ), `'fog_rear'` (後フォグ) |
| `on` | `bool` | `True` で点灯、`False` で消灯 |

#### ライトの設定 (一括マスク)

```python
client.set_lights(mask)
```

| 引数 | 型 | 説明 |
| :--- | :--- | :--- |
| `mask` | `int` | ライトのビットマスク整数を直接設定します。 |

---

### 送信

```python
client.send_update()
```
現在の設定値（アクセル、ブレーキ、ステアリング、ギア、ライト）をパケットにまとめ、UDPで送信します。このメソッドは定期的に（例：50Hz = 20ms毎）呼び出す必要があります。

---

### 終了

```python
client.close()
```
UDPソケットを閉じます。

---

## Appendix: 使用サンプル

以下は、`RealDriverClient` を使用して、Pythonスクリプトから自動的に車両を制御する（減速して停止する）簡単なサンプルです。

```python
import time
from RealDriverClient import RealDriverClient

def main():
    # クライアントの作成
    client = RealDriverClient(ip="127.0.0.1", port=53995, object_id=0)
    
    print("Connecting to esmini...")

    try:
        # メインループ (50Hz)
        for i in range(500): # 10秒間実行
            
            # --- ロジック記述 ---
            
            # 最初の2秒間はアクセルON
            if i < 100:
                client.set_controls(throttle=0.5, brake=0.0, steering=0.0)
                client.set_gear(1) # Drive
            
            # 次の2秒間は惰性走行
            elif i < 200:
                client.set_controls(throttle=0.0, brake=0.0, steering=0.0)
            
            # その後はブレーキで停止
            else:
                client.set_controls(throttle=0.0, brake=0.5, steering=0.0)
                client.set_light_state('break', True) # ブレーキランプ (自動でも点灯しますが手動設定も可)
                
            # ライトのテスト (左ウィンカー点滅)
            if i % 20 < 10:
                client.set_light_state('left', True)
            else:
                client.set_light_state('left', False)

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
