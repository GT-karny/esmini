# GT_RoadManager.cpp フォークパッチマニフェスト

- 対象: `GT_esmini/src/road/GT_RoadManager.cpp`(upstream esmini v3.3.0 `RoadManager.cpp` の全量フォークコピー、上流コミット `ab7c404d` "Prepare release 3.3.0")
- スワップ機構: `EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt` が upstream `RoadManager.cpp` の代わりに本ファイルをビルド(.cpp のみ差し替え、`RoadManager.hpp` は pristine)
- ガバナンス: [opendrive_16_19_support_plan.md](opendrive_16_19_support_plan.md) §3.2
  - フォーク行数**ハード上限 150 行**(2026-07-02 ユーザー承認)。超過は都度承認。
  - マーカー数は ctest(`test_ScenarioReaderParsing` 内 `OdrForkPatches.MarkerCount` テスト、`GT_esmini/test/unit/road/test_OdrForkPatches.cpp`)で機械監視。本表と一致しない場合テスト失敗。
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

- **`[GT_ODR:` マーカー出現数: 17**(hook×2 / country-rev×1 / junc-abort×1 / obj-roadsurface×1 / lane-types×1 / tl-gate×2 / sig-pos×3 / sig-ref×2 / sig-lanes-guard×1 / direct-junc-log×1 / junc-crossing×2)+ CMake 側 `[GT_ODR:cmake]`×2 箇所
- **フォーク追加/変更行数: 75 / 150**(include 1 + country-rev 2 + junc-abort 3 + hook 5 + obj-roadsurface 5 + lane-types 5 + tl-gate 9 + sig-pos 9 + sig-ref 9 + sig-lanes-guard 9 + direct-junc-log 5 + junc-crossing 13) — P3 追加 27 + クラッシュ修正パス 14 + P5 追加 13
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

## 7. 第2種(in-place core edits)マニフェスト — P6 virtual junction

第2種編集とは、2026-07-04 の R1 緩和でユーザー承認された pristine コアファイル(`EnvironmentSimulator/` 配下)への **in-place 直接編集**を指す(第1種=`GT_RoadManager.cpp` フォークの既存 150 行レジーム)。下の fenced YAML ブロックが**唯一の真実(single source of truth)**であり、`scripts/check_core_census.py`・`scripts/check_fork_drift.py`・`scripts/run_odr_conformance.py`・ctest センサス(`OdrForkPatches.MarkerCount` / `OdrForkPatches.SecondClassZeroEditBaseline`)はすべて本ブロックをパースする — スクリプト側への期待値の埋め込みは禁止(`check_fork_drift.py` の陳腐化した `_DEFAULT_EXPECT_ODR=16` が動機となった失敗事例)。予算・ファイルセットは 2026-07-04 ユーザー承認([odr_p6_virtual_junction_design.md](odr_p6_virtual_junction_design.md) §5/§10)。ベースラインは Stage 0b(upstream v3.4.0 マージ)後に `check_core_census.py record-baselines` の 1 コマンドで再記録する。

