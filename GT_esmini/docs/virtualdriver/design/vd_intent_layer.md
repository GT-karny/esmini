# VD 意図層 (Intent Layer) — 4つの語彙を1つに畳んで、外から読めるようにする

> ステータス: **設計中 / 未実装**。
> 知識グラフ: 新設予定 `vd-component:intent-layer`（layer: cross-cutting）。
> 投影元は `vd-func:FUNC-055`（車線変更の発起）、`vd-func:FUNC-056`（追い越し）、
> `vd-func:FUNC-061`（方向指示器）、および `policy:*` 各層。
> 新しい `vd-func` ID は**起こさない** — 本層は運転機能ではなく観測層であり、
> `function_catalog_vd_ad.yaml` の tier/layer/agent/dim いずれの軸にも乗らない。
> **出力面はテレメトリ JSON のみ**。OSI HostVehicleData への投影は意図的にスコープ外（§11）。
> **既存挙動はビット単位で不変であることを設計要件とする。** 本層は読み取り専用の投影であり、
> 制御へ戻る経路を持たない。唯一 CPU を消費するのは §7 の観測スキャンで、既定 OFF。

---

## 1. 出発点 — 素材はあり、形が無い

VD スタックは「なぜ減速しているか」「これから何をするか」をすでに毎フレーム出している。
足りないのはデータではなく**形**である。

### 1-1. 同じ構造が4つの語彙で表現されている

「予告 → 実行」という同一の構造を、4つの機能が4つの別々の語彙で表している。

| 機能 | 予告を表すもの | 実行を表すもの | 出所 |
| :--- | :--- | :--- | :--- |
| 追い越し | `overtake.phase = "signal_out" / "signal_back"` | `"moving_out" / "pass" / "moving_back"` | `OvertakeManeuver.hpp:42-57` |
| 車線変更 | `lane_change.signal_active` (bool) | `lane_change.armed` (bool) | `LaneChangeInitiation.hpp` |
| 信号停止 | `gt.traffic_light.committed` (bool) | 制約 `stop_at_s` の存在 | `TrafficLightAware.cpp:206-223` |
| 一時停止 | `gt.stop_yield.phase = "approach"` | `"hold" / "creep"` | `StopYieldSignAware.hpp:44-50` |
| 右左折 | `junction_turn.dir` ≠ 0 | `junction_turn.on_connector` | `JunctionTurn.hpp:118-124` |

消費側は機能ごとに別のコードを書くことになり、**新しいマニューバが増えるたびに消費側を改修する**
構造になっている。追い越し（2026-08-04）が入ったときに実際そうなった。次に駐車・出庫
（`req-vd-ad:REQ-AD-019/020/022`）が入れば、6つ目の語彙が増える。

### 1-2. 欠けているもの4つ

| # | 欠落 | 具体 |
| :--- | :--- | :--- |
| (a) | **主語** | 「今この減速を決めているのは誰か」が記録されていない。`ManeuverAwareSpeedPlanner.cpp:113-175` が全制約を `std::min` で速度サンプルへ畳み込んだ時点で、勝者が捨てられる |
| (b) | **時間** | すべてが距離（`s` / `dist_to_entry_m` / `required_m` / `dist_to_connection`）。人間に向けた表示は秒で話す |
| (c) | **予告射程** | 機能ごとに 21 秒〜2 秒までばらついている（§1-3） |
| (d) | **取り消し** | コミットラッチは信号にしか無い。「まもなく左へ」と出した後にギャップ待ちで消える、が今のデータでは普通に起きる |

### 1-3. 予告射程の実測（50 km/h = 13.9 m/s 換算）

| 意図 | 決めているもの | 距離 | 秒 |
| :--- | :--- | ---: | ---: |
| 減速（カーブ・速度制限・交差点） | `scan_distance` | 300 m | 21.6 s |
| 停止（先行車） | `idm_lookahead` | 120 m | 8.6 s |
| 停止（交差点コンフリクト） | `conflict_lookahead` | 120 m | 8.6 s |
| 車線変更 | `n_remaining × max(v×6.0s, 40m) + 20m` | 103 m | 7.4 s |
| 停止（信号・標識・横断歩道） | `tl_/sign_/crosswalk_lookahead` | 80 m | 5.8 s |
| **右左折** | `max(indicator_min_distance_m, v×indicator_lead_time)` | **30 m** | **2.2 s** |

右左折だけが桁で外れている。これは道路交通法施行令21条1項（交差点の30 m手前で合図）に
正確に合わせた結果であり、**合図としては正しい**。予告として使えないだけである（§7）。

**射程を揃えようとしてはいけない。** ポリシーの `lookahead` を伸ばすと制約が早く出る＝減速が
早まる＝**運転挙動が変わり回帰ベースラインを引き直すことになる**。観測のために制御を変える
のは本末転倒で、本設計は射程のばらつきを**そのまま出し、`eta_s` で消費側が判別できるようにする**
方針を採る。唯一の例外が右左折で、その理由は §7 に書く。

### 1-4. 実装着手時に覆った想定（2026-08-29）

着手前にソースを当たった結果、本設計の記述が**5点で実装と食い違っていた**。
どれも語彙（§3）と verdict 境界（§4）そのものは動かさないが、素材の在処が違う。
以下の是正を先に反映し、該当節も直してある。

#### 覆った想定1: 隣接レーンの gap は負にならない（§8-3）

§8-3 は「`ScanAdjacentLaneGap` の gap は bumper-to-bumper なので `gap < 0` が並走を意味する」と
書いていた。**負の gap は外に出ない。**

```cpp
sample.gap_lead_m = std::max(0.0, lead_ds - half_ego_front - half_lead_rear);
sample.gap_rear_m = std::max(0.0, rear_ds - half_ego_rear  - half_rear_front);
```
（`src/control/virtualdriver/LaneChangeInitiation.cpp:213,222`）

床が入っているので、車体が縦方向に重なっていても `gap_*_m` は 0.0 で頭打ちになる。
`side` を「負の gap」で判定する設計はそのままでは成立しない。

**採る形**: 床を外すのではなく、**重なりの深さを別フィールドで新設する**。

```cpp
double lead_overlap_m = 0.0;  // [m] <= 0。0.0 = 重なりなし。負 = 縦方向の重なり深さ
double rear_overlap_m = 0.0;  // （gap_*_m が 0 で頭打ちにする前の、床なしの値）
```

床を外す案を採らないのは、`required_lead` / `required_rear` が `gap_min_m` を下限に持つとはいえ
**`gap_min_m = 0` かつ `v = 0` の設定では `0 < 0` と `-2 < 0` で判定が割れる**からである。
新フィールドなら受容判定式が既存フィールドしか読まないので、§8-7 の「`accepted` がビット単位で
同一」が議論ではなく**構造で**保証される。既定値 0.0 が「重なりなし」を意味するので、
手で組んだ合成サンプルが偽の並走に化ける罠も無い（生の gap をそのまま持つと、
`gap_lead_m = 5.0` と `gap_lead_raw_m = 0.0` が同居して矛盾する）。

#### 覆った想定2: 車線変更の `COMPLETING` は merge progress では観測できない（§3-3）

