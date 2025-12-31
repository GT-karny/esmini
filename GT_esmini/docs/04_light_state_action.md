# LightStateAction機能

このドキュメントでは、OpenSCENARIO v1.2の`LightStateAction`を使用したライト制御について説明します。

## LightStateActionとは

`LightStateAction`は、OpenSCENARIO v1.2で定義されているアクションで、車両のライト状態を明示的に制御します。GT_esminiは、このアクションをサポートしており、XOSCファイル内でライトの点灯・消灯・点滅を指定できます。

## サポートされているライトタイプ

GT_esminiは、以下のライトタイプをサポートしています：

| ライトタイプ | 説明 | OpenSCENARIO値 |
|------------|------|---------------|
| デイタイムランニングライト | 昼間走行灯 | `daytimeRunningLights` |
| ロービーム | 前照灯（下向き） | `lowBeam` |
| ハイビーム | 前照灯（上向き） | `highBeam` |
| フォグライト | フォグランプ（前後共通） | `fogLights` |
| フロントフォグライト | 前部フォグランプ | `fogLightsFront` |
| リアフォグライト | 後部フォグランプ | `fogLightsRear` |
| ブレーキランプ | 制動灯 | `brakeLights` |
| ハザードランプ | 非常点滅表示灯 | `warningLights` |
| 左ウインカー | 左方向指示器 | `indicatorLeft` |
| 右ウインカー | 右方向指示器 | `indicatorRight` |
| バックライト | 後退灯 | `reversingLights` |
| ライセンスプレート照明 | ナンバープレート灯 | `licensePlateIllumination` |
| 特殊用途ライト | その他のライト | `specialPurposeLights` |

## ライトの状態

各ライトは、以下の状態を持つことができます：

### モード (mode)

| モード | 説明 | OpenSCENARIO値 |
|--------|------|---------------|
| OFF | 消灯 | `off` |
| ON | 点灯 | `on` |
| FLASHING | 点滅 | `flashing` |

### オプション属性

| 属性 | 説明 | 単位 | デフォルト値 |
|------|------|------|------------|
| `luminousIntensity` | 光度 | cd (カンデラ) | 0.0 |
| `flashingOnDuration` | 点滅時の点灯時間 | 秒 | 0.0 |
| `flashingOffDuration` | 点滅時の消灯時間 | 秒 | 0.0 |
| `color` (R, G, B) | 色 | 0.0-1.0 | (1.0, 1.0, 1.0) 白 |

> [!NOTE]
> GT_esminiは、これらの属性をパースして保持しますが、現在のバージョンでは視覚的な表現には使用されていません。将来のバージョンで、OSGビューアーでの表示に使用される予定です。

## XOSCファイルでの記述方法

### 基本的な構文

`LightStateAction`は、`AppearanceAction`の子要素として記述します：

```xml
<PrivateAction>
    <AppearanceAction>
        <LightStateAction>
            <LightType>
                <VehicleLight vehicleLightType="brakeLights"/>
            </LightType>
            <LightState mode="on"/>
        </LightStateAction>
    </AppearanceAction>
</PrivateAction>
```

### 完全な例: ブレーキランプを点灯