<!-- GT-2ND-CLASS-MANIFEST-BEGIN -->
```yaml
version: 1
baseline_upstream_tag: v3.4.0            # recorded at Stage 0b (merge d7821fd3); re-record: check_core_census.py record-baselines
# --- ctest simple-parse keys (keep exactly these key names, one per line) ---
fork_odr_marker_total: 17
fork_lht_marker_min: 1
cmake_marker_total: 2
fork_odr_expect_lines: 75
fork_line_budget: 150
# --- combined budgets: rows sharing a budget_group are summed against one budget ---
budget_groups:
  router: 220
# --- 2nd-class file set (user-approved 2026-07-04; ScenarioEngine excluded) ---
second_class_files:
  - path: EnvironmentSimulator/Modules/RoadManager/RoadManager.hpp
    upstream_blob_sha: 8dbd661856ced5d7b120901a4c82dfe1fe9b6838
    budget_nonblank: 75
    additive_only: true
    marker_census: {}                    # per-marker-id -> nonblank added lines (empty = zero-edit baseline)
    pr_slice: "PR-A..D"
    status: baseline
  - path: EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp
    upstream_blob_sha: 932165b98754d49edccdba0c879ef8b31a9c74df
    budget_nonblank: 550
    additive_only: false
    marker_census: {}
    pr_slice: "PR-A..D"
    status: baseline
  - path: EnvironmentSimulator/Modules/RoadManager/LaneIndependentRouter.cpp
    upstream_blob_sha: 06a03974266f5852de645c74c6d48c77af5579e2
    budget_nonblank: 220                 # combined router budget (cpp+hpp) -- enforced via budget_group
    budget_group: router
    additive_only: false
    marker_census: {}
    pr_slice: "PR-C"
    status: baseline
  - path: EnvironmentSimulator/Modules/RoadManager/LaneIndependentRouter.hpp
    upstream_blob_sha: bf0cd4c7a74ab03e66f54db1d594b392797bf0b8
    budget_nonblank: 0                   # shares the 220-line router budget with the .cpp row (budget_group)
    budget_group: router
    additive_only: false
    marker_census: {}
    pr_slice: "PR-C"
    status: baseline
  - path: EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSIReporter.cpp
    upstream_blob_sha: 9c4ea053e24c70330e24da357e3558e80b8617b0
    budget_nonblank: 30
    additive_only: false
    marker_census: {}
    pr_slice: "PR-D"
    status: deferred-until-PR-D
  - path: EnvironmentSimulator/Modules/Controllers/ControllerLooming.cpp
    upstream_blob_sha: 93b039f40f11577e79ff05f541ba294af6f757b0
    budget_nonblank: 10
    additive_only: false
    marker_census: {}
    pr_slice: "PR-C"
    status: baseline
# --- fork (1st-class, existing 150-line regime; census cross-checked two-sided) ---
fork_file:
  path: GT_esmini/src/road/GT_RoadManager.cpp
  pristine_counterpart: EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp
  marker_census:        # measured per-id NONBLANK added lines vs the upstream snapshot; sums to fork_odr_expect_lines (75)
    hook: 6             # include 1 + BuildSideModel call-site 5
    country-rev: 2      # init line 1 + condition-flip line 1 (flip line via legacy_sites)
    junc-abort: 3
    obj-roadsurface: 5
    lane-types: 5
    tl-gate: 9          # SetTrafficLightInfo nr_lamps_ block 5 + gate relaxation 4
    sig-pos: 9          # pose-resolution block 7 + 2 SetSignal-ctor-arg lines
    sig-ref: 9          # else-if 1 + post-parse materialization hunk 8
    sig-lanes-guard: 9
    direct-junc-log: 5
    junc-crossing: 13   # crossing type dispatch 7 + IsOsiIntersection empty-connection guard 6
  lht_census: 8         # [GT_LHT]-attributed: comment hunk 5 + 3 swapped-branch lines (legacy_sites)
  header_census: 16     # the "GT_esmini modification" file-header comment block
  legacy_sites:         # pre-S0 hunks whose ADDED lines carry no in-hunk marker (attribution by exact
                        # fork line-span + nonblank count; frozen -- do NOT grow this list for new work,
                        # new hunks must carry in-hunk markers)
    - marker: country-rev
      fork_lines: "4852-4852"
      count: 1
      note: "condition-flip line (empty() negation); the [GT_ODR:country-rev] marker sits on the init line one hunk above"
    - marker: GT_LHT
      fork_lines: "5891-5891"
      count: 1
      note: "LHT 1-A swapped branch condition (contactPoint==END); [GT_LHT] comment is a separate hunk at :5885"
    - marker: GT_LHT
      fork_lines: "5893-5893"
      count: 1
      note: "LHT 1-A swapped branch body (last lane section)"
    - marker: GT_LHT
      fork_lines: "5897-5897"
      count: 1
      note: "LHT 1-A swapped else-branch body (first lane section)"
# --- overlap residuals (v1: zero residuals; sites declared for S2/S5) ---
overlap_residuals:
  - site: "Junction::GetRoadConnectionByIdx"
    fork_markers: ["[GT_LHT]"]
    vj_marker: "vj-lanes"
    residual_nonblank: 0
    fork_variant_test: "pending (fixture 23b, S5)"
  - site: "ParseOpenDriveXML junction/connection loop"
    fork_markers: ["[GT_ODR:junc-crossing]", "[GT_ODR:junc-abort]"]
    vj_marker: "vj-parse-junction"
    residual_nonblank: 0
    fork_variant_test: "pending (parse-variant fixture, S2)"
# --- interpretation rules (recorded per design §5; :interp goldens re-baseline on upstream convergence) ---
rules:
  elementdir_reverse_merge: >-
    INTERPRETIVE (:interp). elementDir '+' means the linked main road is traversed in
    increasing s across the anchor; '-' decreasing. Synthesized counter-connections
    (branch->main) invert the composition: landing heading on the main road = main-road
    tangent at anchor_s, flipped when elementDir '-'; departure main->branch selects the
    branch contact end per the same rule. Absent/UNKNOWN elementDir falls back to
    geometric nearest-heading with WARN. ASAM 10.4 does not pin the reverse composition;
    surfaced upstream in #592/PR-B.
  vj_lanes_merged_semantics: >-
    Junction::GetRoadConnectionByIdx merged fork rule: incoming_contact_s_>=0 ->
    GetLaneSectionByS(anchor) takes precedence; else contactPoint==END -> last lane
    section ([GT_LHT] rule); else first section. Fork-variant test: LHT junction fixture
    x VJ counter-connection (23b).
  pristine_marker_strip: >-
    The pristine copies CARRY [GT_ODR:vj-*] markers permanently; upstream PR branches
    are GENERATED by script-stripping markers (S8 tooling). Marker grammar: single
    [GT_ODR:<id>] inside hunks <=15 nonblank lines; block [GT_ODR:<id>-begin]/[GT_ODR:<id>-end]
    for larger hunks; :interp suffix on interpretation-point hunks.
exclusions:
  - target: wasm (GT_esmini/web/wasm, esminiJS)
    reason: >-
      Pre-existing link break independent of P6: wasm CMakeLists.txt:61-72 swaps in
      GT_RoadManager.cpp which includes gt_esmini/road/OdrSideModel.hpp (fork :78), but
      the wasm target does not compile GT_esmini/src/road/odr_side/*.cpp -> unresolved
      BuildSideModel at link. Structural evidence (emsdk build not run). wasm targets are
      excluded from the VJ invariance contract until the odr_side link repair lands.
```
<!-- GT-2ND-CLASS-MANIFEST-END -->