§3-3 の LANE_CHANGE の COMPLETING 欄は「merge `progress >= 1.0`」だった。**この量は外に出ていない。**
LC のホップは `lc_merge_state_` という resume-merge とは**別インスタンス**で走る
（`ControllerVirtualDriver.cpp:1223-1228`）のに対し、`telemetry_.resume_merge.progress` に出ているのは
resume-merge 自身の `resume_merge_state_` である（同 `:2289-2299`）。

**採る形**: §9-2 項9 が既に確定させている判別に揃える。すなわち
「**arm が解除され、横位置がまだ収束していない**」を COMPLETING / ABORTING の共通条件とし、
`lane_change.aborted_reason` の空／非空だけが両者を分ける。§3-3 の該当セルをそれで置き換えた。
§9-2 項9 の「この2つを分ける情報は `aborted_reason` ただ1つ」という記述は、
もともとこの形を前提にしている。

#### 覆った想定3: ブレーキランプの状態がテレメトリに無い（§3-3）

§3-3 は `STOP` / `SLOW` の ANNOUNCED をブレーキランプ点灯に置いている。
**その状態はコントローラのメンバに閉じていて、テレメトリに出ていない。**

```cpp
if (cmd.brake > 0.05) { brake_light_on_ = true; brake_light_hold_until_ = sim_time_ + 0.35; }
else if (sim_time_ >= brake_light_hold_until_) { brake_light_on_ = false; }
```
（`ControllerVirtualDriver.cpp:2721-2729`）

`driver.brake > 0` から再導出することはできるが、**それは同じ量の2つ目の定義になる**。
上のデバウンス（0.05 の閾値と 0.35 s のラッチ）を投影層が知らないので、
実際に点いている灯と投影が食い違う。§6-4 が「写像は1本だけ作る」で禁じている形そのものである。

**採る形**: `VirtualDriverTelemetry::brake_light_on` を追加公開し、投影は**実際に点いている灯**を読む。
§3-4 の `aborted_reason` と同じ「足りない素材（実装が要る）」扱いで、同じ PR に入れる。
`ApplyLights`（節10）は telemetry 充填（節11）より前に走るので、順序の問題は無い。

#### 覆った想定4: `where` に「位置なし」が要る（§3 / §8-4）

§8-2 (2) は `where` を front / side / rear / oncoming の4値で確定させているが、
§8-4 の表には `where` 欄が `—` の行が4件ある（`no_target_lane` / `route_budget` /
`no_passing_lane` / `suppressed`）。この4件はいずれも実在の生産者を持つので、
4値のどれにも当てはまらない blocker が構造上存在する。

**採る形（2026-08-29 合意）**: 位置語彙は4値のまま据え置き、**不在値**を1つ足して `""` で直列化する。
「位置が無い」を第5の位置にしない。`gap_reason` / `blocked_reason` / `route_lane.diagnostic` が
すでに `""` を「該当なし」に使っているのと同じ流儀であり、消費側から見て
blocker の要素の形が常に同じになる（キーの有無で分岐しなくてよい）。

#### 覆った想定5: 呼び出し位置の節番号（§2-4）

§2-4 は「節11a-11d の後、節11e として置く」と書いていたが、現行の
`ControllerVirtualDriver::Step` は 11a〜11e まで使用済みである（11e = `telemetry_.overtake`、
`ControllerVirtualDriver.cpp:2375-2387`）。意図層は **11f** に置く。

---

## 2. 設計方針 — 投影であって、新しい真実源ではない

### 2-1. 各機能に意図の生成責務を配らない

一見、各機能が自分の意図を名乗るのが自然に見える。**採らない。** それをやると、いま4つある語彙が
6つになるだけである。語彙の統一は、**1か所で投影する**ことによってしか担保されない。

採るのは `BuildAdasFunctionReport`（`AdasFunctionReport.hpp`）と同じ形である。あれは各ポリシーの
出力を OSI の AD 機能行へ投影する純関数で、`control` が `osi` に依存しないという規約
（`GT_esmini/CLAUDE.md` §2）を守りながら、**意味論そのものを単体テスト可能にしている**。
本層も同じ位置に置く。

### 2-2. 既存フィールドは一切変えない

`policy.constraints[]` / `lane_change` / `overtake` / `junction_turn` / `route_lane` は現状のまま
残す。意図層はそれらを読んで**新しいトップレベルブロックを追加する**だけである。
テレメトリ JSON の追加規約（「既存 consumer は知らないキーを無視する」）に従う。

これは互換性のためだけではない。**投影が間違ったときに一次資料へ戻れること**が、
投影層を信用するための前提になる。

### 2-3. 純粋性の範囲

意図の判定には状態が要る（安定 ID の維持、最短表示時間のタイマ）。したがって完全な純関数には
ならない。採る形は次のとおり。

```
(前フレームの IntentState, 今フレームの VirtualDriverTelemetry, dt)
    -> (次フレームの IntentState, std::vector<VdIntent>, std::vector<VdIntentReason>)
```

エンジンにも OSI にも道路網にも依存しない。**道路を1本も読み込まずに「ギャップ待ちのまま
3秒経っても ANNOUNCED から EXECUTING へ進まない」を単体テストで書ける**ことを設計要件とする。

### 2-4. 呼び出し位置

`ControllerVirtualDriver::Step` の telemetry 充填の**後**、**節11f** として置く
（11a〜11e は使用済み。§1-4 覆った想定5）。`VirtualDriverTelemetry` が完成した後に、
それだけを入力として読む。

**既知の穴（§7-3 で扱う）**: `telemetry_.junction_turn` は `maneuver_dir == 0` のフォールバック
分岐でしか埋まらない（`ControllerVirtualDriver.cpp:2100-2104`）。車線変更が指示器を所有している
フレームでは構造体の既定値のままになる。TURN 意図がそこで消える。

---

## 3. 語彙（確定。実装者はこの名前を使うこと）

**enum で持つこと。文字列で持ってはいけない。** 統制語彙のフィールドに検査が無いと、typo では
なく「別の妥当な語」で壊れる（`stopping` と `stop`、`turn` と `turning`）。症状は例外ではなく
「カウンタが増えない」で、静かに死ぬ。文字列化はシリアライザ1か所に閉じ、値集合のテストを
同時に置く（§9-2）。

### 3-1. `IntentKind`

| 値 | 意味 | 出典となる素材 |
| :--- | :--- | :--- |
| `STOP` | 完全に停止する | `PolicyConstraint::Kind::STOP_AT_S` |
| `SLOW` | 速度を落とす（停止はしない） | `MAX_SPEED` / `MAX_SPEED_TO_S` / `midlong.constraints[kind=curve\|speed_limit\|junction]` |
| `LANE_CHANGE` | 隣接車線へ移る | `lane_change.*` |
| `TURN` | 交差点で右折・左折する | `junction_turn.*` |
| `OVERTAKE` | 追い越す（往復の車線変更を含む上位意図） | `overtake.*` |
| `YIELD` | 譲る（停止するとは限らない） | `source == "yield_sign"` の `MAX_SPEED_TO_S` |

`RESUME`（発進・再加速）は第2段とする（§11）。

