# AUTO_RESUME 合流軌道生成 — 設計案（feature:F7 ②）

**状態**: **実装済み**（`de2205a0`、`ResumeMergeProfile.{hpp,cpp}` + config + GUI + ユニットテスト）。
以下は実装時の設計記録である。本文には調査中の訂正（§6-2 / §6-3）がそのまま残っている。
**対象**: 手動オーバーライドで本来の車線の隣に出た状態から AUTO_RESUME したときの急復帰
**既定**: 設計時は OFF。**出荷は ON**（`config/virtual_driver.json` の `resume_merge_enabled: true`、2026-07-28 以降）

---

## 1. 症状と根治点

ユーザー実機報告: 「オーバーライドして手動で走ると自車が本来の車線の隣にいる。その状態で
AUTO_RESUME を押すと元の車線へ戻る動きが急すぎる」。車線 1 本分（約 3.5m）の横ずれからの
復帰であって、通常走行中の操舵の当たりの話ではない。

**根治点は参照経路の瞬時スナップ**（一次証拠 = 製品コード）:

```cpp
// TrajectoryShortPlanner.cpp:130-140
// Anchor the preview to the routed lane CENTER (offset 0) ONLY when no
// deliberate lateral maneuver is active. ...
if (lat_actions.empty())
    pos.SetLanePos(pos.GetTrackId(), pos.GetLaneId(), pos.GetS(), 0.0);
```

横アクションが無い間、preview のアンカーは**毎フレーム・ランプ無しで**車線中心へ寄せられる。
したがって 3.5m ずれた状態の復帰目標は初手から車線中心＝**最短復帰**であり、Pure Pursuit は
その cross-track error をそのまま最大級の操舵指令に変換する。

この欠陥は既に別モジュールのヘッダが名指しで記述している:

```
// AdSteeringEnvelope.hpp:5-13
// The Pure Pursuit driver model (PIDPurePursuitDriver.cpp:30-58) is completely
// STATELESS and has no limit on lateral deviation, rate, or amplitude beyond
// the final `clamp(+-1)` on its own output. TrajectoryShortPlanner.cpp:139-140
// anchors the preview to the routed lane CENTER instantaneously every frame
// (no ramp), so a large raw cross-track error at the moment a manual->
// AUTO_RESUME transition fires turns directly into a maximal steering command.
// Measured with FFB fully OFF: steer_peak 0.957 / yaw rate 1.95 rad/s /
// estimated lateral accel ~29 m/s^2 — the defect is in the AD command itself,
// not the haptic loop.
```

**FFB を切っても出る＝(A) AD 側**という切り分けは既に済んでいる。現行の安全包絡線
（`ComputeAdSteeringEnvelope`）はこの指令を**下流でクランプして隠している**だけで、
参照そのものはステップのまま。

### 1-1. 既存の実測との整合

`test_results/f7_lane_offset_resume.txt`（車線 1 本分条件・出荷既定 jerk_max=0）:

| 量 | 実測 | 位置づけ |
| :--- | ---: | :--- |
| onset 実効躍度（引き継ぎフレーム） | **-245.902 /s²**（全 5 速度で厳密同値） | ステップ参照が包絡線のレート上限に張り付いた結果 |
| PEAK a_lat | 2.58 m/s²（上限 4.3） | 包絡線通過**後**の値 |
| 曲率クランプ境界スナップ | v≥8 で 77–97 フレーム、130–234 /s² | 指令が安全域外へ出て境界へ引き戻され続けている |
| 復帰時間 | v=8 で 2.43s / v=14 で 2.03s | |

全 5 速度で onset が厳密に同値（-245.902）なのは、**速度に依らずステップが上限に張り付く**
＝参照がステップであることの直接証拠。包絡線後の軌跡が単調で a_lat 2.58 に収まって見えるのは
下流クランプの働きであって、参照が滑らかだからではない。

---

## 2. 方針

**AUTO_RESUME の瞬間に、現在の横位置から目標経路へ数秒かけて合流する横オフセット参照を
生成し、preview の各点にその将来値を載せる。** 車線変更相当のマヌーバ。

### 2-0. 合流目標は「ルート車線」（PM 承認済み・2026-07-27）

**合流先はルート車線の中心であって、現在占有している車線の中心ではない。**

根拠はユーザー要求そのもの: 「オーバーライドして隣車線にいる状態から、元の車線へ戻る動きが
急すぎる」。求められているのは**元の車線へ戻ること**であって、隣車線に居着くことではない。
合流目標を現在車線にすると隣車線に居着いて戻らず、要求と正面から矛盾する。

`object_->pos_.GetLaneId()` は**物理占有車線を追う**（§6-2 で実測確定）ので、目標車線の
取得には使えない。ルート側から引く必要がある。

**PM が付けた 3 条件（厳守）**:

1. 既定 OFF のとき従来挙動を**完全に温存**する
2. 回帰ゲートの **deviation=0** を壊さない
3. 現行コードのコメントが `routed lane CENTER` と書いてありながら実装が**現在車線**を
   使っている食い違いを、**実装と同時に解消しコメントも直す**

> 条件 3 の理由: コメントと実装の食い違いを放置するのは、本日何度も踏んだ
> 「静かに間違う」経路そのもの。今回の §6-2 の疑義も、まさにこのコメントを信じたことが
> 発端だった。

### 2-0-1. ルート車線の引き方（調査で確定）

**ルートは実在する**: `resources/xosc/virtual_driver_basic.xosc:41-56` の `AssignRouteAction` は
両 Waypoint とも `roadId="0" laneId="-3"`。つまり合流目標は **road 0 / lane -3**。
`virtual_driver_anticipation.xosc` も同形。

**しかし VD は現在まったくルート非対応**: `ControllerVirtualDriver.cpp` は `GetRoute()` /
`SetRoute()` / `route_->` を一度も呼んでいない（全文 grep で確認）。

**罠: `route->currentPos_` は凍結している。** これを更新する `CalcRoutePosition()` は
`SetRoute()`（割り当て時）・`TeleportTo()`・`MoveAlongS(..., updateRoute=true)` からしか呼ばれない。
VD は `HVDStateApplier::Apply()` → `SetInertiaPos()` → `XYZ2TrackPos`（物理車線経路）で
`pos_` を書くので**`route_` に一切触れない**。したがって `route->GetLaneId()` を素で読むと
`AssignRouteAction` 実行時（s=20）の値のまま。**読む前に必ず自分で同期させる必要がある。**

**採る手順**（プランナは既に `pos.CopyRoute(obj->pos_)` で**隔離クローン**を持っているので、
共有 `Route*` を汚さない。`JunctionTurn.hpp:61-103` と同じ安全な流儀）:

```
1. route = pos.GetRoute();  if (!route || !route->IsValid())        -> フォールバック
2. route->SetTrackS(pos.GetTrackId(), pos.GetS())                    // クローンを同期
3. if (!route->OnRoute())                                            -> フォールバック
4. if (route->GetTrackId() != 期待値)                                 -> フォールバック
5. target_track = route->GetTrackId();  target_lane = route->GetLaneId();
6. SetLanePos(target_track, target_lane, s, d(t_i))                   // core 自身と同じ流儀
```

