# APIリファレンス

このドキュメントでは、GT_esminiLib APIの完全なリファレンスを提供します。

## 関数一覧

| 関数 | 説明 |
|------|------|
| [`GT_Init`](#gt_init) | GT_esminiの初期化 |
| [`GT_Step`](#gt_step) | シミュレーションステップの実行 |
| [`GT_EnableAutoLight`](#gt_enableautolight) | AutoLight機能の有効化 |
| [`GT_GetLightState`](#gt_getlightstate) | ライト状態の取得 |
| [`GT_SetExternalLightState`](#gt_setexternallightstate) | 外部からのライト状態設定 |
| [`GT_Close`](#gt_close) | GT_esminiのクリーンアップ |

---

## GT_Init

GT_esminiを初期化し、OpenSCENARIOシナリオを読み込みます。

### シグネチャ

```c
int GT_Init(const char* oscFilename, int disable_ctrls);
```

### パラメータ

| パラメータ | 型 | 説明 |
|-----------|---|------|
| `oscFilename` | `const char*` | OpenSCENARIOファイル（.xosc）のパス |
| `disable_ctrls` | `int` | コントローラーを無効化するフラグ（0: 有効, 1: 無効） |

### 戻り値

| 値 | 説明 |
|----|------|
| `0` | 成功 |
| `-1` | 失敗（ファイルが見つからない、パースエラー等） |

### 説明

`GT_Init`は、GT_ScenarioReaderを使用してシナリオを読み込みます。これにより、`LightStateAction`が自動的にパースされます。

内部的には、esmini本体の初期化処理も実行されます。

### 使用例

```cpp
#include "GT_esminiLib.hpp"

int main()
{
    if (GT_Init("scenario.xosc", 0) != 0)
    {
        std::cerr << "Failed to initialize GT_esmini" << std::endl;
        return 1;
    }
    
    // シミュレーション実行
    
    GT_Close();
    return 0;
}
```

### 注意事項

- `GT_Init`を使用した場合、esmini本体の`SE_Init`は使用できません
- シナリオファイルのパスは、相対パスまたは絶対パスで指定できます

---

## GT_Step

シミュレーションを1ステップ進めます。

### シグネチャ

```c
void GT_Step(double dt);
```

### パラメータ

| パラメータ | 型 | 説明 |
|-----------|---|------|
| `dt` | `double` | タイムステップ（秒） |

### 戻り値

なし

### 説明

`GT_Step`は、以下の処理を実行します：

1. esmini本体のステップ処理（`SE_Step`相当）
2. AutoLightコントローラーの更新
3. OSI出力の更新（OSIが有効な場合）

### 使用例

```cpp
const double stepTime = 0.05; // 50ms

for (int i = 0; i < 1000; ++i)
{
    GT_Step(stepTime);
}
```

### 注意事項

- `dt`は正の値である必要があります
- 通常、`dt`は0.01〜0.1秒程度が推奨されます

---

## GT_EnableAutoLight

AutoLight機能を有効化します。

### シグネチャ

```c
void GT_EnableAutoLight();
```

### パラメータ

なし

### 戻り値

なし

### 説明

`GT_EnableAutoLight`は、すべての車両に対してAutoLight機能を有効化します。

この関数は、`GT_Init`または`SE_Init`の後、シミュレーション開始前に呼び出す必要があります。

### 使用例

```cpp
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight(); // AutoLight有効化

for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
}

GT_Close();
```

### 注意事項

- この関数は、シミュレーション開始前に1回だけ呼び出してください
- AutoLightは、ブレーキランプ、ウインカー、バックライトを自動制御します

---

## GT_GetLightState

指定した車両のライト状態を取得します。

### シグネチャ

```c
int GT_GetLightState(int vehicleId, int lightType);
```

### パラメータ

| パラメータ | 型 | 説明 |
|-----------|---|------|
| `vehicleId` | `int` | 車両ID（`SE_GetObjectId`で取得） |
| `lightType` | `int` | ライトタイプ（下記の表を参照） |

#### ライトタイプ

| 値 | ライトタイプ |
|----|------------|
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

### 戻り値

| 値 | 説明 |
|----|------|
| `0` | OFF（消灯） |
| `1` | ON（点灯） |
| `2` | FLASHING（点滅） |
| `-1` | エラー（車両が見つからない、拡張が登録されていない） |

### 使用例

```cpp
int vehicleId = 0;
int brakeLight = GT_GetLightState(vehicleId, 6); // BRAKE_LIGHTS

if (brakeLight == 1)
{
    std::cout << "Brake light is ON" << std::endl;
}
else if (brakeLight == 0)
{
    std::cout << "Brake light is OFF" << std::endl;
}
else if (brakeLight == -1)
{
    std::cerr << "Error: Vehicle not found or no light extension" << std::endl;
}
```

### 注意事項

- `vehicleId`は、`SE_GetObjectId`で取得した値を使用してください
- 無効な`lightType`を指定した場合、`-1`が返されます

---

## GT_SetExternalLightState

指定した車両のライト状態を外部から設定します。主にFMUや外部コントローラーからの入力反映に使用されます。

### シグネチャ

```c
void GT_SetExternalLightState(int vehicleId, int lightType, int mode);
```

### パラメータ

| パラメータ | 型 | 説明 |
|-----------|---|------|
| `vehicleId` | `int` | 車両ID（`SE_GetObjectId`で取得） |
| `lightType` | `int` | ライトタイプ（`GT_GetLightState`と同じ値） |
| `mode` | `int` | ライトモード（0: OFF, 1: ON, 2: FLASHING） |

### 戻り値

なし

### 説明

この関数は、`VehicleLightExtension`を介してライト状態を更新します。該当車両にExtensionが存在しない場合は自動的に作成されます。

### 使用例

```cpp
// 車両ID 0 のブレーキランプを点灯させる
GT_SetExternalLightState(0, 6, 1);

// 車両ID 1 の左ウィンカーを点滅させる
GT_SetExternalLightState(1, 8, 2);
```

---

## GT_Close

GT_esminiをクリーンアップします。

### シグネチャ

```c
void GT_Close();
```

### パラメータ

なし

### 戻り値

なし

### 説明

`GT_Close`は、以下のリソースを解放します：

- AutoLightコントローラー
- VehicleLightExtension
- その他のGT_esmini内部リソース

### 使用例

```cpp
GT_Init("scenario.xosc", 0);

// シミュレーション実行

GT_Close(); // クリーンアップ
```

### 注意事項

- `GT_Init`を使用した場合、`GT_Close`のみを呼び出してください（`SE_Close`は不要）
- `SE_Init`を使用した場合、`SE_Close`と`GT_Close`の両方を呼び出してください

---

## データ型

### VehicleLightType

ライトタイプを表す列挙型です。

```cpp
enum class VehicleLightType
{
    DAYTIME_RUNNING_LIGHTS,
    LOW_BEAM,
    HIGH_BEAM,
    FOG_LIGHTS,
    FOG_LIGHTS_FRONT,
    FOG_LIGHTS_REAR,
    BRAKE_LIGHTS,
    WARNING_LIGHTS,
    INDICATOR_LEFT,
    INDICATOR_RIGHT,
    REVERSING_LIGHTS,
    LICENSE_PLATE_ILLUMINATION,
    SPECIAL_PURPOSE_LIGHTS
};
```

### LightState

ライトの状態を表す構造体です。

```cpp
struct LightState
{
    enum class Mode
    {
        OFF,
        ON,
        FLASHING
    };
    
    Mode   mode;                // ライトモード
    double luminousIntensity;   // 光度（cd）
    double flashingOnDuration;  // 点滅時の点灯時間（秒）
    double flashingOffDuration; // 点滅時の消灯時間（秒）
    double colorR;              // 色（赤成分、0.0-1.0）
    double colorG;              // 色（緑成分、0.0-1.0）
    double colorB;              // 色（青成分、0.0-1.0）
};
```

## 使用例

### 完全な例

```cpp
#include "GT_esminiLib.hpp"
#include <iostream>

int main(int argc, char** argv)
{
    // シナリオファイルのパス
    const char* xoscFile = (argc > 1) ? argv[1] : "scenario.xosc";
    
    // GT_esmini初期化
    if (GT_Init(xoscFile, 0) != 0)
    {
        std::cerr << "Failed to initialize GT_esmini" << std::endl;
        return 1;
    }
    
    // AutoLight有効化
    GT_EnableAutoLight();
    std::cout << "AutoLight enabled" << std::endl;
    
    // シミュレーション実行
    const double stepTime = 0.05;
    const int steps = 300; // 15秒間
    
    for (int i = 0; i < steps; ++i)
    {
        GT_Step(stepTime);
        
        // 1秒ごとにライト状態を表示
        if (i % 20 == 0)
        {
            int vehicleId = 0;
            int brake = GT_GetLightState(vehicleId, 6);
            int leftInd = GT_GetLightState(vehicleId, 8);
            int rightInd = GT_GetLightState(vehicleId, 9);
            
            std::cout << "Time: " << (i * stepTime) << "s | "
                      << "Brake: " << brake << " | "
                      << "L-Ind: " << leftInd << " | "
                      << "R-Ind: " << rightInd << std::endl;
        }
    }
    
    // クリーンアップ
    GT_Close();
    std::cout << "Simulation completed" << std::endl;
    
    return 0;
}
```

## 次のステップ

- [基本的な使い方](03_basic_usage.md) - APIの使用方法
- [サンプルシナリオ](07_examples.md) - 実用的な使用例

## 関連ドキュメント

- [LightStateAction機能](04_light_state_action.md) - ライト制御の詳細
- [AutoLight機能](05_auto_light.md) - 自動制御の詳細