**`PolicyConstraint::Kind::YIELD` を材料にしてはいけない（調査で判明）。** この列挙子は
**生産者がゼロ**である。譲れの標識が実際に出すのは `MAX_SPEED_TO_S` + `value =
yield_creep_speed` + `source = "yield_sign"` であり（`StopYieldSignAware.cpp:174-180`）、
`Kind::YIELD` を emit するコードはリポジトリ内に1件も無い。しかも
`ApplyPolicyConstraints` の `switch` に `case YIELD:` は無く `default: break;` に落ちるので、
**仮に誰かが emit しても速度プランナーは黙って無視する**。
`VirtualDriverTelemetryJson.cpp:204` の `case ... YIELD: return "yield";` だけが唯一の
参照であり、出力されたことは一度もない。投影を `Kind` に紐づけると、
**永久に立たない意図**ができあがる。

### 3-1-1. 停止の投影は「誰が言い出したか」に依存させない（不変条件）

**`STOP` 意図は `STOP_AT_S` 制約の存在からのみ投影する。`source` は問わない。**

これは「譲るつもりだったが結果として止まる」を取りこぼさないための不変条件である。
譲れの標識自体は creep 速度まで落とすだけで停止しない（`StopYieldSignAware.cpp:175` の
"No stop (deferred to 3d)"）。実際に止まる判断を下すのは `ConflictPointResolver` で、
そちらは `STOP_AT_S` を `source = "conflict_point"` で出す。

したがって譲って止まる場面では

- `YIELD`（`source = yield_sign`、減速中）
- `STOP`（`source = conflict_point`、`subject_osi_id` = 優先車）

の**2行が同時に立つ**。投影を制約の種類に紐づけている限り、これは自動的に成立する。
逆に「`YIELD` を出したポリシーは `STOP` を出さない」といった**ポリシー単位の分岐を投影層に
書くと、この保証が壊れる**。書かないこと。

**`OVERTAKE` 中は `LANE_CHANGE` 行を出さない。** 追い越しは車線変更のホップ機構を丸ごと借りて
いるので（`vd-component:overtake-maneuver` → `depends-on` → `vd-component:lane-change-initiation`）、
両方出すと同じ横移動が2行に見える。上位意図が立っている間は下位を抑制する。

### 3-2. `IntentPhase`

| 値 | 意味 | 外から観測できるか |
| :--- | :--- | :--- |
| `POSSIBLE` | 要件は見えているが、まだやると決めていない | ✕ |
| `PLANNED` | やると決めた。まだ何もしていない | ✕ |
| `ANNOUNCED` | 合図を出した（指示器・ブレーキランプ） | **○** |
| `EXECUTING` | 動作中 | **○** |
| `COMPLETING` | **やり遂げる**途中（追い越しの復路など） | **○** |
| `ABORTING` | **やめて戻る**途中。中止された動作がまだ収束していない | **○** |
| `ABANDONED` | 取り消した（`cancel_reason` つき）。動作を伴わない | **○**（消えたことが見える） |

**この「外から観測できるか」の列が §4 の配列分割の根拠であり、§9 の verdict 境界そのものである。**

### 3-2-1. `COMPLETING` と `ABORTING` を分ける理由

**終わる状態と戻る状態は同じではない。** どちらも「元の状態へ向かう横移動」に見えるが、
`COMPLETING` は計画どおり最後まで進んでおり、`ABORTING` は計画を捨てている。
HMI が言うべき文が違う（「戻ります」と「取りやめました」）し、検証で問いたいことも違う
（前者は「復路が完了したか」、後者は「中止が安全に収束したか」）。1つの値に畳むと、
**追い越しの復路と、車線変更の失敗が、同じ表示になる。**

`ABANDONED` との違いも明確にしておく。`ABANDONED` は**動作を伴わない取り消し**
（合図だけ出して発起前にやめた、制約が消えた）。`ABORTING` は**動作を伴う取り消し**
（既に横へ動き出していて、その横移動が収束するまで時間がかかる）。

### 3-2-2. `ABORTING` は「元へ戻る」を保証しない（重要）

中止処理（`ControllerVirtualDriver.cpp:1018-1027`）が呼ぶのは
`DisarmLaneChangeHop` + `DisarmResumeMerge` の2つだけで、これは**横オフセットの生成を
止めるだけ**である。元の車線へ戻す動作は実装されていない。止めた後どこへ収束するかは、
中止した瞬間に車体が車線境界のどちら側にいたか、および横方向を誰が引き継いだか
（storyboard アクション / resume-merge / 人間）で決まる。

したがって `ABORTING` の定義は「**戻る**途中」ではなく
「**中止された横移動が、まだどの車線中心にも収束していない状態**」である。
収束先が元の車線か目標車線かは、観測して初めて分かる。
表の「やめて戻る途中」は人間向けの説明であり、実装の契約は上の1文の方である。

**「やっぱりやめて確実に元の車線へ戻す」という挙動そのものは、この設計のスコープ外**
（`vd-component:lane-change-initiation` 側の別タスク）。§11 に記載する。

### 3-3. 投影表（確定）

空欄は「その機能にその段階が存在しない」、✕ は「素材が今は無い」。

| 機能 | POSSIBLE | PLANNED | ANNOUNCED | EXECUTING | COMPLETING |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `LANE_CHANGE` | `n_remaining > 0` | `dist_to_connection <= required_m` かつ未 arm | `signal_active` | `armed` | arm 解除済み・`aborted_reason` が空・`\|lane_offset\|` が未収束（§1-4 覆った想定2） |
| `OVERTAKE` | `considered` | — | `phase = signal_out` | `phase = moving_out \| pass` | `phase = signal_back \| moving_back` |
| `STOP`（信号） | 制約出現かつ `committed = false` | `committed = true` かつ未減速 | ブレーキランプ点灯 | 減速中 | `phase = green` で消滅 |
| `STOP`（標識） | 制約出現 | — | `gt.stop_yield.phase = approach` | `= hold` | `= creep` → `cleared` |
| `STOP`（先行車・横断歩道・コンフリクト・AEB） | 制約出現 | — | ブレーキランプ点灯 | 減速中 | 制約消滅 |
| `TURN` | ✕ **射程不足（§7）** | — | `junction_turn.dir != 0` | `on_connector` | — |
| `SLOW` | `midlong.constraints` 出現 | — | ブレーキランプ点灯 | v 下降中 | — |
| `YIELD` | 制約出現 | — | — | 減速中 | 制約消滅 |

`STOP` / `SLOW` の `ANNOUNCED` が読むのは `VirtualDriverTelemetry::brake_light_on`
（**本設計で新設**。§1-4 覆った想定3）であって `driver.brake` ではない。前者は
`ApplyLights` が実際に灯へ渡している値そのもので、0.05 の閾値と 0.35 s のラッチを含む。

`STOP` の `ANNOUNCED` をブレーキランプに置くのは恣意ではない。**外から見える停止の予告は
実車でもブレーキランプしかない**（OSI `brake_light_state` は `NORMAL` / `STRONG` の2値で、
停止と減速を区別しない）。「止まると決めたがまだ減速していない」は外から見えないので `PLANNED`
に落ちる。この対応が §9 の「外形は verdict 可」を意味のあるものにしている。

### 3-4. `ABORTING` / `ABANDONED` の投影と、足りない素材

上の表に列を足さず別に書く。中止は全機能に横断的で、かつ**素材が1つ足りない**からである。

