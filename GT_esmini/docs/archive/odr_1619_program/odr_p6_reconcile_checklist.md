# P6 ⇔ dev_v0.12 (P7+P8) リコンサイル・チェックリスト

- 作成: 2026-07-05(ユーザー同期ブリーフィング 2026-07-04 の固化)。実施タイミング: **S4–S6 完了後、S7/S8 の PR パッケージ段で 1 回**。
- 背景: feature/odr1619-p6-vj は d05de731(post-P5)分岐 + upstream v3.4.0 マージ済(S0b)。dev_v0.12 はその後 P7(merge 114d9e5f、フォーク 90/150・マーカー20)と P8(merge 4a08c403、92/150・マーカー21)を取り込んだ **v3.3.0 フォークベース**の別線。P8 完了 = 計画上の upstream resync ポイントでもある。
- 方向: **dev_v0.12 → p6-vj へマージ**(P6 線が v3.4.0 ベースラインとガバナンス改修を保持しているため)。

## 衝突面と解決方針

1. **フォーク GT_RoadManager.cpp**: P7 の `[GT_ODR:curvelocal]`×2 + `[GT_ODR:repeat-cubics]`×1、P8 の `[GT_ODR:lane-layers]`×1 を **v3.4.0 フォークへ関数名アンカーで再適用**(マーカー区切りハンク、resync 手順どおり)。P8 hook は :4093 lanes 選択点(`child("lanes")` → `SelectLanesLayer`)。P6 の overlap 残差申告サイト(vj-parse-junction × junc-crossing/junc-abort、vj-lanes × GT_LHT)とは**別サイト** — vj との新規オーバーラップ無しの想定だが、再適用時に census で要機械確認。
2. **CMake `[GT_ODR:cmake]` APPEND リスト**: `OdrObjectExtras.cpp`・`OdrJunctionGeom.cpp`(P7)+ `OdrLaneLayers.cpp`(P8)を追加(現状 `OdrJunctionExtras.cpp` 終端)。マーカー数 2 不変。
3. **odr_side / side model**: `OdrSideExtras.hpp`(P7 object 構造体群 + P8 OdrRoadLaneLayers / lane-link@layer / validity@layer / signal・object temporary・invalidated)、`OdrSideModel.hpp/.cpp`(P8 = merged_lanes_docs + SelectLanesLayer + ParseLaneExtras のマージビュー歩行)。**P6 は OdrSideModel 本体に非接触**(確認済 2026-07-05)— 衝突は `OdrCoverageWhitelist.inc` / `parser_coverage.yaml` の再生成のみ: **union の parser_coverage.yaml から `gen_odr_whitelist.py` で再生成**(P8 時点 676 ペア + P6 の VJ ネイティブ化フリップ)。
4. **★フォーク期待値の表現衝突(最重要)**: P8 は `check_fork_drift.py:64` と `run_odr_conformance.py:90` に定数 92 をハードコード。p6-vj は両者を **§7 2nd-class マニフェスト駆動**(`gt_patch_manifest.load_manifest`)へ置換済み。**解決 = p6-vj のマニフェスト駆動版を採用、P8 の定数編集は破棄**。その上でマニフェスト数値を union へ:
   - `fork_odr_marker_total` 38 → +P7(3)+P8(1)
   - `fork_odr_drift_expect_lines` / `fork_odr_expect_lines` 74 → +P7(15)+P8(2)(v3.4.0 デルタ込み)
   - `fork_file.marker_census` に curvelocal(12)/repeat-cubics(3)/lane-layers(2) 行を追加
   - `legacy_sites` の fork 行番号ピンは P7/P8 再適用でシフト → **再測定**
   - **算術で埋めず `check_core_census.py record-baselines` + 実測で確定**(数値の唯一真実源はマニフェスト、スクリプト埋め込み禁止)
5. **ゴールデン**: マージで JSON は自動解決しない → 統合後に `--update-golden` **単一レビューコミット**で全再生成。P8 追加 = lane_samples_at additive キー(基底2本)/ __temporary 変種2本 / lane_global_ids.json / fixture06 OSI。P7 追加 = RM 23-26/g8 + OSI g4/10/11/26。⚠ **P7 は fixture 23-26 番台を使用 — P6 の 23/23b/23c(handauthored/23_virtual_junction_*)と番号衝突の可能性 → マージ時にファイル名/スラッグ衝突を必ず点検**(ゴールデンのスラッグはパス由来なので実衝突はファイル名重複時のみ)。P6 motion/telemetry ゴールデンは別系統。再生成後、CRLF ゴースト(stat-dirty)を `git diff --ignore-cr-at-eol` で除外し**実変化のみコミット**(P8 で踏んだ罠)。
6. **manifest.yaml フィクスチャ + parser_coverage.yaml**: P7(23-26,g8)+ P8(g1/g2/06 expected_unsupported フリップ、official 変種)+ P6(23/23b/23c、VJ フリップ)の union。**whitelist 追加時は「その属性を XFAIL 期待している全 fixture」を grep で洗う**(P8 で g1 の laneSection@length フリップ漏れを regen FAIL で拾った前例)。

## リコンサイル後の検証(全部)

`check_core_census.py`(二側センサス)+ `check_fork_drift.py`(マニフェスト駆動)/ conformance full / RoadManager_test 126/126(CWD=build/EnvironmentSimulator/Unittest)/ motion+telemetry ゴールデン diff / `run_regression_gate.ps1 -FailOnBehavioral -TelemetryGolden` / validate_catalog。

## 参照

- P8 実装詳細・罠: memory `odr_p8_implementation.md` / 台帳 `gt_roadmanager_patches.md` §8(dev_v0.12 側)
- P6 契約: [odr_p6_virtual_junction_design.md](odr_p6_virtual_junction_design.md)(単一真実源)
- dev_v0.12 top(2026-07-04): 4a08c403(P8)→ 114d9e5f(P7)→ 6f16e2dd(P5)
