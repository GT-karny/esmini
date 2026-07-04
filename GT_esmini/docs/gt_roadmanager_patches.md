# GT_RoadManager.cpp フォークパッチマニフェスト

- 対象: `GT_esmini/src/road/GT_RoadManager.cpp`(upstream esmini v3.3.0 `RoadManager.cpp` の全量フォークコピー、上流コミット `ab7c404d` "Prepare release 3.3.0")
- スワップ機構: `EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt` が upstream `RoadManager.cpp` の代わりに本ファイルをビルド(.cpp のみ差し替え、`RoadManager.hpp` は pristine)
- ガバナンス: [opendrive_16_19_support_plan.md](opendrive_16_19_support_plan.md) §3.2
  - フォーク行数**ハード上限 150 行**(2026-07-02 ユーザー承認)。超過は都度承認。
  - マーカー数は ctest(`test_ScenarioReaderParsing` 内 OdrForkMarkers テスト)で機械監視。本表と一致しない場合テスト失敗。
  - マーカー外ドリフトは `scripts/check_fork_drift.py`(upstream `RoadManager.cpp` との diff がマーカーブロック+ヘッダコメントに限定されることを検証)。
  - 再同期手順: 計画 P9 の書面チェックリスト(関数名アンカーで再適用)。

## 0. CMake swap-zone 拡張(R1 例外 — 第 1 行)