| 機能 | `ABORTING`（動作を伴う中止） | `ABANDONED`（動作を伴わない取り消し） |
| :--- | :--- | :--- |
| `LANE_CHANGE` | 中止された かつ `\|lane_offset\|` が収束閾値超 | `ANNOUNCED` 止まりで消えた（発起前） |
| `OVERTAKE` | 同上（`MOVING_OUT` / `MOVING_BACK` からの中止） | `SIGNAL_OUT` / `SIGNAL_BACK` からの中止 |
| `STOP` / `SLOW` / `YIELD` | — （横移動が無いので該当なし） | 制約消滅 |
| `TURN` | — | 経路変更で消滅 |

**足りない素材（実装が要る）**: テレメトリは「hop が終わった」ことしか出しておらず、
**完了による終了と中止による終了を区別できない**。`armed` の true→false は両方で起きる。
`DisarmLaneChangeHop` は breadcrumb を残す設計だが（ヘッダ記載のとおり `armed` だけ
クリアする）、非 arm 時のテレメトリは `diag_hop` から**次にやるべきホップ**を再計算して
上書きするので、直前に何を中止したかは外に出ない。

必要な追加は1フィールドである。

```
lane_change.aborted_reason : "" | "storyboard" | "resume_merge" | "manual_lateral"
```

`ControllerVirtualDriver.cpp:1020-1027` の中止ブロックで立て、次の arm で消す。
`suppressed` を構成する3条件がそのまま値になる（`has_lateral_storyboard_action` /
`resume_merge_state_.active` / `lat_manual`）。**完了パス（`ControllerVirtualDriver.cpp:1257`
の `DisarmLaneChangeHop`）では立てない** — そこが完了と中止を分ける唯一の点である。

これは既存フィールドを変えない追加であり（§2-2 の規約どおり）、制御に触れない。
**この1フィールドを同じ PR で入れること**を `ABORTING` を語彙に入れる条件とする。
生産者のいない語彙値を残さない（`where` から `cross` を落としたのと同じ規律）。

---

## 4. 2つの配列 — verdict 境界を構造で持つ

### 4-1. 出力の形

```jsonc
"intents": [                    // 外形。ANNOUNCED 以降に達した意図だけが載る
  { "id": 7, "kind": "stop", "phase": "executing",
    "distance_m": 24.1, "eta_s": 3.5, "subject_osi_id": -1,
    "x": 112.4, "y": -8.2 }
],
"intent_reasons": [             // 内部判断。POSSIBLE/PLANNED を含む全段階
  { "id": 7, "kind": "stop", "phase": "executing",
    "source": "traffic_light",           // 動機（なぜするのか）— 常に1つ
    "tier": "compliance",
    "binding_lon": true, "binding_lat": false,
    "committed": true,
    "blockers": [],                      // 阻害（なぜできないのか）— 0個以上。§8
    "cancel_reason": "" },               // 取り消し（なぜやめたのか）— ABANDONED のときだけ
  { "id": 9, "kind": "lane_change", "phase": "planned",
    "source": "route", "tier": "comfort",
    "binding_lon": false, "binding_lat": false,
    "committed": false,
    "blockers": [
      { "where": "rear",  "subject_osi_id": 12,
        "code": "rear_gap",  "quantity": "gap_m", "measured": 6.2, "required": 11.3 },
      { "where": "front", "subject_osi_id": 11,
        "code": "lead_gap", "quantity": "gap_m", "measured": 9.8, "required": 16.7 }
    ],
    "cancel_reason": "" }
]
```

2行目が §8 の主題である。**前と後ろが同時に塞いでいることが、2要素として見える。**
今の `gap_reason` は文字列1つなので、この状況が `"lead_gap"` に潰れる。

`id` で結合する。**同じ意図が両方に載る**（外形側は外形フィールドだけ、理由側は判断フィールド
だけ）。POSSIBLE / PLANNED の意図は `intent_reasons[]` にしか載らない。

### 4-2. なぜ接頭辞でなく配列分割か

既存の verdict 境界は `gt.` / `gt.dbg.` の**キー接頭辞**で表現され、lint が verdict 経路から
機械的に除外できるようになっている（`PolicyDetail.hpp`）。しかし `intents[]` は構造化データで
あり、接頭辞という手段が使えない。

**配列を物理的に2本に割るのが、接頭辞と同じ強度の代替である。** 検査は
「matcher が `intent_reasons` を参照していないか」という文字列1つの grep で済み、
新しい lint 機構を要らない。

`signal_catalog.yaml` の `exposure` 語彙にはすでに `debug`（＝観測できるが verdict-trust 対象外）
があり、`intent_reasons[]` はそれに正確に対応する。**名前に "debug" を含めない**のは、
これが HMI にとっては主要コンテンツだからである（「なぜ止まるのか」を人に見せるのは
デバッグではない）。名前は中身を語るべきで、方針は名前ではなくカタログと lint が持つ。

### 4-3. 右左折で、この分割が噛み合う

W3（§7）で長射程検出した「500 m 先で右折します」は外からまだ見えないので `POSSIBLE`
＝ `intent_reasons[]` 側に載り、HMI はそれを表示する。
法定の「30 m 手前で指示器」は `intents[]` の `ANNOUNCED` として verdict にかかる
（`req-vd-ad:REQ-AD-021` はすでにこれを検証対象にしている）。

**HMI の予告と法令適合の検証が、同じデータの別の面として同居する。** これが §4-1 の形を
採る主たる理由である。

---

## 5. `binding` — `std::min` が捨てている勝者

### 5-1. 何が失われているか

`ApplyPolicyConstraints`（`ManeuverAwareSpeedPlanner.cpp:113-175`）は全制約を速度サンプル列へ
`std::min` で畳み込む。赤信号・先行車・横断歩道が同時に立っているとき、**実際に速度を決めたのが
どれか**は畳み込みの後には残らない。

消費側で「tier 最大 → s 最小」と近似することはできるが、それは**各消費者が独自ルールを持つ**
ことを意味し、画面ごとに違う答えが出る。決定は生成側に置く。

### 5-2. 形

畳み込みのループで、各サンプルの `v` を更新した制約のインデックスを記録し、**ego 直近の
サンプル（`s_ahead` 最小）で勝っている制約**を binding とする。プランナー出力に
`binding_constraint_index`（`-1` = 制約なし）を1つ足すだけで、既存の返り値は変えない。

### 5-3. 縦と横で別々に持つ

`binding` は1つに絞れない。**縦（停止・減速）と横（車線変更・右左折）は同時に支配的になり得る**
（赤信号で止まりながら車線変更の合図を出している、は正常な状態）。
ドメインごとに最大1つ、が正しい粒度である。よって `binding_lon` / `binding_lat` の2フィールドに
分ける。横側の binding は `OverrideManager` のドメイン所有権
（`domain_split_ownership.md`）とは別物であることに注意 — あちらは「人間か AD か」、
こちらは「AD の中でどの意図か」である。

---

## 6. `eta_s` — 言えないことを言わない

### 6-1. 3段構え

| 範囲 | 出典 | 精度 |
| :--- | :--- | :--- |
| 3 秒以内 | `preview.points[].t` を直読 | **正確**（実際の計画速度と時刻） |
| 3 秒より先 | `midlong.v_target_profile[]` を台形則で積分 | 楽観側にずれる（後述） |
| 停止意図 | 解析式 `eta_s = 2·d / v_ego` | 等減速の下で厳密 |

