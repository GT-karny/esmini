# VD 駐車機能 検証計画

> ステータス: **未実装（計画のみ）**。実装は本文書ではなく
> [parking_maneuver_design.md](parking_maneuver_design.md) が扱う。
> 知識グラフ: `req-vd-ad:REQ-AD-019`（駐車枠の探索・選定）/ `req-vd-ad:REQ-AD-020`（駐車マヌーバの実行）/
> `req-vd-ad:REQ-AD-022`（出庫マヌーバの実行）、`vd-func:FUNC-076`/`FUNC-077`/`FUNC-078`。
> 受け皿シーンは `scene:SCN-014`。

## 1. 目的とスコープ

`parking_maneuver_design.md` §11 は、実装フェーズA〜Eの各完了条件に「対応する負matcherが緑であること」を課す。
1本のシナリオが完走するだけでは、境界条件や負系（禁止動作が起きないこと）を要求できないためである。
本文書は、この完了条件を満たすために何を作るかの計画であり、次を定める。

- 検証すべき観点のカタログ（§2）
- 観点を発火させる刺激資産（xodr/xosc/バッチ）のマトリクス（§3）
- 各matcherと、そのmatcherが実際に赤くなる（FAILする）ことを示す資産の対応（§4）
- しきい値の根拠を資産側に残す運用（§5）
- 自動化できる範囲とG29実機が要る範囲の区分（§6）
- 現時点で未設計のまま残す接続点（§7）

実装（駐車コントローラ本体、matcher関数、資産生成スクリプト）はいずれもフェーズA以降の作業であり、本文書は
その前提となる計画を固定するにとどまる。

## 2. 検証観点カタログ

観点は9カテゴリに分かれる。
各観点には slug を付け、実装時に matcher・資産・knowledge graph の記述から一貫して参照できるようにする。

### 2-1. 機能正常系

`req-vd-ad:REQ-AD-019`/`REQ-AD-020` の acceptance_ladder 各段に対応する。

| slug | 内容 |
| :--- | :--- |
| `parking-space-enumeration` | 019a: parkingSpace オブジェクトの列挙が正しく行える |
| `occupied-slot-exclusion` | 019b: 占有中の枠が候補から除外される |
| `access-attribute-respect` | 019c: access 属性が自車属性に合わない枠が除外される |
| `reselection-on-approach-occupation` | 019d: 接近中に選定枠が占有された場合、次点へ再選定する |
| `forward-entry-completion` | 020a: 前進入庫が完走する |
| `reverse-entry-completion` | 020b: 後退（バック）入庫が完走する |
| `mandatory-pose-accuracy` | 020c 必須水準（§1 の必須基準）を満たす |
| `desirable-pose-accuracy` | 020c 望ましい水準（§1 の望ましい基準）を満たす |
| `advanced-scope-nongoal` | 020d が受入対象外と明記する高度化（マヌーバ中の複数回再選定等）が、対象外のままであることの負check |

### 2-2. 境界・レジーム

`parking_maneuver_design.md` §4 の幾何式が定めるレジーム境界を、境界の両側から挟んで確認する観点である。

| slug | 内容 |
| :--- | :--- |
| `han-regime-1-generous-offset` | `ΔY >= R_c + p_r`（§4-2 レジーム1、`Wmin = w0`） |
| `han-regime-2-mid-offset` | `R_c <= ΔY < R_c + p_r`（レジーム2） |
| `han-regime-3-tight-offset` | `ΔY < R_c`（レジーム3） |
| `three-to-five-motion-boundary` | 枠幅 `= Wmin` ちょうどの両側で三画/五画運動が切り替わることを確認する |
| `theta-45-degenerate-four-motion` | `θ = 45°` で中間の直進区間が消滅し四画運動になる境界（§4-3） |
| `theta-min-boundary` | `θ_min`（枠境界をクリアできる最小値）の成立/不成立を両側から確認する |
| `cusp-count-0-1-2-3-regimes` | 切り返し回数0/1/2/3の各regimeを狙って誘発する |
| `aisle-width-lower-bound` | 通路幅が `Xmin`/`Ymin`（§4-2）を満たさない場合に不成立となる |
| `wmin-exact-boundary` | 枠幅 `= Wmin ± ε` で合否が反転することを確認する。式の実装検算そのものを兼ねる |

### 2-3. 縮退・負系

正常に「入れない」判断ができることを確認する観点である。

