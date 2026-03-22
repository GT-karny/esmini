# GT_esmini 概要

## GT_esminiとは

GT_esmini (GroundTruth esmini) は、[esmini](https://github.com/esmini/esmini) (Environment Simulator Minimalistic) を拡張し、車両制御・挙動モデル・ライト機能・信号制御・外部連携・Web UI を追加する拡張モジュールです。esminiは、OpenSCENARIOシナリオを実行するための軽量なシミュレーターですが、標準では詳細な車両ダイナミクスやライト制御、ハンドルコントローラー入力などをサポートしていません。GT_esminiは、これらの機能を追加することで、より現実的な車両シミュレーションとインタラクティブな操作環境を提供します。

## 主な機能

### 1. ManualDriveコントローラー

ハンドルコントローラーやゲームパッドなどの入力デバイスを使って、シミュレーション内の車両をリアルタイムに操作するためのコントローラーです。

- **入力デバイス対応**: SDL2経由でステアリングホイール（Logitech G29等）、ゲームパッドに対応
- **フォースフィードバック (FFB)**: バネ・ダンパー・クーロン摩擦モデルによるリアルなステアリング反力
- **ボタンマッピング**: `manual_drive.json` で全ボタンをカスタマイズ可能（シフトアップ/ダウン、ウインカー、ヘッドライト、ハザード等）
- **ドメイン制御**: 横方向・縦方向を独立して「手動」/「シナリオ」に割り当て可能（例: ステアリングは手動、速度はシナリオ制御）
- **ウインカー自動キャンセル**: ステアリング復帰時に自動消灯するFSM（ステートマシン）搭載
- **HostVehicleData (HVD) 出力**: 操作入力値・パワートレイン情報をOSI経由で外部に出力

### 2. 詳細車両ダイナミクス (RealVehicle)

標準の簡易的な車両移動モデルを拡張し、より物理的な挙動を再現します。

- **サスペンション**: バネ・ダンパーモデルによる加減速時のピッチング、旋回時のローリング
- **パワートレイン**: エンジン回転数 (RPM) とトルクカーブ、ギア比に基づいた駆動力計算
- **地形追従**: OpenDRIVEの路面勾配・起伏に車両姿勢（ピッチ・ロール）を追従
- **設定**: `real_vehicle_params.json` で車種ごとの物理パラメータを定義可能

### 3. 高度ライト制御 (Advanced Lighting)

車両のライト状態を詳細に管理・制御します。

- **OSC v1.2 LightStateAction対応**: シナリオからライトの点灯・点滅を制御可能
- **AutoLight**: 車両状態に応じた自動制御
  - **ブレーキランプ**: 減速度が閾値を超えた際に自動点灯（チャタリング防止付き）
  - **ウインカー**: 車線変更・右左折時に自動点滅、ManualDriveではボタン操作+自動キャンセル
  - **バックランプ**: リバースギア連動
- **ManualDrive統合**: ヘッドライト・ハイビーム・フォグライト・ハザードのトグル操作

### 4. TrafficSignalController

OpenSCENARIOの信号制御を拡張し、OpenDRIVEと連携した信号機の自動制御を実現します。

- **フェーズベース制御**: 信号フェーズの自動サイクリング
- **OpenDRIVE連携**: コントローラーリファレンスによる信号ID自動解決
- **アクション/条件ベース遷移**: 柔軟なフェーズ切り替え

### 5. PythonDriverController

> **開発凍結中**: Python系機能（PythonDriverController・Embedded Python含む）は v0.8 で開発凍結しています。既存機能は引き続き利用可能ですが、新機能追加は予定されていません。

Python スクリプトによるカスタム車両制御。UDP経由でC++エンジンと通信し、シナリオに応じた自動運転ロジックを実装できます。

- ビルドに常時含まれる必須機能（`GT_ENABLE_EMBEDDED_PYTHON` オプションは廃止済み）
- Embedded Python 3.12 を同梱し、外部Python環境不要で動作

### 6. OSIレポート拡張

ADAS/AD開発向けに、OSI (Open Simulation Interface) 出力を強化しています。

- **HostVehicleData**: 自車の操作入力値（ペダル開度・ステアリング角）を `SensorView` に含めて送信
- **速度補正**: 物理演算で得られた正確な速度ベクトルをOSIメッセージに反映
- **デュアル軌道出力**: Ghost（理想軌道）とEgo（制御軌道）を同時出力
- **ライト状態**: 全ライトタイプのOSI出力対応

### 7. Web UI / Electron デスクトップアプリ

ブラウザまたはデスクトップアプリからシミュレーションを実行・管理するGUIです。

- **Electronデスクトップシェル**: カスタムタイトルバー付きのネイティブアプリとして動作
- **プロジェクト管理**: シナリオ・道路・スクリプトの一括管理
- **シミュレーション実行**: GUI からワンクリックでシミュレーション起動
- **ManualDrive設定パネル**: ボタンマッピング・FFBチューニング・ドメイン制御をGUIで設定
- **ライブモニター**: 実行中のOSIデータをリアルタイム表示
- **REST API**: 外部システム（CI/CD等）からの自動実行に対応
- **gRPC / WebSocket**: OSIデータのストリーミング

詳細は [Web UI マニュアル](../web/manual.md) を参照してください。

## esmini本体との関係

GT_esminiは、esminiの**拡張モジュール**として設計されています。

### 非侵襲的な設計

- **ファイルコピーゼロ**: esmini本体のファイルは一切コピーしない
- **継承パターン**: `ScenarioReader`等を継承して機能を拡張
- **コンポジションパターン**: `Vehicle`クラスを継承せず、`VehicleLightExtension`で拡張
- **独立したライブラリ**: GT_esminiは独立したライブラリとしてビルド

### esminiアップデート時の影響

esmini本体がアップデートされても、GT_esminiへの影響は最小限です：
- esmini本体のファイルをコピーしていないため、マージ作業が不要
- publicインターフェースは安定しているため、互換性が保たれやすい
- `ScenarioReader`のインターフェース変更時のみ、`GT_ScenarioReader`の調整が必要

### 使用方法の選択肢

**オプション1: GT_Init を使用（推奨）**
```cpp
#include <gt_esmini/core/GT_esminiLib.hpp>

GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

while (running)
{
    GT_Step(dt);
}

GT_Close();
```

**オプション2: SE_Init を使用（既存機能のみ）**
```cpp
#include "esminiLib.hpp"
#include <gt_esmini/core/GT_esminiLib.hpp>

SE_Init("scenario.xosc", 0);
GT_EnableAutoLight();

while (running)
{
    SE_Step();
    GT_Step(dt);
}

SE_Close();
GT_Close();
```

> **Note**: `LightStateAction`のパース機能を使用するには `GT_Init` が必要です。

## OSI (Open Simulation Interface) 連携

GT_esminiは、OSI v3.5.0に対応しており、以下の情報をOSI出力に含めます：

- ブレーキランプ・ウインカー・バックライト等のライト状態
- HostVehicleData（操作入力値、パワートレイン情報、ADAS状態）
- デュアル軌道（Ghost + Ego）

詳細は [OSI連携](../integration/osi_integration.md) を参照してください。

## 開発ステータス

| フェーズ | 内容 | 状態 |
|:---|:---|:---|
| Phase 1 | GT_esmini構造準備（ディレクトリ構成、ビルドシステム） | 完了 |
| Phase 2 | LightStateAction基本実装 | 完了 |
| Phase 3 | AutoLight機能実装 | 完了 |
| Phase 4 | OSI連携 | 完了 |
| Phase 5 | 外部制御（RealDriver → ManualDrive） | 完了 |
| Phase 6 | Web UI / Electronデスクトップアプリ | 完了 |
| Phase 7 | TrafficSignalController | 完了 |
| Python系 | PythonDriverController / Embedded Python | **開発凍結** |

## ユースケース

### 1. ADAS/AD開発
- 先行車両のブレーキランプ検出テスト
- ウインカー認識アルゴリズムの検証
- 夜間走行シミュレーション（ヘッドライト制御）

### 2. ドライビングシミュレーター
- ハンドルコントローラーによるリアルタイム車両操作
- フォースフィードバックによるリアルなステアリング感
- ドメイン制御による部分的な自動運転体験

### 3. 交通シミュレーション
- 信号制御シナリオの自動実行
- リアルな車両挙動の再現
- 交差点での右左折時のウインカー動作

### 4. HMI開発
- Web UIからのシミュレーション管理
- ライブOSIデータ表示
- コントローラー設定のGUI化

### 5. V2X通信シミュレーション
- ライト状態の車車間通信
- OSI経由での情報共有

## ライセンス

GT_esminiは、Mozilla Public License 2.0の下でライセンスされています。
詳細は [LICENSE](../../../LICENSE) ファイルを参照してください。

## 次のステップ

- [ビルド・インストール](build_install.md) - GT_esminiをビルドする
- [基本的な使い方](basic_usage.md) - 最初のプログラムを作成する
- [サンプルシナリオ](examples.md) - 実例から学ぶ

## 関連リンク

- [esmini公式サイト](https://esmini.github.io/)
- [OpenSCENARIO v1.2仕様](https://www.asam.net/standards/detail/openscenario/)
- [Open Simulation Interface (OSI)](https://github.com/OpenSimulationInterface/open-simulation-interface)