積分は `t(s_k) = Σ 2·Δs / (v_i + v_{i+1})` とする。**区間の平均速度で割ること。**
片端の速度だけで割ると、減速区間で必ず短く出る（`future_trajectory` の実装で同じ誤りを
一度踏んでいる — `signal:ego_planned_path` の是正履歴 (ii)）。

停止だけ解析式を使うのは、等減速で距離 `d` を速度 `v` から 0 まで減速するとき平均速度が `v/2`
であり `t = 2d/v` が厳密に出るのに対し、積分は分母が 0 に近づく端で数値的に暴れるからである。
プランナーが `sqrt(2·a·(s_stop - s))` のランプで近づく（`ManeuverAwareSpeedPlanner.cpp:154-156`）
以上、この形が計画と一致する。

### 6-2. 停止点で写像を打ち切る

`v → 0` なので `1/v` が発散する。**ここで速度の床を入れてはいけない。**

これは既に一度踏んだ罠である。`future_trajectory` では制御用の `min_preview_span` 床
（停止中もパースート先を確保するためのもの）が出力側へ漏れ、報告される線が計画停止点を
追い越していた（`VirtualDriverTypes.hpp:56-68` に是正の経緯が残っている）。
**制御用の床を出力へ流すな**、が導かれた規律である。

正しい扱いは、**最初の計画停止点で s→t 写像を打ち切り、その先の意図は `eta_s = -1`
（算出不能）とする**こと。「停止の向こう側の右折まであと18秒」は、停止時間が未知である以上
原理的に言えない。**言えないことを言わないのが、床を置くより正しい。**

`-1` は「未測定」ではなく「測定したが原理的に出ない」を意味する。消費側が 0 と読まないよう、
absent-is-not-zero の規律（`PolicyDetail.hpp` の `TryGetDetail` が同じ理由で `false` を返す）を
ここでも守る。

### 6-3. `v_target_profile` は上限であって計画速度ではない

`MidLongPlannerSnapshot::v_target_profile` は `(s, v_max)` のペアである
（`VirtualDriverTypes.hpp:95-102`）。実際の速度はこれを下回り得るので、積分から出る `eta_s` は
**楽観側（短め）にずれる**。3 秒以内で `preview` を優先するのはこのためであり、
それより先の値は「上限速度で走った場合の到達時刻」という意味だと文書化する。

### 6-4. 写像は1本だけ作る

s→t 写像は毎フレーム1本だけ構築し、全 intent がそれを参照する。**各機能が独自に秒を計算し
始めると、同じ停止点に2つの違う秒が出る。**（`gt.acc.thw_actual_s` が
`gt.lead_vehicle.gap_m` を読み直す代わりに制御中のポリシー自身の測定値を使っている理由と
同じ — `PolicyDetail.hpp` の「WHEN IT IS LEGITIMATE TO READ THIS CHANNEL BACK」参照。）

---

## 7. 右左折の観測射程 — 合図と観測を分ける

### 7-1. 引数を大きくするだけでは効かない（調査で確定）

当初「`RouteLookaheadJunctionTurn` は距離を引数に取る純関数なので、長い値で呼べばよい」と
見積もった。**効かない。**

```cpp
if (road->GetJunction() == ID_UNDEFINED)
{
    // First road boundary crossed is NOT a junction connector: nothing to
    // detect within this lookahead
    return JunctionTurnLookahead{};
}
```
（`JunctionTurn.hpp:180-184`）

この関数の契約は「**次に跨ぐ道路境界が接続路か**」であって「経路上の次の交差点はどちらへ曲がるか」
ではない。30 m で機能するのは、その距離では交差点に直接つながる道路の上にいるからにすぎない。
300 m で呼べば、途中の通常の道路境界で必ず打ち切られて `dir = 0` が返る。

### 7-2. 観測側は別関数にする

既存関数の意味論を変えて「接続路でない境界を跨いで探索を続ける」ようにすることもできる。
**採らない。** あの関数は法定合図ゲートが依存しており、`reach` の1フレーム先読みは
「離散サンプリングで法定距離を通り過ぎてから点灯する」不具合を潰すために入っている
（`junction_turn_signal.md` §2-4、実測 29.74 m）。

**別関数にすれば「ゲート式不変」が約束ではなく構造的な保証になる。**

```cpp
// 観測専用。経路に沿って歩き、非接続路の境界を跨いでも探索を続ける。
// 合図判定には決して使わない（呼び出し側は telemetry にしか渡さない）。
JunctionTurnLookahead RouteLookaheadNextJunctionTurn(const roadmanager::Position& start,
                                                     roadmanager::OpenDrive*      odr,
                                                     double                       lookahead,
                                                     double                       step);
```

### 7-3. `maneuver_dir == 0` ゲートの外で呼ぶ

§2-4 の穴に対処する。既存の `DetectJunctionTurn` は車線変更が指示器を所有しているフレームでは
呼ばれず、`telemetry_.junction_turn` が既定値のままになる。観測スキャンは**その分岐の外**で
無条件に呼び、結果を意図層へ渡す。合図の判定には一切関与しないので、挙動は変わらない。

### 7-4. 刻みを粗くしてはいけない

コストを下げるために `step` を 10 m にしたくなる。**短い接続路を丸ごと飛び越えて、
その先の出口道路（非接続路）で「なし」を返す。**探索ループは1ステップごとに `GetTrackId()` の
変化しか見ていないので、接続路を跨いだことに気づけない。

刻みは合図側と同じ 2 m を既定とする。300 m で 150 回の `MoveAlongS` になるが、
これは `ManeuverAwareSpeedPlanner` が既に毎フレーム払っている桁と同じである。
それでも**既定 OFF**（`intent_turn_lookahead_m = 0.0`）とし、HMI が要求するときだけ有効化する。
投影そのものは純計算でタダだが、追加スキャンは有料である、という区別を config に持たせる。

---

## 8. 理由 — 動機・阻害・取り消しを分けて持つ

「理由」と一語で呼ばれるものは3種類ある。**混ぜると junk drawer になる**ので、
フィールドを分けて持つ。

| 種別 | 問い | 形 | 現況 |
| :--- | :--- | :--- | :--- |
| **動機** (motive) | なぜそうするのか | `source` — 常に1つ | ● 既に出ている |
| **阻害** (blocker) | なぜできないのか | `blockers[]` — 0個以上 | △ 文字列1つに潰れている |
| **取り消し** (cancel) | なぜやめたのか | `cancel_reason` — 1つ | △ 素材はあるが未整理 |

### 8-1. 阻害理由の現状 — 主語も数値も捨てられている

| 意図 | 今の理由 | 言えること / 言えないこと |
| :--- | :--- | :--- |
| `LANE_CHANGE` | `gap_reason` ∈ `""` / `lead_gap` / `rear_gap` / `rear_ttc` | 前後は言える。**誰か・どれだけ足りないか・横**は言えない |
| `OVERTAKE` | `blocked_reason` ∈ `""` / `suppressed` / `no_passing_lane` / `route_budget` / `oncoming` / `gap` | さらに粗い。同方向車線の lead/rear/ttc を**全部 `"gap"` 1語に潰している**（`ControllerVirtualDriver.cpp:1200`） |
| `TURN` | 無い | §8-6 参照（欠落ではない） |
| `STOP` / `SLOW` / `YIELD` | `source` + `gt.*` detail | こちらは**動機**であって阻害ではない |