```xml
<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
    <FileHeader revMajor="1" revMinor="2" date="2024-12-30" description="Brake Light Test"/>
    <ParameterDeclarations/>
    <CatalogLocations/>
    <RoadNetwork>
        <LogicFile filepath="../../resources/xodr/straight_500m.xodr"/>
    </RoadNetwork>
    <Entities>
        <ScenarioObject name="Ego">
            <Vehicle name="car_white" vehicleCategory="car">
                <ParameterDeclarations/>
                <Performance maxSpeed="69" maxAcceleration="10" maxDeceleration="10"/>
                <Axles>
                    <FrontAxle maxSteering="0.5" wheelDiameter="0.6" trackWidth="1.8" positionX="3.1" positionZ="0.3"/>
                    <RearAxle maxSteering="0.0" wheelDiameter="0.6" trackWidth="1.8" positionX="0.0" positionZ="0.3"/>
                </Axles>
                <Properties/>
            </Vehicle>
        </ScenarioObject>
    </Entities>
    <Storyboard>
        <Init>
            <Actions>
                <Private entityRef="Ego">
                    <PrivateAction>
                        <TeleportAction>
                            <Position>
                                <LanePosition roadId="1" laneId="-1" offset="0" s="10"/>
                            </Position>
                        </TeleportAction>
                    </PrivateAction>
                    <PrivateAction>
                        <LongitudinalAction>
                            <SpeedAction>
                                <SpeedActionDynamics dynamicsShape="step" value="0" dynamicsDimension="time"/>
                                <SpeedActionTarget>
                                    <AbsoluteTargetSpeed value="20"/>
                                </SpeedActionTarget>
                            </SpeedAction>
                        </LongitudinalAction>
                    </PrivateAction>
                </Private>
            </Actions>
        </Init>
        <Story name="LightStory">
            <Act name="LightAct">
                <ManeuverGroup maximumExecutionCount="1" name="LightManeuverGroup">
                    <Actors selectTriggeringEntities="false">
                        <EntityRef entityRef="Ego"/>
                    </Actors>
                    <Maneuver name="LightManeuver">
                        <Event name="BrakeLightOnEvent" priority="overwrite">
                            <Action name="BrakeLightOnAction">
                                <PrivateAction>
                                    <AppearanceAction>
                                        <LightStateAction>
                                            <LightType>
                                                <VehicleLight vehicleLightType="brakeLights"/>
                                            </LightType>
                                            <LightState mode="on"/>
                                        </LightStateAction>
                                    </AppearanceAction>
                                </PrivateAction>
                            </Action>
                            <StartTrigger>
                                <ConditionGroup>
                                    <Condition name="BrakeLightOnCondition" delay="0" conditionEdge="rising">
                                        <ByValueCondition>
                                            <SimulationTimeCondition value="2.0" rule="greaterThan"/>
                                        </ByValueCondition>
                                    </Condition>
                                </ConditionGroup>
                            </StartTrigger>
                        </Event>
                    </Maneuver>
                </ManeuverGroup>
                <StartTrigger>
                    <ConditionGroup>
                        <Condition name="ActStartCondition" delay="0" conditionEdge="rising">
                            <ByValueCondition>
                                <SimulationTimeCondition value="0" rule="greaterThan"/>
                            </ByValueCondition>
                        </Condition>
                    </ConditionGroup>
                </StartTrigger>
            </Act>
        </Story>
        <StopTrigger/>
    </Storyboard>
</OpenSCENARIO>
```

このシナリオでは、シミュレーション開始2秒後にブレーキランプが点灯します。

### 点滅ライトの例

ハザードランプを点滅させる例：

```xml
<PrivateAction>
    <AppearanceAction>
        <LightStateAction>
            <LightType>
                <VehicleLight vehicleLightType="warningLights"/>
            </LightType>
            <LightState mode="flashing" 
                        flashingOnDuration="0.5" 
                        flashingOffDuration="0.5"/>
        </LightStateAction>
    </AppearanceAction>
</PrivateAction>
```

### 光度と色を指定する例

フォグライトを黄色で点灯：

```xml
<PrivateAction>
    <AppearanceAction>
        <LightStateAction>
            <LightType>
                <VehicleLight vehicleLightType="fogLightsFront"/>
            </LightType>
            <LightState mode="on" luminousIntensity="1000">
                <Color>
                    <ColorRGB red="1.0" green="0.8" blue="0.0"/>
                </Color>
            </LightState>
        </LightStateAction>
    </AppearanceAction>
</PrivateAction>
```

### 複数のライトを制御する例

複数のライトを同時に制御するには、複数のイベントを作成します：

```xml
<Event name="TurnLeftEvent" priority="overwrite">
    <Action name="LeftIndicatorOnAction">
        <PrivateAction>
            <AppearanceAction>
                <LightStateAction>
                    <LightType>
                        <VehicleLight vehicleLightType="indicatorLeft"/>
                    </LightType>
                    <LightState mode="flashing" 
                                flashingOnDuration="0.5" 
                                flashingOffDuration="0.5"/>
                </LightStateAction>
            </AppearanceAction>
        </PrivateAction>
    </Action>
    <StartTrigger>
        <ConditionGroup>
            <Condition name="TurnLeftCondition" delay="0" conditionEdge="rising">
                <ByValueCondition>
                    <SimulationTimeCondition value="5.0" rule="greaterThan"/>
                </ByValueCondition>
            </Condition>
        </ConditionGroup>
    </StartTrigger>
</Event>

<Event name="TurnLeftEndEvent" priority="overwrite">
    <Action name="LeftIndicatorOffAction">
        <PrivateAction>
            <AppearanceAction>
                <LightStateAction>
                    <LightType>
                        <VehicleLight vehicleLightType="indicatorLeft"/>
                    </LightType>
                    <LightState mode="off"/>
                </LightStateAction>
            </AppearanceAction>
        </PrivateAction>
    </Action>
    <StartTrigger>
        <ConditionGroup>
            <Condition name="TurnLeftEndCondition" delay="0" conditionEdge="rising">
                <ByValueCondition>
                    <SimulationTimeCondition value="8.0" rule="greaterThan"/>
                </ByValueCondition>
            </Condition>
        </ConditionGroup>
    </StartTrigger>
</Event>
```

