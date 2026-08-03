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

> **2026-08-02 追記**: この §6 は「発起と同時に点灯する」までを決めた。法定タイミング
> （進路変更の何秒前から出すか）は §11 で決める。§11 は本節の3段構造を4段に置き換える。

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

（§11 以降は 2026-08-02 の追記。既存の §1-§10 を参照している箇所があるため、番号は繰り上げない。）

## 11. 方向指示器のリードタイム — 時間で遡れないものを距離で解く

§6 は「発起した瞬間に点灯する」までしか決めていない。実測でも armed と点灯が同一フレーム
（t=2.70）に出ている。ここではその手前、**合図を先に出す**側を決める。

### 11-1. 「LC 開始の3秒前」は素朴には実装できない

VD の発起はギャップ受容に依存する**反応的**な判断である（§4）。隣接レーンに隙間が空いた
フレームで初めて armed になるので、**いつ armed になるかは事前に分からない**。したがって
「armed の3秒前」へタイマーで遡ることは原理的にできない。

前例が答えを持っている。`ControllerRouteDrive` は同じ問題を**距離**で解いている。

```cpp
// Winker leads the (potential) start by winker_lead_time; preserved across all settings.
if (laneAvail && dist <= dSeek + config_.winker_lead_time * speed)
{
    wantDir = dir;
}

const bool seeking    = laneAvail && dist <= dSeek;
```
（`src/control/ControllerRouteDrive.cpp:253-259`。`dist` は現在道路の終端までの距離
`ControllerRouteDrive.cpp:241-244`、`speed` は自車速度 `:236`）

つまり「開始の N 秒前」ではなく「**探し始めるしきい値より `v·N` だけ手前**」である。点灯条件に
ギャップ判定は入らない（`laneAvail` のみ）。ギャップが空かなければ**点灯したまま待つ**。
挙動としても自然で、合図の意味とも合う。

**同型の実装は VD 内にも既にある。** 交差点旋回の先読みがそれで、

```cpp
// Lead-time based lookahead so the signal pre-arms before the intersection.
const double lookahead = std::max(15.0, speed * vd_config_.indicator_lead_time + 10.0);
```
（`src/control/ControllerVirtualDriver.cpp:1820`、`indicator_lead_time` 既定 2.0 =
`config/virtual_driver.json:54`）

本節はこの2つと同じ形を LC 発起へ持ち込む。

> **追記（2026-08-03）— この前例は実は動いていない。**
> 上の `DetectJunctionTurn` を実測したところ、接続路 33.2 m の交差点では左折 6 走行すべてで
> **指示器の点灯フレームが 0**、接続路 14.87 m でも交差点の **7.18 m 手前**でしか点かなかった
> （法定は 30 m 手前。`req-vd-ad:REQ-AD-021` に実測を記載）。
> `RouteLookaheadJunctionTurnDirection` が**出口側の腕に到達して初めて**方向を返す構造のため、
> 接続路長が `lookahead` を食い、しかも曲率減速で速度が落ちるほど `lookahead` が縮む。
> **「前例があるから形は正しい」とは言えない。** 本節が LC 側へ持ち込んだのは
> 「距離で解く」という発想であって、この実装の数値設定ではない。
> 実際 §11-11 では、この速度比例の形そのものが加速中に破綻することを実測で確認し、
> 前進予測形へ置き換えている。

### 11-2. 値の根拠と、国別をどう扱うか

**日本**: 道路交通法 第53条第1項が合図を義務づけ、合図の時期は同法施行令 第21条第1項の表が
定める。進路変更は「**その行為をしようとする時の3秒前のとき**」、右左折は「その行為をしようと
する地点（交差点）の手前の側端から**30メートル手前**の地点に達したとき」。

**国際**: 1968年 道路交通に関するウィーン条約は**秒数を規定しない**。方向指示器による予告は
マニューバの間continueし、完了と同時にやめる、という定性規定にとどまる。ドイツ StVO §5 も
"rechtzeitig und deutlich"（時期を得て明確に）で数値を持たない。米国は州法ごとで、多くは
**距離**（100 ft 等）で書かれている。