問題は3つある。

**(a) 最初の1つで打ち切っている。** `EvaluateGapAcceptance` は3条件を front-to-back で評価し、
最初の失敗で `return` する（`LaneChangeInitiation.cpp:109-143`）。前も後ろも詰まっているとき、
出るのは `"lead_gap"` だけである。**知りたいのはまさにその区別**なので、この形では答えられない。

**(b) 数値がローカルとして死んでいる。**

```cpp
const double required_lead = std::max(cfg.gap_min_m, v_ego * cfg.gap_headway_lead_s);
if (gap.gap_lead_m < required_lead) { result.reason = "lead_gap"; return result; }
```

`required_lead` / `required_rear` / `ttc` はいずれも関数のローカルで、外へ出ない。
これは `PolicyDetail.hpp` が作られたときと**同じ構図**である（「AEB の TTC / 必要減速度が
`Evaluate()` の中のローカルとして死んでいた」）。あのときの答えを、ここでも採る。

**(c) 主語が捨てられている。** `LaneChangeGapSample` は gap と速度だけを返し、
**どの車だったかを返さない**（`LaneChangeInitiation.hpp:148-156`）。`ScanAdjacentLaneGap` は
走査中にオブジェクトを手に持っているので、捨てているだけである。

### 8-2. `blockers[]` の形

```jsonc
"blockers": [
  { "where": "rear",  "subject_osi_id": 12,
    "code": "rear_gap",  "quantity": "gap_m", "measured": 6.2,  "required": 11.3 },
  { "where": "front", "subject_osi_id": 11,
    "code": "lead_gap", "quantity": "gap_m", "measured": 9.8,  "required": 16.7 }
]
```

4つの設計判断がある。

**(1) 配列にする＝全条件を評価する。** 「前も後ろも」が「前が」に潰れるのを止める。
評価コストの増加は無視できる（比較が2つ増えるだけ）。
**制御が変わらないことの根拠**: 制御側が見ているのは `accepted` の bool ただ1つで
（`ControllerVirtualDriver.cpp:1282-1286`）、どの条件で落ちたかは制御に影響しない。
早期 return を外しても `accepted` は同じ値になる（1つでも false なら false）。§8-7 で受入条件にする。

**(2) `where`（位置）と `code`（識別子）を両方持つ。** `where` ∈
`front` / `side` / `rear` / `oncoming` の4値。**HMI は位置で話し、検証はコードで照合する。**
どちらか一方に寄せると、片方が必ず翻訳表を持つことになる。

位置を持たない blocker（§8-4 の `—` 行）には**不在値**を使い、`""` で直列化する
（§1-4 覆った想定4）。位置語彙は4値のままで、「位置が無い」を第5の位置にはしない。
blocker の要素の形が常に同じになるので、消費側がキーの有無で分岐しなくてよい。

**(3) `measured` / `required` のペアで出す。** 「後続車が近すぎます」より
**「後続車まで 6.2 m、必要 11.3 m」**が桁違いに役に立つ。あと何 m 待てばよいかが言える。
`quantity` は単位つきの量名（`gap_m` / `ttc_s` / `budget_m`）で、
単位は値ではなくキー側に置く（`PolicyDetail.hpp` の命名規約と同じ）。

**(4) 主語は OSI id 空間で持つ。** `subject_osi_id` は `OsiIdOf`
（`control/common/OsiIdentity.hpp`）で取る。**シナリオのエンティティ番号ではない** —
両者は別の数であり（実測 Ego=10 / Lead=11 に対しエンティティ番号は 0 / 1）、
被検体や OSI 記録と突き合わせられるのは前者だけである。これで HMI が画面上の実車を指せる。

### 8-3. `side`（並走）は新設が要る

ギャップ判定は1次元である。`Position::Delta` の `ds` の符号で前後に振り分けるので、
**真横の車は `ds ≈ 0` のまま前か後ろのどちらかに分類される。**「横に居る」は今のモデルに無い。

作るのは難しくない。`ScanAdjacentLaneGap` の gap は既に bumper-to-bumper（半車長を両側から
引いている）ので、**その床を外した値が負なら「車体が縦方向に重なっている」＝並走**である。
ただし `gap_lead_m` / `gap_rear_m` 自身には `std::max(0.0, ...)` の床が入っていて負の値は外に出ない
（§1-4 覆った想定1）。**床を外すのではなく、重なりの深さを別フィールドで新設する。**

```cpp
double lead_overlap_m = 0.0;  // [m] <= 0。0.0 = 重なりなし
double rear_overlap_m = 0.0;  // 負 = 縦方向の重なり深さ（床を掛ける前の生の gap）
```

`where = side`、`code = side_overlap`、`measured` はこの負の値をそのまま出す。

重なっているとき既存の `gap_*_m` は 0.0 になり、`required` は `gap_min_m` を下限に持つので
**受容判定はどのみち落ちる。ラベルが `front` / `rear` から `side` に変わるだけ**である。
受容判定式が新フィールドを一切読まないことが、§8-7 の (1) を構造で保証する。

### 8-4. 各意図の blockers（確定表）

| 意図 | `where` | `code` | `quantity` | 素材の現況 |
| :--- | :--- | :--- | :--- | :--- |
| `LANE_CHANGE` | `front` | `lead_gap` | `gap_m` | 値あり / **id 要追加** |
| `LANE_CHANGE` | `rear` | `rear_gap` | `gap_m` | 値あり / **id 要追加** |
| `LANE_CHANGE` | `rear` | `rear_ttc` | `ttc_s` | 値あり / **id 要追加** |
| `LANE_CHANGE` | `side` | `side_overlap` | `gap_m` | **新設**（§8-3） |
| `LANE_CHANGE` | — | `no_target_lane` | — | `route_lane.diagnostic` / `reason` から |
| `OVERTAKE` | `oncoming` | `oncoming_gap` | `gap_m` | `OncomingSample` / **id 要追加** |
| `OVERTAKE` | `front`/`rear`/`side` | 同 `LANE_CHANGE` | | **要分解**（今は `"gap"` 1語） |
| `OVERTAKE` | — | `route_budget` | `budget_m` | `route_budget_m` / `required_m` |
| `OVERTAKE` | — | `no_passing_lane` | — | あり |
| `OVERTAKE` | — | `suppressed` | — | あり |

`OncomingSample` も `has_oncoming` / `gap_m` / `v_oncoming_mps` だけで id を持たない
（`OvertakeManeuver.hpp:181-186`）。LC 側と同じ追加をする。

### 8-5. `blockers[]` は `intent_reasons[]` 側に置く

`measured`（相手との実際の距離）は世界の真実であり、外形に見える。しかし
`required` と「だから塞がれている」という判断は面2の内部である。**混在しているものは
内部側に置く**のが安全側で、理由はこうである。

