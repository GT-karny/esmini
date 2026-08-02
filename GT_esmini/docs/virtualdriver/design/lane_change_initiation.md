# VD 自発的レーンチェンジの発起 (LaneChangeInitiation) — 誰が横を動かしてよいかを決めてから動かす

> ステータス: **設計確定 / 実装中**。
> 知識グラフ: `vd-func:FUNC-055`（自発的レーンチェンジ *発起*）、`req-vd-ad:REQ-AD-017` の
> **acceptance_ladder 段 c**（接続点までに目標レーンへ自発的に車線変更する）。
> 前段は `vd-component:route-lane-plan`（段 a/b、`route_lane_plan_design.md`）。
> 実行側の快適化は `vd-func:FUNC-020`、方向指示器は `vd-func:FUNC-061`。
> **既定 OFF**。有効化しない限り既存挙動はビット単位で不変であることを設計要件とする。

## 1. 出発点 — 何が既にあり、何が無かったか

`route_lane_plan_design.md` の層で「どの車線に居るべきか」「いま外れているか」「接続点まで何 m か」は
毎フレーム出ている。だが**診断のみで是正しない**。目標レーンから外れたまま接続点を通過し、逸脱を
記録して終わる。本設計はその是正、すなわち「自分から車線変更を起こす」側を作る。

着手前に4方向の調査を行い、その結果**当初の想定が2つ覆った**。設計はその2つに強く依存しているので、
先に書く。

### 覆った想定1: ギャップ受容は `ConflictPointResolver` から流用できない

想定は「コリドー＋OBB のギャップ受容判定があるのだから、隣接レーンの前後車にも流用できるはず」
だった。**流用できない。** `ConflictPointResolver` は自分自身のコードで並走関係を機械的に除外している。

```cpp
// (a) Same-direction filter: if the two corridors run nearly parallel /
// same-heading through the region, it is a following relationship, not a
// crossing — leave it to LeadVehicleAware.
if (conflict_geom::CrossingAngleDeg(edx, edy, odx, ody) < cfg_.min_cross_angle_deg)
    continue;  // near-parallel -> not a crossing conflict
```
（`src/control/virtualdriver/policies/ConflictPointResolver.cpp:629-637`、`min_cross_angle_deg` 既定 20°）

車線変更で問題になる隣接車との相対運動は**定義上ほぼ平行**（交差角 ≈ 0°）であり、このフィルタに必ず
弾かれる。判定の核も「経路交差領域への到達時刻窓の重なり」であって、「隣接レーン上の縦方向ギャップが
十分か」とは別の数学である。コリドー生成・タイミング判定・コミットラッチはいずれも交差点前提が
埋め込まれており、転用より新規設計が安い。

流用できるのは `conflict_geom` の純幾何関数（`ConvexClip` / `PolygonArea` 等、
`ConflictPointResolver.hpp:44-88` で公開）のみだが、ギャップ判定は本質的に1次元問題なので**使わない**。

**正しい土台は `LeadVehicleAware` の方だった。**

```cpp
roadmanager::PositionDiff diff = {};
if (!ego->pos_.Delta(&other->pos_, diff, false, cfg_.lookahead)) continue;

if (diff.dLaneId != 0) continue;                 // same lane only
if (diff.ds <= 0.0) continue;                    // must be ahead
```
（`src/control/virtualdriver/policies/LeadVehicleAware.cpp:66-71`）

この2行の除外条件を緩めるだけで、隣接レーンの前走車と後続車が同じ枠組みで取れる。しかも
`Position::Delta()` の `bothDirections` 引数は**既定 true**（`RoadManager.hpp:4357-4361`）で、
`LeadVehicleAware` が明示的に `false` を渡して後方探索を切っているだけである。後方接近車の探索は
API 側に既にある。

### 覆った想定2: 軌道生成は新規に書く必要がない

想定は「resume-merge のクインティックは `d(T)=d'(T)=d''(T)=0`（収束先ゼロ）前提だから、
一般の車線変更には使えない。境界条件から再導出が要る」だった。**再導出は要らない。**

`ResumeMergeProfile` は**レーンについて一切の意見を持たない**モジュールである。

```
// This module has no opinion on lanes at all; it
// only guarantees the OUTPUT keeps d0's sign until it reaches zero
```
（`include/gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp:102-104`）

一方 `TrajectoryShortPlanner` は、merge 中はプレビューのアンカーを**現在レーンではなく
`ctx.merge_track_id` / `ctx.merge_lane_id` に置き**（`TrajectoryShortPlanner.cpp:155-158`）、
そこからの絶対オフセット `d(t)` を各サンプル点に重畳する（同 `:238-260`）。