**`EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt`** に `# [GT_ODR:cmake]` マーカーで odr_side ソース 6 本(P2 で `OdrLaneExtras.cpp`、P3 で `OdrSignalExtras.cpp`、P5 で `OdrJunctionExtras.cpp` を既存 APPEND リストへ追加 — マーカー数不変)と `GT_esmini/include` を追加(**新 R1 例外、2026-07-02 ユーザー承認**)。既存 3 行例外(.cpp スワップ)に次ぐ 2 件目のコア CMake 改変。消費者: esminiRMLib / esminiLib / GT_esminiLib / esmini / replayer / odrviewer / odrplot / esminiJS(esminiJS は未テスト)。

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
| 8 | `[GT_ODR:tl-gate]` | `OpenDrive::ParseOpenDriveXML`(signal ループ)+ `TrafficLight::SetTrafficLightInfo` | ~:4893-4897 / ~:473-478 | 9 | TrafficLight 生成ゲート緩和(ブロック1、4行): `country=="opendrive" && countryRevision<2013 && dynamic` → `dynamic` のみ(P3 クラスタ 11)。全資産分類監査とatomic: [odr_p3_tl_gate_audit.md](odr_p3_tl_gate_audit.md)(コミット済みユニバースのフリップはP0フィクスチャ3件のみ・本番資産0件、ASAM公式46件は実測破損#1の修正)。countryRevision 読取(パッチ3)は診断/PR-1用に維持。ブロック2(5行): 未対応type組合せで `nr_lamps_` が未初期化のまま残る**upstream潜在バグ**の修正(`nr_lamps_=0`) — ゲート緩和で任意typeの動的信号がTrafficLight化されるため顕在化(0xe06d7363 FFIクラッシュ)。 | **PR-1b**(中)+ nr_lamps 初期化は独立PR候補(高) |
| 9 | `[GT_ODR:sig-pos]`(×3: ブロック+ctor引数2箇所) | `OpenDrive::ParseOpenDriveXML`(signal ループ) | ~:4896-4903 + ctor h 引数 2 行 | 9 | <positionRoad>(参照先roadへ物理姿勢接続、zOffset/hOffset/pitch/roll上書き)+ <positionInertial>(センターライン評価による逆写像 — XYZ2TrackPosはSetRoadOSI後のOSIポイント依存でパース時使用不可。場外=|t|>30m or 再構成誤差>0.5m → WARN+skip)+ 1.9 s/t省略(同一road上の解決姿勢からbackfill、position子無しはWARN診断)。実装は odr_side/OdrSignalExtras.cpp ResolveSignalPose。制約: 参照先roadは文書内で先行宣言が必要(パース時解決) | 候補(要メンテナ見解) |
| 10 | `[GT_ODR:sig-ref]`(×2: else-if+フック側) | signal ループ else-if + `[GT_ODR:hook]` 直後 | ~:5006 / ~:5525-5532 | 9 | <signalReference> を参照先Signalのクローンとして実体化(参照側のs/t/orientation/validity、dynamic対象はTrafficLightクローン=tl-gate整合、新規GlobalId+SetAllValidLanes+AddSignal)。全road解析後のフック時点で実体化(文書内前方参照対応)、dangling id は WARN+skip。dynamicクローンは dynamic_signals_ 登録(privateのためフック側)。旧 LOG_ERROR_ONCE(cluster 12 backlog)を置換。実装は odr_side/OdrSignalExtras.cpp MaterializeSignalReferences | **PR-4**(低-中) |
| 11 | `[GT_ODR:sig-lanes-guard]` | `Signal::SetAllValidLanes` | ~:547-556 | 9 | **クラッシュ修正**: 信号 s が道路範囲外(公式 UC_T_Junction: 80m 道路に s=111.5)で `LOG_ERROR_AND_QUIT`(throw)が C API 境界を越え 0xe06d7363 でロード全体を殺していた → WARN+有効レーン未割当に劣化。P5 予定の再訪をバグ修正パスで前倒し | 高(upstream 同一コード) |
| 12 | `[GT_ODR:direct-junc-log]` | `OpenDrive::CheckJunctionConnection`(direct junction 検査) | ~:6684-6689 | 5 | **クラッシュ修正(upstream fmt バグ)**: direct junction の linkedRoad 逆接続検査の LOG_ERROR がプレースホルダ 2 個/引数 1 個 → ログ出力中に `fmt::format_error`(argument not found)が throw されロード全体を殺していた(公式 Ex_Slip_Lane、upstream RoadManager.cpp:6621 と同一)→ 引数補完。不正接続自体は従来どおり return -1 で棄却 | **高**(明白な upstream バグ) |
| 13 | `[GT_ODR:junc-crossing]`(×2: type dispatch + IsOsiIntersection ガード) | `OpenDrive::ParseOpenDriveXML`(junction ループ)+ `Junction::IsOsiIntersection` | ~:5442-5448 / ~:5947-5952 | 13 | **P5 crossing junction 対応**: ①`@type="crossing"` を既知型として認識(WARN + DEFAULT 維持、hpp enum 凍結。roadSection は GT 側 OdrJunctionExtras で捕捉)。②IsOsiIntersection の空/nullptr connection の else 分岐を `return true`→`return false` に(接続ゼロの junction=crossing/virtual は roadSection/crossPath を持ち connection を持たないため、ゴースト intersection レーンを生まない)。全コントロール資産は connection を持つため後者は no-op(P5 で機械証明: `--profile full --update-golden` 後、コントロールセットの OSI ゴールデンが**バイト同一**=空connection分岐に入った資産ゼロ。crossPath フィクスチャの OSI ゴールデン 3 本のみ新規) | **PR-3**(中、junc-abort 隣接) |
| 14 | `[GT_ODR:curvelocal]`(×2: 単数 outline 形受入 + curveLocal else-if) | `OpenDrive::ParseOpenDriveXML`(object outline ループ + corner 子ループ) | ~:5199-5205 / ~:5236-5240 | 12 | **P7 1.9 `<curveLocal>` アウトライン対応**: ①**単数 `<object><outline>` 形の受入**(上流は `<outlines><outline>` の複数形のみ読取。g4 フィクスチャは単数形を使う): container を `outlines_node ? outlines_node : object` に切替え、outline 兄弟を **名前指定** `next_sibling("outline")` で反復(複数形資産はビット同一 — 上流の `<outlines>` 内は `<outline>` のみのため無名反復と等価)。②corner 子ループに `curveLocal` の else-if を追加、ロジックは持たず `gt_esmini::odr::AppendCurveLocalCorners(corner_node, r, obj, outline, next_corner_id)` に全委譲(arc/line/paramPoly3 を弧長でテッセレート → OutlineCornerLocal 群を outline へ直接追加、実装は WP2 の `odr_side/OdrObjectExtras.cpp`)。`next_corner_id` は現 outline の corner 数から採番。cornerRoad/cornerLocal 経路は不変。**注**: WP1/WP2 は corner ループを form-agnostic と仮定していたが実際の上流フォークは複数形限定だったため、g4(単数形)を機能させるべく単数形受入を curvelocal ブロックに内包(当初 ~10 行想定から +2 パッチ相当) | しない(GT専用、上流に curveLocal / 単数 outline 未対応) |
| 15 | `[GT_ODR:repeat-cubics]`(×1) | `RMObject::GetRepeatInstances`(離散 repeat ループ) | ~:5776-5778 | 3 | **P7 1.9 repeat 横方向三次多項式 + detach 対応**: 離散 repeat インスタンスの線形 t 算出後・`pos.SetTrackPosMode` 直前に `gt_esmini::odr::AdjustRepeatInstancePose(this, road, cur_s, factor, ri.s, ri.t)` を挿入(factor = 正規化フラクション ∈[0,1])。@bT/@cT/@dT の三次 + detachFromReferenceLine を s/t に適用(実装は WP2 の `odr_side/OdrObjectExtras.cpp`)。ヘルパは横多項式レコードを持たないレガシーオブジェクトに対し**即 false・s/t 無改変**を返すため既存経路はビット同一。連続アウトライン経路(`CreateContinuousRepeatOutline`)は対象外(文書化済みスコープ) | しない(GT専用、上流に repeat 三次未対応) |

| 16 | `[GT_ODR:lane-layers]`(×1) | `OpenDrive::ParseOpenDriveXML`(road ループ、lanes 選択点) | ~:4093-4094 | 2 | **P8 1.9 レーンレイヤ対応**: `road_node.child("lanes")`(第 1 ノードのみ読取=第 2 `<lanes layer>` の黙殺、最後のサイレント欠落クラス)を `gt_esmini::odr::SelectLanesLayer(road_node, this)` の薄いデリゲートに置換。permanent モード(デフォルト)= permanent ノードを**無コピー・同一ノードで**返す(AddLane/SetGlobalId の DOM 反復順が完全不変 → レーングローバル ID 安定が構造的に自明)。temporary opt-in(env `GT_ODR_LANE_LAYERS=temporary`)= GT 側 `odr_side/OdrLaneLayers.cpp` が laneSection/laneOffset を s 範囲マージした合成 `<lanes>` DOM を返す(合成 document の寿命はインスタンス別サイドモデルが所有 — pending レジストリ→`OdrSideModel::merged_lanes_docs`)。設計判断 D1-D6 は §8 | しない(GT専用、上流 1.9 レイヤ未対応) |

- **`[GT_ODR:` マーカー出現数: 21**(hook×2 / country-rev×1 / junc-abort×1 / obj-roadsurface×1 / lane-types×1 / tl-gate×2 / sig-pos×3 / sig-ref×2 / sig-lanes-guard×1 / direct-junc-log×1 / junc-crossing×2 / curvelocal×2 / repeat-cubics×1 / lane-layers×1)+ CMake 側 `[GT_ODR:cmake]`×2 箇所
- **フォーク追加/変更行数: 92 / 150**(include 1 + country-rev 2 + junc-abort 3 + hook 5 + obj-roadsurface 5 + lane-types 5 + tl-gate 9 + sig-pos 9 + sig-ref 9 + sig-lanes-guard 9 + direct-junc-log 5 + junc-crossing 13 + curvelocal 12 + repeat-cubics 3 + lane-layers 2) — P3 追加 27 + クラッシュ修正パス 14 + P5 追加 13 + P7 追加 15 + **P8 追加 2**(計画見込み ~22 → 実績 2: 選択+マージのロジック全量を GT 側 OdrLaneLayers.cpp へ委譲)
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
- パッチ 8(P3)の期待フリップ: コミット済みユニバースでは P0 フィクスチャ 3 signal のみ(03_dynamic_signal_demote_18 の (a)(b) + 05_vms_boards の gantry_vms)。**本番資産のフリップ 0 件**(既存動的信号は全て country=OpenDRIVE + countryRevision 省略で従来から TrafficLight)。ASAM 公式 46 signal(countryRevision="2013" 明示形)が昇格 = 実測破損 #1 の修正。全行レビューは [odr_p3_tl_gate_audit.md](odr_p3_tl_gate_audit.md)、機械検証は `test_OdrAssetProbe` ゴールデン + `odr_tl_classification_audit.py --check-golden after`。

## 4. P3 追加分の挙動影響(2026-07-03)

- パッチ 8(tl-gate)の期待フリップ: コミット済みユニバースでは P0 フィクスチャ 3 signal のみ(03_dynamic_signal_demote_18 の (a)(b) + 05_vms_boards の gantry_vms)。**本番資産のフリップ 0 件**(既存動的信号は全て country=OpenDRIVE + countryRevision 省略で従来から TrafficLight)。ASAM 公式 46 signal(countryRevision="2013" 明示形)が昇格 = 実測破損 #1 の修正。全行レビューは [odr_p3_tl_gate_audit.md](odr_p3_tl_gate_audit.md)、機械検証は `test_OdrAssetProbe` ゴールデン + `odr_tl_classification_audit.py --check-golden after`。
- パッチ 8/9(P3)の既存資産影響: リポジトリ内 xodr に positionRoad/positionInertial/signalReference はフィクスチャ以外に存在せず(全資産grep)、既存ゴールデン変化は ASAM 公式の signalReference 保有ファイル(RM ゴールデン signs にクローン追加)のみ。UC_5Road_Junction の signalReference は dangling(参照先 id 不在)で WARN+skip の実証例。
- パッチ 9(sig-pos)/10(sig-ref): リポジトリ内 xodr に positionRoad/positionInertial/signalReference はフィクスチャ以外に存在せず(全資産 grep)、既存ゴールデン変化は ASAM 公式の signalReference 保有ファイル(UC_Motorway-Exit-Entry ×2 / UC_5Road_Junction の RM ゴールデン signs にクローン追加)のみ。場外 positionInertial・dangling 参照は WARN+skip(fixture 20 / 単体テストで機械検証)。

## 5. クラッシュ修正パス(post-P3、2026-07-03)の挙動影響

- パッチ 11/12 の期待フリップ: 公式 `Ex_Slip_Lane` / `UC_T_Junction` の rm_init **fail→pass**(manifest 更新済み、新規 RM ゴールデン 2 本)。他の全資産はクラッシュ経路に入らないため不変(conformance full 209P/12XF/0F、既存ゴールデン全一致で機械検証)。
- 併せて GT 所有 `GT_OSIReporter_Traffic.cpp`(フォーク外・行数予算対象外)で 0 灯 TrafficLight を traffic_sign へフォールバック(P3 監査文書の既知劣化の解消。1.5 コントロールセットの OSI ゴールデンはフリップ信号ゼロのため不変)。

## 6. P4 挙動影響 — signal semantics の L2/L3(2026-07-03)

**フォーク追加行数: 0 / 150(P4 の挙動配線はフォーク外の GT ファイルのみ)。** L1(semantics/boards/header regulations のパース→GT サイドモデル格納)は既存 `[GT_ODR:cmake]` スワップゾーンの `OdrSignalExtras.cpp` に閉じ、`GT_RoadManager.cpp` および `EnvironmentSimulator/` は不変。fork-drift は 62/150 のまま(機械検証: `check_fork_drift.py --expect-odr-lines 62` 緑)。

### 6.1 分類方針: カタログ優先・semantics フォールバック(設計判断 1)

国別交通標識カタログ(`resources/traffic_signals/<country>_traffic_signals.txt`)は、出荷済み全資産で使われている**キュレーション済み・テスト済みの唯一の正規経路**。カタログが `@type` を知らない標識だけが OSI 未分類のまま残る(パース時 `osi_type` はカタログ未ヒットの番兵 = `INT_MAX_SENTINEL`、明示 type でマップ先が UNKNOWN の場合は 0)。この**未分類標識に限り** signal `<semantics>` の `<priority @type>` で補完する。理由: カタログは国固有の意味を持つ curated マッピングであり、semantics は「カタログが知らない標識のギャップ」だけを埋める。既存資産の分類は一切変えない。

- **L2 フック位置**: `GT_esmini/src/control/virtualdriver/policies/StopYieldSignAware.cpp`(`Evaluate` 内 `effective_osi` ラムダ)。`GetOSIType()` が既に `TYPE_STOP`(17)/`TYPE_GIVE_WAY`(16)なら semantics は一切参照しない(カタログ優先)。未分類のときのみ `gt_esmini::odr::GetSignalExtras(Position::GetOpenDrive(), sig)` を引き、`has_semantics` かつ `<priority>` が下表に該当すれば effective OSI type を差し替える。`Signal*` ポインタキー(`semantic_class_cache_`)で毎フレームの O(道路×標識)ルックアップを 1 回にメモ化。**semantics 無しの標識は「lookup → null/empty → 即デフォルト経路」で pre-P4 とビット一致**(位相3系バッチが回帰センサ)。
- **L3(OSI)フック位置**: `GT_esmini/src/osi/GT_OSIReporter_Traffic.cpp`(`UpdateStaticTrafficSignals` の sign ブランチ末尾)。`extras == null || !has_semantics` で即スキップ → 既存資産の直列化はバイト不変(conformance full の OSI 35 件・ゴールデン変化 0 で機械検証)。

### 6.2 priority マッピング表(設計判断 2 — 挙動を持つのはこの 2 種のみ)

| `<priority @type>`(1.9 XSD verbatim) | 挙動 | 対応 OSI | 備考 |
| :--- | :--- | :--- | :--- |
| `stop` / `stopLine` | STOP 標識扱い(dwell+creep FSM) | `TYPE_STOP`(17) | `Signal::TYPE_STOP` と同一挙動 |
| `yield` | GIVE_WAY 扱い(creep 減速のみ、full stop は 3d) | `TYPE_GIVE_WAY`(16) | `Signal::TYPE_GIVE_WAY` と同一挙動 |
| `trafficLight` | 挙動なし | — | 動的側の P3 `[GT_ODR:tl-gate]` と対 |
| `4way` / `keepClearLine` / `noParkingLine` / `noTurnOnRed` / `priorityRoad` / `priorityRoadEnd` / `priorityToTheRightRule` / `stopLine` 以外 / `turnOnRedAllowed` / `waitingLine` | 挙動なし(情報のみ) | — | P4 では未配線 |

純関数 `ClassifyPriorityTypes(priority_types)` がドキュメント順で最初の挙動型を返す(STOP が GIVE_WAY に優先するのは資産が両者を混在させないため、位置順で決定)。単体テスト `test_TrafficPolicies.cpp`(`SemanticPriorityFallback.*` 6 件)。

### 6.3 speed/lane semantics のスコープ(設計判断 3)

- **speed/lane semantics の L2 は accessor 止まり(P4 では挙動配線なし)**。速度標識の意味論は「標識通過後に上書きされるまで制限が持続する」ゾーン状態を要し、cluster-14 のデフォルト速度と同様に延期。
- **L3(OSI)は semantic speed を出力する**: カタログ未分類 かつ 標識自身が `@value` 未設定(未設定 `@value` は `value_ == 0.0` + 空 `@unit` として現れる)のときのみ、`value.value` + `value_unit`(`km/h`→KILOMETER_PER_HOUR / `mph`→MILE_PER_HOUR / `m/s`→OTHER、既存カタログ m/s マッピングに整合)を補完。**TYPE は差し替えない** — `SPEED_LIMIT_BEGIN` はゾーン状態依存(判断 3)につき投機的な型マッピングを避ける。board 内容の OSI 出力は延期(plan §8 item 7)。

### 6.4 検証

- 挙動フィクスチャ(受入 ii): `resources/xodr/straight_semantic_stop_sign.xodr`(`@type=9001`=de カタログ非在 + `<priority type="stopLine"/>`)+ `semantic_stop_sign_full_stop.xosc/.expectations.yaml`。**red→green**: T1 実装前は VD が停止せず `stopped_at_stop_sign` = 0.00s で FAIL、実装後は 4.35s(カタログ版 `stop_sign_full_stop` と同値)で PASS。`phase3_batch.yaml` に 1 エントリ追加。
- 不変性: `phase3_batch`(既存 10 件すべて verdict 一致 + 新規 1 件 PASS)/ `phase3d_crosswalk_batch`(scene 09 歩行者信号ゲート = 7 件すべて一致)。L3 正例: 新フィクスチャの OSI `traffic_sign` が `type=17`(STOP)を出力(GT_esminiLib SE_GetOSIGroundTruth 直接プローブで確認)。semantics 無しのカタログ未分類標識は `type=0` のまま(pre-P4 と一致)。
- ゲート: `test_ScenarioReaderParsing` 緑 / `check_fork_drift.py --expect-odr-lines 62` 緑 / conformance `--profile full` = 214P/13XF/0F/0XPASS(OSI ゴールデン変化 0)。

## 7. P7 追加分の挙動影響(2026-07-04)

**フォーク追加行数: +15 / 150(75 → 90)。** WP3 は 2 パッチ(curvelocal 12 + repeat-cubics 3)を追加。挙動ロジックはいずれも WP2 の GT ヘルパ(`odr_side/OdrObjectExtras.cpp`)へ委譲しフォーク外に閉じるが、curvelocal は上流が単数 `<object><outline>` 形を読まない実態が判明したため、単数形受入(container 切替 + `next_sibling("outline")`)を同ブロックに内包した(WP1/WP2 の「corner ループは form-agnostic」という仮定の是正 — 当初 ~10 行想定を超過)。

- パッチ 14(curvelocal): 既存全 xodr 資産に `<curveLocal>` トークン無し(リポジトリ xodr ユニバース全 grep = ヒットはテストフィクスチャ `g4_curvelocal_corner_19.xodr` と公式 `Ex_SmoothObjectOutline` のみ、コントロール/本番資産 0 件)→ curveLocal else-if は既存資産で一度も入らず、cornerRoad/cornerLocal 経路は完全不変。単数形受入も複数形資産では `next_sibling("outline")` が上流の無名 `next_sibling()` と等価(`<outlines>` 内は `<outline>` のみ)につきビット同一。**レガシー資産はビット同一**(RM ゴールデン不変で証明。RM 抽出はオブジェクトをダンプしないため g4 の RM ゴールデンも不変)。意図された挙動変化は g4 の OSI stationary polygon が base_polygon **0 点(degenerate)→ 16 点(ccw、非退化)** へ = 2 本の curveLocal 弧(各 length 6.283、max 1.0m/seg)のテッセレーションが OSI に通ったことの実証。OSI ゴールデンは WP4 以降で新規化(現状 MISSING)。
- パッチ 15(repeat-cubics): 既存全 xodr 資産に repeat `@bT/@cT/@dT` トークン無し(リポジトリ xodr ユニバース全 grep = ヒットはテストフィクスチャ `12_repeat_lateral_poly_19.xodr` のみ、コントロール/本番資産 0 件)→ `AdjustRepeatInstancePose` は横多項式レコードを持たない全レガシーオブジェクトに対し**即 false・s/t 無改変**を返す高速経路をたどり、`GetRepeatInstances` の離散インスタンス s/t は完全不変。**レガシー資産はビット同一**(RM ゴールデン不変で証明。連続アウトライン経路 `CreateContinuousRepeatOutline` は非対象)。12_repeat フィクスチャの OSI 観測は stationary count=1・base_polygon 482 点(連続アウトライン経路由来、パッチ対象外)、横シフト自体は WP2 単体テスト(`AdjustRepeatInstancePose.*`)が機械検証。

### 7.1 WP4: 旗付き authored junction boundary → OSI 交差点輪郭(クラスタ8/9 L3、コミット 47cb8a32)

コアフォーク**追加ゼロ**(90/150 不変・マーカー20 不変・EnvironmentSimulator diff ゼロ)。実装は GT 側 `OdrJunctionGeom.cpp`(WP2 の L1 boundary 格納を世界座標ポリラインへ評価する `BuildAuthoredJunctionBoundaryPolyline()`)+ `GT_OSIReporter.cpp`(継承 `UpdateOSIIntersection()` 後の後段パス `ApplyAuthoredJunctionBoundaries()`)に閉じる。

- **フラグ `GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY`(デフォルト OFF)**: 1/true/yes/on で ON。OFF は完全 no-op(後段パスが即 return) → **既存 OSI ゴールデン全件バイト同一**(コントロールセット + フィクスチャとも不変)。ON 時のみ、`IsOsiIntersection` かつ buildable な polyline を持つ junction について、交差点レーンの `free_lane_boundary_id` リストを合成 `osi3::LaneBoundary` 1 本(id は CommonMini `GetNewGlobalId()` の単調カウンタで衝突なし)で置換。
- **type="lane" セグメント評価**: 参照 road の boundaryLane OUTER エッジ(符号付き t=sign(lane)*GetOuterOffset)を sStart→sEnd(start/begin→0・end→length・数値)で `Position::SetTrackPos` により ≤2m ステップサンプリング(スパン当り ≥2 点、接触点 degenerate は 1 点)。type="joint" は文書化された no-op(連続レーンセグメント間の直線接続)。dangling/degenerate 参照は WARN+false で呼出側が heuristic を温存。
- **検証(WP5)**: `scripts/probe_authored_junction_boundary.py` が OFF/ON の 2 走行を比較 — 手書き authored-boundary フィクスチャで **OFF free_lane_boundary=[29,37,47,59](各 12 点の heuristic)→ ON=[62](authored 輪郭 1 本・4 点)** を機械アサート(両アサート pass)。プログラム的 `SetUseAuthoredJunctionBoundary(bool)` オーバーライドで単体テスト可能。

### 7.2 WP5 最終検証(2026-07-04、受入ゲート i〜vii)

- **(i) GT_RoadGen 公式ジオメトリ資産**: Ex_SmoothObjectOutline_traffic_island / Ex_TrafficIsland-CornerRoad / Ex_Objects / Ex_CrossSectionSurface 4 本 / UC_RoadShape の計 8 資産すべて **exit 0・.osgb > 4KB**(最小 UC_RoadShape=4358B、最大 Ex_Objects=77838B)。
- **(iii) ゴールデンレビュー**: 新規 9 本のみ(RM 23/24/25/26/g8 + OSI g4/10/11/26)。g4 OSI base_polygon=16pt ccw、11_bridge stationary type2(TYPE_BRIDGE)×1、10_object_reference stationary×2、26_object_details stationary×3(非クラッシュ証明)、23/24 RM z_probe=±0.059988@t=±3(解析 rm_expect_z 一致)。既存ゴールデンは `--update-golden` 後も全件 CRLF ゴーストのみ(`git diff` 空)=回帰ゼロ。full profile **233P/0F/13XF/0XP**。
- **(iv) 1.4/1.5 コントロール .osgb サイズ帯**: fabriksgatan/e6mini/straight_500m を P7 exe と dev_v0.12 baseline exe で生成 → **3 資産ともバイト同一(0.0000% 差)**。
- **(vi) 不変スイート**: Release ビルド 0 error、unit ctest **163/163**、conformance full 緑、回帰ゲート(Step1/1.5 緑・Step2 は既知 TL 2 件=`red_stop_green_go`/`green_no_stop` のみ fail=dev 基準一致・他 9 件 pass)、validate_catalog **61/61**、quick smoke(esmini/replayer/odrviewer)3/3、フラグ probe OFF/ON 両アサート pass。
- **(vii) ジオメトリフィクスチャのビューア確認**: 7 フィクスチャ(g4/11/10/26/24/23/g8)を odrviewer `--capture_screen` で撮影 — bridge span・objectReference 2 本のポール・object_details のボックス群・g8 の X 交差点を目視確認。23/24 の断面傾斜(superelevation 劣化、z≈0.06m@t=3)は微小につき既定アングルでは非可視(道路ジオメトリ自体は正常描画)。

## 8. P8 追加分の挙動影響+設計判断(2026-07-04)

**フォーク追加行数: +2 / 150(90 → 92)・マーカー 21。** 計画見込み ~22 行に対し実績 2 行: フックは `road_node.child("lanes")` → `SelectLanesLayer(road_node, this)` の置換のみで、レイヤ選択・s 範囲マージ・合成 DOM 所有・L1 格納の全量を GT 側 `odr_side/OdrLaneLayers.cpp`(新規、既存 `[GT_ODR:cmake]` APPEND リストへ 1 行追加 — マーカー数 2 不変)に委譲した。

### 8.1 設計判断(審査済み設計の実装確定)

- **D1 モード選択機構**: 環境変数 `GT_ODR_LANE_LAYERS`(未設定/`permanent`=デフォルト、`temporary`=opt-in マージ、大文字小文字非区別、未知値 WARN+permanent)。**プロセス毎に 1 回ラッチ**(実行時切替なし=走行ごと再パースの Web ランナー運用と一致)。理由: RM のみのエントリポイント(esminiRMLib / rm_lib.py / GT_RoadGen)は GT 設定ローダを通らないため、全経路で機能する唯一の低侵襲機構。単体テスト用に `SetLaneLayerModeForTest`/`SetLaneLayerModeUseEnv` オーバーライド(P7 WP4 の旗イディオム踏襲)。
- **D2 マージ意味論(境界セクション)**: temporary 被覆範囲 [t0,t1](t0=temp 最小 laneSection s、t1=最終 temp セクション s+@length、無ければ道路端)。合成 `<lanes>` = permanent(s<t0)+ temporary 全部 + permanent(s≥t1)、laneOffset も同一規則。**境界規則**: t1 に一致する permanent セクションが無い場合、t1 時点で活性な permanent セクションを deep copy+s:=t1 で再オープンし、width は Taylor シフト(a'=a+b·ds+c·ds²+d·ds³ …)・height/roadMark は sOffset 繰り上げで再アンカー。laneOffset も同様に t1 で Taylor 再アンカー。中間 @length が次セクション s と不一致なら WARN(被覆は連続とみなす)。**両公式フィクスチャ(Ex_Lane_MultiLaneLayer [40,360] / Ex_Motorway_roadworks [2000,5083])は t0/t1 とも permanent セクション境界と一致するため再オープン経路は資産では不発** — 純ロジック単体テスト(test_OdrLaneLayers)で担保。出力順 = laneOffset 昇順 → laneSection 昇順(グローバル ID 決定性)。
- **D3 合成 DOM 寿命**: pending レジストリ(RegistryMutex 共有、(OpenDrive*, road_id) キー、SelectLanesLayer が生成・キャッシュ=再呼出しは同一ノード)→ 型付き `BuildSideModel` 完了時に `OdrSideModel::merged_lanes_docs` へ移動(サイドモデルと同寿命 = フォークのパースより長生き)。`ClearSideModel` は両方クリア。
- **D4 invalidated の OSI 扱い**: **論理層から除外** — invalidated 信号は OSI TrafficSign/TrafficLight に出力しない(打ち消された規制は ground truth の論理標識ではない)。物理表現の論点は**不発生**(esmini は標識ボードを StationaryObject として出力する経路を持たない)。invalidated **object** は StationaryObject 出力から除外(1.9 意味論=モデル上無効)。**temporary フラグは L1 情報のみ(挙動・出力への影響ゼロ)**。
- **D5 VD**: RouteSignalScan(ScanSignalsAhead)の 1 箇所フィルタで StopYieldSignAware / TrafficLightAware の両ポリシーをカバー(両者は scan 出力を消費、独自反復なし)。「打ち消し標識に従わない」が意味論の本体 — 挙動フィクスチャ `invalidated_stop_sign_ignored`(P4 の semantic stop sign の invalidated 版、min_speed_above 8.0 + speed_above 12.0 で無停止を機械検証)。RouteCrosswalkScan は invalidated crosswalk をスキップ(P5 合成 CROSSWALK は extras 無し=null 経路で不影響)。
- **D6 側モデルの一貫ビュー**: `ParseLaneExtras` は SelectLanesLayer の返す同一ノードを歩く(RM 構造と extras の食い違い防止)。監査(OdrCoverageAudit)は**オリジナル doc** を歩く(authored ファイルの監査が目的、合成 DOM は対象外)。

### 8.2 挙動影響

- **既存資産(単一 `<lanes>`)**: SelectLanesLayer は子 `<lanes>` が 1 個以下なら同一ノードを即返し(モード不問)→ **全コントロール資産でビット同一**。permanent モードは複数レイヤ資産でも第 1 非 temporary ノードを無コピー返却 → AddLane/SetGlobalId(:2388/:1252、DOM 反復順の逐次割当)が完全不変 = **OSI レーングローバル ID 安定**。機械検証: `golden/lane_global_ids.json`(test_OdrLaneLayers.GlobalIdStability、道路先頭レーン基準のオフセット正規化、GT_ODR_PROBE_UPDATE=1 で更新)+ コントロールセット RM/OSI ゴールデン不変。
- **期待フリップ(意図された変化)**: manifest `expected_unsupported_entries` — `g2_lanes_layer_19`(road/lanes@layer、laneSection@length)と `temporary_invalidated_19`(signal/object@temporary/@invalidated ×4)が **[] にフリップ**(ホワイトリスト 659→676 ペア、gen_odr_whitelist.py 再生成)。両 rev9 公式ファイル+g2+06 で [ODR-UNSUPPORTED]==0(要素+属性)。
- **マージ証明点(RM プローブ実測、esminiRMLib 経由=フォーク実効)**: MultiLaneLayer road 1 lane -3 幅 @s=50: permanent **3.75** / temporary **3.625**、s=10/370 は両モード一致(範囲外)。roadworks road 8 レーン数: permanent 全点 13 / temporary **s=100→13(範囲外=permanent 値)、s=2500→5、s=3500→6、s=4500→4、s=5500→13(範囲外=permanent 値)**。s=100/5500 の permanent 一致がマージの s 範囲境界の機械証明。
- **クラスタ 22 の残余**: junction laneLink @fromLayer/@toLayer は P5 実装済みスロットで捕捉済み。P8 追加 = lane `<link><predecessor/successor @layer>`(OdrLaneExtras.link_layers、sparse)+ `<validity @layer>`(signal/object extras、sparse)+ laneSection @length(OdrRoadLaneLayers シャドウ、L1 情報のみ=esmini は自前計算)。
- **rev9 の type 無し center lane**: :4147-4149 の LOG_ERROR は P1/P2 で抑止されて**いない**(申し送りの前提誤り)が、全対象フィクスチャの center lane は type="none" を明示するためスパム新規発生なし(フォーク無改変で据置)。