> **決定: 国別テーブル・国別プリセットは作らない。**
> 規定は国によって**次元そのものが違う**（秒 / 距離 / 定性）。単一の秒パラメータへ畳めない。
> `lane_change_indicator_lead_time_s` を1本置き、**既定を日本の 3.0 s** とする。他国へ
> 合わせたい利用者は値を変える（距離規定の国は `v` で割った秒に換算する）。
> これは `stop_line_stop_target_design.md` で「標識 294 は独 StVO 固有であり ASAM へ
> 一般化できない」と判断したのと同じ筋である。国別解決を要求として立てるなら
> `req-vd-ad` 側の新規要求であって、本設計の範囲ではない。

**3.0 s は下限の充足であって上限ではない。** 距離しきい値に換算した時点で「等速で走り続けたら
3秒後に発起点へ着く」位置での点灯になり、実際にはギャップ待ちが入るので**実リードは 3 秒以上**に
なる。法定側が下限規定なので、この誤差は安全側に倒れる。

### 11-3. 二段しきい値

```
発起  : dist_to_connection <= required_m                                   （既存 §3）
先行合図: dist_to_connection <= required_m + v_ego * lane_change_indicator_lead_time_s
```

`required_m` は §3 の式そのままで、`ShouldAttemptLaneChangeHop()` が使う値と**同一の量**を
使う（別途計算しない）。差は `v_ego * lead_time` の1項だけであり、これが RouteDrive の
`winker_lead_time * speed` に対応する。

> **罠（実装者は必ずガードすること）**: `dist_to_connection` は最終バンドで **`-1.0`**（= 該当なし）
> を返す。
> ```cpp
> if (matched == &plan.bands.back()) { status.dist_to_connection = -1.0; }
> ```
> （`src/control/virtualdriver/RouteLanePlan.cpp:412-419`。フィールドの契約は
> `RouteLanePlan.hpp:77` の "-1 = unknown"）
> `-1.0 <= 任意の正数` は常に真なので、素朴に書くと**最終バンドで指示器が出っぱなしになる**。
> `dist_to_connection >= 0.0` を先に判定する。

先行合図の追加条件は発起と同じ前提を共有する: `route_lane_status.valid` かつ
`!on_target_lane` かつ hop が有効。**ギャップ受容は条件に入れない**（RouteDrive と同じ。
合図は「入りたい」の表明であって「入れる」の表明ではない）。

#### 実測 — 3秒は定速でだけ厳密に出る（2026-08-02）

実装後に `route_lane_batch.yaml` を実 Release ビルドで走らせた結果。

| シナリオ | t_sig | t_arm | リード |
| :--- | ---: | ---: | ---: |
| `lane_change_to_exit_ramp`（隣接車なし） | 2.30 | 2.70 | **0.40 s** |
| `lane_change_to_exit_ramp_with_traffic`（ギャップ待ちあり） | 3.10 | 7.55 | **4.45 s** |

先行点灯そのものは効いている（FUNC-061 の note にあった「発起と同一フレーム t=2.70 で点灯」が、
同じ 2.70 に対して 2.30 点灯へ動いた）。だが**隣接車なしの側が 0.40 s しか出ない**。
テレメトリを読むと理由は一意である。

```
   t      v    dist     req  sig_thr     gap   sig  arm
1.95   6.68  184.02  140.00  160.03   23.99  False False
2.00   6.87  183.68  140.20  160.81   22.87  False False   <- v が 6.67 を超え、床から速度比例へ
2.15   7.45  182.58  150.67  173.04    9.55  False False
2.25   7.84  181.81  157.66  181.18    0.63  False False
2.30   8.04  181.40  161.15  185.25   -3.85  True  False   <- 先行合図
2.70   9.03  177.92  179.03  206.11  -28.19  True  True    <- 発起
```

`required_m` は `v < 6.67 m/s`（= `min_lead_distance_m / lead_time_s`）の間、床 40 m に張り付いて
**140.00 で一定**である。v がその境界を越えた瞬間に速度比例の枝へ移り、そこから
`n_remaining × lead_time_s × a = 3 × 6.0 × 3.9 ≈ 70 m/s` で膨張する。一方 ego が距離を
詰める速さは v ≈ 8 m/s にすぎない。**しきい値が自車より 9 倍速く迫ってくる**ので、
`v × 3.0 ≈ 24 m` のリード距離が 0.4 秒で消える。

