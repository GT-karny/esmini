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

## 次のステップ
- `GT_esminiLib.dll` にリンクして `GT_Init` を実行するテストアプリケーションを作成（または `GT_Loader` を更新）する。
- `AppearanceAction` を含むシナリオを実行し、ライトが点灯することを確認する。
- `AutoLight` を有効にし、シミュレーション中のブレーキ/ウィンカーの挙動を確認する。