**「AD が 6 m のギャップを正しく拒否した」を検証したい matcher は、ギャップを
GroundTruth から自分で測るべきであって、AD 自身の測定値を読むべきではない。**
AD の測り方が間違っていたら、その matcher は間違いを追認する。これは
`conflict_prediction_span` が exposure `debug` に留められているのと同じ理由である。

したがって `blockers[]` は `intent_reasons[]` の一部として出す。HMI は自由に読む。

### 8-6. `TURN` に阻害理由が無いのは欠落ではない

右左折は経路が決めるもので、「曲がれない」という概念を持たない。曲がる手前で止まるのは
**`STOP` 意図**であり、その動機は `source`（`traffic_light` / `conflict_point` /
`crosswalk` / `junction_guard`）として既に出ている。

**`TURN` に blockers を無理に作ると、`STOP` の動機を `TURN` の阻害として二重に記帳することになる。**

HMI が「対向直進車のため右折待ちです」と表示したい場合、正しいデータは

- `TURN`（`EXECUTING`、`on_connector`）
- `STOP`（`source = conflict_point`、`subject_osi_id` = 対向車）

の**2行**であり、`TURN` 1行に理由を詰め込むことではない。**表示文の合成は消費側の仕事で、
投影層は事実を2つ並べるところまでを担う。**

### 8-7. 挙動不変の担保（実装上の主要リスク）

`EvaluateGapAcceptance` の早期 return を外すのは**純関数の意味論変更**である。守ること。

1. `accepted` の値が現状と**ビット単位で同一**であること。
2. 既存の単一文字列 `reason` は**互換のため残し**、`blockers[]` の**先頭要素の `code` と常に一致する**
   こと。すなわち front-to-back の**評価順を変えない**（打ち切りをやめるだけ）。
3. **既存の `test_LaneChangeInitiation.cpp` を1行も書き換えずに通ること。**

3 が最も効く受入条件である。書き換えが要るなら、それは挙動が変わった証拠であって、
テストを直す場面ではない。

### 8-8. 取り消し — 予告は「出す」より「取り消す」が難しい

コミットラッチは信号にしかない（`gt.traffic_light.committed`）。車線変更はギャップ待ちで
結局やめることがあり、右左折はルート変更で消える。**「まもなく左へ」と出した後に
`gap_reason` で待たされ続ける表示は、今のデータで普通に起きる。**

意図が消えるとき、**黙って配列から落とさない**。1フレーム以上 `phase = ABANDONED` を経由させ、
`cancel_reason` を付ける。

| 取り消し | `cancel_reason` の出所 | 段階 |
| :--- | :--- | :--- |
| ギャップが取れず断念 | `blockers[]` の先頭 `code`（§8-2 の (1) により、動機と同じ語彙で書ける） | `ABANDONED` |
| 横移動の途中で中止された | `lane_change.aborted_reason`（§3-4 で新設） | **`ABORTING`** |
| 追い越しを中止 | `overtake.blocked_reason` | 発起前なら `ABANDONED`、横移動中なら `ABORTING` |
| ルートが変わった | `route_lane.rerouted` / `route_lane.reason` | `ABANDONED` |
| 制約が消えた（青になった等） | `"constraint_cleared"`（`""` にしない — 消えたことと未記入は別） | `ABANDONED` |

`ABORTING` は `ABANDONED` と違い**1フレームで終わらない**（横移動の収束を待つ）。
したがって §8-9 の最短表示時間の対象外である — 実際に続いている状態を、
表示のために延長する必要はない。

**`ABANDONED` の瞬間の `blockers[]` は残す。**「なぜやめたか」は「やめる直前に何が塞いでいたか」
そのものであり、そこで配列を空にすると理由が消える。

### 8-9. 最短表示時間は投影層が持つ

`ANNOUNCED` 以降に達した意図は、消える条件が成立しても `intent_min_dwell_s`（既定 1.0 s）は
`COMPLETING` または `ABANDONED` として残す。各機能にヒステリシスを配ると、また語彙が割れる。

**`tier = SAFETY` は免除する。** AEB は即座に出て即座に消えるのが正しい挙動であり、
表示の都合で残すと「まだ効いている」と誤読させる。安全系に表示のための遅延を入れない。

---

## 9. 検証 — 何を作り、何を判定するか

### 9-1. verdict 境界（§4 の帰結）

- **matcher が読んでよいのは `intents[]` だけ。** `intent_reasons[]` は読まない。
- 新しい観測量を `signal_catalog.yaml` へ2件起こす（on-demand 原則に従い、実際に参照される分だけ）。
  - `signal:vd_intent` — exposure `frame`、外形。
  - `signal:vd_intent_reasons` — exposure `debug`、内部判断。**verdict-trust 対象外**。
- 実装ユニットを `component_catalog_vd.yaml` へ登録（`vd-component:intent-layer`、
  layer `cross-cutting`）。`namespaces.yaml` の `count` 更新と `--render` を忘れないこと
  （忘れると lint がハッシュ照合で落ちる）。

### 9-2. 単体テスト（エンジン不要 — ここが本層の主戦場）

1. §3-3 の投影表の**全行**。入力は POD の `VirtualDriverTelemetry` を手で組めば足りる。
2. **語彙の値集合**。`IntentKind` / `IntentPhase` の全列挙子が一意な文字列へ写り、
   文字列側にも列挙子側にも余りが無いこと。`test_AdasSlotTable.cpp` が OSI 列挙に対して
   やっているのと同じ形。
3. `intent_min_dwell_s` の効果と、`tier = SAFETY` の免除。
4. `ABANDONED` を経由すること（消滅が1フレームで配列から落ちないこと）、
   および**そのフレームの `blockers[]` が空になっていないこと**（§8-8）。
5. **`eta_s` が停止点で `-1` になること**（§6-2）。床が入っていないことの直接の証拠になる。
6. **`blockers[]` が前後同時を2要素で返すこと**（§8-2）。合成サンプルで
   `has_lead` / `has_rear` の両方を不足させ、要素数 2 を assert する。
   **これが「最初の1つで打ち切らない」ことの唯一の直接証拠**であり、
   §8-7 の意味論変更が効いたかどうかはここでしか分からない。
7. `where = side` が負の gap で立つこと（§8-3）。`front` / `rear` に落ちないこと。
8. **`measured` < `required` が全 blocker で成り立つこと。** 逆転していたら、
   拾ってきた数値の組が違う（`required_lead` と `gap_rear_m` を取り違える類の間違いは、
   値が両方それらしいので目視では通ってしまう）。
9. **`COMPLETING` と `ABORTING` が取り違わらないこと**（§3-2-1）。同じ
   「`armed` が false になり、横位置がまだ中心にない」入力に対し、
   `aborted_reason` が空なら `COMPLETING`、非空なら `ABORTING` になること。
   **この2つを分ける情報は `aborted_reason` ただ1つ**なので、
   それが立っていないケースを必ず対にしてテストする。
10. `ABORTING` が横位置の収束で終わること。`intent_abort_converged_offset_m` を
    またぐ入力を前後で与え、`ABORTING` → 消滅を確認する。

### 9-3. 負の対照 — ここを省くと完了ではない

「出た」だけ確認して完了にすると、常時 true のフィールドが混ざっていても気づかない。
**両極性を実証するまで完了としない。**

