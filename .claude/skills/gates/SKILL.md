---
name: gates
description: GT_esminiの検証ゲート一式（ユニット/回帰/ODR適合/カタログ）を正しい順序で実行し、結果を正しく解釈する。`/gates` が呼ばれたとき、またはユーザーが「テストして」「検証して」「ゲート回して」「回帰確認」「マージ前チェック」等に言及したとき、およびC++/パーサ/検証資産の変更後に必ず使用する。
---

# gates — 検証ゲートの実行と解釈

## 前提

**ゲートは最新のRelease全ビルドを要求する**（stale-DLLは偽判定の温床）。未ビルドなら先に `/build`。
Pythonは常に `DriverScript/.venv/Scripts/python.exe`（検証venv、pyyaml+osi3+matplotlib入り）。

## 変更領域 → 実行すべきゲート

| 変更した場所 | 実行 |
| :--- | :--- |
| GT_esmini C++（コントローラ/ポリシー等） | 回帰ゲート（下記②） |
| OpenDRIVEパーサ系（GT_RoadManager.cppフォーク / `GT_esmini/src/road/odr_side/`） | ② + ③フル適合（ゴールデン照合） |
| 検証シナリオ/道路資産（resources/xosc, xodr） | ② + ④カタログ |
| webバックエンド/フロントのみ | ①ユニット（+ frontend build） |
| ドキュメントのみ | 不要 |

## コマンド

```powershell
# ① ユニットゲート（ctest: test_ScenarioReaderParsing = GTユニット8ソース）
pwsh scripts/run_gt_tests.ps1

# ② プレマージ回帰ゲート（①含む: Step1=unit(hard) → Step1.5=ODR quick(hard) → Step2=挙動バッチ(WARN)）
pwsh scripts/run_regression_gate.ps1
#   -SkipOdr / -SkipBehavioral / -FailOnBehavioral / -TelemetryGolden あり

# ③ ODR適合フル（パーサ変更時。OSI層+ゴールデン込み）
DriverScript/.venv/Scripts/python.exe scripts/run_odr_conformance.py --profile full --check-matrix

# ④ シナリオカタログ（F1資産の健全性）
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/validate_catalog.py
```

## 結果の解釈（重要）

- **挙動バッチ（Step 2）は per-scenario ベースライン照合**（`scripts/check_phase3_regression.py` +
  `GT_esmini/test/regression_baseline/phase3_expected.yaml`）。既知ベースラインFAIL = `red_stop_green_go` の1件のみ
  （VD信号ポリシー未成熟=ラッチ解除欠如）。`green_no_stop` は F5 の module-dir 修正で in-process 実行が
  実 config（real_vehicle_params 等）を読むようになり意図挙動側に倒れて PASS 化（2026-07-11 baseline更新済み）。
  **照合で deviation が1件でも出たら停止して原因調査**（自分の変更が原因。両方向=新規fail/fail→pass とも検知される）。
- ODR適合は「FAIL/XPASSゼロ」が緑（XFAILは期待どおりの失敗=OK）。ASAM公式フィクスチャは
  thirdpartyのzipが無い環境では自動SKIP（正常）。
- `needs-review` verdict = expectations未定義のバッチエントリ。エラーではない。
- GT_Loader統合テスト（`GT_esmini_Integration_*`）は**作成時から一度も成功していない既知赤**
  （opt-in、`-IncludeIntegration`）。デフォルトゲートには含まれない。

## ゴールデン更新の作法（パーサ変更時）

1. `--update-golden` はLFで書き直すため **CRLF幽霊M / stat-dirty が大量発生**する。
   実差分の判別: `git diff --ignore-cr-at-eol --stat`
2. 実変化ファイルだけ退避 → `git checkout -- <golden dir>` で一括復元 → 実変化のみ戻す
3. ゴールデン更新は**根拠を添えた単独レビューコミット**で（manifest.yamlの`expected`更新とセット）
4. ホワイトリスト属性を追加したら、**その属性をexpected_unsupportedに持つ全fixtureをgrepで洗う**
   （片側だけ直すとauditが落ちる）

## 常設ガード

- フォーク行数/マーカー整合: ctest内（MarkerCount / OdrResyncGuards）+ `scripts/check_fork_drift.py`
- resync手順: `docs/odr_resync_checklist.md`（upstream取込時はこちらが正典）

スモーク実行の作法（PATH/PowerShell/ELECTRON_RUN_AS_NODE）は `/build` のスキルを参照。
