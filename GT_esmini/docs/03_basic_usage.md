# GT_esmini 基本的な使い方

このドキュメントでは、GT_esminiの基本的な使用方法を説明します。

## GT_esminiLib API の概要

GT_esminiは、C言語APIを提供しています。主な関数は以下の通りです：

| 関数 | 説明 |
|------|------|
| `GT_Init` | GT_esminiの初期化（シナリオ読み込み） |
| `GT_Step` | シミュレーションステップの実行 |
| `GT_EnableAutoLight` | AutoLight機能の有効化 |
| `GT_GetLightState` | ライト状態の取得（デバッグ用） |
| `GT_Close` | GT_esminiのクリーンアップ |

詳細は[APIリファレンス](10_api_reference.md)を参照してください。

## 初期化方法

GT_esminiには、2つの初期化方法があります。

### 方法1: GT_Init を使用（推奨）

`GT_Init`を使用すると、LightStateActionのパース機能を含むすべてのGT_esmini機能が利用できます。

```cpp
#include "GT_esminiLib.hpp"

int main()
{
    // GT_esminiの初期化（LightStateAction対応）
    if (GT_Init("scenario.xosc", 0) != 0)
    {
        std::cerr << "Failed to initialize GT_esmini" << std::endl;
        return 1;
    }
    
    // AutoLight機能を有効化（オプション）
    GT_EnableAutoLight();
    
    // シミュレーションループ
    const double stepTime = 0.05; // 50ms
    for (int i = 0; i < 1000; ++i)
    {
        GT_Step(stepTime);
    }
    
    // クリーンアップ
    GT_Close();
    
    return 0;
}
```

**利点:**
- LightStateActionが自動的にパースされる
- GT_esminiの全機能が利用可能
- シンプルなAPI

**制限:**
- esmini本体の`SE_*`関数は使用できない（GT_Stepが内部で呼び出す）

### 方法2: SE_Init を使用（既存機能のみ）

esmini本体の`SE_Init`を使用し、GT_esmini機能を追加で有効化します。

```cpp
#include "esminiLib.hpp"
#include "GT_esminiLib.hpp"

int main()
{
    // esmini本体の初期化
    if (SE_Init("scenario.xosc", 0, 0, 0, 0) != 0)
    {
        std::cerr << "Failed to initialize esmini" << std::endl;
        return 1;
    }
    
    // AutoLight機能を有効化
    GT_EnableAutoLight();
    
    // シミュレーションループ
    const double stepTime = 0.05; // 50ms
    for (int i = 0; i < 1000; ++i)
    {
        SE_Step();           // esmini本体のステップ
        GT_Step(stepTime);   // GT_esmini拡張のステップ
    }
    
    // クリーンアップ
    SE_Close();
    GT_Close();
    
    return 0;
}
```

**利点:**
- esmini本体の`SE_*`関数が使用可能
- 既存のesminiアプリケーションに簡単に統合

**制限:**
- LightStateActionのパース機能は使用できない（`GT_Init`が必要）
- AutoLight機能のみ利用可能

> [!NOTE]
> **どちらを選ぶべきか？**
> - LightStateActionを使用する場合: **方法1 (GT_Init)**
> - AutoLight機能のみ使用する場合: **方法2 (SE_Init)** でも可
> - 既存のesminiアプリケーションに追加する場合: **方法2 (SE_Init)**

## シミュレーションループの実装

### 基本的なループ

```cpp
const double stepTime = 0.05; // 50ms (20Hz)
const double duration = 10.0; // 10秒間実行
const int steps = static_cast<int>(duration / stepTime);

for (int i = 0; i < steps; ++i)
{
    GT_Step(stepTime);
    
    // ここで車両状態を取得したり、他の処理を行う
}
```

### リアルタイム実行

シミュレーション時間を実時間に同期させる場合：

```cpp
#include <chrono>
#include <thread>

const double stepTime = 0.05; // 50ms
auto lastTime = std::chrono::high_resolution_clock::now();

while (running)
{
    auto currentTime = std::chrono::high_resolution_clock::now();
    double dt = std::chrono::duration<double>(currentTime - lastTime).count();
    
    if (dt >= stepTime)
    {
        GT_Step(stepTime);
        lastTime = currentTime;
    }
    else
    {
        // 次のステップまで待機
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
```

### esmini本体のAPIと組み合わせる

esmini本体のAPIを使用して、車両状態を取得できます：

```cpp
#include "esminiLib.hpp"
#include "GT_esminiLib.hpp"

// 初期化
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

// シミュレーションループ
for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
    
    // 車両0の状態を取得（esmini本体のAPI）
    SE_ScenarioObjectState state;
    SE_GetObjectState(0, &state);
    
    std::cout << "Vehicle 0 position: (" 
              << state.x << ", " << state.y << ")" << std::endl;
    
    // ライト状態を取得（GT_esminiのAPI）
    int brakeLight = GT_GetLightState(0, 6); // BRAKE_LIGHTS
    std::cout << "Brake light: " << brakeLight << std::endl;
}

GT_Close();
```

## AutoLight機能の使用

AutoLight機能を有効化すると、車両の動作に応じて自動的にライトが制御されます。

```cpp
GT_Init("scenario.xosc", 0);

// AutoLight機能を有効化
GT_EnableAutoLight();

// シミュレーション実行
for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
    
    // ブレーキランプ、ウインカー、バックライトが自動制御される
}

GT_Close();
```

**自動制御されるライト:**
- **ブレーキランプ**: 減速度が-0.1G以下で点灯
- **ウインカー**: 車線変更時、交差点での右左折時に点灯
- **バックライト**: 後退時（速度が負）に点灯

詳細は[AutoLight機能](05_auto_light.md)を参照してください。