| 負の対照 | 期待 | 既存資産 |
| :--- | :--- | :--- |
| `intent_enabled = false` | `intents[]` と `intent_reasons[]` の**両方**が空 | — |
| 直進で交差点を通過 | `TURN` 意図が出ない | `junction_turn_signal.md` §4-3 の直進負例を流用 |
| 青信号を通過 | `STOP` 意図が出ない | 既存の traffic light シナリオ |
| 追い越しの実行中 | `LANE_CHANGE` 行が出ない（`OVERTAKE` のみ） | 既存の overtake シナリオ |
| `intent_turn_lookahead_m = 0.0` | TURN の `POSSIBLE` が出ない（`ANNOUNCED` は従来どおり出る） | — |
| 隣接車線が空 | `blockers[]` が**空**（`gap_accepted = true` と整合） | `lane_change_to_exit_ramp`（隣接車なし） |
| `TURN` が実行中 | `TURN` 行の `blockers[]` が**常に空**（§8-6） | 既存の junction シナリオ |
| 車線変更が**完了**した | `ABORTING` が出ない（`COMPLETING` になる） | `lane_change_to_exit_ramp` |
| 中断のないシナリオ全体 | `aborted_reason` が最後まで `""` | 同上 |

下から3行目が W3 の負の対照であり、**観測スキャンが既定 OFF であることの証拠**でもある。
最終行は §8-6 の設計判断（`STOP` の動機を `TURN` の阻害として二重記帳しない）を機械で押さえる。
**「常に空」は放置すると死んだフィールドと区別がつかない**ので、
`STOP` 側に対応する `source` が同時に立っていることを併せて assert する。

### 9-4. 挙動不変の証拠

`intent_enabled` の真偽にかかわらず、回帰ゲート
（`car_following_traffic_control_batch.yaml` / `aeb_safety_batch.yaml` /
`manualdrive_adas_batch.yaml`）の deviation が 0 であること。
特に **`intent_turn_lookahead_m > 0` でも 0 であること**を確認する — 観測スキャンが
合図ゲートへ漏れていないことは、コードを読むだけでなくこれで押さえる。

**ただし上の3バッチは車線変更も追い越しも走らせない。** §8-7 の
`EvaluateGapAcceptance` 意味論変更を押さえるのは
**`route_lane_batch.yaml`（車線変更）と `overtake_batch.yaml`（追い越し）**であり、
この2本を回さずに「回帰ゲート緑」と言ってはいけない。どちらも常設ゲートに載っていない
手動実行のバッチなので、**明示的に走らせる手順を実装 PR の説明に書くこと**。
常設ゲートが自分の変更を通過しないことに気づかないのが、この種の変更の典型的な落とし方である。

---

## 10. config キー（確定。実装者はこの名前を使うこと）

`VirtualDriverConfig.hpp` へフラットに足す（既存の `lane_change_*` / `overtake_*` と同じ流儀）。

| キー | 既定 | 意味 |
| :--- | ---: | :--- |
| `intent_enabled` | `true` | 意図層の投影を行う。投影は純計算なので既定 ON |
| `intent_eta_enabled` | `true` | `eta_s` を算出する。false なら全 intent が `-1` |
| `intent_min_dwell_s` | `1.0` | `ANNOUNCED` 以降の最短表示時間 [s]。`tier=SAFETY` は免除 |
| `intent_turn_lookahead_m` | `0.0` | 右左折の観測用スキャン距離 [m]。**0 = 行わない**（従来の合図距離のみ） |
| `intent_turn_scan_step_m` | `2.0` | 同スキャンの刻み [m]。粗くすると短い接続路を飛ばす（§7-4） |
| `intent_abort_converged_offset_m` | `0.3` | `ABORTING` を終わらせる横位置の収束閾値 [m]（§3-2-2） |

`intent_enabled` を既定 ON にできるのは、本層が制御へ戻る経路を持たないからである。
CPU を消費する `intent_turn_lookahead_m` だけを既定 OFF にすることで、
**「タダの投影」と「有料のスキャン」を config の上で分離する。**

**§8 の `blockers[]` に専用キーは足さない。** 阻害理由の数値は制御判定のためにどのみち
計算されている（`required_lead` / `required_rear` / `ttc`）ので、出すかどうかを切り替える
意味が無い。切り替えられる設定を増やすほど、負の対照で確認すべき組み合わせが増える。

---

## 11. スコープ外（意図的に実装しないこと）

| 項目 | 理由 |
| :--- | :--- |
| **OSI HostVehicleData への投影** | 本設計の出力面はテレメトリ JSON のみ。将来やるなら `intents[]` を `gt.intent.*` の KV へ平坦化するだけで、型さえ固まっていれば後付けできる（`custom_detail` は string KV なので構造は平坦化が必要） |
| **`RESUME`（発進・再加速）意図** | 素材が導出でしかない（制約の消滅 + `speed_error > 0` + throttle）。第2段。他の意図が一次資料を持つのに対し、これだけ性質が違う |
| **他車の意図** | **禁止**。`conflict_prediction_span` が exposure `debug` に留められているのと同じ理由で、運転 AI の予測を世界の真実として記録すると面3→面2直結の結合負債を再生産する（`signal:ego_planned_path` の note、`capability_model.md`「他車の軌道予測」） |
| **信号の予告延長** | SPaT（残秒数）が無い以上、80 m 先で青・40 m で赤の場合に予告は 40 m 分しか出せない。原理的な限界であり、`committed` ラッチで「決めたか」を出すのが正しい代替 |
| **ポリシー `lookahead` の変更** | 運転挙動が変わる（§1-3）。観測のために制御を変えない |
| **「やめて確実に元の車線へ戻す」挙動** | 本設計は `ABORTING` を**観測できるようにする**だけで、戻ることを実装しない（§3-2-2）。中止は横オフセット生成の停止であり、収束先は制御されていない。確実に戻すには復帰軌道の生成と、戻る途中の新たなギャップ判定が要る＝`vd-component:lane-change-initiation` 側の別タスク |
| **arm 後のギャップ再評価** | 現状ギャップ判定は未 arm 分岐にしか無く（`ControllerVirtualDriver.cpp:1259`）、一度 arm したら後続車が詰めてきても最後まで行く。「詰められたのでやめる」を作るのは上記と同じ別タスク。**意図層はこの欠落を作り出しても隠しもしない** — 現状を正しく投影すると、そういう場面で `ABORTING` は立たない |
| **予告リードの均一化** | 同上。ばらつきはそのまま出し、`eta_s` で消費側が判別する |

---

## 12. 参考

- `lane_change_initiation.md` §11（方向指示器のリードタイム — 時間で遡れないものを距離で解く）
- `junction_turn_signal.md` §2-1/2-4（接続路の検出と、離散サンプリングの1フレーム先読み）
- `overtake_maneuver.md` §9-1（`considered` の false-PASS ガード — 負の対照の作り方）
- `manualdrive_adas_design.md` §8-2/8-3（`AdasFunctionReport` の投影パターン、
  `reported` と `active` を分けて「測定した上での否」を表現する規律）
- `capability_model.md` §1（検証スパイン）／ §2.2（exposure と verdict-trust）／ §7.1（命名規約）
- `PolicyDetail.hpp`（KV 命名規約と、観測チャネルを制御へ戻さない規律）