| slug | 内容 |
| :--- | :--- |
| `no-free-slot-degradation` | 全枠が占有されている場合、入庫しない |
| `undersized-slot-rejection` | 枠寸法が最小要件未満の場合、候補から除外する |
| `access-mismatch-rejection` | access 属性が合わない枠を候補から除外する |
| `slot-stolen-during-approach` | 接近中に選定枠が奪われた場合、再選定する（`reselection-on-approach-occupation` と対） |
| `slot-not-reselected-during-maneuver` | マヌーバ実行中（§3-5 が定義する範囲）は枠を乗り換えない、という負check |
| `rs-infeasible-slot-rejection` | Reeds-Shepp 検算（§6）が infeasible を返す枠は候補から除外する |
| `correction-loop-exhaustion` | 修正ループ（§7）が3回の切り返しに達しても必須水準を満たせない場合、そのまま停止し未達を報告する |
| `slot-selection-tie-break-stability` | 同点候補が複数ある場合の選定が、実行のたびに同じ結果になる（決定的である） |

### 2-4. 相互作用

既存ポリシー（横断歩道・対向交通・通過交通）との重なりを確認する観点である。

| slug | 内容 |
| :--- | :--- |
| `pedestrian-crossing-aisle-during-approach` | 接近中にアイルを横断する歩行者がいる場合の挙動 |
| `pedestrian-behind-during-reverse` | 後退中に自車後方に歩行者がいる場合の挙動 |
| `oncoming-aisle-traffic` | 対向のアイル交通がある場合の挙動 |
| `adjacent-occupied-slot-clearance` | 隣接する占有枠の車両とのクリアランス |
| `crosswalk-policy-coexistence` | 既存 `CrosswalkPedestrianAware` と駐車モードが同一区間で重なる場合の優先関係。**未設計**（§7） |
| `through-traffic-unaffected-by-parking-ego` | 駐車中の自車が、駐車と無関係な通過交通の挙動に影響を与えない負check |

### 2-5. 左右・向き

| slug | 内容 |
| :--- | :--- |
| `left-side-slot-selection` | 進行方向左側の枠を選ぶケース |
| `right-side-slot-selection` | 進行方向右側の枠を選ぶケース |
| `aisle-approach-both-directions` | アイルへの進入方向が両方向あるケース |
| `mirror-symmetry-consistency` | 左右版シナリオの終端姿勢誤差分布を機械比較し、鏡像符号バグ（左右どちらかだけ式の符号を誤る類のバグ）を検出する |
| `lht-rht-parking-mirroring` | LHT/RHT の切替と駐車のミラーリングが両立する。`scene:SCN-016` の LHT/RHT 軸との交点 |

### 2-6. 装備系

信号・ライトまわりの観点で、§9（自動ハザード点灯）と関連する。

| slug | 内容 |
| :--- | :--- |
| `hazard-covers-maneuver-window` | マヌーバ区間の全体がハザード点灯区間に包含される |
| `hazard-off-outside-maneuver` | マヌーバ区間外ではハザードが消灯している、という負check |
| `reverse-gear-light-consistency` | 後退ギア指令（§8）とバックランプ点灯の整合 |
| `steer-rate-limit-compliance` | `\|dδ/dt\| <= v_δ`（§5-2）が全区間で守られる |
| `indicator-excluded-during-hazard` | ハザード点灯中は通常の方向指示器書き込みが行われない（§9-5 案a） |
| `scenario-lightstateaction-always-wins` | xosc の `LightStateAction` が明示制御する灯火は、駐車モードのフックより常に優先される（§9-4） |

### 2-7. モード遷移

`parking_maneuver_design.md` §2-2/§7 の状態機械そのものを確認する観点である。

| slug | 内容 |
| :--- | :--- |
| `mode-entry-trigger-fires-correctly` | 開始条件（§2-2）が成立したとき、駐車モードへ正しく遷移する |
| `mode-entry-does-not-false-fire` | 開始条件が成立していないとき、駐車モードへ誤って遷移しない負check |
| `mode-exit-clean-on-completion` | マヌーバ完了による終了条件（§2-2）が正しくクリーンに働く |
| `mode-exit-clean-on-scenario-takeover` | シナリオ/手動運転者による制御奪取（§2-2 第3の終了条件）が正しくクリーンに働く |
| `mode-exit-clean-on-correction-exhaustion` | 修正ループ打ち切り（§7-3）による終了が正しくクリーンに働く |
| `fsm-state-coverage` | §5-3 の FSM（`IDLE`〜`DONE`）の全状態が、最低1つの資産で通過する |
| `non-parking-scenario-regression` | 駐車機能の追加が既存（駐車以外の）挙動を壊さない。既存バッチの再実行で兼ねるため新規資産は不要 |

