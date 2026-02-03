# RealDriver サンプルコード ドキュメント

このドキュメントでは、`DriverScript/examples` フォルダ内のサンプルコードについて解説します。
これらのサンプルは、`RealDriver` モジュールを使用して esmini 上の車両を制御する方法を示しています。

## 1. サンプル一覧

| スクリプト名 | 概要 |
| :--- | :--- |
| `scenario_drive_example.py` | ウェイポイント追従と速度制御を行う、最も一般的なシナリオ走行のサンプルです。 |
| `lane_change_example.py` | 安全確認を行いながら車線変更を行うイベント駆動型制御のサンプルです。 |
| `modular_control_example.py` | 横方向（ステアリング）と縦方向（速度）の制御を独立して組み合わせる方法を示します。 |
| `lkas_example.py` | RoadManagerを使用した車線維持支援 (LKAS) のサンプルです。 |
| `gui_controller.py` | Tkinterを使用したGUIで、esmini上の車両を手動操作するクライアントです。 |
| `debug_route_plot.py` | esminiRMLibを使用して、計算された経路や道路形状を可視化するデバッグツールです。 |

---

## 2. 詳細と使用方法

各スクリプトの使用方法と主要な引数について説明します。

### `scenario_drive_example.py`

統合された `ScenarioDriveController` を使用して、指定されたウェイポイントまたはターゲット地点に向かって走行します。

**使用方法:**
```bash
python scenario_drive_example.py --xodr_path <path_to_xodr> [options]
```

**主な引数:**
*   `--xodr_path`: (必須) OpenDRIVE (.xodr) ファイルへのパス。
*   `--mode`: 動作モードを指定します。
    *   `waypoints`: (デフォルト) スクリプト内でハードコードされたサンプルウェイポイントを追従します。
    *   `target`: `--target_x`, `--target_y` で指定された座標への経路を自動計算して追従します。
    *   `udp`: esmini本体からのUDPパケットによるウェイポイント指示を待ち受けます。
*   `--target_speed`: 目標巡航速度 [m/s]。
*   `--id`: 制御対象の車両ID (デフォルト: 0)。

---

### `lane_change_example.py`

`LaneChangeController` を使用して、状況に応じた車線変更のデモンストレーションを行います。デモモードでは一定時間経過後に自動的に左車線への変更を試みます。

**使用方法:**
```bash
python lane_change_example.py --xodr_path <path_to_xodr> [options]
```

**機能:**
*   前後の車両とのギャップやTTC (Time-To-Collision) に基づく安全性チェック。
*   安全と判断された場合の車線変更トリガー。
*   待機中 (IDLE) は通常の速度制御とステアリング維持を実行。

**主な引数:**
*   `--xodr_path`: (必須) OpenDRIVE (.xodr) ファイルへのパス。RoadManagerでの車線認識に使用されます。
*   `--demo`: 実機接続なしでAPIの動作確認を行うデモモードを実行します。

---

### `modular_control_example.py`

`LateralController` (横方向) と `LongitudinalController` (縦方向) を個別に初期化し、組み合わせて使用する例です。
OSI GroundTruth を直接各コントローラに渡すことで、複雑な状態抽出コードを書かずに制御ループを実装できます。

**使用方法:**
```bash
python modular_control_example.py --xodr_path <path_to_xodr> [options]
```

**特徴:**
*   ステアリング制御のみ、または速度制御のみを個別にテスト可能。
*   外部からのUDP入力（速度、ウェイポイント）を個別に受け付ける実装例を含みます。

---

### `lkas_example.py`

`LKASController` を使用した車線維持アシストのサンプルです。
RoadManagerから現在の車線に対するオフセットと相対方位を取得し、レーンセンターを維持するようにステアリングを制御します。

**使用方法:**
```bash
python lkas_example.py --xodr_path <path_to_xodr> [options]
```

---

### `gui_controller.py`

TkinterベースのGUIウィンドウを立ち上げ、スライダーやボタンで車両を直接操作します。
esminiが `RealDriverController` モードで動作している場合に有効です。

**使用方法:**
```bash
python gui_controller.py [options]
```

**機能:**
*   スロットル、ブレーキ、ステアリングのスライダー操作。
*   ギアチェンジ (D, N, R)。
*   各種ライト（ヘッドライト、ウィンカー）の操作。
*   OSI準拠のADAS機能状態の切り替え。

---

### `debug_route_plot.py`

制御スクリプトではありませんが、経路計画や道路形状のデバッグに役立つツールです。
指定されたOpenDRIVEマップを読み込み、RoadManagerを使って道路のリファレンスラインやレーン境界をサンプリングし、Matplotlibでプロットします。

**使用方法:**
```bash
python debug_route_plot.py --xodr_path <path_to_xodr>
```

**出力:**
*   `route_debug_plot_zoom.png`: 生成されたプロット画像。
*   `dense_waypoints_debug.csv`: 計算された詳細ウェイポイントのCSVデータ。