つまり既存の機構は既に「**別のレーンにアンカーを置き、そこまでの横偏差をゼロへ持っていく**」を
実行している。これは車線変更そのものである。resume-merge が「元のルートレーンへ戻る」に見えるのは、
呼び出し側がアンカーに**ルートレーン**を渡しているからにすぎない。

したがって本設計は**アンカーに目標レーンを渡す**。式も `TrajectoryShortPlanner` も変更しない。
`d0` は「自車から目標レーン中心までの符号付き横距離」であり、`d(T)=0` は「目標レーン中心に乗る」を
意味する。境界条件は最初から正しい。

**この判断が本設計の最大の梃子である。** 軌道生成・快適性グリッド探索（`SelectResumeMergeDuration`）・
`d''(0)=a0_lat` の連続性保証・`comfort_unmet` の非黙殺が、すべて無改造で手に入る。

## 2. 所有権 — どこに差し込むかは1点に決まる

VD の横コマンドは次の順で確定する（`src/control/ControllerVirtualDriver.cpp`）。

| 順 | 位置 | 何が起きるか |
| :--- | :--- | :--- |
| 1 | `:879-895` | `ShortPlanContext sctx` 構築 → `short_planner_->Plan(sctx)` |
| 2 | `:897-923` | `driver_model_->Compute(...)` → `auto_cmd`（AD の生の横コマンド） |
| 3 | `:935-938` | `ComputeAdSteeringEnvelope()` が横加速度・ヨーレート・操舵レートで**クランプ** |
| 4 | `:940-982` | `lat_manual` なら `cmd.steering` を運転者の実軸で**丸ごと上書き** |
| 5 | `:989-1023` | `ledger.PublishLateral(...)` でコマンドバスへ |

**`DomainOwnershipLedger` はコントローラ間の所有権しか裁定しない。** `PublishLateral` は
所有者一致だけを見て、override 状態を一切見ない（`src/control/common/DomainOwnershipLedger.cpp:125-128`）。
手介入の防御は台帳ではなく、**上の順序 4 がコード上で上書きしていること**だけで成立している。

したがって設計上の帰結は一意である。

> **AD 発起の横出力は、必ず順序 1（`sctx`）を通して合流させる。**
> そうすれば包絡線（3）と手介入上書き（4）を**自動的に継承する**。
> バスへ直接書いたり、順序 4 より後で `cmd.steering` に触れたりしてはならない
> — 手介入中でも AD の操舵が出てしまう。

### 優先順位を1か所に固定する

VD 内部の横方向の競合は、現状**2か所に分散した手書きロジック**でしか裁かれていない。

- `TrajectoryShortPlanner.cpp:222-261` — `if (!lat_actions.empty()) ... else if (ctx.merge_active) ...`
  （storyboard LC が resume-merge に優先）
- `ControllerVirtualDriver.cpp:805-809` — resume-merge の disarm 3トリガー
  （storyboard 横アクション / 手介入 / ルート喪失）

ここに3つ目の生産者を無防備に足すと、**無視されるか、オフセットが加算されて未検証の合成軌道になるか**の
どちらかになる。特に `lat_actions` は複数エントリを**無条件に加算合成**する
（`TrajectoryShortPlanner.cpp:227-236` の `px += delta_t * tx` 累積）ため、
「AD LC を本物の `LatLaneChangeAction` として `Object` に注入する」実装は**採らない**
（storyboard の LC と加算されて壊れる）。

**確定した優先順位（この順で、コントローラ側の1か所に書く）**:

```
storyboard LaneChangeAction  >  resume-merge  >  AD 発起 LC
```

- storyboard 横アクションが RUNNING の間は AD LC を**発起しない / 進行中なら中止**
- `resume_merge_state_.active` の間は AD LC を**発起しない**（resume-merge 側に第4トリガーを
  足さない。足すと2つの状態機械が相互に disarm し合う構造になる）
- `lat_manual` の間は AD LC を**発起しない / 進行中なら中止**

判定には既存の `has_lateral_storyboard_action` と同じ走査を使う
（`ControllerVirtualDriver.cpp:790-800`）。

## 3. 発起の決断距離 — 単一の固定値にしない

検証に使う道路（`resources/xodr/highway_example_with_merge_and_split.xodr`）の実測が、
単一固定値では破綻することを示している。

