# VD 目標レーン帯 (RouteLanePlan) — レーン単位の経路解決と、その失敗を黙らせない設計

> ステータス: **実装済み**（`RouteLanePlan.hpp/.cpp` + `ControllerVirtualDriver` への配線）。
> 知識グラフ: `vd-func:FUNC-050`（レーンレベル経路計画）/ `vd-func:FUNC-049`（目的地ルーティング）の
> 部分実現、`req-vd-ad:REQ-AD-016` の前段。
> **自発的な車線変更（`vd-func:FUNC-055`）は本設計のスコープ外** — 本層は「どの車線に居るべきか」を
> 算出して外へ出すところまでを担い、寄せる動作は行わない。

## 1. 出発点と、そこで前提が覆ったこと

着手時の前提は「junction を跨ぐルートで connecting road の Waypoint を省くと
`Route::AddWaypoint Skip waypoint ... path not found` でルートが truncate される。だから VD 側で
road を探索して中間 Waypoint を補完する層が要る」だった。issue #31 の症状（交差点で直進/左折 Traj が
高速交互 → 不要減速）の説明としてそう理解されていた。

**この前提は実測で覆った。** `decelerate_for_right_turn.xosc` から road13（connecting road）の
Waypoint を削除した粗い版を走らせると、esmini は road13 を**自分で中間 Waypoint として挿入する**。

```
Added route waypoint 0, 0.00: road_id 3 lane_id -1 s 11.00 (scenario)
Added route waypoint 1, 108.17: road_id 13 lane_id -1 s 4.91 (intermediate)   ← 自動挿入
Added route waypoint 2, 222.32: road_id 2 lane_id 1 s 200.00 (scenario)
```

WARN は1件も出ず、走行軌跡は3点 WP 版と 500 フレーム全てで一致した（最大偏差 0.0000 m）。
`Route::AddWaypoint` は内部で `RoadPath::Calculate`（Dijkstra）を回し、成功時に経路上の中間 road を
`(intermediate)` waypoint として挿入する（`RoadManager.cpp` `Route::AddWaypoint` の
`nodes.size() > 1` 分岐）。**road レベルの補完は esmini が既に持っている。**

したがって「VD 側で road を探索して補完する層」は重複であり、作らない。

## 2. では esmini は何をしていないのか — 3つの穴

粗い WP でも通る一方で、次の3点が欠けている。これが本層の存在理由である。

### 穴1: 最終ホップの目標レーンを検証しない

`RoadPath::Calculate` は `nextRoad == targetRoad` が真になった瞬間に `found = true` を確定する。
レーン接続を検証する `CheckRoad()` は **`else` 節でしか呼ばれない**。つまり探索の途中ホップでは
レーン接続を要求するが、**目的地 road へ着地するホップだけは検証を素通りする**。
`targetPos_->GetLaneId()` はこの関数中で一度も参照されない。

結果、**車線変更しないと入れない目標レーンでも「成功」を返す**。過剰許容である。

`highway_example_with_merge_and_split.xodr` の junction 1 / connection 3 で実測した。road 0 の
driving lane は -1/-2/-3/-4 の4本、うち **lane -4 だけ**が connectingRoad 4 経由で road 2（出口ランプ）
へ繋がる。ここで road 4 を明示 Waypoint に置き、ego を lane -1 から出発させると:

- **警告は1件も出ない**（route は「有効」と判定される）
- ego は lane -1 のまま直進し、出口ランプへ向かわない
- `Entity Ego moved away from route at roadId=3` が **t=15.7 s** に初めて出る。
  分岐を通り過ぎてから、事後にしか分からない

同じ道路で Waypoint を粗く（road0/-1 → road2/-1 の2点）すると、こちらは
`Skip waypoint for scenario routes since path not found` + `Route ego_route is not valid` が出て
route が丸ごと捨てられ、ego は route を持たないまま直進する。

