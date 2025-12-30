# Phase 1: GT_esmini構造準備

## 概要

esminiにライト機能を追加するための基盤となるGT_esminiモジュールの構造を準備します。このフェーズでは、esmini本体のファイルを一切コピーせず、継承パターンを使用して拡張可能な構造を構築します。

## ユーザーレビュー必須事項

> [!IMPORTANT]
> **Phase 1の実装範囲**
> 
> Phase 1では以下の基本構造のみを構築します：
> - GT_esminiディレクトリ構造の作成
> - 基本ヘッダーファイルのスタブ実装（コンパイル可能な最小限の実装）
> - CMakeビルドシステムの設定
> - ビルド成功の確認
> 
> **実際のライト機能実装はPhase 2以降で行います。**

> [!WARNING]
> **esmini本体への変更**
> 
> このプランでは、esmini本体のファイルには**一切変更を加えません**。すべての拡張機能はGT_esminiディレクトリ内で実装し、継承パターンを使用します。
> 
> - ファイルコピー: **ゼロ**
> - esmini本体の変更: **ゼロ**（ルートCMakeLists.txtへのGT_esminiサブディレクトリ追加のみ）

## 提案する変更内容

### GT_esminiモジュール

GT_esminiは独立したモジュールとして実装し、esmini本体のライブラリをリンクします。

---

#### [NEW] [GT_esmini/CMakeLists.txt](file:///e:/Repository/GT_esmini/esmini/GT_esmini/CMakeLists.txt)

GT_esminiモジュール用のCMakeビルドファイルを作成します。

**主な内容:**
- GT_esminiライブラリ（shared/static）のビルド設定
- esmini本体のScenarioEngine、PlayerBase等のライブラリをリンク
- 必要なインクルードパスの設定
- テストサブディレクトリの追加

**依存関係:**
- ScenarioEngine（esmini本体）
- PlayerBase（esmini本体）
- RoadManager（esmini本体）
- CommonMini（esmini本体）

---

#### [NEW] [GT_esmini/ExtraAction.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/ExtraAction.hpp)

esmini本体の`Action.hpp`を拡張するライトアクション定義のヘッダーファイル。

**Phase 1での実装内容:**
- `LightState`構造体の定義（スタブ）
- `VehicleLightType`列挙型の定義
- `OSCLightStateAction`クラスの宣言（スタブ）

**Phase 2以降で実装:**
- 実際のアクション実行ロジック

---

#### [NEW] [GT_esmini/ExtraAction.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/ExtraAction.cpp)

`ExtraAction.hpp`の実装ファイル。

**Phase 1での実装内容:**
- スタブ実装（空の関数本体）

---

#### [NEW] [GT_esmini/ExtraEntities.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/ExtraEntities.hpp)

esmini本体の`Entities.hpp`を拡張するVehicleライト機能のヘッダーファイル。

**Phase 1での実装内容:**
- `VehicleLightExtension`クラスの宣言（スタブ）
- `VehicleExtensionManager`シングルトンクラスの宣言（スタブ）

**設計方針:**
- esmini本体の`Vehicle`クラスを継承せず、コンポジションパターンを使用
- `VehicleExtensionManager`でVehicleオブジェクトと拡張機能を紐付け

---

#### [NEW] [GT_esmini/ExtraEntities.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/ExtraEntities.cpp)

`ExtraEntities.hpp`の実装ファイル。

**Phase 1での実装内容:**
- スタブ実装（空の関数本体）

---

#### [NEW] [GT_esmini/GT_ScenarioReader.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/GT_ScenarioReader.hpp)

esmini本体の`ScenarioReader`を継承してAppearanceActionパース機能を追加するヘッダーファイル。

**Phase 1での実装内容:**
- `GT_ScenarioReader`クラスの宣言
- `ScenarioReader`を継承
- コンストラクタ/デストラクタの宣言
- パース関数の宣言（スタブ）

**Phase 2以降で実装:**
- `ParseLightStateAction`メソッド
- `ParseAppearanceAction`メソッド

---

#### [NEW] [GT_esmini/GT_ScenarioReader.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/GT_ScenarioReader.cpp)

`GT_ScenarioReader.hpp`の実装ファイル。

**Phase 1での実装内容:**
- コンストラクタ/デストラクタの実装（親クラスのコンストラクタ呼び出しのみ）
- スタブ実装