> **これは指示器の欠陥ではなく、決断距離が速度比例であることの帰結である。**
> 定速（`a = 0`）なら `required_m` は動かず、ギャップは v で閉じ、リードは `3v / v = 3.0 s`
> ちょうどになる。**法定の3秒は定速では厳密に満たされ、加速中だけ縮む。**
> ギャップ待ちが入る側（`with_traffic`）は待ちのぶん 4.45 s と法定値を上回る。

**対処は入れない（今回のスコープ）。** 秒で一定のリードを加速中にも保証するには、
しきい値自身の移動速度 `d(required_m)/dt` を閉じ込み速度に含める必要がある
（`(dist - required) / (v + d(required)/dt) <= lead_time`）。フレーム間差分による数値微分が
要り、チャタリングの検討も要る。**実測で挙動が判った今こそ設計できるが、指示器に
リードタイムを入れるという本節の目的の範囲を超える。** `vd-func:FUNC-061` の残タスクとして
記帳する。

matcher のしきい値は実測に合わせて置いた（隣接車なし `min_lead_s: 0.3`、
ギャップ待ちあり `2.0`）。後者を測定値 4.45 の近くに置かないのは、4.45 の大半が
ギャップ待ち時間であり、隣接車の配置を触っただけで matcher が赤くなるからである
（無関係な変更で鳴る検知器は警報疲れを育てる）。

#### 既知の非対称性 — 最終バンドでは先行しない（実装後に判明）

発起側は同じ `-1.0` を**逆向き**に扱う。

```cpp
if (dist_to_connection < 0.0)
{
    return true;  // "not applicable" (final band) -- nothing left to wait for
}
```
（`src/control/virtualdriver/LaneChangeInitiation.cpp:63-66`。FUNC-055 実装時からの既存挙動）

つまり最終バンドでは `ShouldAttemptLaneChangeHop` が真、`ShouldSignalLaneChangeHop` が偽になり、
**「発起 ⇒ 先行合図」の包含関係がここだけ破れる**。この区間で発起した場合、指示器は
armed と同時に点く（＝本節を入れる前の挙動そのまま）。合図が出ないわけではない。

> **判断: `ShouldAttemptLaneChangeHop` 側は変えない。**
> あれは「もう待つ先が無いのだから即座に判断する」という FUNC-055 の意図的な設計であり、
> 反転させると発起の挙動が変わって §8 の「既定 OFF なら既存ベースライン不変」とは別の軸で
> 回帰が動く。指示器のリードタイムを入れるという本節の目的の範囲を超える。
>
> 残る欠陥は「最終バンドでの発起にはリードが付かない」ことである。実害は限定的で、
> 最終バンドは定義上その先に接続点が無い（`RouteLanePlan.cpp:407-411`）＝経路上の
> 車線変更要求としては終端の縁でしか起きない。**恒久的に許容するのではなく、
> `vd-func:FUNC-061` の残タスクとして記帳する。**

### 11-4. 置き場所 — `DetectManeuverDir` に置く

> **決定: `DetectManeuverDir` を4段に拡張する。`AutoIndicatorPolicy` と `IndicatorFSM` は
> 変更しない。**

```
1. storyboard LaneChangeAction が RUNNING → その方向（従来どおり）
2. AD 発起 LC が armed            → lc_init_state_.direction_indicator（従来どおり）
3. AD LC の先行合図が立っている    → その方向          ← 新設
4. どれでもない                    → 0（DetectJunctionTurn へフォールバック）
```

根拠は前例2つで、どちらも「コントローラが先読みして `maneuver_dir` を出す」と言っている。

- `AutoIndicatorConfig::lead_time` は未使用の予約フィールドだが、そのコメントが
  `// [s] reserved — controller sets maneuver_dir this far ahead`
  （`include/gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp:10`）＝
  **責務の置き場所はコントローラ側だと既に宣言されている**
- `DetectJunctionTurn` が同じことを既にやっている（11-1）

実装形は「毎フレーム `lc_signal_dir_`（コントローラのメンバ）を確定させ、`DetectManeuverDir` は
それを読むだけ」にする。§2 の LC ブロック（`ControllerVirtualDriver.cpp:936-1080`）は
`DetectManeuverDir` の呼び出し（`:1417-1425`）より**先**に走るので、順序の心配はない。

```
lc_signal_dir_ =
    armed                          ? lc_init_state_.direction_indicator
  : （先行合図の条件が成立）        ? LaneChangeIndicatorDir(ego_lane_raw, hop.next_hop_lane_id, along_s)
  :                                  0;
```