### 2-8. 計器・ハーネス

観測経路そのものの生死を確認する観点である。
observabilityの欠如は、matcherを正しく書いても検出できないため、機能正常系と独立に扱う。

| slug | 内容 |
| :--- | :--- |
| `final-pose-observability` | 終端姿勢（ヨー偏差・横偏差）を observability 面で観測できるか。**現状観測不能** |
| `cusp-count-observability` | 切り返し回数を observability 面で観測できるか。**現状観測不能** |
| `hazard-observability-path` | ハザード状態の観測経路。フェーズC前倒し実装（`parking_maneuver_design.md` §10-2）で解消する |
| `reverse-gear-observability` | 後退ギア状態の観測経路 |
| `rejection-reason-observability` | 枠が却下された理由（占有/access不一致/寸法不足/RS不可）を区別して観測できるか |
| `correction-attempt-count-observability` | 修正ループの試行回数の観測。切り返し回数（cusp count）とは別量であり、両者を混同しない |
| `frames-alive-sanity` | `frames > 0`。駐車コントローラが実際に `Step()` されていることの生存判定 |

### 2-9. その他

| slug | 内容 |
| :--- | :--- |
| `mode-entry-speed-threshold-boundary` | 開始条件の速度しきい値（§2-2）の境界 |
| `maneuver-duration-upper-bound` | マヌーバ所要時間の上限。無限ループ検知を兼ねる |
| `f7-override-interaction-during-standstill-steer` | 停車中転舵とF7手介入検出の相互作用（§5-1）。**G29実機必須、手動限定カテゴリ**（§6） |
| `forward-parking-contact-point-inversion-check` | §4-4 の自前導出（衝突チェックの当たり点反転）の検算。前角が枠外にはみ出す配置を狙った負資産 |
| `high-occupancy-scale-stability` | 枠の大半が埋まっている高占有率のロットでの安定性 |
| `parking-lane-boundary-non-encroachment` | マヌーバ中、隣接レーンの境界を越えない |

## 3. 刺激資産マトリクス

### 3-1. xodr（コア5種）

生成方式は `parking_maneuver_design.md` §10-4 が決定した**方式B**（`create_parking_lot.py` を直接改変せず、
`resources/scenario_authoring/` 流儀の GT 側ポスト処理として分離）に従う。

| ID | 内容 |
| :--- | :--- |
| `PK-XODR-01` | 標準ロット。1セグメント6-8枠、全空き、access=all、枠寸法2.5×5.0m |
| `PK-XODR-02` | 枠幅が `Wmin` 未満（五画運動を誘発する） |
| `PK-XODR-03` | 狭通路（`aisle-width-lower-bound` 用） |
| `PK-XODR-04` | access 属性混在（一部 handicapped/residents） |
| `PK-XODR-05` | アイルに横断歩道つき |

### 3-2. xosc（コア約15本、PK-XODR-01基準）

- `parking_forward_entry`
- `parking_reverse_entry_regime1`/`regime2`/`regime3`（`ΔY` 大/中/小、§2-2 の3レジームに対応）
- `parking_correction_loop_forced`（望ましい水準に未達 → 修正ループを1回誘発）
- `parking_all_slots_occupied`（負）
- `parking_access_mismatch_only`（負、PK-XODR-04 使用）
- `parking_late_occupation_before_mode`（駐車モード開始前に選定枠が占有される）
- `parking_late_occupation_during_maneuver`（負。マヌーバ実行中に占有される）
- `parking_pedestrian_crossing_aisle`
- `parking_pedestrian_behind_reverse`
- `parking_oncoming_aisle_traffic`
- `parking_left_side_slot` + `parking_right_side_slot`（`mirror-symmetry-consistency` の機械比較に使う対）
- `parking_non_parking_route_regression`（既存バッチ + matcher1行の追記で兼用。新規資産は不要）

### 3-3. 拡張セット（手動運用、`junction_conflict_batch.yaml` と同格）

常設回帰には載せず、手動実行のバッチとして保持する。

