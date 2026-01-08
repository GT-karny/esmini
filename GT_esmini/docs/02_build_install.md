# GT_esmini ビルド・インストール

このドキュメントでは、GT_esminiのビルド方法とインストール手順を説明します。

## 前提条件

GT_esminiは、esmini本体と一緒にビルドされます。以下の環境が必要です。

### Windows

- **Visual Studio 2022** (Community、Professional、またはEnterprise)
  - 「C++によるデスクトップ開発」ワークロードをインストール
  - CMakeはVisual Studio 2022に含まれています
- **Git** (リポジトリのクローン用)

### Linux

- **CMake** 3.15以上
- **C++コンパイラ** C++17サポート (GCC 7+、Clang 5+)
- **Git**

### macOS

- **CMake** 3.15以上
- **Xcode Command Line Tools** または **Clang**
- **Git**

## ステップバイステップ ビルド手順

### Windows (Visual Studio 2022)

#### ステップ 1: PowerShellを開く

PowerShell（コマンドプロンプトではない）を開きます。

#### ステップ 2: esminiリポジトリに移動

```powershell
cd e:\Repository\GT_esmini\esmini
```

> [!NOTE]
> パスは、あなたの環境に合わせて変更してください。

#### ステップ 3: ビルドディレクトリを作成して移動

```powershell
# ビルドディレクトリが存在しない場合は作成
if (!(Test-Path "build")) { New-Item -ItemType Directory -Path "build" }

# ビルドディレクトリに移動
cd build
```

#### ステップ 4: CMakeで設定

Visual Studio 2022のCMakeを使用します：

```powershell
# CMakeをPATHに追加（このセッションのみ）
$env:Path = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;" + $env:Path

# プロジェクトを設定
cmake .. -G "Visual Studio 17 2022" -A x64
```

**期待される出力:**
- CMakeが外部依存関係（OSG、OSI、SUMO等）をダウンロードします（初回は数分かかります）
- 最後に「Build files have been written to: E:/Repository/GT_esmini/esmini/build」と表示されます

#### ステップ 5: GT_esminiをビルド

```powershell
# GT_esminiライブラリのみビルド（高速）
cmake --build . --config Release --target GT_esminiLib

# または、すべてをビルド（時間がかかります）
cmake --build . --config Release
```

**期待される出力:**
- コンパイル進行状況のメッセージ
- 「GT_esminiLib.vcxproj -> E:\Repository\GT_esmini\esmini\build\GT_esmini\Release\GT_esminiLib.dll」

#### ステップ 6: ビルド成功を確認

```powershell
# GT_esminiライブラリが作成されたか確認
Get-ChildItem -Path ".\GT_esmini\Release" -Filter "GT_esminiLib*"
```

**期待されるファイル:**
- `GT_esminiLib.dll` (共有ライブラリ)
- `GT_esminiLib.lib` (インポートライブラリ)
- `GT_esminiLib_static.lib` (静的ライブラリ)

### Linux

#### ステップ 1: esminiリポジトリに移動

```bash
cd /path/to/GT_esmini/esmini
```

#### ステップ 2: ビルドディレクトリを作成して移動

```bash
mkdir -p build
cd build
```

#### ステップ 3: CMakeで設定

```bash
cmake ..
```

#### ステップ 4: GT_esminiをビルド

```bash
# GT_esminiライブラリのみビルド
cmake --build . --config Release --target GT_esminiLib

# または、すべてをビルド
cmake --build . --config Release
```

#### ステップ 5: ビルド成功を確認

```bash
# GT_esminiライブラリが作成されたか確認
ls -lh GT_esmini/libGT_esminiLib*
```

**期待されるファイル:**
- `libGT_esminiLib.so` (共有ライブラリ)
- `libGT_esminiLib_static.a` (静的ライブラリ)

### macOS

macOSでのビルド手順は、Linuxと同様です。

```bash
cd /path/to/GT_esmini/esmini
mkdir -p build
cd build
cmake ..
cmake --build . --config Release --target GT_esminiLib
```

**期待されるファイル:**
- `libGT_esminiLib.dylib` (共有ライブラリ)
- `libGT_esminiLib_static.a` (静的ライブラリ)

## ビルド設定オプション

GT_esminiは、esmini本体のビルドオプションを継承します。一般的なオプション：

### OSG（OpenSceneGraph）可視化を無効化

```powershell
# Windows
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_OSG=OFF

# Linux/macOS
cmake .. -DUSE_OSG=OFF
```

### OSI（Open Simulation Interface）を無効化

```powershell
# Windows
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_OSI=OFF

# Linux/macOS
cmake .. -DUSE_OSI=OFF
```

> [!WARNING]
> OSIを無効化すると、GT_esminiのOSI連携機能が使用できなくなります。

### SUMO統合を無効化

```powershell
# Windows
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_SUMO=OFF

# Linux/macOS
cmake .. -DUSE_SUMO=OFF
```

