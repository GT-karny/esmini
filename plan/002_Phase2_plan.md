# Phase 2: LightStateAction実装プラン

## 概要

OpenSCENARIO v1.2の`LightStateAction`のパースと実行機能を実装します。Phase 1で作成したGT_esminiの構造を基に、実際のライト制御機能を追加します。

## ユーザーレビュー必須事項

> [!IMPORTANT]
> **実装アプローチ（修正版）**
> 
> Phase 1で作成したスタブ実装を完成させます：
> - `GT_ScenarioReader::parseOSCPrivateAction`: オーバーライドしてAppearanceActionを処理
> - `GT_ScenarioReader::ParseLightStateAction`: XMLからLightStateActionをパース
> - `GT_ScenarioReader::ParseAppearanceAction`: AppearanceActionをパース
> - `OSCLightStateAction::Start/Step/End`: ライト状態変更の実行ロジック
> 
> **esmini既存機能の保持**: `parseOSCPrivateAction`内で、AppearanceAction以外は親クラスの実装を呼び出すことで、esminiの全機能をそのまま維持します。

> [!NOTE]
> **parseOSCPrivateActionのオーバーライド方法**
> 
> esminiの`ScenarioReader::parseOSCPrivateAction`は`virtual`ではありませんが、C++の名前隠蔽（name hiding）を利用してオーバーライドできます：
> 
> ```cpp
> // GT_ScenarioReader.hpp
> class GT_ScenarioReader : public scenarioengine::ScenarioReader
> {
> public:
>     // 親クラスのメソッドを隠蔽
>     scenarioengine::OSCPrivateAction* parseOSCPrivateAction(
>         pugi::xml_node actionNode, 
>         scenarioengine::Object* object, 
>         scenarioengine::Event* parent);
> };
> 
> // GT_ScenarioReader.cpp
> OSCPrivateAction* GT_ScenarioReader::parseOSCPrivateAction(
>     pugi::xml_node actionNode, Object* object, Event* parent)
> {
>     // AppearanceActionをチェック
>     for (pugi::xml_node actionChild = actionNode.first_child(); 
>          actionChild; actionChild = actionChild.next_sibling())
>     {
>         if (actionChild.name() == std::string("AppearanceAction"))
>         {
>             return ParseAppearanceAction(actionChild, object, parent);
>         }
>     }
>     
>     // その他のアクションは親クラスに委譲
>     return ScenarioReader::parseOSCPrivateAction(actionNode, object, parent);
> }
> ```
> 
> この方法により、**esminiの既存機能は完全に保持**され、AppearanceActionのみGT_esminiで処理されます。

## 提案する変更

### GT_esmini

#### [MODIFY] [GT_ScenarioReader.hpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/GT_ScenarioReader.hpp)

`parseOSCPrivateAction`メソッドを追加して親クラスのメソッドを隠蔽します。

```cpp
public:
    // 親クラスのメソッドを隠蔽してAppearanceAction対応を追加
    scenarioengine::OSCPrivateAction* parseOSCPrivateAction(
        pugi::xml_node actionNode, 
        scenarioengine::Object* object, 
        scenarioengine::Event* parent);
```

---

#### [MODIFY] [GT_ScenarioReader.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/GT_ScenarioReader.cpp)

**parseOSCPrivateAction**（新規追加）:
- AppearanceActionをチェック
- 見つかった場合、`ParseAppearanceAction`を呼び出し
- その他のアクションは親クラスの実装に委譲

**ParseLightStateAction**（スタブ実装を完成）:
- `LightType/VehicleLight`ノードから`vehicleLightType`属性を読み取り
- 文字列→`VehicleLightType`列挙型への変換（全13種類対応）
- `LightState`ノードから`mode`属性を読み取り（"on", "off", "flashing"）
- オプション属性の読み取り:
  - `luminousIntensity`: 光度（cd）
  - `flashingOnDuration`: 点滅ON時間（秒）
  - `flashingOffDuration`: 点滅OFF時間（秒）
  - `Color`: RGB値（`r`, `g`, `b`属性）
- `transitionTime`属性の読み取り
- `OSCLightStateAction`オブジェクトの作成と返却
- **エラーハンドリング**: 
  - 必須属性（`vehicleLightType`, `mode`）が欠けている場合は`LOG_ERROR`を出力してnullptrを返却
  - 不正な列挙値の場合は`LOG_WARN`を出力してデフォルト値を使用

**ParseAppearanceAction**（スタブ実装を完成）:
- AppearanceActionの子要素を確認
- `LightStateAction`の場合、`ParseLightStateAction`を呼び出し
- Vehicleオブジェクトに`VehicleLightExtension`を登録（未登録の場合のみ）
- パースしたアクションを返却
- **エラーハンドリング**: 
  - 子要素が不明な場合は`LOG_WARN`を出力してnullptrを返却

