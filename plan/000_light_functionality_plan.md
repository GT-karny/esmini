# esminiライト機能実装プラン

## 概要

esminiにライト機能を実装します。以下の2つの機能を実現します：

1. **LightStateAction対応**: OpenSCENARIO v1.2のLightStateActionをパースし、車両のライト状態を制御
2. **AutoLight機能**: 車両の動作に応じて自動的にライトを点灯（ブレーキランプ、ウインカー、バックライト）

## 調査結果サマリー

### OpenSCENARIO v1.2 LightStateAction仕様

- **定義場所**: `thirdparty/openscenario-v1.2.0/Schema/OpenSCENARIO.xsd` (行1354-1366)
- **構造**:
  - `LightStateAction`: `LightType` + `LightState` + `transitionTime`属性
  - `LightType`: `VehicleLight` または `UserDefinedLight`
  - `VehicleLight`: `vehicleLightType`属性（VehicleLightType列挙型）
  - `LightState`: `mode`属性（LightMode）+ オプションで`Color`, `luminousIntensity`, `flashingOnDuration`, `flashingOffDuration`
- **VehicleLightType列挙型** (行628-650):
  - `daytimeRunningLights`, `lowBeam`, `highBeam`
  - `fogLights`, `fogLightsFront`, `fogLightsRear`
  - `brakeLights` ← **AutoLight対象**
  - `warningLights`
  - `indicatorLeft`, `indicatorRight` ← **AutoLight対象**
  - `reversingLights` ← **AutoLight対象（後退時）**
  - `licensePlateIllumination`, `specialPurposeLights`

### esmini既存実装

- **アクション構造**: `EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/Action.hpp`
  - `OSCAction`クラス: `ActionType`列挙型でアクションタイプを管理
  - Private actions: `LONG_SPEED`, `LAT_LANE_CHANGE`, `VISIBILITY`等
  - Global actions: `ENVIRONMENT`, `ADD_ENTITY`等
  - **LightStateActionは未実装**（AppearanceActionも未実装）
- **Object/Vehicleクラス**: `Entities.hpp`
  - `Object`クラス: 基本エンティティクラス
  - `Vehicle`クラス: `Object`を継承
  - **ライト状態を保持するメンバ変数なし**
- **OSI連携**: OSI3のC#コード例に`VehicleClassification.LightState`が存在（`code-examples/osi-groundtruth-cs/osi3/OsiObject.cs`）
  - `IndicatorState`, `BrakeLightState`, `GenericLightState`等のフィールドあり

### 実装方針の決定

**最終方針**：esminiからのファイルコピーを**ゼロ**にし、継承とフック機構で実装します。

**アプローチ**：
1. **継承パターン**: `ScenarioReader`, `ScenarioEngine`を継承した`GT_ScenarioReader`, `GT_ScenarioEngine`を作成
2. **拡張ヘッダー**: `ExtraAction.hpp`, `ExtraEntities.hpp`で拡張定義
3. **フック機構**: 仮想関数のオーバーライドでAppearanceAction等のパース処理を追加
4. **esmini本体への変更**: **完全にゼロ**（ビルドシステムのみ調整）

**メリット**：
- esmini本体のアップデート時、ファイルコピーの同期不要
- GT_esmini独自コードが完全に分離
- 将来的なesmini本体へのコントリビューションが容易
- メンテナンス負荷が最小

## 実装変更内容

### 0. GT_esmini構造準備

#### ディレクトリ構成

```
GT_esmini/
├── ExtraAction.hpp          # ライトアクション拡張定義
├── ExtraAction.cpp          # ライトアクション実装
├── ExtraEntities.hpp        # Vehicleクラス拡張定義
├── ExtraEntities.cpp        # Vehicleクラス拡張実装
├── GT_ScenarioReader.hpp    # ScenarioReader継承クラス
├── GT_ScenarioReader.cpp    # AppearanceActionパース実装
├── GT_ScenarioEngine.hpp    # ScenarioEngine継承クラス（必要に応じて）
├── GT_ScenarioEngine.cpp    # AutoLight更新処理
├── AutoLightController.hpp  # AutoLight機能
├── AutoLightController.cpp
├── GT_esminiLib.hpp         # GT_esmini用ラッパーAPI
├── GT_esminiLib.cpp         # 起動時引数処理等
└── CMakeLists.txt
```