armed のときは従来の値と**ビット単位で同じ**なので、この統合は既存挙動を変えない。
`LaneChangeInitiationState` の POD には触らない（`DisarmLaneChangeHop` などの free 関数が
新フィールドを踏む事故を避ける）。

先行合図中は方向を**毎フレーム再計算してよい**。ラッチが要るのはレーン境界を跨いだ後に
差分が 0 へ潰れるからで（§6）、先行合図の段階ではまだ跨いでいない。armed になった時点で
既存のラッチ（`ArmLaneChangeHop` が確定した `direction_indicator`）へ引き継がれる。

### 11-5. 自動キャンセルは発火しない（調査で確定）

「まだ操舵していない段階で点灯させると自動キャンセルが即座に消すのではないか」という懸念は、
**この設計では成立しない**。経路が2本に分かれているためである。

- 操舵戻りによる自動キャンセルを持つのは `IndicatorFSM`
  （`include/gt_esmini/control/manualdrive/ManualDriveTypes.hpp:78-106`）で、その入力は
  **人間のボタンと人間の操舵**だけである（`ControllerVirtualDriver.cpp:1243-1244`）。
  出力は `ictx.manual_left/right/active` に入る（`:1422-1424`）。
- AD 側の点灯を決めるのは `AutoIndicatorPolicy::Update`
  （`src/control/virtualdriver/AutoIndicatorPolicy.cpp:6-39`）で、**操舵に基づくキャンセルを
  一切持たない**。`maneuver_dir` をラッチし、0 に戻ってから `min_on_time`（既定 0.3 s）だけ
  保持して消すだけである。
- さらに `IndicatorFSM` 自体も、`State::ARMED`（点灯要求済み・未旋回）にはキャンセル分岐が
  存在しない。`ARMED` 節にあるのは `ACTIVE` への昇格条件だけで、消灯は
  `ACTIVE` から閾値を跨いで戻ったときにしか起きない（`ManualDriveTypes.hpp:88-93`）。

したがって「先行合図を `maneuver_dir` に載せる」限り、キャンセル機構には**触れられない**。
逆に言えば、`IndicatorFSM` 側へ AD 由来の点灯を足す実装は責務が混ざるので採らない。

なお `ctx.manual_active` は AD より優先される（`AutoIndicatorPolicy.cpp:11-18`）。これは
正しい（人間が勝つ）ので、そのままにする。

### 11-6. ギャップ待ちが長引いても消さない

> **決定: 点灯の上限時間は設けない。**

理由は3つ。

1. ウィーン条約の定性規定は「予告はマニューバの間continueし、完了でやめる」であり、
   待っている間に消すのは合図の意味を壊す
2. 先行実装（`ControllerRouteDrive`）にも上限が無い。待ちが長い状況＝ギャップが無い状況で
   あり、そこで消灯すると「入りたい」が周囲に伝わらなくなる
3. 上限を入れると「消えた後にまた点く」チャタリングの上限復帰ロジックが要る。挙動と実装の
   両方を複雑にする対価に見合わない

消灯は条件が落ちれば自然に起きる。目標レーン帯に乗った（`on_target_lane`）／バンドを抜けて
`dist_to_connection` が `-1.0` になった／`suppressed` に入った、のいずれでも `lc_signal_dir_`
が 0 になり、`AutoIndicatorPolicy` が `min_on_time` 後に消す。

**入れないまま接続点を通過した場合も同じ**で、§5（現行維持＝通過して逸脱を記録）を変えない。
バンドを抜けた時点で合図は落ちる。

### 11-7. 優先順位は §2 をそのまま継承する

先行合図も `suppressed`（storyboard 横アクション RUNNING / `lat_manual` / resume-merge active）
の間は**出さない**。合図だけ先に出して実際には動けない、という状態を作らないためである。
判定は発起側と同じ `suppressed` を使う（新しい判定を作らない）。

### 11-8. テレメトリ — 意図とランプの両方を出す

`LaneChangeInitiationSnapshot`（`VirtualDriverTypes.hpp:231-242`）に1フィールド足す。

| フィールド | 型 | 意味 |
| :--- | :--- | :--- |
| `signal_active` | bool | AD LC 経路が方向指示器を要求している（先行合図中 or armed 中） |