手順 6 は core 自身の実装と同一パターン（`Route::SetTrackS` の
`RoadManager.cpp:15514`: `currentPos_.SetLanePos(GetWaypoint(..)->GetTrackId(), GetWaypoint(..)->GetLaneId(), local_s, 0.0);`）。

**フォールバックは常に「現在車線」＝今日の挙動**。

**手順 4 が必要な理由（静かに壊れる経路）**: ルート車線がその s に存在しない場合
（車線終端・レーンセクション変更）、`SetLanePos` は `ERROR_GENERIC` を返すが、
それは **`lane_id_` を更新する前**（`RoadManager.cpp:11100-11106`）。一方 `track_id_`/`s_` は
先行する `SetLongitudinalTrackPos` で**既に更新済み**。そして `Route::SetTrackS` は
この戻り値を**握り潰す**（`:15514` は戻り値未検査）。結果、**新しい track/s に古い lane_id が
貼り付いた非整合状態**が戻り値なしで生まれる。`OnRoute()` だけでは検出できないので
track_id の突き合わせが要る。

同様に、ルート道路から物理的に外れた場合、`SetTrackS` は waypoint 一致に失敗して
更新分岐ごとスキップし、**`currentPos_` を前回値のまま残す**（`:15419-15421`, `:15508-15546`）。
`GetLaneId()` はエラーではなく**最後に同期できた値**を静かに返す。だから手順 3・4 は必須。

### 2-0-2. 条件 3 の解釈 — 「コメントに合わせて実装を直す」は条件 2 と衝突する

現行の食い違いの直し方は 2 通りある:

* **(i) 実装をコメントに合わせる** = 常時ルート車線へアンカーする
  → **常時経路の挙動が変わる**。ルート車線に居ない全シナリオに影響し、
  **回帰ゲートの deviation=0 を壊すおそれがある（条件 2 違反）**
* **(ii) コメントを実装（＝現在車線）の真実に直し、ルート車線は合流機能が
  有効なときだけ使う**
  → 既定 OFF で挙動不変（条件 1）・deviation=0 保持（条件 2）・コメントは嘘でなくなる（条件 3）

**(ii) を採る。** 併せて、同じ誤った記述を引用の形で複製している
`AdSteeringEnvelope.hpp:5-13`（"anchors the preview to the routed lane CENTER"）も
**同時に直す**。片方だけ直すとヘッダ間で新たな食い違いを作る。

### 2-0-3. 下流整形が効かないことの実測（第三者所見を検証・訂正して採用）

第三者レビューから同趣旨の所見が回ってきたので**原典で裏を取った**。結論の方向は支持されるが、
挙げられていた根拠 2 点は事実誤認だったので訂正して記載する。

| 第三者の記述 | 検証結果 |
| :--- | :--- |
| 「操舵レートは全条件で上限 2.4590/s に飽和」 | **誤り。** 20 セル中 4 セルは非飽和: v=12/jerk10 が **1.6000**、v=14/jerk10 が **1.4000**、v=14/jerk25 が **2.0000** |
| 「a_lat ピークが 2.5795〜2.5798 で同一」 | **範囲の取り違え。** それは v≥8 の部分集合。実際は速度依存で v=2 が 0.5517、v=4 が 2.2152 |

**正しく述べ直すと、第三者の結論より強い事実になる**（`f7_lane_offset_resume.txt` 全 20 セル）:

| 速度 | a_lat ピーク（jerk_max = 0 / 10 / 25 / 50） | ヨーレートピーク |
| ---: | :--- | ---: |
| 2 | 0.5517 / 0.5517 / 0.5517 / 0.5517 | 0.2758（4 値とも同一） |
| 4 | 2.2152 / 2.2151 / 2.2152 / 2.2153 | 0.5527（同上） |
| 8 | 2.5795 ×4 | 0.3213（同上） |
| 12 | 2.5798 ×4 | 0.2126（同上） |
| 14 | 2.5798 ×4 | 0.1826（同上） |

**指令側は明確に変わっているのに**（`steer_jerk` が 0.679→10→25→50、高速では `steer_rate` も
2.4590→1.4000 と変わる）、**実現運動が 4 桁 1 つも動いていない。**

#### 「その窓が復帰過渡を測っていないだけでは」を潰した

対抗仮説「ピークが手動マヌーバ区間から拾われているなら、MANUAL 中は AD 指令＝躍度上限が
適用されないので一致して当然」は**原典で否定済み**。ピーク採取窓
（`resume_ride_feel.py:410-414`）は二重に守られている:

```python
def in_window_mask():
    return [(f["sim_time"] - t0) >= -1e-9 and override_lat_mask_src[i] is False
            for i, f in enumerate(ext)]
```

`sim_time >= t0`（復帰後）**かつ** `override_lateral is False`（AUTO 所有フレームのみ）。
コメントに「`auto_transition` フラグと `override.lateral` の実際の反転との off-by-one に対する
防御」と明記され、テレメトリ欠損フレームは保守的に非対象。
**計器不具合 #3（復帰過渡の信号が MANUAL 区間から拾われていた）は修正済みで、しかも防御的。**

したがって上表は**復帰過渡そのものについての結果**である。

#### 意味

躍度上限は指令の最初の数フレームを引き伸ばすだけで、**車両の運動ピークを動かしていない**。
これが「角を丸めるだけでは根本的に滑らかにならない」の実測的な内容。

**本設計はここが違う**: `a_bound` を設計値として与え、**ピークそのものを構成的に決める**。

> **予想される改善と、その正直な限界**（未測定・実測で確認する）:
> v=8 の baseline ピーク 2.5795 に対し `a_comfort=1.5` なら約 42% 減。
> ただし `a_bound = max(a_comfort, |a0_lat|)` なので、**ドライバーが既に大きく曲げながら
> 復帰を押した場合は改善幅が縮む**（それが正しい挙動＝渡された運動より悪くしない）。
> ハーネスは C_release で 0.5s 操舵をゼロにしてから復帰パルスを出すので `a0_lat` は小さく、
> **ヘッドレスの数字は実機の「握ったまま復帰」条件より良く出る可能性がある。**

### 2-1. なぜ下流の整形（レート/躍度上限）ではないのか

打ち手C（操舵躍度上限）は実機 3 走行すべてでマージンが劣化し既定 OFF になった
（`f7_residual_handoff_C.md` §11）。機構は「下流でサーボと綱引きする」こと。上流で参照を
整形すれば指令は包絡線の内側に留まるので、同じ失敗機構を踏まない。
**ただし実機 FFB マージンへの影響は未測定であり、断定はしない。**

### 2-2. 既存機構の再利用

storyboard の LaneChange / LaneOffset は既に**同じ形**の処理を持っている:

```cpp
// TrajectoryShortPlanner.cpp:166-181
double future_p   = la.time_based ? (la.current_p + acc_dist / abs_nominal)
                                  : (la.current_p + acc_dist);
future_p          = std::min(future_p, la.P);
double future_off = EvaluateTransitionShape(la.shape, la.startVal, la.A, future_p / la.P);
double delta_t    = (future_off - la.current_off) * la.lane_sign;
px += delta_t * tx;  py += delta_t * ty;
```