つまり**記述の仕方によって、同じ「物理的に不可能な経路」が別の壊れ方をする**。片方は警告あり
（ただし VD は見ていない）、片方は警告なし。

### 穴2: 失敗が戻り値に出ない

`Route::AddWaypoint` は末尾で無条件に `return 0;` する。失敗の痕跡は `invalid_route_` フラグと
`LOG_WARN` だけで、呼び出し側が `Route::IsValid()` を明示的に見ない限り気づけない。
VD は `IsValid()` を resume-merge の経路解決（`ResolveResumeMergeRouteLane`）でしか見ておらず、
そこは横位置補間の armed 判定に使われるだけで、診断としては外に出ていなかった。

### 穴3: 「どの車線に居るべきか」という情報がそもそも存在しない

`RoadPath` も `LaneIndependentRouter` も**1本の経路**を返す。「この road では -1 か -2 のどちらでも
次に繋がる」という**レーン集合**の概念はどちらにも無い。

## 3. 設計判断

### 3-1. 補完のトリガ条件 — 「road の補完」ではなく「lane 到達性の検証」

esmini が road 列を補完した後、その road 列が**レーン単位で連続しているか**を検証する。
検証は Waypoint の隣接ペアごとに `Road::GetConnectingLaneId()` を引くだけで足りる。

- **連続していれば何もしない**（esmini の結果をそのまま使う）
- **切れていれば** `LaneIndependentRouter` で lane レベル経路を**1回だけ**再解決する

`RoadPath` ではなく `LaneIndependentRouter` を使うのは、後者だけが target lane を厳密照合するため
（`GetNextNodes` の `lanePair.second == targetLaneId`）。穴1 の当事者である `RoadPath` に
穴1 の検証をさせることはできない。

再解決は **1回だけ**行い、再帰しない。2回目の不連続はそのまま `"reroute_failed"` として報告する。
リルートの連鎖は `vd-func:FUNC-051/052` の領域であり、本層の責務ではない。

### 3-2. レーン帯をどの層に置いたか — 新層を立て、インターフェースは立てない

`GT_esmini/include/gt_esmini/control/virtualdriver/RouteLanePlan.hpp` に**独立した層**として置き、
`IShortPlanner` / `IMidLongPlanner` / `ITrafficPolicy` のようなインターフェースは**立てない**。

| 候補 | 判断 |
| :--- | :--- |
| `ManeuverAwareSpeedPlanner`（`IMidLongPlanner`）に混ぜる | **不採用**。縦方向プランナーに横方向の関心を混ぜると層の責務が濁る。`v_target(s)` と目標レーン帯は消費者も更新頻度も異なる |
| `policies/` に `ITrafficPolicy` 実装として置く | **不採用**。ポリシーは「制約（`PolicyConstraint`）を出す」契約。レーン帯は制約ではなく計画であり、`MAX_SPEED`/`STOP_AT_S` のどれにも当てはまらない |
| 新インターフェース `IRouteLanePlanner` を立てる | **不採用**。既存のインターフェースはいずれも「差し替え可能な実装が複数ありうる」から抽象化されている。本層は1実装しかなく、抽象化の動機がない |
| **free 関数 + POD の独立層**（採用） | `policies/RouteSignalScan` / `RouteCrosswalkScan` と同じ流儀。状態を持たない純関数で、テストが道路さえあれば書ける |

配置は `virtualdriver/` 直下（`policies/` の下ではない）。policy ではなく plan だからである。

### 3-3. プロファイルの形 — `s → レーン集合` ではなく `road → レーン集合`

`ManeuverAwareSpeedPlanner` の `v_target(s)` は `vector<pair<double,double>>`（`(s_ahead, v_max)`）で、
s は **ego からの相対距離**である。これに揃えるかを検討したが、揃えなかった。

理由は**更新の周期が違う**こと。`v_target(s)` は ego 位置に依存する（毎フレーム 150 回の `MoveAlongS`
で作り直す）。一方レーン帯は route の静的な性質で、**route が変わらない限り不変**である。
毎フレーム相対距離へ焼き直すのは、変わらないものを変わる形で持つことになる。