`indicator.left/right`（既存、`VirtualDriverTypes.hpp:135-139` →
`VirtualDriverTelemetryJson.cpp:157`）だけでは**足りない**。ランプが点いている理由が AD LC の
先行合図なのか `DetectJunctionTurn` の交差点旋回なのかを区別できず、検証が偽 PASS を出しうる。

逆に `signal_active` だけでも足りない。意図が計算されただけで `AutoIndicatorPolicy` に握り
潰されていても真になるからである。**matcher は両方を見る**（11-9）。

既存の `lane_change.direction` は armed 時のみ非 0 という契約
（`VirtualDriverTypes.hpp:236` / `ControllerVirtualDriver.cpp:1669`）を**変えない**。
direction は hop のプロパティであって合図のプロパティではない。

### 11-9. 検証 — 先行を機械判定する

> **新 matcher `indicator_leads_lane_change` を新設する。**

§7 は「新 matcher は作らない」と決めたが、それは「新しい観測量が増えないから」という理由
だった。今回は**指示器ランプという新しい観測量が増える**ので、その前提が成り立たない。
`route_lane_plan_holds` は `frames[i]["route_lane"]` しか読まない
（`web/backend/services/vd_metrics.py:1520-1571`）ので、そこへ別ブロックの判定を混ぜない。

判定内容（`must` キー `min_lead_s`、既定なし＝必須）:

1. `lane_change.signal_active` が最初に真になる時刻 `t_sig` と、`lane_change.armed` が最初に
   真になる時刻 `t_arm` を取る
2. **`t_arm - t_sig >= min_lead_s`**（これが「発起より前に点灯している」の本体）
3. `[t_sig, t_arm]` の全フレームで `indicator.left` または `indicator.right` が真
   （意図がランプまで届いたことの確認。ここが 11-8 の「両方を見る」）
4. 点いている側が hop の方向と一致（`lane_change.direction` は armed 後にしか出ないので、
   armed フレームの `direction` と突き合わせる）

どちらかのブロックが1フレームも存在しなければ **skip**（pass にしない）。既存の
`route_lane_plan_holds` が `route_lane` ブロック欠落に対して取っている作法
（`vd_metrics.py:1557-1571`）をそのまま踏襲する。

刺激に使うのは `lane_change_to_exit_ramp_with_traffic`。ギャップ待ちがあるぶん
`t_arm - t_sig` が長く出るので、先行の存在を見やすい。隣接車なしの
`lane_change_to_exit_ramp` は待ちが無いぶんリードが `v·lead_time` ぶんに縮むが、
それでも 0 ではないので両方に付けてよい。

### 11-10. config キー（確定。実装者はこの名前を使うこと）

| キー | 型 | 既定 |
| :--- | :--- | :--- |
| `lane_change_indicator_lead_time_s` | number | **3.0** |

**既存の `indicator_lead_time`（2.0、交差点旋回用）とは別キーにする。** 法的規定が別物
（右左折は「30 m 手前」、進路変更は「3秒前」）で、根拠も次元も違う。1本にまとめると片方を
合わせたときにもう片方が規定から外れる。

追従先は §8 の5点セットと同じ。`lane_change_initiation_enabled=false` のときこのキーは
一切参照されない（先行合図の計算そのものが `enabled` の内側にある）ので、**既定 OFF の
不変条件は §8 のまま**である。

### 11-11. 加速中のリードを保つ — しきい値を追うのをやめ、T 秒後を予測する

11-3 は「対処は入れない」と決め、根治式として
`(dist - required) / (v + d(required)/dt) <= lead_time` を書き残した。実装するにあたって
**この式を実トレースに当てたところ、期待した 3.0 s は出ない**ことが判った。以下はその測定と、
採った別形の記録である。

#### 素朴な微分形が効かない理由 — 微分は「この直後に来る枝」を見ていない

11-3 のトレースに根治式をそのまま当てた結果。

| t | 根治式の time_to_threshold | 合図（<= 3.0）? |
| ---: | ---: | :--- |
| 1.95 | 0.70 s | ✅ |
| 2.30 | 0.31 s | ✅ |

**リードは 0.40 s から 0.70 s にしかならない。** 原因は `required_m` の**枝**にある。

```
required = n_remaining * max(v * lead_time_s, min_lead_distance_m) + reserve_distance_m
```
（`src/control/virtualdriver/LaneChangeInitiation.cpp:47-55`）