合流軌道は**この機構を流用**する（内部生成の LaneOffset 相当）。新しい幾何を書かない
＝出荷済みで検証済みの経路に乗る。**ただし `lane_sign` は流用しない — §2-4 の罠を参照。**

### 2-3. 所要時間の決め方

`EvaluateTransitionShape` の SINUSOIDAL は
`start + delta · (1 - cos(π p)) / 2`（`TransitionDynamics.hpp:21`、**ヘッダオンリー**。
`src/control/common/TransitionDynamics.cpp` は存在しない）。
`start = d0`, `delta = -d0`, `p = t/T` を入れると `d(t) = d0/2 · (1 + cos(π t / T))` となり、
横加速度は `|d''|max = d0 π² / (2 T²)`。
快適横加速度上限 `a_comfort` を与えると

```
T = π · sqrt( d0 / (2 · a_comfort) )
```

d0 = 3.3m, a_comfort = 1.5 m/s² → **T ≈ 3.3s**（ユーザーの言う「数秒かけて戻る」と一致）。
T は `[T_min, T_max]` にクランプする。既定値は保守的に置き、実測で詰める。

> 注: この式は横方向の 2 階微分のみで、経路曲率由来の a_lat は別に載る。安全包絡線が
> 最終防波堤として残るので上限超過は起きないが、**設計値としての a_comfort が実現されるか
> は実測で確認する**（§5 の設計値テスト）。

### 2-4. 符号規約 — 誤ると合流が逆向きになる（一次証拠で確定済み）

**罠**: 既存オーバレイの `* la.lane_sign`（`TrajectoryShortPlanner.cpp:177`）を素朴に流用すると、
片側の車線で合流が**逆向き**になる。

一次証拠:

1. `SetLanePos` の `offset` 引数は**生の道路 +t 軸**の量であり、車線側で反転しない。
   `Lane2Track`（`RoadManager.cpp:10177`）が
   `t_ = offset_ + road->GetLaneOffset(s_) + lane_section->GetCenterOffset(s_, lane_id_) * (lane_id_ < 0 ? -1 : 1);`
   と、`offset_` を**符号反転なしで直接** `t_` に加算している（`SIGN(lane_id)` が掛かるのは
   車線幅由来の `GetCenterOffset` の方だけ）。
2. `Position::GetOffset()`（`RoadManager.cpp:12808-12811`）は `return offset_;` の素の読み出しで、
   `SetLanePos` の `offset` 引数の**厳密な逆**。往復で変換は起きない。
3. 既存オーバレイの `lane_sign` が必要なのは、**OpenSCENARIO アクション側が値を
   「車線符号に非依存な空間」に格納しているから**。`OSCPrivateAction.cpp:918` に
   `// Make offsets agnostic to lane sign` と明記され、`:919-920` / `:1078,1096` で
   `SIGN(lane_id) * offset` として格納、`:1122-1133` で同じ `SIGN(lane_id)` を掛けて戻している。
   つまり `lane_sign` は**OSC 格納形式を生 +t 系へ戻すための補正**であって、
   「t 軸 vs 車線側」の一般的な補正ではない。

**結論**: `GetOffset()` 由来の d は**既に生 +t 系**なので、**`lane_sign` を掛けてはならない。**
`(tx, ty) = (-sin(h_road), cos(h_road))`（`TrajectoryShortPlanner.cpp:169-170`、`GetHRoad()` は
生の `h_road_`）も同じ生 +t 系なので、`d * (tx, ty)` がそのまま正しい。

§5-2 の**符号テスト**（左右両方向のずれで合流がずれと逆向きに働く）は、この罠を機械で
固定するために置く。

### 2-5. 参照は preview の点ごとに置く（定数オフセットでは合流にならない）

`MoveAlongS(ds, 0.0, ...)` は**設定済みのオフセットを歩行中も保持する**
（`RoadManager.cpp:10973`: `SetLanePos(track_id_, lane_id_, s_ + (done ? ds_road : 0.0), offset_ + signed_dLaneOffset);`
で `dLaneOffset=0` ゆえ `offset_` がそのまま持ち越される。境界/仮想交差点の退避路 `:10900` `:10950` も同様。
0 に強制リセットされるのは幅ゼロ車線の脱出路 `:11014` のみ）。

したがってアンカーで 1 回だけオフセットを立てると、**preview 全体が一定オフセットの平行経路**に
なってしまい、`:130-138` のコメントが警告する「parallel and off-center forever」そのものになる。
合流にするには preview の各点 i に将来値 `d(t_i)` を置く必要がある。実装は既存オーバレイと同じ
`px += d_i * tx; py += d_i * ty`（ただし `lane_sign` なし・**絶対値**）の形を採る。
既存オーバレイが**相対差分** `(future_off - current_off)` なのは、そちらが車両アンカー基準
（`SetLanePos(...,0.0)` をスキップする側）だから。合流は `lat_actions.empty()` 側＝中心線
アンカー基準なので**絶対値**が正しい。

### 2-6. control_point_offset との関係（干渉しない）

前方制御点の `MoveAlongS`（`TrajectoryShortPlanner.cpp:120-127`）は車線中心スナップ
（`:139-140`）より**前**に走るので、合流オフセットは s を前輪まで進めた**後**の同じルート点に
乗る。コントローラ側の補正（`ControllerVirtualDriver.cpp:361-363`）は
`dstate.x += cp_used * cos(h)` と**純粋に縦方向**で、横方向の合流オフセットとは直交する。
`:116-118` の「control point と anchor は 1 つのルート点に載せる（hard-won）」不変条件は壊れない。

---

## 3. 制約の守り方

| 制約 | 守り方 |
| :--- | :--- |
| **既定 OFF・無効時は従来と完全一致** | `enabled=false` のとき合流状態は非アームで `SetLanePos(...,0.0)` の既存行がそのまま実行される。演算経路が 1 つも変わらない＝ビット一致。ユニットで恒等を固定する |
| **回帰ゲート deviation=0** | 上記より原理保証。加えて回帰シナリオは override/resume を踏まないので ON でも発火しない |
| **安全包絡線の上限を超えない** | 整形は包絡線の**上流**のみ。`ComputeAdSteeringEnvelope`（`ControllerVirtualDriver.cpp:379-381`）と最終曲率クランプの順序は一切変更しない。`3125c6e2` で直した「整形が安全上限の外側で働く」欠陥を作り直さない |
| **R1 Clean Core** | 編集は `GT_esmini/` 配下のみ。`EnvironmentSimulator/` は読むだけ |

---

## 4. フレーム順序（確認済み）

`ControllerVirtualDriver::Step` の実行順（`ControllerVirtualDriver.cpp`）:

| 行 | 処理 | 合流にとっての意味 |
| ---: | :--- | :--- |
| 263 | `override_mgr_.Update(frame, timeStep)` | **`Plan()` より前**。復帰エッジと同じフレームでアーム可能、1 フレーム遅れは出ない |
| 310 | `physics_backend_->GetPose(...)` | 前フレームの物理積分後の姿勢 |
| 338 | `short_planner_->Plan(sctx)` | ここに合流参照を入れる |
| 366 | `driver_model_->Compute(...)` | Pure Pursuit |
| 379-381 | `ComputeAdSteeringEnvelope(...)` | 安全包絡線（**触らない**） |
| 405 | `UpdateAdSteeringEnvelopeState(...)` | 次フレームのレート/躍度アンカー |
| 420 | `physics_backend_->StepPedalSteer(cmd, ...)` | 物理積分 |
| 456 | `state_applier_.Apply(object_, ...)` | **物理が `object_->pos_` を所有**（毎フレーム書き戻し） |

