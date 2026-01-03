# FMI (FMU) ビルドサポート調査結果

GT_esminiの機能をFMI (Functional Mock-up Interface) 規格のFMU (Functional Mock-up Unit) としてビルドする場合の実現可能性と課題についての調査結果です。

## 概要

esminiは `OSMP_FMU` ディレクトリにおいて、OSI (Open Simulation Interface) を入出力とするFMUのビルドを提供しています。
GT_esminiの拡張機能がこのFMUに含まれるかどうかは、実装方式に依存します。

## 機能別の対応状況（デフォルトFMUビルド時）

| 機能 | 対応状況 | 理由 |
|------|----------|------|
| **Dual Trajectory Reporting**<br>(Ghost/Ego軌道出力) | ✅ **対応** | `ScenarioEngine` ライブラリに `GT_OSIReporter.cpp` が組み込まれるため、標準のFMUビルドでもこのロジックは有効になります。 |
| **LightStateAction** | ❌ **未対応** | FMUのエントリーポイント (`esmini.cpp`) が `SE_Init` を使用しており、`GT_ScenarioReader` が初期化されないためです。 |
| **AutoLight機能** | ❌ **未対応** | 同様に、`GT_EnableAutoLight` などの初期化関数が呼び出されないため、機能が無効になります。 |

## 完全対応させるための改修案

GT_esminiの全機能をFMUでも使用可能にするためには、`OSMP_FMU/esmini.cpp` に対して以下の改修が必要です。

1.  **ヘッダーの変更**: `#include "esminiLib.hpp"` に加えて `#include "GT_esminiLib.hpp"` を追加。
2.  **初期化ロジックの変更**:
    - `doExitInitializationMode` 内の `SE_Init` 呼び出しを `GT_Init` に変更。
    - または、`SE_Init` の後に `GT_EnableAutoLight` などを呼び出すロジックを追加。
3.  **リンク設定**: `CMakeLists.txt` で `GT_esminiLib` (または関連オブジェクト) をリンク対象に追加。

## 結論

- **軌道情報の連携 (Dual Trajectory)** だけが目的であれば、現状のビルド構成のままで `OSMP_FMU` をビルドすれば機能します。


## 実装済みソリューション: GT専用FMUディレクトリ

調査結果に基づき、**`esmini/GT_OSMP_FMU`** ディレクトリを作成し、GT機能対応版FMUの実装を完了しました。

### 変更点

1.  **ディレクトリ構成**: `OSMP_FMU` をベースに `GT_OSMP_FMU` を新規作成。
2.  **初期化ロジック (`GT_OSMP_FMU/esmini.cpp`)**:
    - `SE_Init` の代わりに `GT_Init` を使用することで、`GT_ScenarioReader` による拡張アクション (LightStateAction) に対応。
    - `GT_InitWithArgs` もサポートし、引数 (`--autolight` 等) によるAutoLight機能の有効化が可能。
3.  **リンク設定 (`GT_OSMP_FMU/CMakeLists.txt`)**:
    - `GT_esminiLib_static` をリンクするように変更。


### 実装詳細（追加修正）

初期化ロジックの変更に加え、Windows/MSVC環境でのビルドを成功させるために以下の修正を行いました。
- **esmini.cpp**: 可変長配列 (VLA) の使用を `std::vector` に置き換え (MSVC非互換回避)。
- **CMakeLists.txt**: `git describe` 失敗時のバージョン文字列処理を修正し、`stdc++fs` リンクを除去。

### ビルド手順

ビルドの前に、**GT_esmini本体 (`GT_esminiLib`) がReleaseモードでビルド済みであること**を確認してください。

```powershell
cd e:\Repository\GT_esmini\esmini\GT_OSMP_FMU

# ビルドディレクトリの作成とCMake設定
# (Visual Studioのバージョンは環境に合わせて自動検出されます)
cmake -S . -B build -A x64

# Releaseモードでビルド
cmake --build build --config Release
```

### 成果物
- `build/esmini.fmu`: 配布用FMUファイル
- `build/Release/esmini.dll`: 実体DLL