- `undersized_slot_rejection`
- `theta45_degenerate`
- `theta_min_boundary`
- `aisle_width_infeasible`
- `high_occupancy_scale`
- `lht_mirrored`
- `forward_contact_point_check`
- `crosswalk_coexistence`

### 3-4. バッチ設計

**`parking_maneuver_batch.yaml`（常設候補）**は、正例と負例を同一バッチに混在させる。
`resources/xosc/verification/aeb_safety_batch.yaml` が確立した前例と同じ理由による。
同バッチのヘッダコメントは「AEB は『衝突を緩和する』と『衝突コースが無いのに発火しない』が対で初めて主張になり、
片方だけを回帰に載せると閾値を下げて正例を通し負例を壊す退行がゲートを素通りする」と明記しており、正負を
同一マニフェスト・同一ベースラインに置くことで per-matcher 照合が必ずその取引を捕まえる。
駐車機能でも同じ構図が成り立つ。
たとえば「占有枠を除外する」（正）と「除外判定を無効化しても検知漏れなく再選定できる」（負の裏付け）を別バッチに
分けると、占有判定の閾値や条件を緩めて正例だけを通す退行が、負側のバッチを回さない限り検知されない。

**`parking_maneuver_extended_batch.yaml`（手動）**は §3-3 の拡張セットを束ねる。

全 xodr は ODR 適合 quick（`scripts/run_odr_conformance.py --profile quick`）を通す。
全 xosc は waypoint 規律（`scripts/check_route_waypoints.py`。issue #31 の根本原因＝ジャンクションを跨ぐ Route が
ConnectingRoad の Waypoint を落とすと `RoadManager::Route::AddWaypoint` が経路解決に失敗し、ルートが黙って
打ち切られる、という規律）に準拠する。

## 4. matcher拡充

### 4-1. 赤実証資産という考え方

matcher は「正しい挙動で緑になる」ことに加えて、「壊れた挙動では確実に赤くなる」ことが必要である。
前者だけを確認して matcher を常設すると、条件式の書き間違いや閾値の緩め忘れがあっても常に緑を返し続ける
matcher が回帰ゲートに紛れ込む。
これは実測やシミュレーション計器が対象より鈍く、問題を検出できないまま何週間も見過ごされた実例（本プロジェクトの
計測規律で繰り返し確認されてきた失敗形）と同じ構造であり、matcher についても同じ規律を適用する。
このため、新設・拡充する matcher は必ず「その matcher が実際に FAIL する資産」（**赤実証資産**）を1つ以上対応づけ、
資産が存在しない matcher は常設ゲートに載せない。

### 4-2. 拡充表

| matcher | 状態 | 概要 | 赤実証資産 |
| :--- | :--- | :--- | :--- |
| `parking_final_pose` | 新規（必須/望ましい2閾値セット） | §1 の必須/望ましい水準を判定する | `parking_correction_loop_forced` |
| `parking_cusp_count` | 新規 | 切り返し回数の上限判定 | `theta_min_boundary` 近傍で3回超になる配置 |
| `parking_hazard_active` | 新規（正） | マヌーバ区間がハザード点灯区間に包含される | コア資産（既存分） |
| `parking_hazard_off_outside_maneuver` | 新規（負） | マヌーバ区間外でハザードが消灯している | 通常走行バッチ（既存）へ must として追加 |
| `parking_indicator_excluded_during_hazard` | 新規（`respect_scenario_override` フラグ付き） | ハザード中は通常の指示器書き込みをスキップする | 同上 |
| `parking_slot_selected` | 新規（`expect_none` 対応） | 選定した枠のIDを判定する | `parking_all_slots_occupied`/`parking_access_mismatch_only` で `expect_none: true` |
| `parking_slot_excludes_occupied` | 新規 | 占有枠が選定から除外される | 占有判定を無効化する config を当てた同一資産 |
| `parking_reselection_on_approach_occupation` | 新規 | 接近中の占有発生で再選定する | `parking_late_occupation_before_mode` |
| `parking_no_reselection_during_maneuver` | 新規（負） | マヌーバ実行中は枠を乗り換えない | `parking_late_occupation_during_maneuver` |
| `parking_correction_loop_bounded` | 新規 | 修正ループの試行回数が3以下（`cusp_count` とは別量） | `correction-attempt-count-observability` 系の資産 |
| `parking_gives_up_reports_telemetry` | 新規 | 打ち切り（§7-3）で未達フラグがテレメトリへ出る | `correction_loop_exhaustion` |
| `parking_steer_rate_bounded` | 新規 | `\|dδ/dt\| <= v_δ` | `v_δ` を config で緩めた同一資産（`deceleration_profile_smooth` の jerk 計算と同型の赤実証） |
| `parking_reverse_gear_matches_segment` | 新規 | 後退指令区間とギア状態の整合 | E2E での赤実証は困難なため C++ 単体テストで赤実証し、E2E は緑担保のみとする |
| `min_obb_separation_above` | 既存の再利用 | 衝突分離判定。新設せず既資産の `must:` に載せる | （既存の赤実証を継承） |
| `parking_mode_not_entered_without_trigger` | 新規（負） | 開始条件不成立時にモードへ入らない | 既存バッチの `expectations` へ1行追加するのみ、新規資産は不要 |

