# ウォークスルー：AutoLight機能の実装

## 概要
このフェーズでは、ライト機能を備えたesminiの拡張エントリーポイントとなる `AutoLight` 機能と `GT_esminiLib` ライブラリの実装に焦点を当てました。実装では以下の統合に成功しました：
- **AutoLightController**：ブレーキランプ、バックランプ、およびウィンカーの自動制御ロジック。
- **GT_ScenarioReader Extensions**：標準のesminiが無視する `LightStateAction` (AppearanceAction) をロードするための「デルタ解析（Delta Parsing）」戦略。
- **GT_esminiLib**：esminiのコアロジックをラップし、`GT_Init`、`GT_Step` などをエクスポートする共有ライブラリ。

## 実装の詳細

### GT_esminiLib の構造
コアファイルを変更せずに `esminiLib` を拡張するために、「ソースインクルード」戦略を採用しました：
- `GT_esminiLib.cpp` は `esminiLib.cpp` を直接インクルードしています。
- これにより、esminiの標準APIで使用される内部的な静的 `ScenarioPlayer* player` インスタンスへのアクセスが可能になります。
- `GT_Init` は（同モジュールにコンパイルされた）`SE_Init` を呼び出し、その後追加のセットアップを実行します。

### デルタ解析（Delta Parsing）戦略
`esmini` の標準パーサーは認識しない `PrivateAction` タイプ（`AppearanceAction` など）を無視するため、`GT_Init` で2回目のパスをトリガーします：
1. `SE_Init` が標準の初期化を実行します。
2. `GT_Init` が OpenSCENARIO XML を再ロードします。
3. `GT_ScenarioReader::ParseExtensionActions` が XML と対応する Storyboard オブジェクトを走査します。
4. `OSCLightStateAction` オブジェクトを手動でインスタンス化し、対応する `Event` のアクションリストに注入します。

### コンパイル修正
ビルドプロセス中に解決された主な課題は以下の通りです：
- **シンボルのアクセス性**：`ScenarioEngine` のプライベートメンバ `catalogs` に対するゲッター/アクセッサーを追加しました。
- **メソッドの可用性**：`AutoLightController` に `Enable()` を、`VehicleExtensionManager` に `Clear()` を追加しました。
- **API エクスポート**：`GT_ESMINI_API` が `dllexport`/`dllimport` を正しく処理するようにしました。

## 使用例 (Usage Example)

`GT_esminiLib` を使用して `AutoLightController` を有効にするコード例は以下の通りです。C/C++アプリケーションから `GT_esminiLib.dll` をロードして使用します。

```cpp
#include "GT_esminiLib.hpp"

// ...

// 1. esminiの初期化と拡張機能のロード
// これにより、通常のesmini初期化に加え、ライト拡張アクションの読み込みが行われます
if (GT_Init("my_scenario.xosc", 0) != 0) {
    // エラー処理
    return -1;
}

// 2. AutoLight機能の有効化
// これを呼び出すことで、車両の挙動（減速、後退、旋回）に応じたライト自動制御が開始されます
GT_EnableAutoLight();

// 3. シミュレーションループ
double dt = 0.05;
while (run_simulation) {
    // ステップ実行
    // 物理演算の更新後、AutoLightControllerが各車両のライト状態を更新します
    GT_Step(dt);
    
    // ... 描画処理など ...
}

// 4. 清掃処理
GT_Close();
```

## ビルド検証
`GT_esminiLib` ターゲットは Release 構成で正常にビルドされました：
```powershell
cmake --build build --target GT_esminiLib --config Release
# Exit code: 0
```
これにより、ビルドディレクトリに `GT_esminiLib.dll`（および .lib）が生成されました。

## 検証とテスト結果
実装は、カスタムテストランナー `GT_Loader.exe` と特定のOpenSCENARIOファイル `autolight_test.xosc` を使用して検証されました。

### テストシナリオ (`autolight_test.xosc`)
- **Init**: デルタ解析を通じて `AppearanceAction` (フォグランプ) をロードしようと試みます。
- **イベント 1**: t=2秒付近での急ブレーキ（減速）。
- **イベント 2**: t=6秒付近での後退（負の速度）。

### 検証プロセス
1.  **ビルド**: `GT_esminiLib` と `GT_Loader` の再ビルドに成功しました。
2.  **実行**: `GT_Loader.exe` でシナリオを15秒間実行しました。
3.  **結果 (`verification_final.log`)**:
    - **初期化**: 「サニタイズされた」シナリオのロードに成功し、標準esminiでの `AppearanceAction` エラーを回避しました。
    - **ブレーキランプ**: 減速を自動検出し、ライトが点灯しました。
        - `Time 2s ... Brake: 0`
        - `Time 2.5s ... Brake: 1` (ブレーキイベント開始)
        - `Time 4.5s ... Brake: 0` (ブレーキイベント終了)
    - **バックランプ**: 後退動作を自動検出し、ライトが点灯しました。
        - `Time 6s ... Rev: 0`
        - `Time 6.5s ... Rev: 1` (後退イベント開始)
        - `Time 10.5s ... Rev: 1` (後退中)

### ログ抜粋
```text
Time 2s | ID 0 | Fog: 0 | Brake: 0 | Rev: 0 | L-Ind: 0 | R-Ind: 0
[2.050] [info] BrakeTime: true...
Time 2.5s | ID 0 | Fog: 0 | Brake: 1 | Rev: 0 | L-Ind: 0 | R-Ind: 0
...
[6.050] [info] ReverseTime: true...
Time 6.5s | ID 0 | Fog: 0 | Brake: 1 | Rev: 1 | L-Ind: 0 | R-Ind: 0
```

### 残された課題
今回の検証で判明した、今後の改善が必要な項目は以下の通りです：

1.  **Initアクションの適用と永続化 (Fog: 0 の原因)**
    *   **現象**: デルタ解析で `Init` フェーズのアクションとしてロードされた `AppearanceAction`（フォグランプ点灯）が、シミュレーションログ上で `0` (OFF) のままとなっている。
    *   **分析**: `GT_ScenarioReader` でアクションを手動実行 (`Start(0.0)`) していますが、その後の `esmini` の初期化プロセスや最初の `Step` 更新のタイミングで、状態がリセットまたは上書きされている可能性があります。
    *   **対策**: `GT_Init` とシミュレーションループ開始の間の状態管理を調査し、拡張アクションの効果が確実に持続するように適用タイミングを見直す必要があります。

2.  **ブレーキランプロジックの最適化**
    *   **現象**: バック走行開始時（0km/hから後方へ加速）に、負の加速度が発生するためブレーキランプが一瞬点灯しています（ログ: `Time 6.5s | ... Brake: 1 | Rev: 1`）。
    *   **分析**: 物理的には「負の加速度」ですが、ドライバーの操作としては「アクセル（リバース）」であり、ブレーキは踏んでいません。
    *   **対策**: ギアがリバースに入っている、あるいは速度と加速度の符号が一致している（加速中である）場合はブレーキランプを点灯させないよう、判定ロジックを洗練させる必要があります。

3.  **インジケーター制御の高度化**
    *   現状のレーンチェンジ検知はステートレス（その瞬間の状態のみで判定）であり、実際の車のような「3回点滅」や「完了まで維持」といったステートフルな挙動は未実装です。これらは今後のフェーズでの機能強化項目となります。

## 結論
AutoLight機能と拡張ロードメカニズムの実装が完了し、主要なユースケース（ブレーキ、バック）での動作が検証されました。Initアクションの永続化など一部の統合課題は残されていますが、コアとなる自動ライト制御機能は `GT_esminiLib` を介して利用可能です。