**重要**: esminiからのファイルコピーは**一切なし**。すべて継承で実装。

#### ビルドシステム

##### [NEW] [GT_esmini/CMakeLists.txt](file:///e:/Repository/GT_esmini/esmini/GT_esmini/CMakeLists.txt)

GT_esmini用のCMakeLists.txtを作成し、esmini本体のライブラリをリンク

### 1. LightStateAction対応

#### 新規ファイル（GT_esmini内）

##### [NEW] [ExtraAction.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/ExtraAction.hpp)

esmini本体の`Action.hpp`を拡張する形でライトアクション定義:

```cpp
#pragma once

#include "Action.hpp"  // esmini本体のAction.hppをインクルード

namespace gt_esmini
{
    // ライト状態を表す構造体
    struct LightState
    {
        enum class Mode { OFF, ON, FLASHING };
        
        Mode mode;
        double luminousIntensity;  // optional
        double flashingOnDuration;  // optional (for FLASHING mode)
        double flashingOffDuration; // optional (for FLASHING mode)
        // Color (R, G, B) - optional
        double colorR;
        double colorG;
        double colorB;
    };
    
    // VehicleLightType列挙型
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
    
    // LightStateActionクラス（esminiのOSCActionを継承）
    class OSCLightStateAction : public scenarioengine::OSCAction
    {
    public:
        OSCLightStateAction(scenarioengine::StoryBoardElement* parent)
            : scenarioengine::OSCAction(static_cast<scenarioengine::OSCAction::ActionType>(100), parent)  // 100番台をGT_esmini拡張用に使用
        {}
        
        virtual ~OSCLightStateAction() {}
        
        void Start(double simTime) override;
        void Step(double simTime, double dt) override;
        void End() override;
        
        OSCLightStateAction* Copy();
        std::string Type2Str() override { return "LightStateAction"; }
        
        VehicleLightType lightType_;
        LightState lightState_;
        double transitionTime_;
        
    private:
        double startTime_;
        bool isTransitioning_;
    };
}
```

##### [NEW] [ExtraAction.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/ExtraAction.cpp)

`OSCLightStateAction`の実装

##### [NEW] [ExtraEntities.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/ExtraEntities.hpp)

esmini本体の`Entities.hpp`を拡張する形で Vehicleクラスにライト機能追加:

```cpp
#pragma once

#include "Entities.hpp"  // esmini本体のEntities.hppをインクルード
#include "ExtraAction.hpp"

namespace gt_esmini
{
    // Vehicleクラスの拡張（継承ではなくコンポジション）
    class VehicleLightExtension
    {
    public:
        VehicleLightExtension(scenarioengine::Vehicle* vehicle);
        ~VehicleLightExtension();
        
        void SetLightState(VehicleLightType type, const LightState& state);
        LightState GetLightState(VehicleLightType type) const;
        
        void SetAutoLight(bool enabled) { autoLightEnabled_ = enabled; }
        bool IsAutoLightEnabled() const { return autoLightEnabled_; }
        
        // ライト状態を保持
        std::map<VehicleLightType, LightState> lightStates_;
        bool autoLightEnabled_ = false;
        
    private:
        scenarioengine::Vehicle* vehicle_;  // 元のVehicleオブジェクトへの参照
    };
    
    // Vehicle拡張管理クラス（シングルトン）
    class VehicleExtensionManager
    {
    public:
        static VehicleExtensionManager& Instance();
        
        VehicleLightExtension* GetExtension(scenarioengine::Vehicle* vehicle);
        void RegisterExtension(scenarioengine::Vehicle* vehicle, VehicleLightExtension* ext);
        
    private:
        std::map<scenarioengine::Vehicle*, std::unique_ptr<VehicleLightExtension>> extensions_;
    };
}
```

##### [NEW] [ExtraEntities.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/ExtraEntities.cpp)

`VehicleLightExtension`の実装

##### [NEW] [GT_ScenarioReader.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/GT_ScenarioReader.hpp)

esmini本体の`ScenarioReader`を継承してAppearanceActionパース機能を追加:

```cpp
#pragma once

#include "ScenarioReader.hpp"  // esmini本体
#include "ExtraAction.hpp"     // GT_esmini拡張

namespace gt_esmini
{
    class GT_ScenarioReader : public scenarioengine::ScenarioReader
    {
    public:
        GT_ScenarioReader(scenarioengine::Entities* entities, 
                         scenarioengine::Catalogs* catalogs, 
                         scenarioengine::OSCEnvironment* environment, 
                         bool disable_controllers = false)
            : scenarioengine::ScenarioReader(entities, catalogs, environment, disable_controllers)
        {}
        
        virtual ~GT_ScenarioReader() {}
        
    protected:
        // AppearanceActionパース用の拡張ポイント
        // ScenarioReaderのparseOSCPrivateActionをオーバーライド
        // または、新規にパース関数を追加してフック
        
        // LightStateActionパース
        OSCLightStateAction* ParseLightStateAction(pugi::xml_node node);
        
        // AppearanceActionパース（LightStateActionを含む）
        scenarioengine::OSCPrivateAction* ParseAppearanceAction(pugi::xml_node node, 
                                                                scenarioengine::Object* object,
                                                                scenarioengine::Event* parent);
    };
}
```

##### [NEW] [GT_ScenarioReader.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/GT_ScenarioReader.cpp)

AppearanceActionとLightStateActionのパース実装:

```cpp
#include "GT_ScenarioReader.hpp"
#include "ExtraEntities.hpp"

namespace gt_esmini
{
    OSCLightStateAction* GT_ScenarioReader::ParseLightStateAction(pugi::xml_node node)
    {
        OSCLightStateAction* action = new OSCLightStateAction(nullptr);
        
        // LightTypeパース
        pugi::xml_node lightTypeNode = node.child("LightType");
        pugi::xml_node vehicleLightNode = lightTypeNode.child("VehicleLight");
        std::string lightTypeStr = parameters.ReadAttribute(vehicleLightNode, "vehicleLightType");
        
        // 文字列→列挙型変換
        if (lightTypeStr == "brakeLights") action->lightType_ = VehicleLightType::BRAKE_LIGHTS;
        else if (lightTypeStr == "indicatorLeft") action->lightType_ = VehicleLightType::INDICATOR_LEFT;
        // ... 他のライトタイプも同様
        
        // LightStateパース
        pugi::xml_node lightStateNode = node.child("LightState");
        std::string modeStr = parameters.ReadAttribute(lightStateNode, "mode");
        
        if (modeStr == "on") action->lightState_.mode = LightState::Mode::ON;
        else if (modeStr == "off") action->lightState_.mode = LightState::Mode::OFF;
        else if (modeStr == "flashing") action->lightState_.mode = LightState::Mode::FLASHING;
        
        // オプション属性
        if (!lightStateNode.attribute("luminousIntensity").empty())
        {
            action->lightState_.luminousIntensity = std::stod(parameters.ReadAttribute(lightStateNode, "luminousIntensity"));
        }
        
        // transitionTime
        if (!node.attribute("transitionTime").empty())
        {
            action->transitionTime_ = std::stod(parameters.ReadAttribute(node, "transitionTime"));
        }
        
        return action;
    }
    
    scenarioengine::OSCPrivateAction* GT_ScenarioReader::ParseAppearanceAction(
        pugi::xml_node node, 
        scenarioengine::Object* object,
        scenarioengine::Event* parent)
    {
        // AppearanceActionの子要素を確認
        pugi::xml_node appearanceChild = node.first_child();
        
        if (std::string(appearanceChild.name()) == "LightStateAction")
        {
            OSCLightStateAction* action = ParseLightStateAction(appearanceChild);
            // Vehicleにライト拡張を登録
            if (object->type_ == scenarioengine::Object::Type::VEHICLE)
            {
                scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(object);
                VehicleExtensionManager::Instance().RegisterExtension(vehicle, new VehicleLightExtension(vehicle));
            }
            return action;
        }
        // 将来的にAnimationAction等も追加可能
        
        return nullptr;
    }
}
```

**重要**: `ScenarioReader::parseOSCPrivateAction`をオーバーライドするのではなく、
GT_esminiLib側で`GT_ScenarioReader`を使用することで、esmini本体への変更をゼロにします。

---

### 2. AutoLight機能

#### 新規ファイル（GT_esmini内）

##### [NEW] [AutoLightController.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/AutoLightController.hpp)

AutoLight機能のコントローラークラス:

```cpp
#pragma once

#include "Entities.hpp"  // esmini本体
#include "ExtraEntities.hpp"  // GT_esmini拡張

namespace gt_esmini
{
    class AutoLightController
    {
    public:
        AutoLightController(scenarioengine::Vehicle* vehicle, VehicleLightExtension* lightExt);
        ~AutoLightController();
        
        // 毎フレーム呼び出し
        void Update(double dt);
        
        // 設定
        void SetEnabled(bool enabled);
        bool IsEnabled() const { return enabled_; }
        
    private:
        scenarioengine::Vehicle* vehicle_;
        VehicleLightExtension* lightExt_;  // ライト拡張への参照
        bool enabled_;
        
        // ブレーキランプ制御
        void UpdateBrakeLights(double acceleration);
        
        // ウインカー制御
        void UpdateIndicators();
        
        // バックライト制御（新規追加）
        void UpdateReversingLights();
        
        // 前回の状態
        double prevSpeed_;
        int prevLaneId_;
        double laneChangeStartTime_;
        bool isInLaneChange_;
    };
}
```

##### [NEW] [AutoLightController.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/AutoLightController.cpp)

実装内容:

- **ブレーキランプ**: 加速度が-0.1G（約-0.98 m/s²）以下で点灯
- **ウインカー**: 
  - 車線変更検出: `laneId`の変化を監視
  - 交差点検出: `junctionId`が有効な場合、進行方向から左右折を判定
  - 点灯タイミング: 車線変更/右左折開始時に点灯、完了後消灯
- **バックライト（reversingLights）**: 速度が負（後退中）の場合に点灯

---

### 3. 起動時引数対応

##### [NEW] [GT_esminiLib.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/GT_esminiLib.cpp)

GT_esmini専用のラッパーライブラリを作成し、`GT_ScenarioReader`を使用:

```cpp
#include "esminiLib.hpp"  // esmini本体
#include "GT_ScenarioReader.hpp"
#include "ExtraEntities.hpp"
#include "AutoLightController.hpp"

namespace gt_esmini
{
    // GT_esmini用のプレイヤー管理
    static gt_esmini::GT_ScenarioReader* gt_reader = nullptr;
    
    // AutoLightController管理クラス
    class AutoLightManager
    {
    public:
        static AutoLightManager& Instance()
        {
            static AutoLightManager instance;
            return instance;
        }
        
        void InitAutoLight(scenarioengine::Entities* entities)
        {
            for (size_t i = 0; i < entities->object_pool_.size(); i++)
            {
                scenarioengine::Object* obj = entities->object_pool_[i];
                if (obj->type_ == scenarioengine::Object::Type::VEHICLE)
                {
                    scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(obj);
                    auto* lightExt = VehicleExtensionManager::Instance().GetExtension(vehicle);
                    if (!lightExt)
                    {
                        lightExt = new VehicleLightExtension(vehicle);
                        VehicleExtensionManager::Instance().RegisterExtension(vehicle, lightExt);
                    }
                    lightExt->SetAutoLight(true);
                    controllers_[vehicle] = std::make_unique<AutoLightController>(vehicle, lightExt);
                }
            }
        }
        
        void UpdateAutoLight(double dt)
        {
            for (auto& pair : controllers_)
            {
                pair.second->Update(dt);
            }
        }
        
    private:
        std::map<scenarioengine::Vehicle*, std::unique_ptr<AutoLightController>> controllers_;
    };
    
    // GT_esmini初期化関数（esminiのSE_Init代わり）
    int GT_Init(const char* oscFilename, int disable_ctrls)
    {
        // esmini本体の初期化処理を参考に、GT_ScenarioReaderを使用
        // ...
        
        // --auto-light引数チェック
        if (SE_Env::Inst().GetOptions().IsOptionArgumentSet("auto-light"))
        {
            // 初期化は後で（エンティティ作成後）
        }
        
        return 0;
    }
    
    // GT_esmini更新関数（esminiのSE_Step後に呼び出し）
    void GT_Step(double dt)
    {
        AutoLightManager::Instance().UpdateAutoLight(dt);
    }
    
    // GT_esmini用のAutoLight有効化
    void GT_EnableAutoLight()
    {
        // SE_GetScenarioEngine()からentitiesを取得
        // AutoLightManager::Instance().InitAutoLight(entities);
    }
}
```

**使用方法**:

**オプション1: GT_Init を使用（推奨）**
```cpp
// GT_ScenarioReader を使用してAppearanceAction対応
gt_esmini::GT_Init("scenario.xosc", 0);
if (auto_light_enabled)
{
    gt_esmini::GT_EnableAutoLight();
}

while (running)
{
    SE_Step();  // esmini本体のステップ
    gt_esmini::GT_Step(dt);  // GT_esmini拡張のステップ
}
```

