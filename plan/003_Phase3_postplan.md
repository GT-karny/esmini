# Phase 3 完了に向けた検証計画

## 現在のステータス
- **実装完了**: `AutoLightController`, `GT_ScenarioReader` (Delta Parsing), `GT_esminiLib`
- **ビルド完了**: Release構成で `GT_esminiLib.dll` の生成に成功
- **ドキュメント**: `003_Phase3_walkthrough.md` を保存済み

## 残された課題
実装されたロジックが、実際のシミュレーション実行時に正しく動作するか（ライト状態が期待通りに遷移するか）を検証する必要があります。
現在のesmini標準ビューアは `VehicleLightExtension` の状態を描画しないため、動作検証は**ログ出力**または**デバッガ/テストコード**を通じた内部ステートの確認を中心に行います。

## 次のステップ（検証プロセス）

### Step 1: テストランナーの作成 (`GT_TestRunner`)
`GT_esminiLib.dll` をリンクし、APIを利用してシミュレーションを実行する簡易C++アプリケーションを作成します。
- **場所**: `e:\Repository\GT_esmini\esmini\GT_esmini\test\GT_TestRunner.cpp` (新規作成)
- **機能**:
    - `GT_Init` でシナリオをロード
    - `GT_EnableAutoLight` をコール
    - `GT_Step` でループ実行
    - **検証用フック**: `VehicleExtensionManager` シングルトンにアクセスし、毎フレーム（または状態変化時）にライトの状態をコンソールに出力して動作を確認する。

### Step 2: テスト用シナリオの作成
各AutoLight機能をトリガーする専用のOpenSCENARIOファイルを作成します。
- **場所**: `e:\Repository\GT_esmini\esmini\GT_esmini\test\scenarios\autolight_test.xosc`
- **構成**:
    1.  **急減速**: 直線道路で加速後、急ブレーキをかけるイベント（ブレーキランプ検証）
    2.  **後退**: 一度停止し、バックする操作（バックランプ検証）
    3.  **交差点右左折**: 交差点で右折/左折を行う経路（ウィンカー検証）
    4.  **AppearanceAction**: `Init` フェーズでのライト点灯指定（デルタ解析の検証）

### Step 3: 検証の実行と修正
1. `GT_TestRunner` をビルド・実行する。
2. ログを確認し、適切なタイミングで `BRAKE_LIGHTS`, `REVERSING_LIGHTS`, `INDICATOR` が ON/OFF しているか確認する。
3. 必要に応じてパラメータ（減速検知の閾値、ステアリング角の閾値など）を調整する。

### Step 4: 完了報告
検証結果を記録し、Phase 3 の完了とする。
次のPhase 4（OSI統合など）へ進むための基盤が整う。

## スケジュール案
1. **[直近]** `GT_TestRunner` の実装とビルド設定 (CMakeLists.txt更新)
2. **[直近]** テストシナリオ `autolight_test.xosc` の作成
3. **[実行]** 動作確認とパラメータ調整