`v < min_lead_distance_m / lead_time_s`（= 6.67 m/s）の間は床の枝にいるので
`d(required)/dt = 0` である。微分は「しきい値は動かない」と報告し、`(dist - 140) / v ≈ 7.5 s` を返す。
ところが実際にはその直後に v が枝をまたぎ、`required` は 70 m/s で膨張しはじめ、
発起までは 0.95 s しか残っていない。**現在の微分値は、次の瞬間に来る枝の乗り換えを原理的に
予見できない。** 微分は局所的には正確（t=1.95 で 0.70 s 予測に対し実際 0.75 s）だが、
局所的に正確であることと 3 秒先を当てられることは別である。

これは数値微分の実装品質の問題ではない。11-3 が心配していたチャタリングやホップ完了時の
不連続（`n_remaining` が落ちる瞬間の -1000 m/s 相当のスパイク）は**どれも本質ではなかった**。

#### 採る形 — 「T 秒後に発起条件が成立しているか」を直接問う

> **決定: `ShouldSignalLaneChangeHop` を距離形から前進予測形へ置き換える。併用しない。**

```
v_pred    = min(v + max(0, a) * T,  max(v, v_cap))
travel    = (v + v_pred) / 2 * T
先行合図  ⟺ dist_to_connection - travel <= RequiredLaneChangeDistance(n_remaining, v_pred, cfg)
```

`max` で距離形と併用しないのは、どちらが効いたか判らなくなるからである。併用する必要も無い。
**a = 0 のとき、この式は距離形と厳密に一致する**からである。

```
dist - v*T <= required(v)   ⟺   dist <= required(v) + v*T      （= 11-3 の距離形）
```

つまりこれは置き換えであって拡張であり、**定速時の挙動はビット単位で不変**である。
11-3 の距離形は、この一般形の a=0 における特殊解だったことになる。

この形が持つ性質は3つある。

- **`n_remaining` を微分しない。** n は引数のまま `RequiredLaneChangeDistance` へ渡るので、
  ホップ完了時の離散ジャンプが微分スパイクに化けない。11-3 が「要る」とした
  リセットもクランプも**不要になる**
- **前フレーム値も dt も要らない。** 純関数のままなので
  `test_LaneChangeInitiation.cpp` の既存の流儀（gtest・エンジン非依存）でそのまま検査できる
- 発起側 `ShouldAttemptLaneChangeHop` に触らない。最終バンドの
  `dist_to_connection < 0.0` ガード（11-3 の罠）も**維持する**

#### `v_cap` は `last_action_target_` から取る（`target_speed` ではない）

キャップが要るのは `v + a*T` が**実際には到達しない速度まで外挿される**からである。
キャップ無しだと緩加速のケースでリードが 6.2 s まで伸び、法定値は満たすものの
「正しい理由で緑」ではなくなる（過剰点灯 2 倍）。

> **注意（実装者向け）**: `ResolveTargetSpeed()`（`src/control/ControllerVirtualDriver.cpp:1727-1788`）が
> 返す `target_speed`（同 `:551`）は**遷移の途中経過を補間した現在時刻の参照値**であって、
> 未来の速度ではない。ランプ中は `v_ref ≈ v` なので、これをキャップに使うと
> `v_pred = min(v + aT, v) = v` となり**前進予測が丸ごと無効化される**。
> 使うのは終端値を保持しているメンバ `last_action_target_`（同 `:1776`、`vt` を記憶している側）。

`max(v, v_cap)` としてキャップが現在速度を下回らないようにする。減速（`a < 0`）は 0 に潰す。
どちらも「早めに点灯する」側へ倒れ、法定値が下限規定である以上これは安全側である（11-2）。

#### チャタリング対策は平滑化ではなくラッチ

ego 加速度は `object_->pos_.GetAccLong()` から取れるが、これは**位置の二階後退差分**であり、
独立した平滑化フィルタは経路上に存在しない。実測（`lane_change_to_exit_ramp`、
`ego.speed` のフレーム間差分）:

| 区間 | 値 |
| :--- | ---: |
| 合図窓（1.5–3.0 s）の dv/dt | 3.886 → 3.868（単調・滑らか） |
| **t=2.50–2.65 の4フレーム** | **1.092**（2.8 m/s² の落ち込みが 0.2 s 継続） |
| フレーム間 \|da\| の p95 / max | 0.43 / **3.19** |

