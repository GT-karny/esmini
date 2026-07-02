# GT_RoadManager.cpp フォークパッチマニフェスト

- 対象: `GT_esmini/src/road/GT_RoadManager.cpp`(upstream esmini v3.3.0 `RoadManager.cpp` の全量フォークコピー、上流コミット `ab7c404d` "Prepare release 3.3.0")
- スワップ機構: `EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt` が upstream `RoadManager.cpp` の代わりに本ファイルをビルド(.cpp のみ差し替え、`RoadManager.hpp` は pristine)
- ガバナンス: [opendrive_16_19_support_plan.md](opendrive_16_19_support_plan.md) §3.2
  - フォーク行数**ハード上限 150 行**(2026-07-02 ユーザー承認)。超過は都度承認。
  - マーカー数は ctest(`test_ScenarioReaderParsing` 内 OdrForkMarkers テスト)で機械監視。本表と一致しない場合テスト失敗。
  - マーカー外ドリフトは `scripts/check_fork_drift.py`(upstream `RoadManager.cpp` との diff がマーカーブロック+ヘッダコメントに限定されることを検証)。
  - 再同期手順: 計画 P9 の書面チェックリスト(関数名アンカーで再適用)。

## 0. CMake swap-zone 拡張(R1 例外 — 第 1 行)

**`EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt`** に `# [GT_ODR:cmake]` マーカーで odr_side ソース 4 本(P2 で `OdrLaneExtras.cpp` を既存 APPEND リストへ追加 — マーカー数不変)と `GT_esmini/include` を追加(**新 R1 例外、2026-07-02 ユーザー承認**)。既存 3 行例外(.cpp スワップ)に次ぐ 2 件目のコア CMake 改変。消費者: esminiRMLib / esminiLib / GT_esminiLib / esmini / replayer / odrviewer / odrplot / esminiJS(esminiJS は未テスト)。

- **`[GT_ODR:cmake]` マーカー出現数: 2**(単一の swap-zone 例外だが 2 箇所の APPEND: ①`list(APPEND SOURCES odr_side/*.cpp)`、②`target_include_directories(... GT_esmini/include)`)。`test_OdrForkPatches.MarkerCount` ctest が本数を機械監視(本表と一致しない場合テスト失敗)。

## 1. フォーク内パッチ一覧

| # | マーカー | 関数アンカー | 位置(2026-07-03 時点) | 行数 | 内容 | upstream PR |
|---|---|---|---|---|---|---|
| 1-A | `[GT_LHT]` | `OpenDrive::CheckJunctionConnection` | ~:5827 | (既存) | LHT: connectingRoad 端の laneSection 選択修正 | 済(却下歴あり、維持) |
| 2 | `[GT_ODR:hook]` (include) | ファイル先頭 include 群 | :78 | 1 | `gt_esmini/road/OdrSideModel.hpp` include | しない(GT専用) |
| 3 | `[GT_ODR:country-rev]` | `OpenDrive::ParseOpenDriveXML`(signal ループ) | ~:4819-4824 | 2 | countryRevision 読取修正: 初期値 2013→0 + 反転 `empty()` 条件の正立(**absent→0 のレガシー保存形**、explicit 値を尊重) | **PR-1**(高) |
| 4 | `[GT_ODR:junc-abort]` | `OpenDrive::ParseOpenDriveXML`(junction connection ループ) | ~:5433-5438 | 3 | connectingRoad 欠落時の全パース中断(return false)→ WARN+当該 connection スキップに劣化 | **PR-3**(中) |
| 5 | `[GT_ODR:hook]` (呼出) | `OpenDrive::ParseOpenDriveXML` 末尾、`CheckConnections()` 直前 | ~:5491-5495 | 5 | `gt_esmini::odr::BuildSideModel(doc, this)` — 側モデル構築+網羅監査。false(=`<include>` 検出)でパース中断 | しない(GT専用) |
| 6 | `[GT_ODR:obj-roadsurface]` | `RMObject::Str2Type` | ~:3002-3006 | 5 | `roadSurface`(1.8+)を既知型として静かに NONE へ(`ObjectType` enum は hpp 凍結のため追加不可 — 計画からの設計変更) | 候補(upstream 側 enum 追加が前提、PR-2 隣接) |
| 7 | `[GT_ODR:lane-types]` | `OpenDrive::ParseOpenDriveXML`(lane type switch 末尾) | ~:4224-4228 | 5 | ODR 1.6/1.8 レーン型を既存 enum へ接続(P2)。**マッピング根拠**: `walking`→SIDEWALK(1.8 の歩行者専用レーン。既存 OSI NONDRIVING/SIDEWALK マッピングに乗る)/ `curb`→CURB(hpp:903 の死に enum + 既存 OSI NONDRIVING/BORDER マッピングの接続。enum は 1.5 時代から存在するのにパース文字列が無かった)/ `shared`→BIDIRECTIONAL(1.8 spec の shared=複数交通参加者・双方向共用レーン。「走行可能+両方向」の意味論が最近傍。OSI は DRIVING/NORMAL)/ `slipLane`→CONNECTING_RAMP(1.8 spec の slip lane=交差点を迂回する短絡接続路。接続ランプが機能的最近傍。OSI は DRIVING/CONNECTINGRAMP)。元の正確な文字列は `OdrLaneExtras.type_str` に保存(OSI subtype 忠実性・将来ネイティブ対応用)。観測点=「unknown lane type」LOG_ERROR の消滅+OSI ゴールデン TYPE_UNKNOWN ゼロ(fixture 13 の `osi_expect_no_unknown` で機械検証) | **PR-2**(中-高) |