そこで `RouteLaneBand`（road 単位のセグメント）の列として持つ:

```cpp
struct RouteLaneBand {
    id_t             road_id;
    std::vector<int> lanes;             // road の「出口端」における lane id 集合
    bool             exit_at_road_end;  // 出口が s=length 側か s=0 側か
    double           exit_s;
};
```

`lanes` の意味を**出口端に統一**したのが要点である。road 内に複数 laneSection があると lane id が
途中で振り替わるため、「road のどこでの lane id か」を決めないと集合の比較が成立しない。
ego 側も比較の直前に `Road::GetConnectedLaneIdAtS()` で出口端へ正規化する。

`s` に対する引き当ては `EvaluateRouteLaneStatus()` が毎フレーム行う（現在 road の band を引き、
出口端までの残距離を出す）。**「不変な計画」と「毎フレームの照合」を分けた**、というのがこの層の形である。

### 3-4. キャッシュ — route の同一性が変わったときだけ再構築

`LaneIndependentRouter::FindGoal()` は反復上限を持たない（`RoadPath::Calculate` の 100 回キャップと
違う）。毎フレーム呼ぶ設計にはできない。既存の唯一の利用例
（`ControllerRouteDrive`）も `pathCalculated_` フラグでキャッシュしている。

そこで `(Route* ポインタ, minimal_waypoints_ の (track, lane) 列のハッシュ)` をキーに、
変わったときだけ `BuildRouteLanePlan()` を呼び直す。ハッシュ衝突は最悪1フレーム古い計画を使うだけで、
診断の性質上それは許容できる。

### 3-5. 純関数と、ログを出す場所

`RouteLanePlan.cpp` は **`LOG_WARN` を一切呼ばない**。診断は `diagnostic` / `reason` という
固定語彙の文字列として**戻り値で返す**。

これは既存の VD の作法（`ControllerVirtualDriver.cpp` 本体は `LOG_INFO` のみで、警告は
`VirtualDriverConfig` や各ポリシーの I/O 層に押し出されている）とは逆に見えるが、意図的である。
**「劣化した計画をログに出すかどうか」はコントローラの文脈判断**であり、純関数が決めることではない。
テストからも同じ判定を戻り値として読める。

## 4. 「沈黙させない」の実装

現状の最大の問題は、ルート解決が失敗しても走行中の症状としてしか観測できないことだった
（Claude memory `silent_instrument_and_tolerance_creep` と同型）。3つの出口を設けた。

### 4-1. 警告（走らなくても分かる）

`plan.diagnostic` が非空になったとき `LOG_WARN` を **1回だけ**出す（同じ理由の再警告はしない。
理由が変わったら再度出す）。既存の作法に従い `"VirtualDriver RouteLanePlan: ..."` を前置し、
**何が起きたか＋どう継続するか**を1行に収める。

固定語彙は次の通り。値を増やすときはこの表も更新すること。

| `diagnostic` | 意味 |
| :--- | :--- |
| `""` | 正常 |
| `"no_route"` | object が route を持たない（層を呼ばなかった。呼び出し側が入れる） |
| `"invalid_route"` | `Route::IsValid()` が false（esmini 側で waypoint が skip された） |
| `"empty_waypoints"` | `minimal_waypoints_` が空 |
| `"link_not_found"` | 隣接 road 間のリンクが引けない |
| `"lane_discontinuity"` | レーン単位で不連続（`discontinuity_road_id` に road） |
| `"reroute_failed"` | 再解決も失敗 |
| `"no_opendrive"` | OpenDrive シングルトンが無い |

ログ API は既設の `GT_SetLogCallback` / `GT_GetLastError` 経由で外部（Python ハーネス等）へ届く。

### 4-2. テレメトリ（診断として機能させる）