概ね滑らかだが、多フレームにわたる外れがある。`v + a*T` は T=3 でこれを 8.4 m/s の
速度誤差に増幅するので、素の述語だけでは合図が明滅しうる。

> **決定: EMA を入れず、先行合図をホップ単位でラッチする。**
> 一度立った先行合図は、そのホップが続く限り保持する（`lc_signal_latched_` /
> `lc_signal_latched_lane_`）。条件（`valid` / `!on_target_lane` / `!suppressed` /
> `dist_to_connection >= 0`）が落ちればラッチも落ちる。

EMA を採らない理由は2つ。**時定数を新たに正当化しなければならない**こと、そして
**ラッチのほうが合図の意味に合っている**ことである。「入りたい」と表明した合図が
加速度の一時的な落ち込みで消えるのは、11-6 が「ギャップ待ちの間も消さない」と決めたのと
同じ理由で誤りである。ラッチは構成上単調なので、チャタリングは起こりえない。

なお `GetAccLong()` は `prepareGroundTruth()`（`ScenarioEngine.cpp:678-680`）が
コントローラの `Step()` より**後**に更新するため、`Step()` 内で読める値は**1フレーム古い**。
今フレームの加速度は原理的に存在しない（このフレームの指令はまだ出していない）ので、
直近の実現済み状態として受け入れる。

#### 3.0 s は `lane_change_to_exit_ramp` では原理的に出ない

**この修正で `lane_change_to_exit_ramp` のリードが 3.0 s になることはない。**
修正するのは合図側の述語だけで発起側は触らないので `t_arm = 2.70` は動かず、
シナリオは t=0 から始まる。**達成しうるリードの上限は 2.70 s** である。

しかもその 2.70 s は**飽和した値**である。前進予測は t=0 の時点で既に「3 秒後には発起している」と
判定するので、合図は初回フレームから出っぱなしになる。これでは
「規則が 3.0 s を計算した」のか「常時点灯しているだけ」なのかを**外から区別できない**。
段 a の刺激としては十分でも、段 b・段 c の証拠にはならない。

> **決定: 検証資産を2本足す。1本では足りない。**
>
> | シナリオ | 何を分離するか |
> | :--- | :--- |
> | 定速走行から車線変更 | 段 b。`required` が動かないので リード = `3v/v` = 3.00 s ちょうど |
> | 緩やかな加速中に車線変更 | 段 c。発起が加速中に起きる位置まで助走を伸ばす |
>
> どちらも **ego を lane -3 から発進させ n_remaining = 1 にする**。既存2本の
> lane -1 / -2 発進では `required = 3 × (13.889 × 6) + 20 = 270 m` に対して
> road0 が与える距離が 190 m しかなく、**ego が生成された瞬間に既にしきい値の内側**にいる。
> n_remaining = 1 なら `required = 103.3 m` で 190 m の助走に収まる。

`lane_change_to_exit_ramp.expectations.yaml` の `min_lead_s: 0.3` に付いていた
「**このルールを変えずに min_lead_s を上げるな**」という条件は、本節でルールを変えたことで
解除される。上げる際は `reason` を「ルールを変えた」旨へ書き直す（値だけ動かさない）。

## 12. パラメータの意味を画面から伝える

§8 で config キーが9個増え、§11 で10個目が増えた。GUI（`VirtualDriverPanel.tsx:747-791`）は
`Lead time (s)` のような**単位付きの短ラベルだけ**で、値の意味は画面に出ていない。
セクション冒頭の JSX コメント（`:741-746`）はコード上の注釈で、画面には出ない。

### 12-1. 伝えるべきことは3つに絞る

全項目に説明を撒くと階層が壊れる（hint sprawl）。伝えるべきは次の3点に絞る。

1. **3つの距離パラメータが1つの式に入る**こと
   `required_m = n_remaining × max(v × lead_time_s, min_lead_distance_m) + reserve_distance_m`
2. **「いつ動き出すか」と「入っていいかの安全判定」は別の関心**であること
3. **後方ギャップだけ後続車の速度で測る**こと（自車速度ではない。§4 の理由）

### 12-2. 層分け（L0 / L1 / L2）