### 4-1. d0（捕捉する初期横ずれ）の出所

L456 で物理姿勢が `object_->pos_` に書き戻されるため、手動走行中も `object_->pos_` は真の自車
位置である。プランナは `pos.Duplicate(obj->pos_)`（`TrajectoryShortPlanner.cpp:107`）で同じ
ものを見ており、テレメトリ `ego.offset`（`ControllerVirtualDriver.cpp:491` = `pos_.GetOffset()`）
＝ハーネスが読む量とも**同一の一次量**。したがって d0 はここから取る。

> ドライバ自身は `object_->pos_` ではなく物理バックエンド姿勢で自己位置決めする
> （`ControllerVirtualDriver.cpp:298-311` のコメント）。両者は同じ物理積分結果に由来するため
> 一致するはずだが、**一致を前提にせずユニットで固定する**（不一致は初期段差として現れる）。

---

## 5-0. 合否基準（**onset 実効躍度の絶対値を基準に使わない**）

> 監査指摘（2026-07-27）を検算して採用。

`onset_effective_jerk = -245.902 /s²` は **`2.45902 / 0.01` に厳密一致**する。
2.4590/s は正規化操舵レート上限（ハーネス自己検証 `steer_rate<=2.459`。v=14/jerk10 が
1.4000 と下回るので実在の上限）、0.01 は sim 刻み。つまりこの値は

```
onset_effective_jerk = (レート上限) / dt        ← 刻みに反比例する
```

であって、**車両や人間が感じる物理量ではない**。dt=0.05 なら 49.180 になる
（＝是正前の旧値そのもの）。全 5 速度で厳密同値なのも当然で、速度に依存しない量だから。

**したがって「245.902 を下げた」を改善の証拠に使ってはならない。**
刻みを変えるだけで動く数字であり、ユーザー要求（滑らかで自然なステア操作）とも無関係。

### 使ってよい基準

| # | 基準 | 種別 |
| :-- | :--- | :--- |
| 1 | **1 フレームでレート上限まで飽和する構造が消えること**（エッジでの \|Δrate\| < 上限、かつ速度をまたいで同値にならないこと） | 構造 |
| 2 | **横加速度のピーク**（実現値 = ヨーレート×速度） | 刻み非依存・体感量 |
| 3 | **ルート車線への復帰所要時間**（現状は `dev_converge_s = None` ＝**そもそも戻らない**） | 刻み非依存・体感量 |
| 4 | **横位置 `dev` の時間変化**（形・単調性） | 刻み非依存・体感量 |
| 5 | **オーバーシュートの有無** | 刻み非依存・体感量 |

2〜5 は**打ち手 C（躍度上限）が動かせなかった量**でもある（§2-0-3）。ここを動かせるかが
本設計の実質。

`onset_effective_jerk` は**証拠としては出し続ける**（構造が消えたかの判定に使う）が、
**絶対値の低減を成果として報告しない**。

---

## 5. 検証計画

### 5-1. ヘッドレス（確立済み条件）

`scripts/ffb_spike/resume_ride_feel.py --mode lane-offset` の `run_network_arm_lane_shift`
（車線 1 本分・5 速度）で OFF / ON 対照。旧方式（一定操舵を当て続ける）は車線幅の条件を
作れていなかったため使わない（`f7_lane_offset_resume.txt` の request #1 参照）。

**掃引機構（ハーネス改修はほぼ不要）**: 出荷 config は読むだけで、走行ごとに temp JSON を書き
（`resume_ride_feel.py:648-653`）、xosc の `ConfigFile` プロパティでそれを指す（`:692-696`）。
新キーの掃引は `cfg["..."] = value` を `:783-790` に 1 行足すだけ。ただし C++ 側の
name→member マップに載っていないキーは**黙って無視される**ので、DLL 再ビルドは必要。

**復帰エッジの定義**: ハーネス独自定義ではなく、テレメトリの `auto_transition` が最初に立った
フレーム（`:387-391` と `:1133-1136` で独立に同じ再計算）。C++ 側の manual→auto フラグを信頼する。

測る量と出所:

| 量 | 出所 |
| :--- | :--- |
| onset_rate_step / onset_effective_jerk | 引き継ぎフレームを跨いだ差分（**意図的に ungated**、`:528-536`） |
| PEAK a_lat | `yaw_rate × speed` の**実現値**（実自車ヨー×実速度、`:562`）。指令からの推定ではない |
| steer_rate / steer_jerk (gated) | splice / ±1 飽和 / kappa 飽和の 3 系統でゲート（`:498-509`） |
| 曲率クランプのスナップ | kappa ゲートで除外されたフレームのみの max（`:544-553`） |
| 復帰時間 / オーバーシュート | `vd_resume_transient.compute_metrics()` に委譲 |

**`trajectory_monotonic` は判定に使わない。** 機構が判明した: 収束判定が
「|offset| が目標±0.3m かつ |heading_dev| ≤ 0.03rad を 30 フレーム維持」（`:1010-1017`）で
**静止までは追い込まないため、エッジ時点で残留ヨー運動がある**。判定は許容差 1e-6 の
厳密非増加（`:1705`）なので、最初の 1 サンプルで必ず落ちる。

> **この残留ヨーは設計にも効く**（§2-3 の形状選択、下記）。

### 5-1-1. 形状の見直し — 初期横速度を持つ

エッジ時点で heading_dev が最大 0.03 rad 残る（v=8 で横速度 ≈ 0.24 m/s）。
`EvaluateTransitionShape` の SINUSOIDAL は `d'(0) = 0` に固定されるため、これを使うと
**t=0 で横速度に段差が入る** — まさに我々が消そうとしている種類の不連続。

したがって形状は**5 次多項式**とし、境界条件を
`d(0)=d0, d'(0)=v·sin(ψ0), d''(0)=0, d(T)=0, d'(T)=0, d''(T)=0` で解く（両端 C²）。
`EvaluateTransitionShape` はヘッダの 4 行の一次結合にすぎず（`TransitionDynamics.hpp:11-31`）、
再利用による節約はほぼ無い一方で境界条件の自由度を失うため、ここでは使わない。

T の決定は §2-3 の raised-cosine 解を初期値とし、5 次多項式の `max|d''|` を固定格子で
サンプリングして `a_comfort` 以下になるまで T を伸ばす（決定的・テスト可能）。

### 5-2. ユニット

1. **恒等テスト** — 既定 OFF で、実装前後のプランナ出力がビット一致
   （`test_AdSteeringEnvelope.cpp:737-757` の `ShippedDefaultLeaves...Disabled` の流儀）
2. **既定 OFF 固定** — デフォルト構築した config の `enabled` が false
3. **設計値テスト** — ON のとき、実現 a_lat ピークが指定 `a_comfort` 以下（全速度）
4. **符号テスト** — 左右両方向のずれで、合流が**ずれと逆向き**に働く（符号の取り違え検知）
5. **解除テスト** — storyboard 横アクション発生・MANUAL 再ラッチ・ルート変更で合流が解除される