**オプション2: SE_Init を使用（既存機能のみ）**
```cpp
// esmini本体の機能のみ使用
SE_Init("scenario.xosc", 0);

// GT_esmini機能を追加で有効化
if (auto_light_enabled)
{
    gt_esmini::GT_EnableAutoLight();  // 既存のentitiesに対してAutoLight有効化
}

while (running)
{
    SE_Step();  // esmini本体のステップ
    gt_esmini::GT_Step(dt);  // GT_esmini拡張のステップ
}
```

**質問への回答**:
> **Q: esminiにもとからある機能はSE_Init、追加機能だけGT_***にすることはできる？**
> 
> **A: 可能です。**
> - `SE_Init`を使用してesmini本体の機能でシナリオを読み込み
> - その後、`gt_esmini::GT_EnableAutoLight()`等のGT_esmini機能を追加で有効化
> - ただし、**LightStateActionのパース機能は`GT_Init`を使用しないと利用できません**
> - AutoLight機能のみ使いたい場合は`SE_Init` + `GT_EnableAutoLight()`で可能
> - LightStateActionも使いたい場合は`GT_Init`が必要

**重要**: GT_esminiLibは別ライブラリとして提供されるため、
既存のesminiアプリケーションに影響を与えません。

---

### 4. OSI連携（Phase 4で実装予定）

**方針**: GT_esmini内でOSI出力を拡張

##### [NEW] [GT_esmini/OSIReporterExtension.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/OSIReporterExtension.hpp)

```cpp
#pragma once

#include "OSIReporter.hpp"  // esmini本体
#include "ExtraEntities.hpp"

namespace gt_esmini
{
    class OSIReporterExtension
    {
    public:
        // OSI出力にライト状態を追加
        static void UpdateLightState(osi3::MovingObject* movingObject, 
                                     scenarioengine::Vehicle* vehicle);
    };
}
```

実装:
```cpp
void OSIReporterExtension::UpdateLightState(osi3::MovingObject* movingObject, 
                                           scenarioengine::Vehicle* vehicle)
{
    auto* lightExt = VehicleExtensionManager::Instance().GetExtension(vehicle);
    if (!lightExt) return;
    
    // Brake lights
    auto brakeLightState = lightExt->GetLightState(VehicleLightType::BRAKE_LIGHTS);
    if (brakeLightState.mode == LightState::Mode::ON)
    {
        movingObject->mutable_vehicle_classification()->mutable_light_state()->set_brake_light_state(
            osi3::MovingObject::VehicleClassification::LightState::BRAKE_LIGHT_STATE_NORMAL);
    }
    
    // Indicators, reversing lights等も同様に設定
}
```

**注**: Phase 4で実装。Phase 1-3では省略可能。

---

## ユーザーレビュー必須事項

> [!IMPORTANT]
> **実装方針の確認（最終版）**
> 
> ユーザーフィードバックを反映した最終方針：
> 
> 1. **esmini本体への変更**: **完全にゼロ**（ビルドシステムのみ調整）
> 2. **ファイルコピー**: **完全にゼロ**（継承パターンで実装）
> 3. **継承による拡張**: `GT_ScenarioReader`が`ScenarioReader`を継承
> 4. **拡張ヘッダー**: `ExtraAction.hpp`, `ExtraEntities.hpp`で拡張定義
> 5. **AutoLight対象**: ブレーキランプ、ウインカー、バックライト（reversingLights）
> 6. **AppearanceAction**: GT_ScenarioReaderでパース実装
> 7. **OSI連携**: Phase 4で実装（後回し可）
>
> **esminiアップデート時の影響**:
> - esmini本体のファイルは一切コピーしていないため、アップデートの影響を受けにくい
> - `ScenarioReader`のインターフェースが変更された場合のみ、`GT_ScenarioReader`の調整が必要
> - ただし、publicインターフェースは安定しているため、影響は最小限

> [!WARNING]
> **AppearanceActionのパース実装**
> 
> `AppearanceAction`のパース処理も実装します：
> 
> - `GT_ScenarioReader::ParseAppearanceAction`メソッドで実装
> - `LightStateAction`を子要素として処理
> - 将来的に`AnimationAction`等の他の子要素も追加可能な設計
> - esmini本体への変更は**ゼロ**（継承とオーバーライドで実現）