| 層 | 何を | どう |
| :--- | :--- | :--- |
| **L0**（常時表示） | 関心の分離と、式 | サブ見出しでグループを割り、Timing 群の直下に式を1行だけ等幅で併記する |
| **L1**（近接） | 後方ギャップの基準速度 | Gap 群の直下に1行だけ注記。**ここだけ**に置く |
| **L2**（オンデマンド） | 各パラメータ1文 | 各フィールドの `title` ツールチップ |

グループ分けは次のとおり。バックエンドのキー順ではなく、**利用者の関心**で割る。

| グループ | キー |
| :--- | :--- |
| **Timing — いつ動き出すか** | `lead_time_s` / `min_lead_distance_m` / `reserve_distance_m` / `indicator_lead_time_s` |
| **Gap acceptance — 入っていいか** | `gap_min_m` / `gap_headway_lead_s` / `gap_headway_rear_s` / `gap_ttc_min_s` |
| **Comfort — 動きの質** | `lateral_accel_comfort` |

`indicator_lead_time_s` を Timing 群に入れるのは、それが §11-3 のとおり**同じ式に足し込まれる**
量だからである（合図は Gap の関心には属さない）。

### 12-3. 実装手段 — 新コンポーネントを作らない

> **決定: `NumberInput` に `title` を通す。`description` prop は足さない。**

- `NumberInputProps` は `InputHTMLAttributes<HTMLInputElement>` を継承しているので
  （`web/frontend/src/components/ui/Input.tsx:34-37`）、`title` は**型を変えずに渡せる**。
  ただし現状 `{...rest}` は `<input>` へ展開されるため、ラベル上ではツールチップが出ない。
  `NumberInput` 内で `title` を `FieldWrapper` の `<div>` 側へ回す（`Input.tsx:12-19, 39-45`。
  3行の変更で、`title` を渡していない既存 149 箇所の呼び出しは無変更で動く）
- `description` prop を足さない理由: `Checkbox` / `ToggleSwitch` が持つ `description`
  （`Input.tsx:63-76`）は**1行ラベル横の短文**用途であり、2列グリッドの数値フィールド8個に
  常時表示の説明文を付けると階層が崩れる。L2 はオンデマンドで足りる
- ツールチップの前例は既にあり、いずれも**ネイティブ `title` 属性**である
  （`ControllerSection.tsx:137-141` の Route Drive レーンチェンジ説明、
  `FfbMarginPanel.tsx:169,176,183` の数値指標説明）。専用 `<Tooltip>` コンポーネントは
  リポジトリに存在しない。**ここで新設しない**

**言語**: ラベルと L0/L1 の画面テキストは英語（パネル既存の全ラベル・`<p>` と揃える）。
L2 のツールチップは日本語（`ControllerSection.tsx:140` が確立した唯一のツールチップ前例に
揃える）。

### 12-4. 3面を同じ説明で揃える（VD-GUI-PARITY / issue #33）

GUI だけで完結させない。同じ内容を次の2か所にも入れる。

| 面 | 場所 | 現状 |
| :--- | :--- | :--- |
| config JSON | `config/virtual_driver.json:40` の `_lane_change_initiation` | セクション単位で1個。9キーぶんを1文にまとめてある |
| API 既定値 | `web/backend/api/virtual_driver_api.py:351-376` の `DEFAULT_VIRTUAL_DRIVER_CONFIG` | 同じ `_xxx` コメントキー流儀で再掲 |

どちらも `_xxx` という文字列値キーをコメント代わりに使う流儀で、`test_virtual_driver_api.py`
の `test_put_preserves_comment_keys` が PUT でこれらが消えないことを検査している。
**流儀は変えない**（キーを増やして個別コメント化しない）。既存の1文に、12-1 の3点と
`lane_change_indicator_lead_time_s` を追記する。

> **注意（記録しておく）**: VD-GUI-PARITY を機械的に検査する仕組みは**無い**。
> `test_virtual_driver_roundtrip.py` が突き合わせているのは「config JSON ⇔ バックエンドの
> `KNOWN_KEYS`」の2面だけで、フロントエンドの `EDITABLE_KEYS` は誰も検査していない
> （`EDITABLE_KEYS` は `VirtualDriverPanel.tsx` の外から参照されていない）。
> 3面目の追従は人が守る規約のままである。検査の自動化は本設計のスコープ外だが、
> 増えるたびに手で確認する必要があることを明示しておく。