- 出口ランプへの接続は `laneLink from="-4" to="-1"` **1本のみ**。road 0 の lane -4 からしか入れない
- ego は lane -1 から出発する。到達には **-1→-2→-3→-4 の3回連続**の車線変更が要る
- 接続点までは 190 m（実測 `dist_to_connection=190.00`）
- **lane -4 はテーパである**。`laneSection s=50` で幅0から生え、
  `width a=0 b=0 c=0.000576 d=-3.072e-06` に従って s=175 で 3.0 m に達する

多項式を評価すると幅は s=75 で 0.31 m、s=112.5 で 1.50 m、s=150 で 2.69 m。
つまり **190 m は「3回ぶん自由に使える 190 m」ではない**。最後の -3→-4 は実質的に締切側へ寄る。

そこで決断距離は**残りの車線変更回数に比例させる**。

```
required_m = n_remaining * max(v * lane_change_lead_time_s, lane_change_min_lead_distance_m)
           + lane_change_reserve_distance_m
```

`dist_to_connection <= required_m` になったフレームで1回目を発起する。`n_remaining` は現在レーンから
目標レーン帯までの最小ホップ数。既定値（`lead_time_s=6.0`, `min_lead_distance_m=40.0`,
`reserve_distance_m=20.0`）を上記シナリオに入れると
`3 * max(83.3, 40) + 20 = 270 m > 190 m` となり、**走行開始の1フレーム目で発起する**。
締切が厳しい道路では早く動き、余裕がある道路では接続点手前まで待つ、という意図した挙動になる。

`lead_time_s` を LC 実行時間（実測 ~3.5 s）より大きい 6.0 s にしているのは、**ギャップ待ちの時間を
含める**ため。ギャップが空かずに1回ぶん待たされても次の締切に間に合う。

### 1回に1レーンだけ動く

3レーン先へ一気に軌道を張らない。`d0` を1レーンぶん（現在レーン中心 → 隣接レーン中心）に取り、
完了したら次の1回を発起する。理由は3つ。

1. ギャップ受容は**隣接レーンに対してしか判定できない**。2つ先のレーンの前後車との隙間は、
   1つ目のレーンに入るまで意味を持たない
2. `SelectResumeMergeDuration` の快適性グリッド探索は `d0` に対して T を選ぶ。3レーンぶんの `d0` を
   渡すと、快適性を満たす T が長大化するか `comfort_unmet` になる
3. 中止（手介入・storyboard 割り込み）したときの復帰状態が、レーン境界上ではなくレーン中心になる

完了判定は「`state.active` が false になり、かつ自車の lane id が今回の目標レーン」。
目標レーンに乗れていなければ、新しい `d0` で**同じレーンへ再発起**する。

## 4. ギャップ受容 — 縦方向1次元問題として解く

`LeadVehicleAware` の走査パターンを土台に、隣接レーン（`dLaneId` が目標方向へ ±1）の
前走車・後続車を取る。`Delta()` の `bothDirections` は **true** を渡す（後方探索を有効にする）。

受容条件は3つすべてを満たすこと。

| 条件 | 式 | 既定 |
| :--- | :--- | :--- |
| 前方ギャップ | `gap_lead >= max(gap_min_m, v_ego * headway_lead_s)` | `gap_min_m=8.0`, `headway_lead_s=1.2` |
| 後方ギャップ | `gap_rear >= max(gap_min_m, v_rear * headway_rear_s)` | `headway_rear_s=1.0` |
| 後方 TTC | 後続車が接近中（`v_rear > v_ego`）なら `gap_rear / (v_rear - v_ego) >= ttc_min_s` | `ttc_min_s=3.0` |

後方ギャップに**後続車の速度**を使うのは、隙間を詰めているのが後続車だからである。自車速度で測ると、
自車が遅く後続車が速い場合に危険側へ倒れる。

隣接レーンに車が居なければ無条件に受容する。ギャップが無い間は発起せず、締切
（`dist_to_connection` が `required_m` を割り込んでもなお空かない）を過ぎたら**入れないまま通過する**
（§5）。

### 出力の型を `PolicyConstraint` にしない

`PolicyConstraint::Kind` は `NONE / STOP_AT_S / MAX_SPEED / MAX_SPEED_TO_S / YIELD / WAIT_UNTIL` のみで
（`include/gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp`）、いずれも**縦方向の制約**である。
「今この隙間に入ってよいか」という横方向の可否は、この語彙のどれにも当てはまらない。

したがって本層は `ITrafficPolicy` の実装に**しない**。`RouteLanePlan` と同じく
**`virtualdriver/` 直下の free 関数 + POD の独立層**とする（`policies/` の下ではない）。
`route_lane_plan_design.md` §3-2 と同じ判断であり、同じ理由である。

