# GT_esmini Extension Module

GT_esminiは、標準の `esmini` 環境シミュレータに対して、車両制御・挙動モデル・ライト機能・信号制御・Web UIなどを追加する拡張モジュールです。
本モジュールは `esmini` 本体を変更することなく、追加のライブラリとして統合されています。

## 1. 標準esminiとの主な差分 (Key Differences)

標準の `esmini` 機能に加え、以下の点が拡張されています。

- **コントローラファミリ (Controller Family)**: 標準の `DefaultController` に加え、4種の独自コントローラを実装。`ControllerVirtualDriver`（法規判断つき自動運転・現行開発の主軸）、`ControllerManualDrive`（ハンドル/ゲームパッド手動運転）、`ControllerRouteDrive`（経路追従+車線変更）、`ControllerKinematic`（軌道曲率からステア生成）。詳細は下記「コントローラファミリ」参照。
- **VirtualDriver（自動運転）**: フル車両物理を人間並みのドライバーロジックで自動運転させる 4層プランナースタック。trajectory（短期）→ 中長期速度計画 → 法規ポリシー（先行車追従・信号停止・一時停止/譲れ標識・対向車ギャップ受容・横断歩道・無信号交差点優先）を Phase 0〜3e まで実装済み。
- **ManualDrive制御**: SDL2経由でハンドルコントローラー（Logitech G29等）やゲームパッドによるリアルタイム車両操作に対応。フォースフィードバック・ボタンマッピング・ドメイン制御をサポート。
- **車両挙動 (Physics)**: 簡易モデルではなく、ピッチ・ロール姿勢変化やエンジン特性を含む詳細な車両ダイナミクス (`RealVehicle`) を実装。
- **ライト制御 (Lighting)**: OpenSCENARIO v1.2 `LightStateAction` に対応し、さらに車両状態3系統の自動点灯（ブレーキ灯・ウインカー・バックランプ）と、F6 環境駆動ヘッドライト（夜間/トンネル/自動ハイビーム）を追加。ManualDriveではボタン操作+自動キャンセルにも対応。
- **地形追従 (Terrain)**: 路面の起伏に車両姿勢（ピッチ・ロール）を追従させる機能の**足場（凍結スタブ）**を用意。※現時点では**未実装**（下記「地形・路面追従」参照）。
- **信号制御 (Traffic Signal)**: OpenDRIVE連携のTrafficSignalControllerによるフェーズベース信号自動制御。
- **OSI出力 (Reporting)**: **自車入力値** (HostVehicleData) や、車両ライト状態を OSI (Open Simulation Interface) ストリームに追加。Ghost/Ego デュアル軌跡出力に対応。
- **検証工場 (Verification Factory)**: VirtualDriver の挙動を in-process（`GT_esminiLib.dll`）でバッチ実行・注釈・回帰照合する検証基盤。`gt_sim_test.py batch`、per-scenario 回帰ゲート、OpenDRIVE 1.6–1.9 適合ハーネス、シナリオ/道路カタログ生成を含む。詳細は下記「検証工場」参照。
- **Web UI / Electron**: ブラウザまたはElectronデスクトップアプリからシミュレーションを管理・実行。REST API・gRPC・WebSocket対応。
- **FMU / 配布パッケージ**: `GT_esmini` を OSMP FMU (`esmini.fmu`) としてエクスポート可能（Protocol B）。また `GT_Sim_v<VERSION>.zip` として EXE 配布パッケージをビルド可能。

---

## 2. 機能詳細 (Detailed Features)

### コントローラファミリ (Controller Family)
標準の `DefaultController`（運動学のみ）に対し、GT_esmini は用途別に4種のコントローラを提供します。

| Controller | 走行ロジック | 車両モデル | 用途 |
|:---|:---|:---|:---|
| `ControllerVirtualDriver` | 自動生成ペダル+ステアで Default 等価挙動 + 法規判断 | RealVehicle 物理 | 法規判断つき自動運転（開発の主軸） |
| `ControllerManualDrive` | 人間入力（SDL2/ネットワーク） | RealVehicle 物理 | ハンドル/ゲームパッド運転（DiL） |
| `ControllerRouteDrive` | 経路追従 + 車線変更（横方向強化） | 既定に委譲 | ルート走行 |
| `ControllerKinematic` | 軌道曲率からステア生成 | 軽量 `vehicle::Vehicle` | 軽量な軌道追従 |

> 凍結: `ControllerPythonDriver` / `ControllerRealDriver` は互換維持のため残置（下記「開発凍結中の機能」参照）。

