# GT_esmini - Extended esmini with Light Functionality

GT_esminiは、esminiにライト機能を追加する拡張モジュールです。

## 概要

このモジュールは以下の機能を提供します：

1. **LightStateAction対応**: OpenSCENARIO v1.2のLightStateActionをパースし、車両のライト状態を制御
2. **AutoLight機能**: 車両の動作に応じて自動的にライトを点灯（ブレーキランプ、ウインカー、バックライト）

## 実装状況

### Phase 1: GT_esmini構造準備 ✅ 完了

- [x] GT_esminiディレクトリ構造の作成
- [x] 基本ヘッダー/ソースファイルのスタブ実装
- [x] CMakeビルドシステムの設定

### Phase 2: LightStateAction基本実装（未実装）

- [ ] AppearanceActionパース実装
- [ ] LightStateActionパース実装
- [ ] VehicleLightExtension実装
- [ ] テストシナリオ作成

### Phase 3: AutoLight機能実装（未実装）

- [ ] AutoLightController実装
- [ ] GT_esminiLib完成
- [ ] バックライト対応
- [ ] テスト作成

### Phase 4: OSI連携（未実装）

- [ ] OSIReporterExtension実装
- [ ] テスト作成

### Phase 5: 統合テスト（未実装）

- [ ] 回帰テスト
- [ ] ドキュメント更新

## ビルド方法

### 前提条件

- CMake 3.10以上
- C++14対応コンパイラ（Visual Studio 2017以上、GCC 7以上、Clang 5以上）
- esmini本体のビルド環境

### ビルド手順

#### Windows (Visual Studio)

```powershell
# ビルドディレクトリの作成
cd e:\Repository\GT_esmini\esmini
mkdir build
cd build

# CMake設定（Visual Studio 2019の例）
cmake .. -G "Visual Studio 16 2019" -A x64

# ビルド
cmake --build . --config Release

# または Visual Studio で build\esmini.sln を開いてビルド
```

#### Windows (MinGW)

```bash
cd e:\Repository\GT_esmini\esmini
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --config Release
```

#### Linux / macOS

```bash
cd /path/to/GT_esmini/esmini
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### ビルド成果物

ビルドが成功すると、以下のファイルが生成されます：

- `GT_esminiLib.dll` / `GT_esminiLib.so` (共有ライブラリ)
- `GT_esminiLib_static.lib` / `GT_esminiLib_static.a` (静的ライブラリ)

## ファイル構成

```
GT_esmini/
├── ExtraAction.hpp          # ライトアクション拡張定義
├── ExtraAction.cpp          # ライトアクション実装
├── ExtraEntities.hpp        # Vehicleクラス拡張定義
├── ExtraEntities.cpp        # Vehicleクラス拡張実装
├── GT_ScenarioReader.hpp    # ScenarioReader継承クラス
├── GT_ScenarioReader.cpp    # AppearanceActionパース実装
├── AutoLightController.hpp  # AutoLight機能
├── AutoLightController.cpp
├── GT_esminiLib.hpp         # GT_esmini用ラッパーAPI
├── GT_esminiLib.cpp         # 起動時引数処理等
├── CMakeLists.txt           # ビルド設定
├── README.md                # このファイル
└── test/                    # テストディレクトリ
    └── CMakeLists.txt
```

## 設計方針

### esmini本体への影響を最小化

GT_esminiは以下の方針で実装されています：

1. **ファイルコピーゼロ**: esmini本体のファイルは一切コピーしません
2. **継承パターン**: `ScenarioReader`等を継承して機能を拡張
3. **コンポジションパターン**: `Vehicle`クラスには継承せず、`VehicleLightExtension`で拡張
4. **独立したモジュール**: GT_esminiは独立したライブラリとしてビルド

### esmini本体への変更

esmini本体への変更は**最小限**に抑えられています：

- **ルートCMakeLists.txt**: `add_subdirectory(GT_esmini)`の1行のみ追加

## 使用方法（Phase 3以降で実装予定）

### オプション1: GT_Init を使用（推奨）

```cpp
#include "GT_esminiLib.hpp"

// GT_ScenarioReader を使用してAppearanceAction対応
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

while (running)
{
    SE_Step();         // esmini本体のステップ
    GT_Step(dt);       // GT_esmini拡張のステップ
}

GT_Close();
```

### オプション2: SE_Init を使用（既存機能のみ）

```cpp
#include "esminiLib.hpp"
#include "GT_esminiLib.hpp"

// esmini本体の機能のみ使用
SE_Init("scenario.xosc", 0);

// GT_esmini機能を追加で有効化
GT_EnableAutoLight();

while (running)
{
    SE_Step();         // esmini本体のステップ
    GT_Step(dt);       // GT_esmini拡張のステップ
}

SE_Close();
GT_Close();
```

## ライセンス

このプロジェクトは、Mozilla Public License 2.0の下でライセンスされています。
詳細は[LICENSE](../LICENSE)ファイルを参照してください。

## 貢献

貢献を歓迎します！詳細は[CONTRIBUTING.md](../CONTRIBUTING.md)を参照してください。

## 関連リンク

- [esmini本体](https://github.com/esmini/esmini)
- [OpenSCENARIO v1.2仕様](https://www.asam.net/standards/detail/openscenario/)
