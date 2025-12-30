# GT_esmini ライト機能実装 進捗チェックリスト

`000_light_functionality_plan.md` に基づいた実装フェーズごとの進捗状況です。

## Phase 1: GT_esmini 構造準備
- [x] `GT_esmini` ディレクトリ作成
- [x] `ExtraAction.hpp` / `ExtraAction.cpp` 作成 (スタブ)
- [x] `ExtraEntities.hpp` / `ExtraEntities.cpp` 作成 (スタブ)
- [x] `GT_ScenarioReader.hpp` / `GT_ScenarioReader.cpp` 作成 (`ScenarioReader` 継承、スタブ)
- [x] `GT_esmini/CMakeLists.txt` 作成
- [x] 初期ビルド確認 (DLL生成)

## Phase 2: LightStateAction 基本実装
- [x] `GT_ScenarioReader` に `AppearanceAction` パース処理を実装
- [x] `GT_ScenarioReader` に `LightStateAction` パース処理を実装 (`ParseLightStateAction`)
- [x] `OSCLightStateAction` クラスの実装 (`ExtraAction.cpp`)
- [x] `VehicleLightExtension` クラスの実装 (`ExtraEntities.hpp`) ※注: 今回の実装では `OSCLightStateAction` 内で状態を持つ構成としたため、`VehicleLightExtension` は使用せず、代わりに `AutoLightController` 実装時に必要に応じて再検討。
- [x] ユニットテストの実装 (`test_ScenarioReaderParsing.cpp`)
    - [x] 正常系 (Valid Attributes)
    - [x] 準正常系 (Missing Optional Attributes)
    - [x] 異常系 (Invalid Values)
- [x] 統合テストランナーの実装 (`GT_Loader.cpp`)
- [x] テストシナリオ (XOSC) の作成と検証
    - [x] `test_brake_lights.xosc`
    - [x] `test_indicators.xosc`
    - [x] `test_multiple_lights.xosc`
    - [x] `test_error_missing_attribute.xosc`
    - [x] `test_error_invalid_value.xosc`

## Phase 3: AutoLight 機能実装
- [ ] `AutoLightController.hpp` / `AutoLightController.cpp` 作成
    - [ ] ブレーキランプ制御ロジック (減速度検知)
    - [ ] ウインカー制御ロジック (車線変更・右左折検知)
    - [ ] バックライト制御ロジック (後退検知)
- [ ] `GT_esminiLib.hpp` / `GT_esminiLib.cpp` の実装拡張
    - [ ] `AutoLightManager` の実装
    - [ ] `GT_Init` での初期化処理
    - [ ] `GT_Step` での更新処理
    - [ ] `GT_EnableAutoLight` の実装
    - [ ] `--auto-light` 等の起動時引数処理
- [ ] テスト実装
    - [ ] ユニットテスト (`test_AutoLightController.cpp`)
    - [ ] 統合テストシナリオ (`test_auto_light_*.xosc`)
    - [ ] 優先順位テスト (Manual Action vs AutoLight)

## Phase 4: OSI 連携 (オプション)
- [ ] `OSIReporterExtension.hpp` / `OSIReporterExtension.cpp` 作成
- [ ] OSI メッセージへの `LightState` 反映処理の実装
- [ ] テスト実装
    - [ ] OSI 出力確認テスト

## Phase 5: 統合テスト & ドキュメント
- [ ] 既存シナリオへの回帰テスト (Regression Testing)
- [ ] 最終的なドキュメント更新 (`README.md` 等)
- [ ] リリース準備

---
**凡例:**
- [x]: 完了
- [ ]: 未完了
- [/]: 進行中