- **`[GT_ODR:` マーカー出現数: 6**(hook×2 / country-rev×1 / junc-abort×1 / obj-roadsurface×1 / lane-types×1)+ CMake 側 `[GT_ODR:cmake]`×2 箇所
- **フォーク追加/変更行数: 21 / 150**(include 1 + country-rev 2 + junc-abort 3 + hook 5 + obj-roadsurface 5 + lane-types 5)
- 事前承認済みコンティンジェンシー残(未使用): **lane-border フォールバック ~8 は P2 で不使用のまま温存** — border→width 正規化は公開 `Lane::AddLaneWidth` 経由で GT 側(`odr_side/OdrLaneExtras.cpp` の `ApplyBorderWidths`)に実装。既存フック呼び出し `BuildSideModel(doc, this)` が P2 新設の型付きオーバーロード(`roadmanager::OpenDrive*`)へ **exact match で自動束縛**されるため、フォーク改変ゼロで実現。/ P6 分割ヘルパー ~25 / lane @direction ~25

## 2. 挙動影響(P1 検証で証明)

- 既存全 xodr 資産(~113 ファイル)は `countryRevision` 属性を持たない(全資産 grep 0 件)→ パッチ 3 は既存資産で無影響(TrafficLight 分類ベースライン `golden/trafficlight_classification.json` の前後一致で証明)。
- パッチ 4 の期待フリップ(意図された変化、manifest.yaml に記録): `02_invalid_junction_connection_14.xodr` rm_init fail→pass。**公式 `Ex_Slip_Lane`・`UC_T_Junction` はフリップせず凍結維持** — P1 検証(2026-07-03)でクラッシュ真因は junction 中断とは別箇所と判明: UC_T_Junction=信号 validity 解決(`Signal::SetAllValidLanes` が道路長超過 s=111.5 でレーンセクション不在)、Ex_Slip_Lane=connection/laneLink ループ後の未特定箇所。クラッシュサイトは manifest.yaml の expected_notes に記録済み。修正は P5(junction 週)以降の追加パッチ候補(要マニフェスト行追加)。
- パッチ 5 の期待フリップ: `16_include_error_15.xodr` rm_init pass→fail(ハードエラー化は仕様、P9 で解決実装かハードエラー仕様化かをユーザー判断)。
- パッチ 6: 既存資産に `roadSurface` オブジェクト無し → ログ差のみ(該当 LOG_ERROR が出なくなる)。

## 3. P2 追加分の挙動影響(2026-07-03)

- パッチ 7(lane-types): 既存資産(1.4/1.5 中心)に walking/curb/shared/slipLane トークン無し → 既存資産のレーン型は完全不変(RM/OSI ゴールデン不変で証明)。意図されたゴールデン変化は fixture `13_lane_types_16_18` の RM レーン型(NONE→SIDEWALK/CURB/BIDIRECTIONAL/CONNECTING_RAMP)のみ。
- border→width 正規化(フォーク改変ゼロ、GT 側): `<border>` を持つ資産はリポジトリ内に無し(公式 `Ex_Lane-Border` フィクスチャのみ)→ 既存資産ビット同一。意図されたゴールデン変化は `Ex_Lane-Border` の幅 0.0→border 代数値のみ。
- lane `<speed>` L2(ManeuverAwareSpeedPlanner): 既存資産に lane `<speed>` 無し → サイドモデル lane_extras 空で即 return 0 の高速経路、v_limit はビット同一。
- LHT パッチ 1-A 非摂動: lane-types パッチは laneSection 選択ロジック(CheckJunctionConnection ~:5827)に触れない。LHT スモーク+`e6mini-lht`/`UC_LHT_Complex` ゴールデン不変で確認。
