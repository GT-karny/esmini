# logidrivepy モジュール

`logidrivepy` は、Logitech製ステアリングコントローラー（G29, G923など）をPythonから制御するためのライブラリです。Logitech Gaming Steering Wheel SDKのラッパーとして機能します。

## インストール

`requirements.txt` に含まれているため、`pip install -e .` でインストールされます。

```bash
pip install logidrivepy
```

## 基本的な使い方

`logidrivepy` を使用するには、`LogitechController` クラスを使用するのが便利です。

### 1. 初期化

スクリプトの開始時にコントローラーを初期化する必要があります。

```python
from logidrivepy import LogitechController

controller = LogitechController()

# ステアリングの初期化 (ウィンドウハンドルが必要な場合があるため、GUIアプリ推奨ですが、Consoleでも動作する場合があります)
# ignore_xinput_controllers=True にするとXInputデバイス（ゲームパッド等）を無視します
if controller.steering_initialize(ignore_xinput_controllers=True):
    print("Logitech SDK Initialized")
else:
    print("Failed to initialize Logitech SDK")
```

### 2. メインループ

コントローラーの状態を更新するために、毎フレーム `logi_update()` を呼び出す必要があります。

```python
import time

try:
    while True:
        # SDKの状態更新
        if not controller.logi_update():
            print("Logi update failed (disconnected?)")
            break
            
        # ここで入力取得やフォースフィードバックの処理を行う
        
        time.sleep(1/60) # 60Hz程度でループ
except KeyboardInterrupt:
    pass
finally:
    controller.steering_shutdown()
```

### 3. 入力の取得

`get_state_engines(index)` を使用してコントローラーの状態を取得します。

```python
# index 0 は1つ目のコントローラー
state = controller.get_state_engines(0)

if controller.is_connected(0):
    # ステアリング: -32768 (左) ～ 32767 (右)
    steering_val = state.contents.lX
    
    # アクセル: -32768 (踏んでない) ～ 32767 (最大) ※設定による
    throttle_val = state.contents.lY
    
    # ブレーキ
    brake_val = state.contents.lRz
    
    # クラッチ
    clutch_val = state.contents.rglSlider[0]
    
    print(f"Steer: {steering_val}, Throttle: {throttle_val}, Brake: {brake_val}")

    # ボタン入力チェック (ボタン0が押されているか)
    if controller.button_triggered(0, 0):
        print("Button 0 pressed")
```

### 4. フォースフィードバック

フォースフィードバックを適用する関数も用意されています。

```python
# スプリングフォースの適用
# index, offsetPercentage, saturationPercentage, coefficientPercentage
controller.LogiPlaySpringForce(0, 0, 50, 50)

# スプリングフォースの停止
controller.LogiStopSpringForce(0)
```

## 主な関数リファレンス

| 関数名 | 説明 |
| --- | --- |
| `steering_initialize(ignore_xinput_controllers)` | SDKを初期化します。 |
| `logi_update()` | コントローラーの状態を更新します。毎フレーム呼び出す必要があります。 |
| `steering_shutdown()` | SDKを終了しリソースを解放します。 |
| `get_state_engines(index)` | 指定インデックスのコントローラーの状態構造体を取得します。 |
| `is_connected(index)` |指定インデックスにコントローラーが接続されているか確認します。 |
| `button_triggered(index, button_number)` | 指定ボタンが押されたか確認します。 |
| `button_released(index, button_number)` | 指定ボタンが離されたか確認します。 |
| `button_is_pressed(index, button_number)` | 指定ボタンが押され続けているか確認します。 |
| `LogiPlaySpringForce(index, ...)` | スプリングフォース効果を再生します。 |
| `LogiStopSpringForce(index)` | スプリングフォース効果を停止します。 |

## 定数

`logidrivepy` には多くの定数が定義されています。

- `LOGI_MAX_CONTROLLERS = 4`
- `LOGI_FORCE_SPRING = 0`
- `LOGI_FORCE_DAMPER = 2`
- 等

詳細は `logidrivepy.constants` を参照してください。