`v_δ` の config 化は、`parking_steer_rate_bounded` の赤実証を「実装コードを壊す」ではなく「config を緩める」で
作れる形にするための前提であり、`parking_maneuver_design.md` §5-2 の設計に含める。

### 4-3. 運用コスト

matcher を追加するたびに `namespaces.yaml` の `matcher` エントリの enum 形式 `id_pattern`
（source_of_truth: `GT_esmini/web/backend/services/vd_metrics.py`）へ追記が必要である。
本文書が計画する新規 matcher は14件あり、これは1回の作業で済む追記ではなく、フェーズごとに matcher を
拡充するたびに繰り返し発生する運用コストとして計画に織り込む。

## 5. しきい値の根拠を資産メタデータに残す運用

`parking_maneuver_design.md` §1 は望ましい水準のヨー偏差 ±3° を「ISO 16787 の実務水準を示す二次資料の値。
原文は未確認」と明記している。
このように**原典を確認していない値**をそのまま使う場合、根拠の出典と確認状況を資産側（`expectations.yaml` の
`notes` フィールド、既存の `aeb_safety_batch.yaml` や `stop_line_stop_target.md` の実測記録が同じ形を取る）に
残す。

枠寸法・`Wmin`/`Xmin`/`Ymin` 等、幾何式から導かれる閾値についても同様に、式のどのパラメータからどう導いたかを
資産メタデータへ残す。

この運用の目的は、しきい値がのちに調整されるとき、「なぜこの値だったか」を追えるようにすることである。
根拠が残らないまま調整を繰り返すと、失敗を隠すために閾値だけを緩め、原因を追わない対応が積み重なる
（本プロジェクトで過去に3週間気づかれなかった実例がある）。
根拠が資産側に固定されていれば、緩める変更は根拠との食い違いとして差分に現れる。

## 6. 自動化可能/手動限定の区分

| 区分 | 対象 | 理由 |
| :--- | :--- | :--- |
| 自動化可能 | §2-1〜§2-8 のほぼ全観点 | `gt_sim_test.py batch` による in-process 実行（`GT_esminiLib.dll`、venv `DriverScript/.venv`）で完結する |
| 手動限定（G29実機必須） | `f7-override-interaction-during-standstill-steer` | 停車中転舵とF7手介入検出の相互作用は、実ハンコンのFFBサーボ追従特性に依存し、in-process 実行では代替できない |

`parking_maneuver_batch.yaml`/`parking_maneuver_extended_batch.yaml` はいずれも自動化可能な観点だけを束ね、
G29実機必須の観点は別途 field-test 手順（`field-test/` 配下の既存文書と同じ形）で扱う。

## 7. 残ギャップ

- **通路長制約（`Xmin`/`Ymin`）と RS 検算の接続が未設計**。どの段で通路長不足を弾くかが決まっていない
  （`parking_maneuver_design.md` §13 のオープン課題と同一）。
- **`crosswalk-policy-coexistence` の優先関係が未設計**。既存 `CrosswalkPedestrianAware` と駐車モードが同一
  ego・同一区間で重なる場合にどちらを優先するかが決まっていない。
- **LHT/RHT × 駐車のミラーリング検証は拡張セット送り**（`lht_mirrored`、§3-3）。常設回帰には含めない。
- **tie-break の決定性**（`slot-selection-tie-break-stability`）は、実装時に選定ロジック側の仕様として明記する。
  本文書は観点としてのみ立て、判定規則そのものは定めない。