### 5-3. ゲート

unit → 回帰ゲート（`pwsh` で `run_regression_gate.ps1`、Step 0〜2.7、deviation=0 確認）→
parity 30 本 → 実機。

### 5-4. 計器の扱い（本日 7 件の不具合を踏まえた規律）

- ハーネス自身が出す警告・除外は**全数を報告に転記**する。警告付き条件の数値は**未測定扱い**
- 出所を辿れない数値は**未測定扱い**
- 測定器は測定対象より細かく
- 恒等再生チェックを関門にしてから結果を出す
- 1 ケースのスモークで通したことにせず、格子全体で自己検証する

**テレメトリ精度は確認済み**: `VirtualDriverTelemetryJson.cpp:24,35` が `std::ios::fixed` +
`precision(9)` で 9 桁。ハーネスが必ず印字する `PROVISIONAL`（`resume_ride_feel.py:1855-1859`）は
**凍結済み実機ログ 3 本にのみ係る注記**（旧 4 桁で採取されたため）で、新規クローズドループ走行の
数値には係らない。**新たな計器不具合ではない。**

#### 転記が必要な警告・除外の全カタログ

ハード関門（中断）:
- `FAIL: derivative self-test did NOT recover jerk_cap=...`（`:1785-1787`）
- `FAIL: DLL not found at ... -- run /build first`（`:1793`）
- `FAIL: DLL at ... does NOT emit envelope.steer_jerk_active -- ... stale ...`（`:1800-1801`）
- `_make_variant_speed_fixed` の `AccelAction` xpath 一致数 != 1 で `RuntimeError`（`:685-687`）

警告・除外（非致命・**報告に必ず載せる**）:
- `WARNING ... snap_max=101.1 ... Pass --snap-max 0`（`:1871-1875`、jerk-grid で `--snap-max` 未指定時）
- `WARNING: {n}/{N} cases had no auto_transition edge / missing telemetry`（`:1887-1888`）
- `{name}: LATCHED at frame ... -- excluded {n} frames from onset onward`（`:1829-1831`）
- `max_residual>0.08` だがラッチ無しの near-miss 注記（`:1835-1836`）
- lane-offset モードのセル単位除外（`:1102/1108/1114`）:
  `maneuver did not converge within {s}s -- excluded, not measured` /
  `route departure detected -- excluded, not measured` /
  `no auto_transition edge / missing telemetry -- excluded`

自己検証:
- 格子全体: `verify_grid_self_check`（`:1537-1563`）→ `PASS: no violations in any checked cell.` /
  `FAIL: {n} violation(s)`（`:1897-1903`）
- lane-offset セル単位: `self-check: steer_rate<=2.459 PASS/FAIL  steer_jerk<={jm} PASS/FAIL`（`:1682-1683`）

#### 恒等チェックの欠損（規律 9 に対する既知のギャップ）

`_selftest_derivative_chain()`（`:234-255`）は既知入力→既知出力の**本物の関門**で、微分連鎖の
算術を 1e-6 で検証しハード関門になっている（`:1781-1790`）。`_preflight_check_new_telemetry()`
（`:1566-1595`）は DLL 鮮度の関門。

**しかしクローズドループ・シミュレーション自体の決定性/再現チェックは走っていない。**
その機構は `vd_resume_transient._diff_frames_against_backup()`（`vd_resume_transient.py:677-692`）
として存在するが、`resume_ride_feel.py` は `vrt.main()` を呼ばないため**一度も発火しない**。

→ **本作業で埋める**: OFF/ON 対照の前に「同一 config を 2 回走らせてフレームが一致すること」を
関門として足す。これが通らないうちは OFF/ON の差を乗り味の差として読まない。

---

## 6. 編集点（承認後に着手）

| # | 層 | ファイル | 備考 |
| :-- | :--- | :--- | :--- |
| 1 | C++ 既定定数 | 新規ヘッダ（`kAdEnvelopeDefault*` の命名流儀に倣う） | 出荷値と候補値を名前で分離 |
| 2 | C++ struct | `VirtualDriverConfig.hpp` | bool `=false` ＋数値ノブ |
| 3 | C++ parse | `VirtualDriverConfig.cpp` の `kBoolFields[]` / `kDoubleFields[]` ＋アクセサ | 各 1 行 |
| 4 | ロジック | `TrajectoryShortPlanner.cpp:139-144` / `:166-181`、`ShortPlanContext`（`IShortPlanner.hpp:14-30`） | 本体 |
| 5 | アーム | `ControllerVirtualDriver.cpp:263`（エッジ）→ `sctx` へ状態を渡す | |
| 6 | テレメトリ | `VirtualDriverTelemetryJson.cpp`（合流進捗・捕捉 d0・目標 T） | `ShortPlannerSnapshot` は cross-session contract なので**そちらには足さない** |
| 7 | 出荷 json | `config/virtual_driver.json` | C++ 既定と手で数値同期 |
| 8 | backend | `web/backend/api/virtual_driver_api.py` の `_BOOL_KEYS` / `_NUMBER_KEYS` / `DEFAULT_VIRTUAL_DRIVER_CONFIG` | **`KNOWN_KEYS` が唯一の関門。登録漏れは PUT が 422 で黙って落ちる** |
| 9 | 型契約 | `web/frontend/src/api/client.ts` | **FROZEN CONTRACT（L1-22）。editor 側 `packages/esmini` との同期が必要＝PM 判断事項** |
| 10 | GUI | `web/frontend/src/components/simulation/VirtualDriverPanel.tsx` の `EDITABLE_KEYS` ＋新セクション | `LiveVdPanel` / `FfbMarginPanel` は表示専用で対象外 |
| 11 | docs | `docs/virtualdriver/guides/ffb_override_tuning.md` に節追加 ＋ 本ファイル | |

---

## 6-2. 計器の不具合 #8 — 「車線 1 本分」条件は条件を作れていなかった（**決着済み・確定**）

> **2026-07-27 決着走行で確定。** 以下の「疑い」は実測で裏付けられた。
> 出力: `test_results/f7_lane_offset_semantics_probe.txt`
> 計器: `GT_esmini/test/headless/vd_lane_offset_semantics_probe.py`
> DLL mtime 2026-07-27 09:37:44 / マヌーバ収束 True / 出荷 config 無改変
>
> ```
> t=4.030s  B_lane_shift  track 0->0  lane -3 -> -4   offset -1.7482 -> +1.9425   (1 フレームで 3.691m 跳躍)
> AUTO_RESUME edge  t=10.560s  lane=-4  offset=-3.310435118
>   -> BEYOND lane -4's edge by 1.360m  (|offset|=3.310 > half-width 1.950)
> ```
>
> `-1.7482` は lane -3 の半幅 1.75、`+1.9425` は lane -4 の半幅 1.95。
>
> **確定事項**:
> 1. `ego.offset` は**車線相対**で、境界で不連続に張り替わる
> 2. 復帰エッジで自車は**走行車線の外**（lane -4 の外端を 1.360m 越え＝`type="stop"` の lane -5 の中）。
>    ルート車線 -3 の中心から **約 7.0m ＝ 車線約 2 本分**
> 3. 既存の逸脱検査は**原理的に検出できない**。実際の出力は
>    `departed: False, lane_values_seen: [-4, -3]`（lane が最寄り走行車線 -4 で clamp され、
>    -4 は許容帯の中）
>
> **無効になったもの**: 引き継ぎ文書の条件 1「達成」の判定根拠／打ち手 C の乗り味評価の地盤／
> 本設計の当初前提 `d0 ≈ 3.3m`。
>
> **正しい「隣車線」条件**: ルート車線中心から **3.70m**（-3↔-4 の中心間 = 3.5/2 + 3.9/2）。
> `ego.offset` では表現できない。