## トラブルシューティング

### 問題: CMakeが見つからない

**Windows:**
```powershell
# Visual Studio 2022のインストールを確認
Test-Path "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"

# falseの場合、Visual Studio 2022 Community Editionをインストール
# ダウンロード: https://visualstudio.microsoft.com/downloads/
```

**Linux:**
```bash
# CMakeをインストール
# Ubuntu/Debian:
sudo apt-get install cmake

# Fedora/RHEL:
sudo dnf install cmake
```

**macOS:**
```bash
# Homebrewを使用してCMakeをインストール
brew install cmake
```

### 問題: ビルドがエンコーディングエラーで失敗する

**解決策:** すべてのソースコードのコメントは英語である必要があります。エンコーディング関連のエラーが表示される場合は、`GT_esmini/`ディレクトリ内のすべての`.cpp`および`.hpp`ファイルが英語のコメントのみを使用していることを確認してください。

### 問題: "storyBoard_" 未定義識別子

**解決策:** これはPhase 1で修正されました。`ExtraAction.cpp`が`storyBoard_`の代わりに`parent_`を使用している最新バージョンであることを確認してください。

### 問題: ActionType::PRIVATE が見つからない

**解決策:** これはPhase 1で修正されました。`ExtraAction.cpp`が`ActionType::PRIVATE`の代わりに`ActionType::USER_DEFINED`を使用していることを確認してください。

### 問題: リンクエラー（未解決の外部シンボル）

**解決策:** 
1. クリーンビルドを試してください（下記参照）
2. esmini本体のライブラリが正しくビルドされていることを確認
3. CMakeの設定を再実行

## クリーンビルド

問題が発生した場合は、クリーンビルドを試してください：

**Windows:**
```powershell
# ビルドディレクトリを削除
cd e:\Repository\GT_esmini\esmini
Remove-Item -Recurse -Force build

# ステップ3から再開
```

**Linux/macOS:**
```bash
# ビルドディレクトリを削除
cd /path/to/GT_esmini/esmini
rm -rf build

# ステップ2から再開
```

## ビルド成果物

ビルドが成功すると、以下のファイルが生成されます：

### Windows
- `build/GT_esmini/Release/GT_esminiLib.dll` (共有ライブラリ)
- `build/GT_esmini/Release/GT_esminiLib.lib` (インポートライブラリ)
- `build/GT_esmini/Release/GT_esminiLib_static.lib` (静的ライブラリ)
- `build/bin/GT_Loader.exe` (統合テスト用実行ファイル)

### Linux
- `build/GT_esmini/libGT_esminiLib.so` (共有ライブラリ)
- `build/GT_esmini/libGT_esminiLib_static.a` (静的ライブラリ)
- `build/bin/GT_Loader` (統合テスト用実行ファイル)

### macOS
- `build/GT_esmini/libGT_esminiLib.dylib` (共有ライブラリ)
- `build/GT_esmini/libGT_esminiLib_static.a` (静的ライブラリ)
- `build/bin/GT_Loader` (統合テスト用実行ファイル)

## インストール

GT_esminiは、通常、ビルドディレクトリから直接使用します。システムワイドなインストールは必要ありません。

アプリケーションから使用する場合は、以下をインクルードパスとリンクパスに追加してください：

**インクルードパス:**
- `GT_esmini/esmini/GT_esmini/` (GT_esminiLib.hpp等)
- `GT_esmini/esmini/EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/` (esmini本体のヘッダー)

**リンクパス:**
- `GT_esmini/esmini/build/GT_esmini/Release/` (Windows)
- `GT_esmini/esmini/build/GT_esmini/` (Linux/macOS)

**リンクライブラリ:**
- `GT_esminiLib.lib` または `GT_esminiLib_static.lib` (Windows)
- `libGT_esminiLib.so` または `libGT_esminiLib_static.a` (Linux)
- `libGT_esminiLib.dylib` または `libGT_esminiLib_static.a` (macOS)

## 次のステップ

ビルドが成功したら：

1. **esmini機能のテスト** - GT_esminiが既存機能を壊していないことを確認：
   ```powershell
   cd build\bin
   .\esmini.exe --osc ..\..\resources\xosc\cut-in.xosc --window 60 60 800 400
   ```

2. **GT_esmini機能のテスト** - AutoLight機能を確認：
   ```powershell
   cd build\bin
   .\GT_Loader.exe ..\GT_esmini\test\scenarios\autolight_test.xosc
   ```

3. **基本的な使い方を学ぶ** - [基本的な使い方](03_basic_usage.md)を参照

4. **サンプルを試す** - [サンプルシナリオ](07_examples.md)を参照

## 関連ドキュメント

- [概要](01_overview.md) - GT_esminiの全体像
- [基本的な使い方](03_basic_usage.md) - APIの使用方法
- [トラブルシューティング](09_troubleshooting.md) - より詳細な問題解決