### ControllerVirtualDriver（自動運転スタック）
「フル車両物理を、人間並みのドライバーロジックで自動運転させる」ことを目標とした、プラガブルな4層プランナー + 横断1層のスタックです（設計は [docs/virtualdriver/roadmap.md](docs/virtualdriver/roadmap.md)）。上位層の制約が下層へ流れ込みます。

- **短期プランナー (`TrajectoryShortPlanner`)**: `(x, y, v, t)` プレビュー軌道を生成。
- **中長期プランナー (`ManeuverAwareSpeedPlanner`)**: 右左折・カーブ・停止要求を先読みして `v_target(s)` カーブを生成。
- **法規ポリシー層 (`ITrafficPolicy`)**: 状況に応じた stop/yield 制約を出力。実装済みポリシー:
  - `LeadVehicleAware` — IDM ベースの先行車追従（Phase 3a）
  - `TrafficLightAware` — 自レーン前方の信号 phase を取得し黄信号判断つきで停止（Phase 3b）
  - `StopYieldSignAware` — STOP/YIELD 標識で停止・必要時譲り（Phase 3c）
  - `ConflictPointResolver` — フットプリント・コリドー空間時間占有による対向車ギャップ受容（Phase 3d）+ OpenDRIVE `<priority>` を用いた無信号交差点優先（Phase 3e）
  - `CrosswalkPedestrianAware` — 横断歩道の歩行者待ち（`RouteCrosswalkScan`）
- **逆制御ドライバモデル (`PIDPurePursuitDriver`)**: 軌道から throttle/brake/steer を逆算（Stanley/MPC へ差替え可能な設計）。
- **横断層**: `AutoIndicatorPolicy`（Auto/Manual ウインカー切替）、OverrideManager（横/縦独立の手動オーバーライド）。
- **テレメトリ**: 各層の判定を集約し `GT_GetVirtualDriverTelemetry()` C-API / `telemetry.jsonl`（per-frame ego + preview + policy constraints）で公開。検証工場が消費します。
- **設定**: `config/virtual_driver.json`

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
- **AutoLight（車両状態3系統）**: 車両の状態変数を監視し、ライトを自動制御するロジックを搭載。
  - **ブレーキランプ**: 減速度が閾値を超えた際に自動点灯（チャタリング防止機能付き）。
  - **ウインカー**: 車線変更・右左折時に自動点滅。ManualDriveではボタン操作+ステアリング復帰で自動キャンセル。
  - **バックランプ**: リバースギア連動。
- **F6 環境駆動ヘッドライト**: 環境条件に応じてヘッドライトを自動制御（既定 OFF、`config/auto_light.json` または CLI `--autolight-headlights` で有効化）。
  - **夜間ロービーム**: 照度（illuminance）> 太陽高度 > 時刻の優先順で夜間判定して点灯。
  - **トンネル**: OpenDRIVE `<tunnel>` 区間内で点灯。
  - **自動ハイビーム**: ロービーム点灯中かつ前方に車両が居ないとき点灯（ヒステリシス付き）。
  - **設定**: `config/auto_light.json`。詳細は [docs/features/auto_light.md](docs/features/auto_light.md)。

### TrafficSignalController
OpenSCENARIOの信号制御機能を拡張します。
- **フェーズベース制御**: 信号フェーズの自動サイクリング
- **OpenDRIVE連携**: コントローラーリファレンスによる信号ID自動解決
- **キーファイル**: `src/scenario/GT_TrafficSignalController.*`

### 地形・路面追従 (Terrain Tracking) ※未実装（凍結スタブ）
> **状態: 未実装。** 姿勢合成の足場のみ存在します（`RealVehicle::SetTerrainAttitude(pitch, roll)` と
> `terrain_pitch_ / terrain_roll_` 成分）。現状すべての呼び出し側（`ControllerRealDriver` /
> `ControllerPythonDriver`）が `terrain_pitch = terrain_roll = 0` を渡すため、地形成分は常にゼロで**動作しません**。
> 有効化する手段（道路法線サンプリング → 姿勢供給）は未実装で、着手予定はありません（監査 CTL-4）。

将来の設計意図（未実装）:
- OpenDRIVEの道路ジオメトリをサンプリングし、車両の接地点に応じた姿勢を合成する。
- 簡略化されたレイキャストモデルで路面の勾配（法線ベクトル）を取得し、車両のピッチ・ロール姿勢に合成する。

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

### 検証工場 (Verification Factory)
VirtualDriver の挙動を機械的に検証する基盤です。コントローラを xosc に埋め込んだシナリオを `GT_esminiLib.dll` 経由で in-process 実行し、テレメトリを回帰照合します。