### 6-2-1. 正しい制御量＝ルート相対横偏差（再構成できる）

車線相対 `offset` と車線幅表から、**境界で連続な**ルート相対横偏差が復元できる:

```
dev = signed_center_to_center(route_lane → current_lane) + offset
```

e6mini road 0 なら `dev(lane=-3) = offset`、`dev(lane=-4) = -3.70 + offset`。

**実測で検算**: 境界直前 `dev = -1.7482`、境界直後 `dev = -3.70 + 1.9425 = -1.7575`。
差 0.009m（1 フレーム分の実移動）で**連続**。再構成は妥当。

以後、条件の指定・収束判定・報告はすべて `dev` で行う。`ego.offset` を目標に使わない。

---

## 6-3. （旧）疑いの記録

### 確定した事実（凍結実機テレメトリ 6 本・実行不要のログ解析）

`lane_id` は**物理占有車線を追い**、`offset` は新しい車線基準へ**張り替えられて境界で不連続に飛ぶ**:

```
f7_realwheel_basic.jsonl  t=7.56  track 0->0  lane -3 -> -2   offset +1.749 -> -1.807
```

`+1.749` は lane -3 の半幅 1.75 ちょうど、`-1.807` は lane -2 の半幅 1.825 の内側。
全 6 走行で **max|ego.offset| = 1.81m**（半車線幅で頭打ち）、車線変化の無いフレームの
1 フレーム最大変化は **0.019m**。つまり車線内では滑らかで、**跳ぶのは車線境界だけ**。

コード側の裏付け: `HVDStateApplier.cpp:29-31` → `SetInertiaPos`（`updateTrackPos` 既定 true）
→ `XYZ2TrackPos` → `SetTrackPosMode` → `Track2Lane`（`RoadManager.cpp:10329`）→
`lane_id_ = lane_section->GetLaneIdByIdx(lane_idx_)`（`:9229`）。`lockOnLane_` は false のまま。

### そこから導かれる疑い

ハーネスは復帰エッジで **`offset_at_edge = -3.230m`** を報告している。しかし `ego.offset` は
走行車線上では半車線幅で頭打ちになるので、**-3.230 は「隣の走行車線にいる」状態では出ない**。

e6mini road 0 の車線構成（実 xodr 実測）: lane -2 driving 3.65 / **lane -3 driving 3.50（ルート）**
/ **lane -4 driving 3.90** / lane -5 **type="stop"** 2.85。offset 負方向＝外側（-4, -5 側）。

`-3.230` が成立するのは、自車が **lane -4 の外端をさらに約 1.28m 越えて "stop" 車線に出て**、
最寄り走行車線が -4 に張り付いた（clamp された）状態のみ。

**推定される機構**: ハーネスの P 制御は目標を `|ego.offset| = 3.5` で指定している。この量は
**車線境界でリセットする座標**なので、境界を越えるたびに |offset| が小さく戻り、制御が
「まだ足りない」と外へ押し続ける → **最後の走行車線を越えるまで止まらない**。
旧方式で最終 |offset| が 9.5–14m になった件も同じ機構で説明がつく。

### 影響範囲

- 引き継ぎ文書の**条件 1「達成」**の判定根拠
- §4-2 の乗り味の数値（打ち手 C の評価地盤）
- 本設計の `d0 ≈ 3.3m` という前提

**正しい条件指定は、車線境界でリセットしない量**（ルート車線中心からの横偏差＝`t` 系の量、
または車線幅を積算した量）でなければならない。

### まだ確定していないこと（誠実に）

`resume_ride_feel.py` は **lane-offset モードのフレームを保存しない**（実機 jsonl を読むだけ・
`:325` 以外に永続化なし）ので、**lane-shift 走行中の `ego.lane` を直接観測できていない**。
決着には 1 セル走らせて `ego.lane` を `ego.offset` と並べて出すだけでよい（DLL 必要＝
ビルド排他の解除待ち）。**それまでこの節は「強い疑い」であって確定ではない。**

---

## 7. 未解決（承認前に潰す／PM 判断）

1. ~~`pos.GetLaneId()` は現在車線か~~ → **確定**（§6-2）。物理占有車線を追う。
   合流目標は**ルート車線**とする方針で PM 承認済み（§2-0）。
   **残る設計課題**: ルート車線をどう引くか（`Route` 側の API、ルートが無い場合の
   フォールバック、ルート車線がこの s に存在しない場合）。調査中。
2. ~~オフセットの符号規約~~ → **解決**（§2-4）。`lane_sign` を流用しないことが要点。
3. **`client.ts` の凍結契約**（編集点 9）。GUI 露出はユーザー要求に明記されているが、
   editor 側リポジトリはこのツリー外で同期できない。PM 判断。
4. **実機 FFB マージンへの影響は未測定**。ヘッドレスで良化しても実機で確認するまで断定しない。
5. **単発走行の実機マージンは判断材料にならない**（同一条件で 2.12x→1.41x と振れた実績）。
   実機で語るなら反復必須。

---

## 8. 実装設計（詳細）

### 8-1. モジュール構成

**新規の純ロジックモジュール** `ResumeMergeProfile`（`AdSteeringEnvelope` / `FfbTargetServo` と
同じ流儀: esmini 依存なし・単体でユニットテスト可能）。

責務は「(d0, ḋ0, T) から任意時刻の横オフセット目標 d(t) を返す」ことだけ。
道路も車線もルートも知らない。ルート車線の解決と preview への配置はプランナ側の責務。

この分離により、**軌道の数学はエンジン無しで全数テストできる**。

### 8-1-1. 「自然なステア操作」の解釈（ユーザー要求 3 回目・自分の判断）

ユーザーは「滑らかに」だけでなく「**自然なステア操作**になるように」と言っている。
これを次のように読む:

**ステア角の履歴が、ドライバーが握っていた操作から連続していること。**

ステア角 δ は経路曲率 κ にほぼ比例し（`δ ≈ atan(L·κ)`）、曲率は横位置の 2 階微分。
つまり **`d''` が引き継ぎ時点の実値と一致していなければ、ステア角に段差が入る**。

補強: 両端の微分をゼロに固定した 5 次多項式は、人間の運動を記述する
**最小躍度軌道（minimum-jerk / Flash-Hogan）そのもの**であり、「人間が運転しているように
見える動き」を求める要求に対して恣意的でない選択である。

**PM の読み（上限をかけて角を丸めるだけでは応えられない）は妥当**と判断する。
実際、打ち手 C（躍度上限）は実機 3 走行すべてでマージンが劣化した。
ただし PM の読みは**不完全**で、次項の欠陥を含んでいた。