---

#### [NEW] [GT_esmini/AutoLightController.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/AutoLightController.hpp)

AutoLight機能のコントローラークラスのヘッダーファイル。

**Phase 1での実装内容:**
- `AutoLightController`クラスの宣言（スタブ）

**Phase 3以降で実装:**
- ブレーキランプ制御ロジック
- ウインカー制御ロジック
- バックライト制御ロジック

---

#### [NEW] [GT_esmini/AutoLightController.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/AutoLightController.cpp)

`AutoLightController.hpp`の実装ファイル。

**Phase 1での実装内容:**
- スタブ実装

---

#### [NEW] [GT_esmini/GT_esminiLib.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/GT_esminiLib.hpp)

GT_esmini用のラッパーAPIのヘッダーファイル。

**Phase 1での実装内容:**
- `GT_Init`関数の宣言（スタブ）
- `GT_Step`関数の宣言（スタブ）
- `GT_EnableAutoLight`関数の宣言（スタブ）

**Phase 3以降で実装:**
- 起動時引数処理
- AutoLight管理機能

---

#### [NEW] [GT_esmini/GT_esminiLib.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/GT_esminiLib.cpp)

`GT_esminiLib.hpp`の実装ファイル。

**Phase 1での実装内容:**
- スタブ実装（空の関数本体、戻り値のみ）

---

### esmini本体への変更

#### [MODIFY] [CMakeLists.txt](file:///e:/Repository/GT_esmini/esmini/CMakeLists.txt)

ルートCMakeLists.txtにGT_esminiサブディレクトリを追加します。

**変更内容:**
- `add_subdirectory(GT_esmini)`を追加（EnvironmentSimulatorの後）

**変更箇所:**
- 304行目の`add_subdirectory(EnvironmentSimulator)`の後に追加

---

### テスト構造

#### [NEW] [GT_esmini/test/CMakeLists.txt](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/CMakeLists.txt)

テストディレクトリ用のCMakeファイル。

**Phase 1での実装内容:**
- 基本的なテスト構造の準備（空のファイルまたは最小限の設定）

**Phase 2以降で実装:**
- ユニットテスト
- 統合テスト
- シナリオベーステスト

---

## 検証プラン

### ビルドテスト

Phase 1の主な検証項目はビルドの成功です。

#### 1. CMake設定の確認

```bash
# ビルドディレクトリの作成
cd e:\Repository\GT_esmini\esmini
mkdir build_gt
cd build_gt

# CMake設定
cmake ..

# GT_esminiターゲットが認識されているか確認
cmake --build . --target GT_esminiLib --config Release --dry-run
```

**期待される結果:**
- CMakeエラーなく設定が完了
- GT_esminiLibターゲットが認識される

#### 2. ビルドの実行

```bash
# GT_esminiライブラリのビルド
cmake --build . --target GT_esminiLib --config Release

# 全体ビルド
cmake --build . --config Release
```

**期待される結果:**
- コンパイルエラーなくビルドが完了
- `GT_esminiLib.dll`（または`.so`）が生成される
- `GT_esminiLib_static.lib`（または`.a`）が生成される

#### 3. 基本的な動作確認

Phase 1では実際の機能実装はないため、以下の確認のみ行います：

```bash
# esmini本体の既存機能が正常に動作するか確認
cd e:\Repository\GT_esmini\esmini\build_gt\bin
.\esmini.exe --osc ..\..\resources\xosc\cut-in.xosc --window 60 60 800 400
```

**期待される結果:**
- esmini本体の機能が正常に動作（GT_esminiの追加によって既存機能が壊れていないことを確認）

### 手動確認項目

- [ ] GT_esminiディレクトリ構造が正しく作成されている
- [ ] すべてのヘッダーファイル（.hpp）が作成されている
- [ ] すべての実装ファイル（.cpp）が作成されている
- [ ] CMakeLists.txtが正しく設定されている
- [ ] ビルドが成功する
- [ ] esmini本体の既存機能が正常に動作する

---

## 実装後の次のステップ

Phase 1完了後、Phase 2に進みます：

### Phase 2: LightStateAction基本実装
- AppearanceActionパース実装
- LightStateActionパース実装
- VehicleLightExtension実装
- テストシナリオ作成と検証