`VirtualDriverTelemetry::route_lane`（`RouteLanePlanSnapshot`）として毎フレーム出す。
JSON 契約は `VirtualDriverTelemetryJson.cpp` の `ToJson()` が単一ソースで、pull C-API
（`GT_GetVirtualDriverTelemetry`）と UDP ライブ配信の両方が同じ形を運ぶ。

要点は **`target_lanes` と `dist_to_connection` を必ず併記する**こと。「目標レーン帯から外れている」
だけでは行動に繋がらず、「あと何 m で分岐、入るべきは lane -4」で初めて診断になる。

自発 LC が未実装である以上、当面この出力は**診断としてのみ機能する**。それは想定内で、
`vd-func:FUNC-055` を実装したときにそのまま入力になる。

### 4-3. 逸脱記録

road が切り替わった瞬間に、直前フレームが目標レーン外だったら**逸脱として記録**する
（カウント + 直近の road id + `LOG_WARN`）。逸脱は稀なのでラッチしない。

判定は「直前フレームの `target_lanes` が非空」であることを条件に加えている。route を持たない
シナリオ（`AssignRouteAction` を書かないものが大半）では `on_target_lane` が常に false になるため、
これを入れないと通常の road 遷移すべてが逸脱として記録されてしまう。**検知器が母数を取り違える**
典型で、そうなった警報は無いより悪い。

### 4-4. 実測（この層が実際に何を捕まえたか）

`resources/xodr/highway_example_with_merge_and_split.xodr`（junction 1 / connection 3。road 0 の
driving lane -1/-2/-3/-4 のうち **lane -4 だけ**が出口ランプ road 2 へ繋がる）で 2 本走らせた。

**`route_valid_off_target_lane_for_exit_ramp.xosc`（connecting road 4 を明示 WP に置く）**

esmini は WARN を1件も出さない。route は「有効」と判定される。そこへ本層が:

```
t=  0.05  road=0  ego_lane=-1  target=[-4]  on_target=false  dist_to_connection=190.00
t= 15.70  [warn] VirtualDriver RouteLanePlan: left road 0 in lane -1 but the route requires one of -4
t= 15.70  road=3  reason=off_plan_road  deviation_count=1  last_deviation_road_id=0
```

**走行開始直後（分岐の 190 m 手前）の1フレーム目**で「居るべきは lane -4、今は -1、あと 190 m」が
出ている。これが「走らなくても分かる形」の実体である。esmini 側が最初にこの経路の破綻に触れるのは
t=15.7 の `Entity Ego moved away from route`（分岐を通り過ぎた後）で、**約 15 秒の差**がある。

**`merge_required_for_exit_ramp.xosc`（粗い 2 WP）**

```
[warn] Route::AddWaypoint Skip waypoint for scenario routes since path not found
[warn] Warning: Route ego_route is not valid, will be ignored for the default controller.
[warn] VirtualDriver RouteLanePlan: invalid_route — target-lane guidance unavailable, continuing with position-based lane keeping
```

esmini 側の2行は元から出ていた（が VD は見ていなかった）。3行目が本層で、以降
`route_lane.diagnostic == "invalid_route"` としてテレメトリにも残り続ける。

### 4-5. 実装上の罠

**キャッシュキーに `CopyRoute` したクローンのポインタを使ってはいけない。**
`Position::CopyRoute` は毎回 `route_ = new Route` する。VD の既存イディオム
（`Duplicate` + `CopyRoute` で隔離クローンを作る）をそのままキャッシュ判定に流用すると、
アドレスが毎フレーム変わるので**キャッシュが常に miss** し、計画の再構築と警告ラッチのリセットが
毎フレーム走る。実装初版がこれで、`invalid_route` の警告が 560 フレーム全部に出た。

判定は共有 `Route*`（`object_->pos_.GetRoute()`）で行い、クローンは**キャッシュミス時にだけ**作る。
`CopyRoute` は全 waypoint の deep copy なので、コスト面でも毎フレーム作る理由がない。

