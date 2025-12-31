# GT_esmini 概要

## GT_esminiとは

GT_esmini (Grand Touring esmini) は、[esmini](https://github.com/esmini/esmini) (Environment Simulator Minimalistic) にライト機能を追加する拡張モジュールです。esminiは、OpenSCENARIOシナリオを実行するための軽量なシミュレーターですが、標準ではライト制御機能をサポートしていません。GT_esminiは、この機能を追加することで、より現実的な車両シミュレーションを可能にします。

## 主な機能

GT_esminiは、以下の2つの主要機能を提供します：

### 1. LightStateAction対応

OpenSCENARIO v1.2で定義されている`LightStateAction`をサポートします。これにより、シナリオファイル（XOSC）内でライトの状態を明示的に制御できます。

**サポートされているライトタイプ:**
- デイタイムランニングライト (Daytime Running Lights)
- ロービーム / ハイビーム (Low Beam / High Beam)
- フォグライト (Fog Lights)
- ブレーキランプ (Brake Lights)
- ウインカー (Indicator Left/Right)
- バックライト (Reversing Lights)
- ハザードランプ (Warning Lights)
- その他（ライセンスプレート照明、特殊用途ライト）

**制御可能な属性:**
- モード: OFF / ON / FLASHING
- 光度 (Luminous Intensity)
- 点滅パターン (Flashing On/Off Duration)
- 色 (Color RGB)

### 2. AutoLight機能

車両の動作に応じて自動的にライトを点灯・消灯する機能です。シナリオファイルに明示的な記述がなくても、リアルな車両挙動を再現できます。

**自動制御されるライト:**

| ライトタイプ | 点灯条件 | しきい値 |
|------------|---------|---------|
| ブレーキランプ | 減速時 | -0.1G (-0.98 m/s²) 以下 |
| ウインカー（左/右） | 車線変更時、交差点での右左折時 | レーンID変化、ジャンクション内の進行方向 |
| バックライト | 後退時 | 速度が負 |

**優先順位:**
- `LightStateAction`が実行された場合、AutoLightの状態を上書きします
- AutoLightで点灯していたライトも、`LightStateAction`で消灯可能
- `LightStateAction`で設定された状態は、次のAutoLight更新まで保持されます

## esmini本体との関係

GT_esminiは、esminiの**拡張モジュール**として設計されています。以下の特徴があります：

### 非侵襲的な設計

- **ファイルコピーゼロ**: esmini本体のファイルは一切コピーしません
- **継承パターン**: `ScenarioReader`等を継承して機能を拡張
- **独立したライブラリ**: GT_esminiは独立したライブラリとしてビルド
- **最小限の変更**: esmini本体への変更はビルドシステムのみ

### esminiアップデート時の影響

esmini本体がアップデートされても、GT_esminiへの影響は最小限です：
- esmini本体のファイルをコピーしていないため、マージ作業が不要
- publicインターフェースは安定しているため、互換性が保たれやすい
- `ScenarioReader`のインターフェース変更時のみ、`GT_ScenarioReader`の調整が必要

### 使用方法の選択肢

GT_esminiは、2つの使用方法を提供します：

**オプション1: GT_Init を使用（推奨）**
```cpp
#include "GT_esminiLib.hpp"

// GT_ScenarioReaderを使用してLightStateAction対応
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

while (running)
{
    GT_Step(dt);  // esmini本体のステップも含む
}

GT_Close();
```

**オプション2: SE_Init を使用（既存機能のみ）**
```cpp
#include "esminiLib.hpp"
#include "GT_esminiLib.hpp"

// esmini本体の機能のみ使用
SE_Init("scenario.xosc", 0);

// GT_esmini機能を追加で有効化
GT_EnableAutoLight();

while (running)
{
    SE_Step();       // esmini本体のステップ
    GT_Step(dt);     // GT_esmini拡張のステップ
}

SE_Close();
GT_Close();
```

> [!NOTE]
> `LightStateAction`のパース機能を使用するには、`GT_Init`が必要です。
> AutoLight機能のみを使用する場合は、`SE_Init` + `GT_EnableAutoLight()`で可能です。

## OSI (Open Simulation Interface) 連携

GT_esminiは、OSI v3.5.0に対応しており、ライト状態をOSI出力に含めることができます。これにより、外部のADAS/AD開発ツールとの連携が可能になります。

**OSI出力に含まれるライト情報:**
- ブレーキランプ状態 (`BrakeLightState`)
- ウインカー状態 (`IndicatorState`)
- バックライト状態 (`ReverseLightState`)
- フォグライト状態 (`GenericLightState`)
- その他のライト状態

詳細は[OSI連携](06_osi_integration.md)を参照してください。

## 実装状況

GT_esminiは、以下のフェーズで実装されました：

- ✅ **Phase 1**: GT_esmini構造準備（ディレクトリ構成、ビルドシステム）
- ✅ **Phase 2**: LightStateAction基本実装（パース処理、アクション実行）
- ✅ **Phase 3**: AutoLight機能実装（ブレーキ、ウインカー、バックライト）
- ✅ **Phase 4**: OSI連携（GT_OSIReporter統合）
- 🔄 **Phase 5**: 統合テスト（進行中）

## ユースケース

GT_esminiは、以下のようなシナリオで有用です：

### 1. ADAS/AD開発
- 先行車両のブレーキランプ検出テスト
- ウインカー認識アルゴリズムの検証
- 夜間走行シミュレーション（ヘッドライト制御）

### 2. 交通シミュレーション
- リアルな車両挙動の再現
- 交差点での右左折時のウインカー動作
- 渋滞時のブレーキランプ連鎖

### 3. HMI開発
- 車両周辺の視覚的フィードバック
- ライト状態の可視化
- ドライバー支援システムのテスト

### 4. V2X通信シミュレーション
- ライト状態の車車間通信
- OSI経由での情報共有

## ライセンス

GT_esminiは、Mozilla Public License 2.0の下でライセンスされています。
詳細は[LICENSE](../../LICENSE)ファイルを参照してください。

## 次のステップ

- [ビルド・インストール](02_build_install.md) - GT_esminiをビルドする
- [基本的な使い方](03_basic_usage.md) - 最初のプログラムを作成する
- [サンプルシナリオ](07_examples.md) - 実例から学ぶ

## 関連リンク

- [esmini公式サイト](https://esmini.github.io/)
- [OpenSCENARIO v1.2仕様](https://www.asam.net/standards/detail/openscenario/)
- [Open Simulation Interface (OSI)](https://github.com/OpenSimulationInterface/open-simulation-interface)
