# upstream resync チェックリスト(ODR 1.6-1.9 パッチ再適用手順)

- 作成: 2026-07-05(P9b、計画 §5 P9b の命名成果物)
- 対象: 第1種(`GT_RoadManager.cpp` フォーク、100/150行・マーカー21種)+第2種(in-place コア編集: RoadManager.hpp/.cpp、LaneIndependentRouter.cpp/.hpp、ControllerLooming.cpp、予約 OSIReporter.cpp)+ R1 CMake 例外 3件([GT_ODR:cmake]×2箇所 / [GT_ODR:osi-path])
- 機械真実源: [gt_roadmanager_patches.md](gt_roadmanager_patches.md) §7 の fenced YAML manifest(予算・census・blob SHA)。**本チェックリストは手順、数値は常に manifest が正**。
- リハーサル実績: `scripts/odr_resync_rehearsal.py` が v3.4.0 スナップショットへの全ハンク乾式再適用を機械証明(2026-07-05: 7ファイル/126ハンク PASS、レポート `GT_esmini/test/odr_fixtures/reports/resync_rehearsal.md` にファイル別の関数アンカー×マーカー一覧)。次回 sync 前に**必ず再実行**し、着手時点のハンク台帳を確定すること。

## 0. 事前(サイクル見積り: 0.5-2日/回、P6設計書§5)

1. [ ] `scripts/odr_resync_rehearsal.py` 実行 → PASS + 最新ハンク台帳(anchor×marker 表)を取得。
2. [ ] `scripts/check_core_census.py` / `check_fork_drift.py` / `check_resync_guards.py` が現状態で緑(汚れた状態から sync を始めない)。
3. [ ] conformance full + unit ctest 緑のベースラインコミットを確保(`--update-golden` はしない)。
4. [ ] upstream の対象タグを決定(例 v3.5.0)。`git log v3.4.0..v3.5.0 -- EnvironmentSimulator/Modules/RoadManager` で対象ファイルの改変量を見積る。**MoveToConnectingRoad / MoveAlongS / RoadPath::Calculate / Route::SetTrackS に触れるコミットは3-wayレビュー最優先**(P6設計書§5 Merge playbook)。

## 1. 新規コピーの取得と第2種の再適用

5. [ ] 各第2種ファイルについて `git show <new-tag>:<path>` を新規コピーとして取得。
6. [ ] リハーサルレポートのハンク台帳を上から順に、**関数名アンカー**で位置決めして再適用(行番号は使わない)。ブロック形マーカー(`[GT_ODR:<id>-begin/-end]`)はブロック全体を1ハンクとして移植。
7. [ ] upstream が当該関数を改変していた場合は 3-way レビュー: (a) upstream 変更を採用しつつ GT ハンクを意味論的に再配置、(b) 判断に迷う場合は take-theirs + INTERPRETIVE ゴールデン再基準化(§4)。**コードは無条件 take-theirs が原則**(P6設計書§5 Convergence)。
8. [ ] `RoadManager.hpp` は **additive-only** 制約を維持(manifest `additive_only: true`)。
9. [ ] フォーク(第1種): upstream の新 `RoadManager.cpp` を `GT_RoadManager.cpp` へ全量コピーし直し、リハーサル台帳のフォーク60ハンク(マーカー21種+ヘッダコメント)を関数アンカーで再適用。[GT_LHT] は vj-lanes ブロック内在(census 上 vj-lanes 帰属)に注意。
10. [ ] R1 CMake 例外の生存確認: `EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt` の `[GT_ODR:cmake]`×2(odr_side APPEND + include dir)、`support/cmake/common/locations.cmake` の `[GT_ODR:osi-path]`(OSI 3.7.0 フラットパス固定 — upstream 収束不能の恒久例外、詳細は台帳 §0b)。upstream が osi.cmake / locations.cmake を書き換えた場合は zlib 4点セット(台帳 §0b)の要否も再確認。

## 2. handled-by-upstream ホワイトリスト再基準化