## 5. 入れなかったときの振る舞い — 現行を維持する

目標レーンに入れないまま接続点を通過した場合、**そのまま通過し、逸脱を記録する**。
これは `route_lane_plan_design.md` §5 の決定（2026-08-02）をそのまま引き継ぐ。停止も待機もしない。
逸脱後の経路復帰は `vd-func:FUNC-052`（REQ-AD-017 段 d）の担当であり、本設計のスコープ外。

逸脱の記録は既存の機構がそのまま働く。road が切り替わった瞬間に直前フレームが目標レーン外だったら
`deviation_count` を +1 する（`ControllerVirtualDriver.cpp:731-748`）。**この値が段 c の合否そのもの**
である（§7）。

## 6. 方向指示器 — 発起側を同じラッチに繋ぐ

`vd-func:FUNC-061` は現在「発起側が無いので storyboard LC にしか同期しない」状態にある。実際
`DetectManeuverDir()` は `getPrivateActions()` から RUNNING の `LAT_LANE_CHANGE` を探すだけで、
見つからなければ 0 を返す（`ControllerVirtualDriver.cpp:1554-1585`）。

AD 発起 LC は storyboard の Action を作らない（§2 の理由）ので、**この経路には決して現れない**。
これは `kinematic_action_visibility` で既知の「横アクション可視性ギャップ」と同型である。

そこで `DetectManeuverDir()` を次の順に拡張する。

1. storyboard LC が RUNNING → 従来どおり（ポインタ同一性でラッチ）
2. storyboard LC が無く、AD 発起 LC が進行中 → **AD LC の方向を返す**
3. どちらも無い → 0（既存の `DetectJunctionTurn()` へフォールバック）

方向は既存の `LaneChangeIndicatorDir(current_lane, target_lane, along_s)` をそのまま使う。
ラッチの理由（レーン境界を跨いだ瞬間に現在レーン id が目標に一致して差分が 0 に潰れる）は
AD LC でも同じなので、**発起時に1回だけ確定して LC 完了まで保持する**。

## 7. 検証 — 何を作り、何を判定するか

### 資産の現況

`resources/xosc/verification/06_route_lane/` は2本だけで、**どちらも段 c には使えない**。
`route_valid_off_target_lane_for_exit_ramp.xosc` は自身のコメントで
`VD does not perform autonomous lane changes - vd-func:FUNC-055 is out of scope for RouteLanePlan by design`
と明記しており、「是正しないこと」を確認するための資産である。両ファイルとも Entities は **ego 1台のみ**で、
ギャップ受容を刺激する隣接車が存在しない。

**xodr は新規作成しない。** `highway_example_with_merge_and_split.xodr` は既に
「一部レーンしか接続を持たない出口」という段 c 向けトポロジを持っている（§3）。
`resources/scenario_authoring/road_catalog/` に lane-restricted な接続を作る生成器は無いが、
既存 xodr で足りるので生成器の新設も要らない。

### 新設するシナリオ2本

| シナリオ | 何を刺激するか |
| :--- | :--- |
| `lane_change_to_exit_ramp.xosc` | 隣接車なし。**発起そのもの**を分離して見る。ego は -1 から出発し、190 m 以内に3回の LC を自発的に打って -4 で接続点を通過する |
| `lane_change_to_exit_ramp_with_traffic.xosc` | lane -2 / -3 に NPC を配置。**ギャップ受容**を刺激する。ego は隙間が空くのを待ってから移る |

1本目が段 c の一次証拠、2本目が「無謀に動かないこと」の証拠である。名前に工程名・序数を使わない
（命名3規約）。

### matcher — 既存を拡張し、新設しない

段 c の合否を判定する材料は**既に C++ 側がフレーム精度で出している**。`deviation_count` が
「road 遷移の瞬間に目標レーン外だったか」であり、これは段 c の主張
「接続点までに目標レーンへ自発的に車線変更する」の否定そのものである。

足りないのは**成功側を表現するキー**だけである。現在 `route_lane_plan_holds` が受け付けるのは
`expect_diagnostic` / `expect_rerouted` / `expect_target_lanes` / `expect_on_target_lane` /
`min_deviations` の5つで（`web/backend/services/vd_metrics.py:1567-1577`）、
**下限しか無い**。`min_deviations: 0` は常に真で無意味なので、成功を主張できない。