**技術的詳細**:
- `parameters.ReadAttribute()`を使用してXML属性を読み取り
- esminiの既存パース関数（`strtod`, `strtoi`等）を活用
- ログ出力は`LOG_INFO`, `LOG_WARN`, `LOG_ERROR`マクロを使用
- **ログレベル制御**: 環境変数`GT_ESMINI_LOG_LEVEL`で制御可能（将来的に実装）

---

#### [MODIFY] [ExtraAction.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/ExtraAction.cpp)

`OSCLightStateAction`のスタブ実装を完成させます。

**Start(double simTime)**:
- `startTime_`を記録
- `object_`メンバから対象Vehicleを取得
- `VehicleExtensionManager`から`VehicleLightExtension`を取得
- `transitionTime_`が0の場合、即座にライト状態を適用
- `transitionTime_`が0より大きい場合、`isTransitioning_`をtrueに設定
- `SetLightState()`でライト状態を設定
- **ログ出力**: `LOG_INFO("LightStateAction started: vehicle={}, lightType={}, mode={}")`（デバッグモード時のみ）

**Step(double simTime, double dt)**:
- `isTransitioning_`がtrueの場合、経過時間を確認
- `transitionTime_`経過後、`isTransitioning_`をfalseに設定
- 現在はシンプルに即座に適用（将来的にフェード効果を追加可能）

**End()**:
- トランジション中の場合、即座に最終状態を適用
- **ログ出力**: `LOG_INFO("LightStateAction ended")`（デバッグモード時のみ）

**技術的詳細**:
- `object_`メンバ（親クラスから継承）を使用して対象Vehicleを取得
- **エラーハンドリング**: 
  - Vehicleが見つからない場合は`LOG_ERROR`を出力してアクションを中止
  - VehicleLightExtensionが見つからない場合は`LOG_ERROR`を出力

**ログ出力の制御**:
```cpp
#ifdef GT_ESMINI_DEBUG_LOGGING
    LOG_INFO("LightStateAction started: vehicle={}, lightType={}, mode={}", 
             vehicle->GetName(), 
             static_cast<int>(lightType_), 
             static_cast<int>(lightState_.mode));
#endif
```

---

### テストシナリオ

#### [NEW] [test_light_state_action.xosc](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/scenarios/test_light_state_action.xosc)

LightStateActionをテストするためのOpenSCENARIOファイルを作成します。

**シナリオ内容**:
- 単一車両（Ego）
- 3つのイベント:
  1. t=1.0s: ブレーキランプをON（transitionTime=0）
  2. t=3.0s: 左ウインカーをFLASHING（flashingOnDuration=0.5, flashingOffDuration=0.5）
  3. t=5.0s: すべてのライトをOFF

**OpenSCENARIO v1.2構造**:
```xml
<AppearanceAction>
  <LightStateAction transitionTime="0.0">
    <LightType>
      <VehicleLight vehicleLightType="brakeLights"/>
    </LightType>
    <LightState mode="on"/>
  </LightStateAction>
</AppearanceAction>
```

---

## 検証プラン

### 自動テスト（Phase 2で実装）

#### 1. ユニットテスト（Google Test）

##### [NEW] [test/unit/test_LightStateAction.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/unit/test_LightStateAction.cpp)

`OSCLightStateAction`のユニットテスト。Start/Step/Endの動作を検証します。

##### [NEW] [test/unit/test_ScenarioReaderParsing.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/unit/test_ScenarioReaderParsing.cpp)

`GT_ScenarioReader`のパース機能のユニットテスト。正常系とエラーハンドリングを検証します。

---

#### 2. 統合テスト（シナリオベース）

##### [NEW] [test/integration/test_LightStateActionIntegration.cpp](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/integration/test_LightStateActionIntegration.cpp)

実際のXOSCファイルを読み込んで、エンドツーエンドでテストします。

**テストケース**:
1. **ParseAndExecute_BrakeLights**: ブレーキランプONのシナリオを読み込み、実行後にライト状態を検証
2. **ParseAndExecute_Indicators**: ウインカーFLASHINGのシナリオを検証
3. **ParseAndExecute_MultipleActions**: 複数のLightStateActionを含むシナリオを検証
4. **ErrorHandling_MissingAttribute**: 必須属性が欠けたXOSCファイルでエラーハンドリングを検証
5. **ErrorHandling_InvalidValue**: 不正な値を含むXOSCファイルでエラーハンドリングを検証

