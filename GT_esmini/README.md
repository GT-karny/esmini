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

### Build Instructions

This section provides detailed, step-by-step instructions for building GT_esmini. These instructions have been verified to work successfully.

### Prerequisites

#### Windows

- **Visual Studio 2022** (Community, Professional, or Enterprise)
  - Install "Desktop development with C++" workload
  - CMake is included with Visual Studio 2022
- **Git** (for cloning the repository)

#### Linux/macOS

- **CMake** 3.15 or higher
- **C++ compiler** with C++17 support (GCC 7+, Clang 5+)
- **Git**

### Step-by-Step Build Instructions

#### Windows with Visual Studio 2022

**Step 1: Open PowerShell**

Open PowerShell (not Command Prompt) in your preferred location.

**Step 2: Navigate to esmini repository**

```powershell
cd e:\Repository\GT_esmini\esmini
```

**Step 3: Create and enter build directory**

```powershell
# Create build directory if it doesn't exist
if (!(Test-Path "build")) { New-Item -ItemType Directory -Path "build" }

# Enter build directory
cd build
```

**Step 4: Configure with CMake**

Use Visual Studio 2022's bundled CMake:

```powershell
# Add CMake to PATH for this session
$env:Path = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;" + $env:Path

# Configure the project
cmake .. -G "Visual Studio 17 2022" -A x64
```

**Expected output:**
- CMake will download external dependencies (OSG, OSI, SUMO, etc.) - this takes several minutes on first run
- You should see "Build files have been written to: E:/Repository/GT_esmini/esmini/build"

**Step 5: Build GT_esmini**

```powershell
# Build only GT_esmini library (faster)
cmake --build . --config Release --target GT_esminiLib

# OR build everything (takes longer)
cmake --build . --config Release
```

**Expected output:**
- Compilation progress messages
- "GT_esminiLib.vcxproj -> E:\Repository\GT_esmini\esmini\build\GT_esmini\Release\GT_esminiLib.dll"

**Step 6: Verify build success**

```powershell
# Check if GT_esmini libraries were created
Get-ChildItem -Path ".\GT_esmini\Release" -Filter "GT_esminiLib*"
```

**Expected files:**
- `GT_esminiLib.dll` (shared library)
- `GT_esminiLib.lib` (import library)
- `GT_esminiLib_static.lib` (static library)

#### Linux/macOS

**Step 1: Navigate to esmini repository**

```bash
cd /path/to/GT_esmini/esmini
```

**Step 2: Create and enter build directory**

```bash
mkdir -p build
cd build
```

**Step 3: Configure with CMake**

```bash
cmake ..
```

**Step 4: Build GT_esmini**

```bash
# Build only GT_esmini library
cmake --build . --config Release --target GT_esminiLib

# OR build everything
cmake --build . --config Release
```

**Step 5: Verify build success**

```bash
# Check if GT_esmini libraries were created
ls -lh GT_esmini/libGT_esminiLib*
```

**Expected files:**
- `libGT_esminiLib.so` (shared library)
- `libGT_esminiLib_static.a` (static library)

### Troubleshooting

#### Issue: CMake not found

**Windows:**
```powershell
# Verify Visual Studio 2022 installation
Test-Path "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"

# If false, install Visual Studio 2022 Community Edition
# Download from: https://visualstudio.microsoft.com/downloads/
```

**Linux/macOS:**
```bash
# Install CMake
# Ubuntu/Debian:
sudo apt-get install cmake

# macOS:
brew install cmake
```

#### Issue: Build fails with encoding errors

**Solution:** All source code comments should be in English. If you see encoding-related errors, check that all `.cpp` and `.hpp` files in `GT_esmini/` directory use English comments only.

#### Issue: "storyBoard_" undefined identifier

**Solution:** This was fixed in Phase 1. Make sure you have the latest version where `ExtraAction.cpp` uses `parent_` instead of `storyBoard_`.

#### Issue: ActionType::PRIVATE not found

**Solution:** This was fixed in Phase 1. Make sure `ExtraAction.cpp` uses `ActionType::USER_DEFINED` instead of `ActionType::PRIVATE`.

### Build Configuration Options

GT_esmini inherits build options from esmini. Common options:

```powershell
# Disable OSG (OpenSceneGraph) visualization
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_OSG=OFF

# Disable OSI (Open Simulation Interface)
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_OSI=OFF

# Disable SUMO integration
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_SUMO=OFF
```

### Clean Build

If you encounter issues, try a clean build:

**Windows:**
```powershell
# Remove build directory
cd e:\Repository\GT_esmini\esmini
Remove-Item -Recurse -Force build

# Start fresh from Step 3
```

**Linux/macOS:**
```bash
# Remove build directory
cd /path/to/GT_esmini/esmini
rm -rf build

# Start fresh from Step 2
```

### Next Steps

After successful build:

1. **Test esmini functionality** to ensure GT_esmini didn't break existing features:
   ```powershell
   cd build\bin
   .\esmini.exe --osc ..\..\resources\xosc\cut-in.xosc --window 60 60 800 400
   ```

2. **Proceed to Phase 2** implementation (LightStateAction parsing)

3. **Create test scenarios** with LightStateAction elements

## ビルド成果物

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