## AutoLightとの優先順位

`LightStateAction`は、AutoLight機能よりも優先されます：

1. **LightStateActionが実行される前**: AutoLightが有効な場合、自動制御が行われる
2. **LightStateActionが実行された時**: AutoLightの状態を上書きする
3. **LightStateActionが実行された後**: 次のAutoLight更新まで、LightStateActionの状態が保持される

### 例: AutoLightとLightStateActionの組み合わせ

```cpp
// AutoLight有効化
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

// シミュレーション実行
// - 0-2秒: AutoLightがブレーキランプを制御
// - 2秒: LightStateActionでブレーキランプを強制的にOFF
// - 2秒以降: AutoLightが再び制御を開始（減速時に点灯）
```

詳細は[AutoLight機能](05_auto_light.md)を参照してください。

## 実行例

### GT_Loaderを使用した実行

```bash
# Windowsの場合
cd build\bin
.\GT_Loader.exe ..\..\GT_esmini\test\scenarios\test_brake_lights.xosc

# Linux/macOSの場合
cd build/bin
./GT_Loader ../../GT_esmini/test/scenarios/test_brake_lights.xosc
```

### ライト状態の確認

プログラムから状態を確認：

```cpp
GT_Init("test_brake_lights.xosc", 0);

for (int i = 0; i < 100; ++i)
{
    GT_Step(0.05);
    
    int brakeLight = GT_GetLightState(0, 6); // BRAKE_LIGHTS
    if (brakeLight == 1)
    {
        std::cout << "Brake light is ON at time " << (i * 0.05) << "s" << std::endl;
    }
}

GT_Close();
```

## テストシナリオ

GT_esminiには、LightStateActionのテストシナリオが含まれています：

| ファイル | 説明 |
|---------|------|
| `test_brake_lights.xosc` | ブレーキランプの点灯・消灯 |
| `test_indicators.xosc` | ウインカーの点滅 |
| `test_multiple_lights.xosc` | 複数のライトを同時制御 |

これらのシナリオは、`GT_esmini/test/scenarios/`ディレクトリにあります。

## エラーハンドリング

### 無効なライトタイプ

存在しないライトタイプを指定した場合、GT_esminiは警告を出力しますが、シミュレーションは継続されます。

### 無効なモード

`mode`属性に無効な値（`on`、`off`、`flashing`以外）を指定した場合、デフォルトで`off`として扱われます。

### 属性の欠落

必須属性（`vehicleLightType`、`mode`）が欠落している場合、パースエラーが発生し、シミュレーションが停止します。

## 制限事項

現在のバージョンでは、以下の制限があります：

1. **視覚的表現**: ライト状態はOSI出力に反映されますが、OSGビューアーでの視覚的表現はサポートされていません
2. **UserDefinedLight**: `UserDefinedLight`はパースされますが、内部的には使用されません
3. **transitionTime**: `transitionTime`属性はパースされますが、現在は無視されます（即座に状態が変化）

## 次のステップ

- [AutoLight機能](05_auto_light.md) - 自動ライト制御の詳細
- [サンプルシナリオ](07_examples.md) - 実用的な使用例
- [OSI連携](06_osi_integration.md) - OSI出力でのライト状態

## 関連ドキュメント

- [基本的な使い方](03_basic_usage.md) - GT_esminiの基本
- [APIリファレンス](10_api_reference.md) - 関数の詳細仕様
- [OpenSCENARIO v1.2仕様](https://www.asam.net/standards/detail/openscenario/) - 公式仕様書