## ライト状態の取得

デバッグや検証のために、ライト状態を取得できます。

```cpp
int vehicleId = 0; // 車両ID（SE_GetObjectIdで取得）

// ブレーキランプの状態を取得
int brakeLight = GT_GetLightState(vehicleId, 6); // 6 = BRAKE_LIGHTS

// 戻り値:
// 0: OFF
// 1: ON
// 2: FLASHING
// -1: エラー（車両が見つからない、拡張が登録されていない）

if (brakeLight == 1)
{
    std::cout << "Brake light is ON" << std::endl;
}
```

**ライトタイプの番号:**

| 番号 | ライトタイプ |
|------|------------|
| 0 | DAYTIME_RUNNING_LIGHTS |
| 1 | LOW_BEAM |
| 2 | HIGH_BEAM |
| 3 | FOG_LIGHTS |
| 4 | FOG_LIGHTS_FRONT |
| 5 | FOG_LIGHTS_REAR |
| 6 | BRAKE_LIGHTS |
| 7 | WARNING_LIGHTS |
| 8 | INDICATOR_LEFT |
| 9 | INDICATOR_RIGHT |
| 10 | REVERSING_LIGHTS |
| 11 | LICENSE_PLATE_ILLUMINATION |
| 12 | SPECIAL_PURPOSE_LIGHTS |

## クリーンアップ処理

シミュレーション終了時には、必ず`GT_Close()`を呼び出してください。

```cpp
// 方法1: GT_Init使用時
GT_Close();

// 方法2: SE_Init使用時
SE_Close();
GT_Close();
```

`GT_Close()`は以下の処理を行います：
- AutoLightコントローラーの解放
- VehicleLightExtensionの解放
- その他のリソースのクリーンアップ

## OSI出力の使用

OSI (Open Simulation Interface) 出力を使用する場合：

```cpp
#include "esminiLib.hpp"
#include "GT_esminiLib.hpp"

// 初期化
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

// OSIソケットを開く
if (SE_OpenOSISocket("127.0.0.1:48198") == 0)
{
    std::cout << "OSI socket opened" << std::endl;
}

// シミュレーションループ
for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
    
    // OSI出力は自動的に更新される
}

// クリーンアップ
SE_CloseOSISocket();
GT_Close();
```

詳細は[OSI連携](06_osi_integration.md)を参照してください。

## 完全な例

以下は、GT_esminiを使用した完全な例です：

```cpp
#include "GT_esminiLib.hpp"
#include "esminiLib.hpp"
#include <iostream>
#include <chrono>
#include <thread>

int main(int argc, char** argv)
{
    // コマンドライン引数からシナリオファイルを取得
    std::string xoscFile = "scenario.xosc";
    if (argc > 1)
    {
        xoscFile = argv[1];
    }
    
    std::cout << "Loading scenario: " << xoscFile << std::endl;
    
    // GT_esminiの初期化
    if (GT_Init(xoscFile.c_str(), 0) != 0)
    {
        std::cerr << "Failed to initialize GT_esmini" << std::endl;
        return 1;
    }
    
    // AutoLight機能を有効化
    GT_EnableAutoLight();
    std::cout << "AutoLight enabled" << std::endl;
    
    // シミュレーション設定
    const double stepTime = 0.05; // 50ms
    const double duration = 15.0; // 15秒間実行
    const int steps = static_cast<int>(duration / stepTime);
    
    std::cout << "Running simulation for " << duration << " seconds..." << std::endl;
    
    // シミュレーションループ
    for (int i = 0; i < steps; ++i)
    {
        GT_Step(stepTime);
        
        // 0.5秒ごとにライト状態を表示
        if (i % 10 == 0)
        {
            int vehicleId = 0;
            int brake = GT_GetLightState(vehicleId, 6);    // BRAKE_LIGHTS
            int leftInd = GT_GetLightState(vehicleId, 8);  // INDICATOR_LEFT
            int rightInd = GT_GetLightState(vehicleId, 9); // INDICATOR_RIGHT
            int reverse = GT_GetLightState(vehicleId, 10); // REVERSING_LIGHTS
            
            std::cout << "Time " << (i * stepTime) << "s | "
                      << "Brake: " << brake << " | "
                      << "L-Ind: " << leftInd << " | "
                      << "R-Ind: " << rightInd << " | "
                      << "Rev: " << reverse << std::endl;
        }
        
        // リアルタイム実行（オプション）
        // std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::cout << "Simulation completed." << std::endl;
    
    // クリーンアップ
    GT_Close();
    
    return 0;
}
```

このコードは、[GT_Loader.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/integration/GT_Loader.cpp)を簡略化したものです。

## ビルド方法

上記のコードをビルドするには：

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.15)
project(MyGT_esminiApp)

# GT_esminiのインクルードパスを追加
include_directories(
    ${CMAKE_SOURCE_DIR}/../GT_esmini
    ${CMAKE_SOURCE_DIR}/../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles
)

# 実行ファイルを作成
add_executable(MyApp main.cpp)

# GT_esminiLibをリンク
target_link_libraries(MyApp GT_esminiLib)
```

**ビルド:**
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## 次のステップ

- [LightStateAction機能](04_light_state_action.md) - シナリオでライトを制御する
- [AutoLight機能](05_auto_light.md) - 自動ライト制御の詳細
- [サンプルシナリオ](07_examples.md) - 実用的な使用例
- [APIリファレンス](10_api_reference.md) - 関数の詳細仕様

## 関連ドキュメント

- [概要](01_overview.md) - GT_esminiの全体像
- [ビルド・インストール](02_build_install.md) - 環境構築
- [トラブルシューティング](09_troubleshooting.md) - 問題解決