> [!CAUTION]
> **AutoLight機能の動作仕様（最終版）**
> 
> ユーザーフィードバックを反映した最終仕様：
> 
> - **ブレーキランプ**: 加速度-0.1G（約-0.98 m/s²）以下で点灯
> - **ウインカー**: 
>   - 車線変更時: `laneId`変化を検出して点灯
>   - 交差点右左折時: `junctionId`有効時に進行方向から判定
> - **バックライト（reversingLights）**: 速度が負（後退中）の場合に点灯
> - **優先順位**: 
>   - **LightStateActionが実行された場合、AutoLightの状態を上書き**
>   - AutoLightで点灯していたライトも、LightStateActionで消灯可能
>   - LightStateActionで設定された状態は、次のAutoLight更新まで保持
>   - 例: AutoLightでブレーキランプ点灯中 → LightStateActionで消灯 → AutoLightが再度点灯判定

## 検証プラン

### 自動テスト戦略

**テストディレクトリ構成**:
```
GT_esmini/test/
├── unit/                          # ユニットテスト
│   ├── test_AutoLightController.cpp
│   ├── test_VehicleLightExtension.cpp
│   ├── test_LightStateAction.cpp
│   └── CMakeLists.txt
├── integration/                   # 統合テスト
│   ├── test_LightStateActionParsing.cpp
│   ├── test_AutoLightIntegration.cpp
│   └── CMakeLists.txt
├── scenarios/                     # テストシナリオ（XOSC）
│   ├── test_light_state_action.xosc
│   ├── test_auto_light_brake.xosc
│   ├── test_auto_light_indicator.xosc
│   ├── test_auto_light_reversing.xosc
│   ├── test_light_priority.xosc
│   └── README.md
├── scripts/                       # テストスクリプト
│   ├── run_all_tests.py
│   ├── verify_light_output.py
│   └── compare_expected.py
└── CMakeLists.txt
```

### 1. ユニットテスト（Google Test使用）

#### [NEW] [GT_esmini/test/unit/test_AutoLightController.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/unit/test_AutoLightController.cpp)

#### [NEW] [GT_esmini/test/unit/test_VehicleLightExtension.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/unit/test_VehicleLightExtension.cpp)

#### [NEW] [GT_esmini/test/unit/test_LightStateAction.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/unit/test_LightStateAction.cpp)

### 2. 統合テスト

#### [NEW] [GT_esmini/test/integration/test_LightStateActionParsing.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/integration/test_LightStateActionParsing.cpp)

### 3. シナリオベーステスト

#### [NEW] [GT_esmini/test/scenarios/test_light_state_action.xosc](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/scenarios/test_light_state_action.xosc)

#### [NEW] [GT_esmini/test/scripts/run_all_tests.py](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/scripts/run_all_tests.py)

### 4. テスト実行方法

```bash
# ユニットテストのみ実行
cd GT_esmini/build
ctest --output-on-failure

# 全自動テスト実行
cd GT_esmini/test
python scripts/run_all_tests.py
```

### 5. 手動テスト（補完）

---

## 実装順序

1. **Phase 1: GT_esmini構造準備**
   - GT_esminiディレクトリ作成
   - `ExtraAction.hpp/cpp`, `ExtraEntities.hpp/cpp`作成
   - `GT_ScenarioReader.hpp/cpp`作成（ScenarioReader継承）
   - CMakeLists.txt作成
   - ビルド確認

2. **Phase 2: LightStateAction基本実装**
   - `GT_ScenarioReader`にAppearanceActionパース実装
   - `GT_ScenarioReader`にLightStateActionパース実装
   - `VehicleLightExtension`実装
   - テスト: LightStateAction機能テスト

3. **Phase 3: AutoLight機能実装**
   - `AutoLightController.hpp/cpp`作成
   - `GT_esminiLib.hpp/cpp`作成（起動時引数処理、AutoLight管理）
   - バックライト（reversingLights）対応
   - テスト: AutoLight機能テスト（ブレーキ、ウインカー、バック）、優先順位テスト

4. **Phase 4: OSI連携（後回し可）**
   - `OSIReporterExtension.hpp/cpp`作成
   - テスト: OSI出力確認

5. **Phase 5: 統合テスト**
   - 既存シナリオの回帰テスト
   - ドキュメント更新