そこで `max_deviations` を追加する（最終フレームの `deviation_count` がこの値以下）。
実装は既存の `min_deviations` ブロックの比較演算子を反転するだけで足りる。
`expect_on_target_lane` は EXISTS 判定（1フレームでも一致すれば真）なので段 c には粗く、**触らない**
— `max_deviations` だけで合否は表現できる。

**新 matcher は作らない。** 判定対象の signal は既存の `signal:route_lane_conformance` のままであり、
新しい観測量は増えない。

## 8. 既定 OFF と、その代償の払い方

**既定 OFF とする。** 挙動を変える機能であり、既定 ON にすると既存の回帰ベースラインが動く。
`policy_junction_priority` と F6（`headlight_enabled`）が通った道と同じである。

> **設計要件**: `lane_change_initiation_enabled=false` のとき、`/gates` の既存ベースラインは
> **deviation ゼロ**であること。動いたら設計かフラグの効き方が間違っている。

前例は `resume_merge_enabled` で、追従先は**5点セット**である（1つでも漏らすと GUI と実体がずれる）。

| # | ファイル | 何を足すか |
| :--- | :--- | :--- |
| 1 | `include/gt_esmini/control/virtualdriver/VirtualDriverConfig.hpp` | C++ 側の既定値（単一ソース） |
| 2 | `src/control/virtualdriver/VirtualDriverConfig.cpp` | JSON パース |
| 3 | `config/virtual_driver.json` | 配布既定 + `"// "` コメントキー |
| 4 | `web/backend/api/virtual_driver_api.py` | `_BOOL_KEYS` / `_NUMBER_KEYS` / `DEFAULT_VIRTUAL_DRIVER_CONFIG` |
| 5 | `web/frontend/src/components/simulation/VirtualDriverPanel.tsx` + `web/frontend/src/api/client.ts` | フォーム項目と型 |

`client.ts` は VD-GUI-PARITY（issue #33）の指示に明示されていないが、**型定義がここにあるので追従先に含む**。

### config キー（確定。実装者はこの名前を使うこと）

| キー | 型 | 既定 |
| :--- | :--- | :--- |
| `lane_change_initiation_enabled` | bool | **false** |
| `lane_change_lead_time_s` | number | 6.0 |
| `lane_change_min_lead_distance_m` | number | 40.0 |
| `lane_change_reserve_distance_m` | number | 20.0 |
| `lane_change_gap_min_m` | number | 8.0 |
| `lane_change_gap_headway_lead_s` | number | 1.2 |
| `lane_change_gap_headway_rear_s` | number | 1.0 |
| `lane_change_gap_ttc_min_s` | number | 3.0 |
| `lane_change_lateral_accel_comfort` | number | 1.5 |

`lane_change_lateral_accel_comfort` は `ResumeMergeConfig::a_lat_comfort` へ渡す。
LC 用に**別インスタンス**の `ResumeMergeConfig` / `ResumeMergeState` を持ち、resume-merge の
`resume_merge_cfg_` / `resume_merge_state_` とは**記憶域を共有しない**（デバッグ時にどちらが armed か
区別できなくなるため）。`duration_min_s` / `duration_max_s` / `min_offset_m` は resume-merge の既定を流用する。

## 9. スコープ外（意図的に実装しないこと）

| 項目 | 理由 |
| :--- | :--- |
| 逸脱後の経路復帰 | `vd-func:FUNC-052`（REQ-AD-017 段 d） |
| 経路終点での停車 | `vd-func:FUNC-054`（段 e） |
| 遅い先行車を追い越すための LC | FUNC-055 の note には含まれるが、本 PR は**経路要求 LC のみ**。追い越し判断は別の意思決定 |
| 専用レーン（バスレーン等）回避の LC | 同上 |
| 目標レーンに入れなかったときの減速・待機 | §5（現行維持の決定） |
| resume-merge への第4 disarm トリガー | §2（優先順位で排他するので不要） |
| `ConflictPointResolver` の一般化 | §1（並走関係は設計上の対象外と明記されている） |

## 10. 参考

- 前段の設計: `GT_esmini/docs/virtualdriver/design/route_lane_plan_design.md`
- 所有権の正典: 同 `domain_split_ownership.md` / `control_ownership_pitfalls.md`
- 軌道の正典: `include/gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp` のヘッダコメント
- 検証道路: `resources/xodr/highway_example_with_merge_and_split.xodr`（junction 1 / connection 3）
- 記述ルール（Waypoint の静的検証）: 同 `scenario_authoring_foundation.md` §10 と
  `scripts/check_route_waypoints.py`