**`no_route` は警告しない。** route を持たないエンティティ（`AssignRouteAction` を書かない
シナリオ）が大半で、それは劣化ではなく通常状態である。警告に混ぜると、意味のある診断が埋まる。
テレメトリには `diagnostic == "no_route"` として残す。

**`ID_UNDEFINED` を生で出さない。** `discontinuity_road_id` が意味を持つのは lane 不連続の場合だけで、
それ以外では `ID_UNDEFINED`（= 4294967295）が入る。これをそのままメッセージに埋めると
実在の road id に見える。メッセージを2系統に分けている。

**最終 band の `dist_to_connection` は `-1.0`。** 最終 road の `exit_s` は目的地 waypoint の s であって
接続点ではない。距離を出すと「接続点まで N m」と読め、目的地 s を過ぎた後は 0.0 に張り付いて
「今まさに接続点」と読めてしまう。

## 5. スコープ外（意図的に実装していないこと）

| 項目 | 理由 |
| :--- | :--- |
| **自発的な車線変更** | `vd-func:FUNC-055`。本層は「居るべき車線」を出すところまで |
| 目標レーンに入れなかったときの停止・待機 | そのまま通過し、逸脱を記録するのみ（決定済み） |
| リルート（走行中の経路再計画） | `vd-func:FUNC-051/052` |
| 起点と目的地だけからの経路生成 | `req-vd-ad:REQ-AD-016` 本体。本層はその前段 |
| road 列そのものの補完 | esmini が既にやっている（§1） |
| config フラグ / シナリオ Property | 既定で常に働く。切り替えを設けない（決定済み。したがって `VirtualDriverPanel.tsx` / `virtual_driver_api.py` の更新も不要 — VD-GUI-PARITY, issue #33 の対象外） |

## 6. 静的チェッカとの関係

並行して `scripts/check_route_waypoints.py`（`fixes #31`, commit 27c7eecf）が作られている。
これは xodr から `transitions[(predRoad, predLane, succRoad, succLane)]` を組み、xosc の Waypoint 列を
**静的に**照合するリンタである。

本層の実行時判定は**同じ判定の動的版**にあたる。ただし判定の母数が違う。

- 静的チェッカは「1ホップの隣接テーブル」を見るので、road id ペアだけでは connecting road が
  一意に決まらないケース（`ambiguous-connecting-road`）を落とす
- 本層は実際の探索（`RoadPath` / `LaneIndependentRouter`）の結果を見るので、候補が複数あっても
  目的地まで届く枝を選べる

したがって**静的チェッカが黒と言ったものが実行時には通る**ことがある。実測でその例が出ている
（`decelerate_for_right_turn.xosc` の粗い版）。逆に**静的チェッカが緑でも実行時に逸脱する**ケースが
穴1（最終ホップの目標レーン未検証）である。両者は補完関係であり、どちらかで代替できない。

## 7. 参考

- 実装: `GT_esmini/include/gt_esmini/control/virtualdriver/RouteLanePlan.hpp`, `GT_esmini/src/control/virtualdriver/RouteLanePlan.cpp`
- 配線: `GT_esmini/src/control/ControllerVirtualDriver.cpp`（自己位置取得の直後、resume-merge の直前）
- テレメトリ: `GT_esmini/include/gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp`（`RouteLanePlanSnapshot`）, `GT_esmini/src/control/virtualdriver/VirtualDriverTelemetryJson.cpp`
- ユニット: `GT_esmini/test/unit/virtualdriver/test_RouteLanePlan.cpp`（合成 xodr で後ろ向き伝播と不連続検出を検証）
- 挙動: `resources/xosc/verification/06_route_lane/merge_required_for_exit_ramp.xosc`
- 穴1 の原典: `EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp` `RoadPath::Calculate` の `nextRoad == targetRoad` 分岐（READ-ONLY / R1）
- 記述ルール（静的側）: `GT_esmini/docs/virtualdriver/design/scenario_authoring_foundation.md` §10