**実装例**:
```cpp
TEST(LightStateActionIntegration, ParseAndExecute_BrakeLights)
{
    // GT_ScenarioReaderでシナリオを読み込み
    GT_ScenarioReader reader(...);
    reader.loadOSCFile("test/scenarios/test_brake_lights.xosc");
    
    // シナリオを実行
    ScenarioEngine engine;
    engine.step(1.5);  // t=1.0sのイベント後
    
    // ライト状態を検証
    Vehicle* vehicle = entities->GetObjectByName("Ego");
    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
    ASSERT_NE(ext, nullptr);
    
    LightState state = ext->GetLightState(VehicleLightType::BRAKE_LIGHTS);
    EXPECT_EQ(state.mode, LightState::Mode::ON);
}
```

---

#### 3. テストシナリオ（XOSC）

##### [NEW] [test/scenarios/test_brake_lights.xosc](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/scenarios/test_brake_lights.xosc)

ブレーキランプONのテストシナリオ。

##### [NEW] [test/scenarios/test_indicators.xosc](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/scenarios/test_indicators.xosc)

ウインカーFLASHINGのテストシナリオ。

##### [NEW] [test/scenarios/test_multiple_lights.xosc](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/scenarios/test_multiple_lights.xosc)

複数のライトアクションを含むテストシナリオ。

##### [NEW] [test/scenarios/test_error_missing_attribute.xosc](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/scenarios/test_error_missing_attribute.xosc)

エラーハンドリングテスト用（必須属性欠落）。

##### [NEW] [test/scenarios/test_error_invalid_value.xosc](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/scenarios/test_error_invalid_value.xosc)

エラーハンドリングテスト用（不正な値）。

---

#### 4. ビルド設定

##### [NEW] [test/unit/CMakeLists.txt](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/unit/CMakeLists.txt)

ユニットテストのビルド設定。

##### [NEW] [test/integration/CMakeLists.txt](file:///e:/Repository/GT_esmini/esmini/GT_esmini/test/integration/CMakeLists.txt)

統合テストのビルド設定。

---

#### 5. テスト実行

**すべてのテストを実行**:
```powershell
cd e:\Repository\GT_esmini\esmini\GT_esmini\build
ctest --output-on-failure
```

**特定のテストのみ実行**:
```powershell
ctest -R LightStateAction --output-on-failure
```

**期待結果**: すべてのテストがPASSし、エラーハンドリングテストで適切なエラーメッセージが出力される

---

### ユーザー確認事項

> [!NOTE]
> **確認事項**
> 
> 1. **テストシナリオの内容**: 上記のテストシナリオ（ブレーキランプ、ウインカー、複数ライト、エラーケース2種）で十分ですか？
> 
> 2. **ログ出力の制御**: デバッグログは`#ifdef GT_ESMINI_DEBUG_LOGGING`で制御します。リリースビルドでは出力されません。この方式でよろしいですか？

---

## 実装順序

1. **GT_ScenarioReader.hpp**: `parseOSCPrivateAction`メソッド宣言を追加
2. **GT_ScenarioReader.cpp**: `parseOSCPrivateAction`, `ParseLightStateAction`, `ParseAppearanceAction`の実装
3. **ExtraAction.cpp**: `OSCLightStateAction`のStart/Step/Endの実装
4. **テストシナリオ**: 5つのXOSCファイルを作成（正常系3つ、エラー系2つ）
5. **ユニットテスト**: `test_LightStateAction.cpp`, `test_ScenarioReaderParsing.cpp`の作成
6. **統合テスト**: `test_LightStateActionIntegration.cpp`の作成
7. **CMakeLists.txt**: テストのビルド設定
8. **ビルドとテスト**: `ctest --output-on-failure`で全テスト実行
9. **デバッグと修正**: 問題があれば修正

---

## 技術的な注意事項

### 名前隠蔽（Name Hiding）の利用

`GT_ScenarioReader::parseOSCPrivateAction`は`virtual`でない親クラスのメソッドを隠蔽します。これにより、GT_ScenarioReaderを使用する場合のみAppearanceActionが処理され、esmini本体の動作には影響しません。

**注意点**:
- `GT_ScenarioReader`を使用する場合、必ず`GT_ScenarioReader*`型で扱う必要があります
- `ScenarioReader*`型にアップキャストすると、親クラスの実装が呼ばれます

### XMLパースの互換性

OpenSCENARIO v1.2の仕様に準拠し、esminiの既存パース関数（`parameters.ReadAttribute()`等）を使用します。

### メモリ管理

- `OSCLightStateAction`オブジェクトはesminiのシナリオエンジンが管理
- `VehicleLightExtension`は`VehicleExtensionManager`がunique_ptrで管理
- 手動deleteは不要