11. [ ] upstream 差分から「upstream がネイティブ対応した要素」を列挙(例: PR-1/PR-2/PR-VJ の受理、または独立実装)。
12. [ ] 各該当要素について **GT 側処理を撤去**(フォークハンク削除 or odr_side パーサの当該パス無効化)し、`parser_coverage.yaml` の該当エントリへ `handled_by_upstream: true` を付与 + `evidence` を upstream 行番号へ書き換え。
13. [ ] `gen_odr_whitelist.py` 再生成。**`check_resync_guards.py` が矛盾(handled_by_upstream なのに GT evidence 残存=二重パース)と .inc ドリフトを機械検出**する — 緑になるまで 12 を完遂。
14. [ ] 合成系(crossPath CROSSWALK 900M / bridge 910M / objectReference 920M)を upstream が実装した場合: GT 合成を撤去し、ctest `OdrResyncGuards.*`(ID重複・再パース蓄積の常設ガード)で二重合成が出ないことを確認。

## 3. ベースライン再記録と census

15. [ ] `scripts/check_core_census.py record-baselines --prune` で新タグの blob SHA + スナップショットを再記録(manifest `baseline_upstream_tag` 更新)。
16. [ ] manifest の per-file `marker_census` / `fork_odr_expect_lines` / `fork_odr_drift_expect_lines` を**実測で**更新(算術で書かない — `check_core_census.py` / `check_fork_drift.py --quiet` の実測値を転記)。予算超過(fork>150 / 第2種 per-file)は**着手前にユーザー承認**。
17. [ ] ctest `OdrForkPatches.MarkerCount` / `OdrForkPatches.SecondClassCensus` 緑。

## 4. ゴールデン再生成(単一レビューコミット規約)

18. [ ] まず**再生成せずに** conformance full を実行し、FAIL を分類:
    - CONTRACTUAL ゴールデン(パース保持/接続性/T2パススルー/OSI非交差点分類)の FAIL = **回帰**。コード側を直す(ゴールデンで吸収しない)。
    - INTERPRETIVE(`:interp` — elementDir 逆合成、membership −1、着地ヘディング等)の FAIL = upstream 収束による再基準化対象。
19. [ ] 再基準化が正当と判断した分のみ `--update-golden` を実行し、**ゴールデン差分だけの単一コミット**を作成(コミットメッセージに理由と件数を明記、コード変更と混ぜない — ゴム印化防止)。
20. [ ] motion / telemetry ゴールデン(P6 オラクル)も同一規約。

## 5. 出口ゲート(全部緑で sync 完了)

21. [ ] 全リビルド(**stale-DLL 罠**: ゲート前に必ず対象 DLL 群を再ビルド — P6 S5 インシデント)。
22. [ ] conformance `--profile full --check-matrix`(census / fork-drift / resync-guards 内蔵)。
23. [ ] unit ctest 全緑 + upstream `RoadManager_test` 全数緑(フォークビルドで)。
24. [ ] 回帰ゲート `scripts/run_regression_gate.ps1`(挙動 FAIL は既知ベースラインとの diff のみ許容)。
25. [ ] validate_catalog / replayer / odrviewer スモーク。
26. [ ] wasm: `GT_esmini/web/wasm/build.sh` 再ビルド + `web/wasm/smoke/index.html` 手動ブラウザスモーク(CI レグなし=手動、台帳 §7 exclusions 注記)。
27. [ ] 台帳(gt_roadmanager_patches.md)へ sync 記録を追記(タグ、ハンク増減、handled-by-upstream 移行、ゴールデン再基準化コミット)。

## 常設ガード一覧(本チェックリストの外で毎回勝手に走るもの)

| ガード | 内容 | 実行点 |
|---|---|---|
| `check_core_census.py` | 二側 per-marker census vs manifest(ミラー忘れ/予算) | conformance 全プロファイル + ctest |
| `check_fork_drift.py` | フォーク vs 現 pristine のマーカー外ドリフト | conformance 全プロファイル |
| `check_resync_guards.py` | whitelist 再生成ドリフト / 重複パス / handled-by-upstream 矛盾 / 合成IDベース離間 | conformance 全プロファイル(P9b 新設) |
| ctest `OdrResyncGuards.*` | 合成オブジェクト ID 重複ゼロ + 再パース蓄積ゼロ(挙動側) | unit ctest ゲート(P9b 新設) |
| ctest `OdrForkPatches.*` | マーカー数 / 第2種 census の manifest 一致 | unit ctest ゲート |
