# DriverScript - RealDriver Python Package

esmini RealDriverController を外部アプリケーションから制御するためのPythonパッケージです。

## インストール

```bash
cd DriverScript
pip install -e .
```

## 依存関係

- Python >= 3.8
- protobuf >= 6.30.2

## パッケージ構成

```
DriverScript/
├── realdriver/          # コアライブラリ
│   ├── client.py        # RealDriverClient (UDP制御クライアント)
│   ├── osi_receiver.py  # OSI GroundTruth 受信ラッパー
│   ├── udp_common.py    # UDP/OSI共通ユーティリティ
│   ├── pid_controller.py # PID制御器
│   ├── rm_lib.py        # esminiRMLib.dll ラッパー
│   └── lkas.py          # LKAS コントローラ
├── osi3/                # OSI Protobufモジュール
├── examples/            # サンプルスクリプト
│   ├── gui_controller.py    # Tkinter GUI コントローラ
│   └── lkas_example.py      # LKAS + RealDriver サンプル
├── docs/                # ドキュメント
│   └── RealDriverAPI_Reference.md
└── bin/                 # esminiRMLib.dll (ビルド時に自動配置)
```

## 使用方法

### 基本的な使用例

```python
from realdriver import RealDriverClient, LightMode, IndicatorMode

# クライアント作成
client = RealDriverClient(ip="127.0.0.1", port=53995)

# 制御ループ
for _ in range(100):
    client.set_controls(throttle=0.5, brake=0.0, steering=0.0)
    client.set_gear(1)  # Drive
    client.set_headlights(LightMode.LOW)
    client.send_update()
    time.sleep(0.02)  # 50Hz

client.close()
```

### GUIコントローラの起動

```bash
python examples/gui_controller.py --ip 127.0.0.1 --port 53995
```

### LKASサンプルの起動

```bash
python examples/lkas_example.py --xodr_path <path/to/map.xodr> --lib_path ./bin/esminiRMLib.dll
```

## 主要クラス

| クラス | 説明 |
|--------|------|
| `RealDriverClient` | UDP経由でesminiを制御するクライアント |
| `OSIReceiverWrapper` | OSI GroundTruthを受信してEgo車両状態を取得 |
| `EsminiRMLib` | esminiRMLib.dll のctypesラッパー |
| `LKASController` | 車線維持支援コントローラ |
| `PIDController` | 汎用PID制御器 |

## ポート設定

| ポート | 方向 | 用途 |
|--------|------|------|
| 53995 | 送信 | RealDriver制御 (HostVehicleData) |
| 48198 | 受信 | OSI出力 (GroundTruth) |

## ドキュメント

詳細なAPIリファレンスは [docs/RealDriverAPI_Reference.md](docs/RealDriverAPI_Reference.md) を参照してください。