### 8-2. 軌道の閉形式（初期曲率を一致させる）

#### 直した欠陥 — `d''(0) = 0` はステア角に段差を作る

当初案は境界条件を `d''(0) = 0` としていた。これは**引き継ぎ時点の曲率をゼロと決めつける**
ことであり、ハンドルが切られた状態で復帰すると**指令ステア角に段差**が入る。
数値で確認した段差（v=8, wheel_base=2.7）:

| 引き継ぎ時の a_lat | 実際のステア角 | 当初案の開始角 | **段差** |
| ---: | ---: | ---: | ---: |
| 1.00 | +2.416° | 0.000° | **-2.416°** |
| 2.58 | +6.212° | 0.000° | **-6.212°** |

**まさに「不自然なステア操作」を自分で作る設計だった。** これは PM から渡された確認事項
（カーブ途中開始＝開始時点でステアが中立から離れている条件）と**同一の性質**の条件である。

#### 正しい境界条件（6 条件・5 次のまま）

```
d(0) = d0,  d'(0) = v0_lat,  d''(0) = a0_lat      ← 実測の初期曲率を一致させる
d(T) = 0,   d'(T) = 0,       d''(T) = 0
```

`u = t/T`、`A = d0`、`B = v0_lat·T`、`C = a0_lat·T²` と置くと

```
d(u) = A + B·u + (C/2)·u²
     + (-10A - 6B - 1.5C)·u³
     + ( 15A + 8B + 1.5C)·u⁴
     + ( -6A - 3B - 0.5C)·u⁵
```

**数値検算済み**: 全境界条件の残差 1e-14 台（機械イプシロン）。
初期ステア角の不一致は **0.0000°**（構成上ゼロ）。

境界条件は**数値検算済み**（残差 d(1), d'(1), d''(1) すべて 1e-14 台＝機械イプシロン）。

横加速度は `d''(t) = d''(u)/T²`。

#### T の決定 — 快適上限は「悪化させない」で定義する

参考値（`v0_lat = a0_lat = 0` のときのみ成立する閉形式）: u 領域の `max|d''|` は数値で
**5.773502692** ＝ `10/√3`（相対誤差 3.2e-13）なので `T = sqrt((10/√3)·|d0| / a_comfort)`。
d0=3.3, a=1.5 → 3.564s。**ただし実装はこの閉形式を使わない**（下記の理由で一般には成り立たない）。

**構造的事実（検算で確定）**: `d''(0) = a0_lat` を固定する以上、**`max|d''| ≥ |a0_lat|` が常に成立**する。
したがって **`|a0_lat| > a_comfort` のとき `max|d''| ≤ a_comfort` は原理的に達成不能**。
どんな T を選んでも満たせない（T を 1.5〜6.0 で全走査して最小値が厳密に `|a0_lat|` に一致することを確認）。

**採用する規則**:

```
a_bound = max(a_comfort, |a0_lat|)
```

意味は「**ドライバーが渡してきた横加速度より悪くしない。渡された値が快適域内なら快適域に収める。**」
快適上限は**合流が加える運動**に課すものであって、引き継いだ初期条件に遡って課すものではない。
この定義なら常に実行可能で、かつ「自分の上限を破る」`3125c6e2` 型の欠陥にならない。

**手続き（決定的・グリッド走査）**:

```
T = min{ T ∈ [T_min, T_min+ΔT, ..., T_max] : max_u |d''(u; d0, v0_lat·T, a0_lat·T²)| / T² ≤ a_bound }
   該当が無ければ T = T_max とし、テレメトリに未達フラグを立てる
```

反復の収束性に依存せず格子上で決まるのでユニットで厳密に固定できる。

**検算結果**（d0=3.3, v0_lat=0.24, a_comfort=1.5, 格子 1.5〜6.0 / 0.05 刻み）:

| `a0_lat` | `a_bound` | T | 実現 a_lat | ピーク|d| |
| ---: | ---: | ---: | ---: | ---: |
| 0.00 | 1.50 | 3.90s | 1.492 | 3.360m |
| +1.00 | 1.50 | 4.35s | 1.482 | 3.486m |
| +2.58 | 2.58 | 3.50s | 2.580 | 3.570m |
| +4.00 | 4.00 | 2.75s | 4.000 | 3.528m |
| -1.00 | 1.50 | 3.75s | 1.499 | 3.326m |
| -2.58 | 2.58 | 3.15s | 2.580 | 3.311m |

全条件で実行可能、膨らみは最大 +0.29m。`|a0_lat|` が大きいほど T が短くなるのは
**既に曲がっている車ほど早く戻れる**という物理どおりの挙動。

> **`v0_lat` 単独でも当初案は破綻していた**（`d''(0)=0` を保ったまま閉形式 T を使うと、
> `v0_lat=0.24`＝ハーネスがエッジで残す残留ヨーそのもので実現 a_lat が 1.762＝**+17% 超過**、
> `v0_lat=1.0` で 2.596＝+73% 超過）。オーバーシュートは +0.053m と小さく、
> **膨らみだけ見れば「問題なし」と誤読するところだった。**

### 8-3. アーム／解除の状態機械

| 遷移 | 条件 | 動作 |
| :--- | :--- | :--- |
| **アーム** | `enabled` かつ `JustTransitionedToAuto()`（`ControllerVirtualDriver.cpp:263` で `Plan()` 前に更新済み）かつ ルート車線が解決できた かつ `|d0| ≥ d0_min` | **d0・v0_lat・a0_lat** を捕捉し T を決めて固定。以後 T は再計算しない |
| **進行** | 毎フレーム | `u += dt/T`。preview の点 i には `d(u + i·dt_preview/T)` を置く（u>1 は 0 にクランプ） |
| **完了** | `u ≥ 1` | 解除。以後は従来経路 |
| **解除（横アクション）** | storyboard の `lat_actions` が非空になった | 即解除。既存のオーバレイ経路に完全に譲る |
| **解除（手動再ラッチ）** | `IsLateralManual()` が true になった | 即解除。次の復帰で改めてアーム |
| **解除（ルート喪失）** | ルート車線が解決できなくなった（§2-0-1 の手順 1/3/4 のいずれかが不成立） | 即解除＝現在車線アンカーの従来挙動へフォールバック |

`|d0| < d0_min` でアームしないのは、通常走行の微小な横ずれで合流が働かないようにするため
（既定 OFF の原則とは別に、ON にしたときの誤発火を防ぐ）。

#### `a0_lat`（初期横加速度）の取得

**中立でない初期舵角を最初から前提に入れる**（PM 確認事項）。候補は 2 つ:

* **(a) 実現値から**: `yaw_rate × speed`（ハーネスの `a_lat_realized` と同じ定義、
  `resume_ride_feel.py:562`）。実際の車両運動そのもので、舵角・タイヤ・車速の効果を全部含む
* **(b) 指令舵角から**: `κ = tan(δ)/L`、`a_lat = κ·v²`。指令に対応するが実現とはズレる

**(a) を採る。** 合流の目的は**ドライバーが感じている運動から連続させる**ことなので、
指令値ではなく実現値に合わせるのが要求に忠実。`GetPose()` 由来のヨーレートは既に
毎フレーム読んでいる（`ControllerVirtualDriver.cpp:310`）ので追加コストも無い。