- **バッチ実行**: `GT_esmini/scripts/verification/gt_sim_test.py batch <manifest.yaml>` で複数シナリオを一括実行。Phase 別のマニフェスト（`resources/xosc/verification/phase3_batch.yaml` ほか `phase3d_batch.yaml` / `phase3d_crosswalk_batch.yaml` / `phase3e_batch.yaml`）。
- **per-scenario 回帰ゲート**: `scripts/check_phase3_regression.py` がバッチ結果を committed ベースライン `GT_esmini/test/regression_baseline/phase3_expected.yaml` と per-scenario / per-matcher で照合。回帰ゲート Step 2 と CI（Windows・非ブロッキング）で実行。
- **注釈 UI**: バッチ実行結果は Web バックエンドの注釈レジストリに自動登録され、人間が verdict ラベル（`natural` / `too_aggressive` 等）を付与可能。類似度による自動判定は構想段階（[docs/virtualdriver/F4_annotation_similarity_design.md](docs/virtualdriver/F4_annotation_similarity_design.md)、未実装）。
- **OpenDRIVE 適合ハーネス**: `scripts/run_odr_conformance.py` が ODR 1.6–1.9 のスキーマ + esminiRMLib RM + OSI レイヤを検証（`--profile quick` は回帰ゲート Step 1.5）。
- **シナリオ/道路カタログ**: `resources/scenario_authoring/validate_catalog.py` が生成道路/シナリオを esmini headless + gt_sim_test で検証（詳細は [resources/scenario_authoring/README.md](../resources/scenario_authoring/README.md)）。

ゲートの実行順・解釈はルート `CLAUDE.md` §5 および `/gates` スキルを参照してください。

### FMU エクスポート・配布パッケージ
- **FMU (Protocol B)**: `GT_esmini` を OSMP FMU としてエクスポート。ルートから `cmake --build build --config Release --target esmini_fmu` でビルドし、`build/GT_OSMP_FMU/esmini.fmu` を生成（`esmini.cpp` のみをコンパイルし `GT_esminiLib_static` をリンク）。インターフェース仕様は [../GT_OSMP_FMU/FMU_Interface_Specification.md](../GT_OSMP_FMU/FMU_Interface_Specification.md)。
- **EXE 配布**: フロントエンドビルド → PyInstaller → ZIP を経て `dist/GT_Sim_v<VERSION>.zip` を生成。`GT_Sim.bat` から起動し `http://127.0.0.1:8000`。詳細は [docs/reference/distribution_guide.md](docs/reference/distribution_guide.md)。

## 3. 開発凍結中の機能

> **Python系機能は v0.8 で開発凍結しています。** PythonDriverController・Embedded Python・DriverScript関連の新機能追加は予定されていません。既存機能は引き続き利用可能です。

### PythonDriverController
- **オプトイン**: `GT_ENABLE_EMBEDDED_PYTHON` は既定 **OFF**（audit SUB-1）。開発ビルドでは PythonDriverController は除外され、Python3 開発ヘッダも不要。
- 配布パッケージは `-DGT_ENABLE_EMBEDDED_PYTHON=ON`（`scripts/build_package.ps1`）でビルドし、Embedded Python 3.12 を同梱。
- 有効ビルドでは `GT_Sim` 起動時に `GT_Sim build: PythonDriverController=ENABLED` が出力される。

## ビルド・導入

ルートディレクトリの `CLAUDE.md` または `README.md` (esmini本体) のビルド手順を参照してください。本モジュールはルートのビルドプロセスに自動的に含まれます。

## フォルダ構成 (Refactored)

- `include/gt_esmini/core`: 公開C APIと共通抽象 (`IConfigLoader`)
- `include/gt_esmini/scenario`: OpenSCENARIO拡張 (`GT_ScenarioReader`, `TrafficSignalController`)
- `include/gt_esmini/io`: UDP I/O
- `include/gt_esmini/control`: 各種コントローラ / 物理 / 姿勢更新 (`ControllerVirtualDriver`, `ControllerManualDrive`, `ControllerRouteDrive`, `ControllerKinematic`, `RealVehicle`, `AutoLightController`)。VirtualDriver のプランナー/ポリシー実装は `src/control/virtualdriver/`（`policies/` 含む）。
- `include/gt_esmini/osi`: OSI/HostVehicleData連携
- `src/{core,scenario,io,control,osi}`: 実装本体
- `config/`: 実行時設定 (`real_vehicle_params.json`, `host_vehicle_config.json`, `manual_drive.json`, `virtual_driver.json`, `route_drive_controller.json`, `kinematic_controller.json`, `auto_light.json`)
- `web/`: Web UI / Electron デスクトップアプリ

## include規約

- 外部利用コード: `#include <gt_esmini/core/GT_esminiLib.hpp>`
- GT_esmini内部: `#include "gt_esmini/..."`
