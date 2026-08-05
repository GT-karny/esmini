# manualdrive_adas バッチの自己決定論性（ベースライン凍結の前提確認）

**測定日**: 2026-08-06 · **状態**: 凍結（一次記録・後から書き換えない）
**対象**: `resources/xosc/verification/manualdrive_adas_batch.yaml`（30 シナリオ）
**ビルド**: commit `2937062d` から Protocol A Release フルリビルド、
`build/GT_esmini/Release/GT_esminiLib.dll`（2026-08-06 01:11）
**目的**: `GT_esmini/test/regression_baseline/manualdrive_adas_expected.yaml` を
凍結してよいかの判定（検証計画 §7-5 の自己決定論性コントロール）。

## なぜ先にこれを測るのか

期待値の凍結は、そのとき出た値を「これが正しい」と宣言する行為である。
凍結する対象が実行ごとに揺れていたら、以後の赤は**退行なのか揺れなのか永久に弁別できない**。
本リポジトリには実際に、バッチ実行を繰り返すと断続的に発生するネイティブクラッシュがある
（`gt_sim_test_intermittent_crash_2026-08-05.md`、指紋＝発生シナリオとフォールトアドレスが
毎回変わる）。これが混入した状態で凍結すると、`error` を正常値として焼き付けるか、
逆にクリーンな実行を「退行」と呼ぶことになる。

## 測定

同一マニフェスト・同一ビルドを **3 回連続**実行した（`test_results/mdadas_freeze1..3`）。

| 観点 | 結果 |
| :--- | :--- |
| バッチ判定 | 3 回とも `overall=pass` / `{pass:30, fail:0, needs-review:0, error:0}` |
| ネイティブクラッシュ | 3 回とも **なし**（`access violation` 0 件） |
| 決定フィールドの一致 | **3 回とも完全一致**（各実行 20,400 フレーム × 30 シナリオ） |
| matcher 判定文字列の一致 | 3 回とも完全一致（`verdict.json` の event/status/detail） |

**決定フィールド**の定義は検証計画 §7-5 に従う——ADAS 調停後の実効ペダル
（`hvd.inputs.throttle` / `brake` / `steering` / `gear`）と、機能行ごとの
`state_name` 列。加えて `driver_override{present, active, reasons}` と
`custom_state` も比較対象に含めた（フェーズB〜Dで足した観測量であり、
ここが揺れれば REQ-AD-028 の主張が揺れる）。

ログに出る `[error] Incoming road N connecting road M failed get lane by id ±k`
は `fabriksgatan_traffic_lights.xodr` のレーン結線に対する既存メッセージで、
バッチの `error` カウントとは別物である（3 回とも同一の 8 行）。

## 比較器そのものの検定（両極性）

「一致した」という観測は、**比較器が差を見つけられることを示してからでないと**意味を持たない。
恒等的に一致を返す比較器でも同じ出力になるためである。

- **正**: 3 実行の実データ → `RESULT: IDENTICAL`（exit 0）
- **負**: `md_lka_drift [left]` の 1 フレームだけ `steering` を **1e-6** ずらした複製
  → `1 frame(s) differ ... first at index 100` を報告（exit 1）

1e-6 の単一フィールド差で発火するので、決定フィールドの実質的な揺れを見逃す余地はない。

## 凍結の判断

上記により (a) 自己決定論性、(b) クラッシュ非混入 の両方が満たされたので、
`mdadas_freeze3` の結果を committed baseline として凍結した
（3 実行が完全一致である以上、どの実行を採っても同一である）。

## 次に赤が出た人へ

`error` が 1 以上で赤くなったときは、**まず `access violation` かどうかを見る**。
そうであれば `deviations` は判定材料にならない——そのシナリオは測定されていないので
比較対象が無い。単独再実行してクリーンに 30/30 が出るかを確認するのが、
既知の断続クラッシュと本物の回帰を分ける最短の手順である。