> **カーブ途中開始シナリオとの関係**: 検出器側担当が診断中の「開始時点でステアが中立から
> 離れていると即オーバーライド判定になる」不具合と、本設計は**同じ条件**を扱う。
> ただし依存関係は無い — 本設計は `a0_lat` を**実測値として受け取るだけ**で、
> それが何に由来するかを問わない。検出器側の診断結果が出たら、
> **合流のアーム条件が誤検出に引きずられないか**だけ突き合わせる（アームは
> `JustTransitionedToAuto()` に依存するので、誤ラッチ→誤復帰が起きれば合流も誤発火する）。

### 8-4. ルート車線アンカーの副次的利点（設計判断の補強）

現在車線アンカーには、**復帰の途中で目標が飛ぶ**という構造的欠陥がある。
`lane_id` は物理占有車線を追う（§6-2 で実測確定）ので、戻る途中で車線境界を跨いだ瞬間に
アンカー車線が切り替わり、**目標が車線幅ぶん（e6mini road 0 なら中心間 3.70m）ステップする**。
車線境界付近でヨー運動が残っていれば、`GetClosestLaneIdx` が**フレーム間で往復**して
目標が 2 つの車線中心を行き来することもありうる。

ルート車線アンカーは track/lane を固定するので、**この不連続が原理的に起きない**。
合流が滑らかであるためには、参照が滑らかであるだけでなく**参照の帰属先が動かない**ことが要る。

> これは仮説ではなく `lane_id` の実測挙動からの演繹。ただし
> 「現行の急復帰にこの機構がどれだけ寄与しているか」は**未測定**。
> 決着走行は lane の全変化イベントを記録するので、往復が起きていれば
> そこに現れる（そのために追加の走行は要らない）。

### 8-5. パラメータと既定値（すべて保守的・ON は明示操作）

| キー | 型 | 既定 | 意味 |
| :--- | :--- | ---: | :--- |
| `resume_merge_enabled` | bool | **false** | マスターゲート。false で従来と完全一致 |
| `resume_merge_a_lat_comfort` | double | 1.5 | 設計横加速度上限 [m/s²]。安全包絡線の 4.3 とは独立（あちらは最終防波堤） |
| `resume_merge_duration_min_s` | double | 1.5 | T の下限 |
| `resume_merge_duration_max_s` | double | 6.0 | T の上限。これを超える横ずれは 6 秒で戻れる範囲まで詰める |
| `resume_merge_min_offset_m` | double | 0.5 | この未満はアームしない |

C++ 側の定数は `kResumeMergeDefault*` で `ResumeMergeProfile.hpp` に集約し、
`VirtualDriverConfig` のフィールド既定初期化子がそれを読む
（`AdSteeringEnvelope.hpp:62-67` が確立した「C++ 側が単一の真実源」の流儀）。
`config/virtual_driver.json` と `virtual_driver_api.py` の
`DEFAULT_VIRTUAL_DRIVER_CONFIG` は**別言語なので手で数値同期**する。

### 8-6. テレメトリ（新規フィールド・加算のみ）

`ShortPlannerSnapshot` は cross-session contract（`VirtualDriverTypes.hpp:8-13`）なので**触らない**。
コントローラ側テレメトリに `resume_merge` ブロックを新設する:

| フィールド | 意味 |
| :--- | :--- |
| `active` | 合流進行中か |
| `d0` | 捕捉した初期横ずれ [m] |
| `v0_lat` | 捕捉した初期横速度 [m/s] |
| `a0_lat` | 捕捉した初期横加速度 [m/s²]（＝初期曲率＝初期ステア角の代理） |
| `a_bound` | 実際に課した上限 `max(a_comfort, |a0_lat|)` |
| `comfort_unmet` | T_max でも `a_bound` を満たせなかった |
| `duration_s` | 決定した T |
| `progress` | u ∈ [0,1] |
| `target_offset` | このフレームの d(u) [m] |
| `route_track` / `route_lane` | 解決したルート車線（フォールバック時は現在車線と一致） |
| `fallback_reason` | 空文字＝正常。非空＝どの手順で落ちたか |

`fallback_reason` を出すのは、**静かに現在車線へ落ちる**のを検知できるようにするため
（§2-0-1 の罠 2 件はどちらも戻り値なしで静かに壊れる）。

### 8-7. ユニットテスト一覧

| # | 名前（案） | 固定する事実 |
| :-- | :--- | :--- |
| 1 | `ShippedDefaultLeavesResumeMergeDisabled` | 既定構築の config で `enabled == false` |
| 2 | `DisabledIsBitIdenticalNoOp` | OFF のとき出力が従来経路とビット一致 |
| 3 | `ProfileSatisfiesAllSixBoundaryConditions` | d/d'/d'' が両端で厳密に境界条件を満たす（**`a0_lat ≠ 0` を含む**） |
| 3b | `StartCurvatureMatchesHandoverExactly` | **開始ステア角の不一致が厳密にゼロ**。初期舵角が中立でない全条件で。§8-2 の欠陥（-2.416° / -6.212° の段差）の再発防止 |
| 4 | `LateralAccelNeverExceedsHandoverOrComfort` | 全 d0 **× 全 v0_lat × 全 a0_lat（すべて両符号）** で `max|d''| ≤ max(a_comfort, |a0_lat|)`。**v0_lat=a0_lat=0 だけで通すと当初案の欠陥を素通しする** |
| 4b | `ZeroInitialDerivativesMatchClosedForm` | v0_lat=a0_lat=0 のとき T が `sqrt((10/√3)|d0|/a_comfort)` と一致（閉形式との突き合わせ＝計器の恒等関門） |
| 4c | `ReportsUnmetBoundAtDurationCeiling` | T_max でも満たせない条件で未達フラグが立つ（黙って諦めない） |
| 4d | `ComfortBoundIsInfeasibleBelowHandoverAccel` | `|a0_lat| > a_comfort` のとき `max|d''| ≥ |a0_lat|` を固定（構造的事実の明文化。将来「なぜ快適上限を超えるのか」で誰かが壊しにくる箇所） |
| 5 | `DurationClampedToConfiguredRange` | T が `[T_min, T_max]` を出ない |
| 6 | `MergeDirectionOpposesOffsetSign` | **左右両方向**で合流がずれと逆向き（§2-4 の符号の罠） |
| 7 | `InitialLateralVelocityAwayFromTargetStillConverges` | v0_lat が離れる向きでも収束し、膨らみが上限内 |
| 8 | `DisarmsOnStoryboardLateralAction` | `lat_actions` 非空で即解除 |
| 9 | `DisarmsOnManualRelatch` | 手動再ラッチで即解除 |
| 10 | `FallsBackToCurrentLaneWhenRouteUnavailable` | ルート無し／`OnRoute()` false／track 不一致の 3 経路すべてでフォールバックし `fallback_reason` が立つ |
| 11 | `DoesNotArmBelowMinOffset` | `|d0| < d0_min` でアームしない |

3・4・5・6・7 は `ResumeMergeProfile` 単体（エンジン不要）。8・9・10・11 は状態機械。
