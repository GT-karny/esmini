# GT_esmini Extension Module

GT_esminiは、標準の `esmini` 環境シミュレータに対して、車両制御・挙動モデル・ライト機能・信号制御・Web UIなどを追加する拡張モジュールです。
本モジュールは `esmini` 本体を変更することなく、追加のライブラリとして統合されています。

## 1. 標準esminiとの主な差分 (Key Differences)

標準の `esmini` 機能に加え、以下の点が拡張されています。

- **ManualDrive制御**: SDL2経由でハンドルコントローラー（Logitech G29等）やゲームパッドによるリアルタイム車両操作に対応。フォースフィードバック・ボタンマッピング・ドメイン制御をサポート。
- **車両挙動 (Physics)**: 簡易モデルではなく、ピッチ・ロール姿勢変化やエンジン特性を含む詳細な車両ダイナミクス (`RealVehicle`) を実装。
- **ライト制御 (Lighting)**: OpenSCENARIO v1.2 `LightStateAction` に対応し、さらに自動点灯ロジック（ブレーキ、ウインカー等）を追加。ManualDriveではボタン操作+自動キャンセルにも対応。
- **地形追従 (Terrain)**: 路面の起伏に車両姿勢（ピッチ・ロール）を追従させる機能を追加。
- **信号制御 (Traffic Signal)**: OpenDRIVE連携のTrafficSignalControllerによるフェーズベース信号自動制御。
- **OSI出力 (Reporting)**: **自車入力値** (HostVehicleData) や、補正済みの速度情報をOSI (Open Simulation Interface) ストリームに追加。
- **Web UI / Electron**: ブラウザまたはElectronデスクトップアプリからシミュレーションを管理・実行。REST API・gRPC・WebSocket対応。

---

## 2. 機能詳細 (Detailed Features)

### ManualDriveコントローラー
ハンドルコントローラーやゲームパッドからの入力でシミュレーション車両をリアルタイムに操作します。
- **入力デバイス**: SDL2経由（Logitech G29、ゲームパッド等）
- **フォースフィードバック (FFB)**: バネ・ダンパー・クーロン摩擦モデル
- **ボタンマッピング**: `manual_drive.json` で全ボタンをカスタマイズ（シフト、ウインカー、ヘッドライト、ハザード等）
- **ドメイン制御**: 横方向・縦方向を独立して「手動」/「シナリオ」に割り当て
- **ウインカー自動キャンセル**: ステアリング復帰で自動消灯（FSM搭載）
- **設定**: `config/manual_drive.json` または Web UI の ManualDrive設定パネル

### 詳細車両ダイナミクス (RealVehicle)
標準の簡易的な車両移動モデルを拡張し、より物理的な挙動を再現します。
- **サスペンション**: バネ・ダンパーモデルによる加減速時のピッチングや旋回時のローリングを計算。
- **パワートレイン**: エンジン回転数 (RPM) とトルクカーブ、ギア比に基づいた駆動力計算。
- **設定**: `config/real_vehicle_params.json` で車種ごとの物理パラメータを定義可能。

### 高度ライト制御 (Advanced Lighting)
車両のライト状態を詳細に管理・制御します。
- **OSC v1.2 対応**: 標準では未対応の `LightStateAction` をパースし、シナリオからライトの点灯・点滅を制御可能。
- **AutoLight**: 車両の状態変数を監視し、ライトを自動制御するロジックを搭載。
  - **ブレーキランプ**: 減速度が閾値を超えた際に自動点灯（チャタリング防止機能付き）。
  - **ウインカー**: 車線変更・右左折時に自動点滅。ManualDriveではボタン操作+ステアリング復帰で自動キャンセル。
  - **バックランプ**: リバースギア連動。

### TrafficSignalController
OpenSCENARIOの信号制御機能を拡張します。
- **フェーズベース制御**: 信号フェーズの自動サイクリング
- **OpenDRIVE連携**: コントローラーリファレンスによる信号ID自動解決
- **キーファイル**: `src/scenario/GT_TrafficSignalController.*`

### 地形・路面追従 (Terrain Tracking)
OpenDRIVEの道路ジオメトリをサンプリングし、車両の接地点に応じた姿勢制御を行います。
- 簡略化されたレイキャストモデルで路面の勾配（法線ベクトル）を取得し、車両のピッチ・ロール姿勢に合成します。

### OSIレポート拡張 (Enhanced OSI Reporting)
ADAS/AD開発向けに、OSI出力を強化しています。
- **HostVehicleData**: 自車の操作入力値（ペダル開度やステアリング角）を `SensorView` に含めて送信。
- **速度補正**: 物理演算で得られたより正確な速度ベクトルをOSIメッセージに反映。
- **デュアル軌道**: Ghost（理想軌道）とEgo（制御軌道）を同時出力。
- **ライト状態**: 全ライトタイプの OSI 出力対応。

### Web UI / Electron デスクトップアプリ
ブラウザまたはElectronアプリからシミュレーションを管理・実行するGUIです。
- **技術スタック**: FastAPI (Backend) + React/TypeScript/Vite (Frontend) + Electron (Desktop)
- **主な機能**: プロジェクト管理、シミュレーション実行、ManualDrive設定、ライブOSIモニター、REST API
- **配布**: Electronアプリとしてパッケージ化し、`GT_Sim.exe` で直接起動可能

## 3. 開発凍結中の機能

> **Python系機能は v0.8 で開発凍結しています。** PythonDriverController・Embedded Python・DriverScript関連の新機能追加は予定されていません。既存機能は引き続き利用可能です。

### PythonDriverController
- ビルドに常時含まれる必須機能（`GT_ENABLE_EMBEDDED_PYTHON` オプションは廃止済み）
- `GT_Sim` 起動時に `GT_Sim build: PythonDriverController=ENABLED` が出力される
- Embedded Python 3.12 を同梱

## ビルド・導入

ルートディレクトリの `CLAUDE.md` または `README.md` (esmini本体) のビルド手順を参照してください。本モジュールはルートのビルドプロセスに自動的に含まれます。

## フォルダ構成 (Refactored)

- `include/gt_esmini/core`: 公開C APIと共通抽象 (`IConfigLoader`)
- `include/gt_esmini/scenario`: OpenSCENARIO拡張 (`GT_ScenarioReader`, `TrafficSignalController`)
- `include/gt_esmini/io`: UDP I/O
- `include/gt_esmini/control`: ManualDrive制御 / 物理 / 姿勢更新 (`ControllerManualDrive`, `RealVehicle`, `AutoLightController`)
- `include/gt_esmini/osi`: OSI/HostVehicleData連携
- `src/{core,scenario,io,control,osi}`: 実装本体
- `config/`: 実行時設定 (`real_vehicle_params.json`, `host_vehicle_config.json`, `manual_drive.json`)
- `web/`: Web UI / Electron デスクトップアプリ

## include規約

- 外部利用コード: `#include <gt_esmini/core/GT_esminiLib.hpp>`
- GT_esmini内部: `#include "gt_esmini/..."`
