# OSI 論理レーンと HostVehicleData.route — 規格モデルと GT の現状

> ステータス: **T / S0 / S1 / S4 / S2.5 / S2.5b / S3 完了。S2 / S5 が未実装**（2026-09-24）。
> したがって §3-1 の表はそれより前の姿である — `reference_line[]` / `logical_lane[]` /
> `logical_lane_boundary[]` / `logical_lane_assignment[]` と `HostVehicleData.route`（§3-3）は
> **既定で出る**（`GT_OSI_LOGICAL_LANE=0` で opt-out、設計書 §7-3）。
> まだ出ていないのは連結性（`predecessor/successor/adjacent`、S2）だけ。
> 本書は「規格が何を要求しているか」と「GT が今どこまで
> 出しているか」を突き合わせた現状記録であり、実装方針は
> [`logical_lane_and_route_design.md`](logical_lane_and_route_design.md) にある。
> 知識グラフ: `capability_model.md` §2.2a **W4**（`route` が populate されていない）の該当行。
> 材料側は `req-vd-ad:REQ-AD-016/017` / `vd-component:route-lane-plan` /
> `signal:route_lane_conformance`。
> 調査日: 2026-09-23。OSI は `externals/osi/v11`（3.7.0）、proto 本文は `DriverScript/osi3/`。

---

## 1. 目的と、その手前にあるもの

埋めたいのは `HostVehicleData.route`（`osi_hostvehicledata.proto` field 14）である。
`MovingObject.future_trajectory`（`signal:ego_planned_path`、実装済み）とは別系統で、
こちらは「自車がこの先どの車線を通るつもりか」をナビゲーション相当の粒度で外へ出す欄にあたる。

```
Route
 ├ route_id
 └ route_segment[]
    └ lane_segment[]  { logical_lane_id, start_s, end_s }
```

点列ではなく **論理レーンへの参照の列**である。したがって `route` だけを埋めると
`logical_lane_id` が実在しない id を指す。値は入っているのに参照が無言で壊れている状態になり、
順序を入れ替えることはできない。

**ブロッカーは 1 つだけで、logical lane を誰も出していないことである。**
`GroundTruth.logical_lane` はフィールドとしては v10 / v11 いずれにも定義があるが、
core esmini も GT_esmini も C++ では一切埋めていない（リポジトリ全体の grep で当たるのは
`EnvironmentSimulator/code-examples/` の C# サンプルのみ）。GT が出しているのは `osi_lane` で、
これは後述のとおり別モデルである。

---

## 2. 規格モデル — logical lane は osi_lane と何が違うか

### 2-1. OpenDRIVE レーンとの対応

`osi_logicallane.proto` の `LogicalLane` 本文が対応関係を明示している。

> If OSI is generated from OpenDRIVE, then LogicalLanes map directly to OpenDRIVE lanes.

**論理レーンは OpenDRIVE レーンと 1:1** である。同型・単一後続・単一先行という条件を満たす連続レーンを
1 本へ併合することは許されているが、義務ではない。

対して `osi_lane` は「路面標示が引かれている区画」であり、同じ proto がその差を例示している。

> a road with two driving directions but no road markings in-between would be presented as two
> LogicalLanes, but only one Lane.
> on intersections, each driving path is one LogicalLane, but the whole area is one Lane of type
> TYPE_INTERSECTION.

さらに、抜けが許されない。

> Outside of intersections, logical lanes are constructed such that each point on the road belongs to
> at least one (typically: exactly one) logical lane. So there are no gaps between logical lanes.

つまり走行レーンだけでなく歩道・縁石・路肩・中央分離帯も論理レーンとして出す。
`osi_lane` は中央レーンと交差点内レーンを除外している（[`GT_OSIReporter_Geometry.cpp:1043`](../../src/osi/GT_OSIReporter_Geometry.cpp#L1043)
の `!lane->IsCenter() && !lane->IsOSIIntersection()`）ので、母集合そのものが違う。

### 2-2. 連結性は 4 系統に分かれる

`osi_lane` の連結性は `Classification.lane_pairing[] { antecessor_lane_id, successor_lane_id }` の
1 種類だけである。論理レーンは 4 つに分かれる。

| 種別 | フィールド | 構造 | `osi_lane` 側 |
| :-- | :-- | :-- | :-- |
| 前方接続 | `predecessor_lane[]` | `LaneConnection{other_lane_id, at_begin_of_other_lane}` | なし |
| 後方接続 | `successor_lane[]` | 同上 | `lane_pairing` |
| 横隣接 | `left/right_adjacent_lane[]` | `LaneRelation{other_lane_id, start_s, end_s, start_s_other, end_s_other}` | `left/right_adjacent_lane_id[]`（id のみ） |
| 重なり | `overlapping_lane[]` | 同じ `LaneRelation` | なし |

構造上の差が 2 点ある。

**`at_begin_of_other_lane`** — 相手レーンの始端に付くのか終端に付くのかを bool で持つ。

> If true: LaneConnection is at the beginning of the other lane.
> If false: LaneConnection is a the end of the other lane.

OpenDRIVE の `contactPoint`（START / END）がそのまま写る情報である。`osi_lane` の
antecessor / successor にはこの区別がなく、終端同士が向かい合う接続を表現できない。
GT は直接ジャンクション / 仮想ジャンクションの処理で既にこの分岐を書いている
（[`GT_OSIReporter_Geometry.cpp:205-215`](../../src/osi/GT_OSIReporter_Geometry.cpp#L205-L215)）。
**情報は持っているが、今は OSI 面へ出さずに捨てている。**

**「右」「左」は走行方向ではなく参照線方向で定義される。**

> "Right" is in definition direction (not driving direction), so right lanes have smaller T
> coordinates.

既存の `osi_lane` は `lane_id + 1` を左隣として入れており、これは t が増える側なので結果は一致する。
ただし同じファイルの `centerline_is_driving_direction` のほうは road rule で反転している
（LHT では逆になる）。**同じ関数の中に「反転する量」と「反転しない量」が同居している**ので、
論理レーン側を書くときに巻き込まれやすい。

### 2-3. 論理レーンは単独では出せない

`LogicalLane` は 2 つの連れ子を要求する。

```
GroundTruth
 ├ reference_line[]          ← ST 座標系の土台。poly_line[] = {world_position, s_position, t_axis_yaw}
 ├ logical_lane_boundary[]   ← osi の lane_boundary とは別物。点が ST を持つ
 └ logical_lane[]            ← start_s / end_s は reference_line の s
```

`LogicalLaneBoundary` が既存の `LaneBoundary` と別型である理由は proto 本文が設計判断として
明記している。

> %Lane boundaries cannot be shared with physical lanes. This results in more data needed. This can
> mostly be mitigated by only transmitting the lane boundaries during initialization.

**境界ポリラインが丸ごともう 1 系統増える。** これは実装の都合ではなく規格の構造なので削れない
（§5-5 のサイズ影響の主因）。

精度要件は次の 2 つである。

> If the boundary approximates a curve, the points must be chosen in a way that the lateral distance
> to the ideal line does not exceed 5cm.
> The Z error must not exceed 2cm.

esmini の OSI 点生成はこの XY 側と同じ値を既定にしている（`OSI_MAX_LATERAL_DEVIATION = 0.05`、
[`CommonMini.hpp:68`](../../../EnvironmentSimulator/Modules/CommonMini/CommonMini.hpp#L68)）。
**XY の 5cm は既存点の再利用で構成的に満たせる。** Z の 2cm は別基準で、勾配・カントのある道路では
保証されない（§4-2 C）。

### 2-4. Route の意味論 — 並列レーンの集合であって、単一レーン列ではない

`osi_route.proto` の `RouteSegment` は「その区間で自由に動いてよい並列レーンの集合」である。

> Together, the listed logical lane segments should form a continuous area, where the traffic agent
> can move freely. These will mostly be parallel lanes.
> Each time there is a successor-predecessor relation between the logical lanes along the route, a
> new RouteSegment starts.

これは VD の `RouteLaneBand`（`road_id` + **複数の** `lanes`、
[`RouteLanePlan.hpp:36-42`](../../include/gt_esmini/control/virtualdriver/RouteLanePlan.hpp#L36-L42)）
とほぼ同型である。VD 側の材料は正しい形をしている。

向きの規約に落とし穴がある。

> The LogicalLaneSegment allows that start_s > end_s. If start_s < end_s, then the traffic agent
> should traverse the segment in the logical lane's reference line definition direction.

**`start_s > end_s` が「参照線と逆向きに走れ」の表現である。** 一方 `LogicalLane` 自身は
`end_s > start_s` を要求する（区間は必ず正順）。**同じ 2 つの double が message によって意味が違う。**
RHT で負レーンを走る通常ケースがまさに `start_s > end_s` 側に落ちるので、ここを取り違えると
「全経路が逆走として出る」という、値は入っているのに意味が反転した出力になる。

### 2-5. 「自車が経路のどこにいるか」は Route 側に書かない

**OSI に route 相対のオフセットを入れる欄は存在しない。** `osi_common.proto` /
`osi_object.proto` を通して route を参照するフィールドは 1 つもない。`Route` は「どの論理レーンを
どこからどこまで通るか」を述べるだけで、自車がその中のどこにいるかは述べない。

規格が用意しているのは**レーン相対**の欄である。

```
MovingObject.moving_object_classification.logical_lane_assignment[]   (osi_object.proto:752)
  ├ assigned_lane_id   LogicalLane への参照
  ├ s_position         縦（論理レーンの ST 座標系）
  ├ t_position         横
  └ angle_to_lane      レーンに対する向き [rad]
```

**この s / t がどの点のものかは規格が決めている。** `osi_common.proto` は `s_position` を
「S position of **the object reference point** on the lane」と書き、`BaseMoving.position` を
「The reference point for position and orientation: the center (x,y,z) of the bounding box」と
定義している。つまり MovingObject の参照点は **bounding box の中心**であり、`base.position` と
`logical_lane_assignment.s_position` は同じ点を別の座標系で述べたものでなければならない。

esmini はエンティティを **origin**（出荷カタログ車では後軸）で置き、box は `center_x` だけ前に
ある。両者を混同すると**車長方向に丸ごと 1 つぶんずれた値が、形としては完全に正しく見える**。
同じ規約は `signal_catalog.yaml` の `ego_planned_path` で先に決着しており、GT はそれに従う。

経路相対はこの 2 つの**合成**で出す — `assigned_lane_id` が `route` のどの `RouteSegment` に属し、
`s_position` がその `[start_s, end_s]` のどこにあるか。`osi_logicallane.proto` も
`The S coordinate of the reference line makes it easy to find e.g. which object is next on a lane,
using the LogicalLaneAssignment of the objects.` と、この合成を前提に書いている。

割り当ては単一ではない。

> any object overlapping the lane more than 5cm has to be assigned to the lane

`repeated` であり、**車線を跨いでいる間は両方のレーンに割り当てる**のが規格である。現行の
単一値 `assigned_lane_id` では表現できない状態がここでは表現できる。

---

## 3. GT の現状

### 3-1. 出しているもの・出していないもの

| GroundTruth のフィールド | GT の状態 | 実装箇所 |
| :-- | :-- | :-- |
| `lane[]` | 出している | `GT_OSIReporter_Geometry.cpp` `UpdateOSIRoadLane()` |
| `lane_boundary[]` | 出している | 同 `UpdateOSILaneBoundary()` |
| `reference_line[]` | **出していない** | — |
| `logical_lane_boundary[]` | **出していない** | — |
| `logical_lane[]` | **出していない** | — |
| `moving_object[].moving_object_classification.assigned_lane_id[]` | 出している（物理レーンの global id、deprecated field4 との dual emit） | `GT_OSIReporter_Moving.cpp:977-979` |
| `moving_object[].moving_object_classification.logical_lane_assignment[]` | **出していない** | — |

### 3-2. 交差点の扱いが規格と逆向きになっている

`osi_lane` 側では、OSI 交差点である junction の接続路レーンを**すべて 1 本の
`TYPE_INTERSECTION` レーンへ融合**し、id には junction のグローバル id を振っている
（[`GT_OSIReporter_Geometry.cpp:224-229`](../../src/osi/GT_OSIReporter_Geometry.cpp#L224-L229)）。

この融合は `signal:ego_lane` の既知の欠けとして既に記録されている。交差点内では
`assigned_lane_id`（接続路レーンのグローバル id）が `lane_map` に無く、実測 171/15800 フレームで
join が外れる。

規格は交差点について逆を要求している（§2-1 の引用: each driving path is one LogicalLane）。
つまり **交差点の中は「既存の osi_lane を作り直す」のではなく、今まったく出していないものを
新規に出す**ことになる。ここが実装量の主な塊であると同時に、`signal:ego_lane` の join 欠けを
論理レーン面で解消できる余地でもある。

### 3-3. HostVehicleData 側

`GT_HostVehicleReporter` は `UpdateFromObjectState(const scenarioengine::Object* egoObj)` で
ego の `Object` を受けており、`egoObj->pos_.GetRoute()` に届く。したがって経路そのものへの
アクセス経路は既にある。`route` を埋めていないのは配線の不在であって、材料の不在ではない。

`capability_model.md` §2.2a の **W4** 行がこれを既に記録している。

> `custom_detail` / `custom_state` / `DriverOverride` / 実 `Name` 列挙 / **`route`** /
> `vehicle_motion.current_curvature` が populate されていない（`GT_HostVehicleReporter.cpp:343-350`）
> — 状態: 一部（`custom_state`/`DriverOverride`/**`route`**/`current_curvature` は未着手）

---

## 4. RoadManager から導けるか

`GT_esmini/src/road/GT_RoadManager.cpp`（`RoadManager.cpp` の全文フォーク）と
`EnvironmentSimulator/Modules/RoadManager/RoadManager.hpp` を確認した結果を示す。

### 4-1. 既存 public API で足りるもの

| 必要な情報 | 取得元 | 備考 |
| :-- | :-- | :-- |
| 参照線ポリライン | `LaneSection::GetRefLineOSIPoints()` | レーンセクション単位で既存 |
| 点の s 座標 | `PointStruct.s` | `Position::GetS()` 由来＝**road s**。`{s,x,y,z,h,p,r,nx,ny,endpoint}` |
| `t_axis_yaw` | `PointStruct.nx/ny` → `atan2(ny, nx)` | 法線が既に格納済み |
| 横位置 t | `LaneSection::GetOuterOffset(s, lane_id)` / `GetCenterOffset` | 符号はレーン側で付ける |
| レーン種別 | `Lane::GetLaneType()` | `LANE_TYPE_*` → `LogicalLane::TYPE_*` は既存の osi_lane 写像とほぼ同形 |
| 前後接続 | `Lane::GetLink()` + `Road::GetLink()` | 既存の連結解決（`Geometry.cpp:1404-1560`）を流用可能 |
| `at_begin_of_other_lane` | `Connection::GetContactPoint()` / `RoadLink::GetContactPointType()` | 既に分岐として存在 |
| 交差点内の接続路レーン | `Junction::GetConnectionByIdx()` → `Connection::GetLaneLink()` | osi_lane では捨てている情報 |
| `move_direction` | `Road::GetRule()` + lane_id の符号 | 既存の `centerline_is_driving_direction` と同じ式 |
| `passing_rule` | `LaneRoadMark::GetLaneChange()`（`INCREASE/DECREASE/BOTH/NONE`） | OpenDRIVE `@laneChange` がそのまま写る |
| `street_name` | `Road::GetName()` | |
| 速度制限 | `Road::GetSpeedByS(s)` | 道路単位（§4-2 B） |
| 5cm 精度 | `OSI_MAX_LATERAL_DEVIATION = 0.05` | 既存 OSI 点が規格値とちょうど一致 |

**RoadManager への改修は不要である。** これは R1 上きわめて大きい。`RoadManager.hpp` は
core census で `total=77/77` の満額枠であり、1 行も増やす余地がない。

### 4-2. 導けないもの

| # | 導けないもの | 理由 | 扱い |
| :-- | :-- | :-- | :-- |
| **A** | `overlapping_lane`（重なり区間の s レンジ） | 幾何的な重なり判定で、RoadManager に概念がない | 必須ではない（repeated）。別スコープへ切り出す |
| **B** | レーン単位の速度制限（OpenDRIVE `<lane><speed>`） | パーサは `<type><speed>` しか読まない（[`GT_RoadManager.cpp:3987`](../../src/road/GT_RoadManager.cpp#L3987)） | `traffic_rule[].speed_limit` は道路単位値で代用。`traffic_rule` は optional なので規格違反ではない |
| **C** | Z 方向 2cm 精度 | `OSI_MAX_LATERAL_DEVIATION` は XY 平面の判定 | 既知の非充足として記録。厳密化はカント厳密化と同じ土俵 |
| **D** | 物理境界のないレーンの境界 | `Lane::GetLaneBoundaryGlobalId()` が `ID_UNDEFINED` を返す場合がある | 論理境界は両側必須なので `GetOuterOffset` から合成する。隠れた工数 |
| **E** | レーンセクション単位の経路バンド | `RouteLaneBand` は**道路単位**、しかも出口端でのみ評価 | 道路内のレーンセクションを辿り直す層が新規に要る |
| **F** | 経路区間の実 s 値 | バンドは `exit_s`（0.0 か road length）しか持たない | 自車の現在 s から終点 WP の s までを切り出す計算が新規 |
| **G** | 経路始点からの累積距離（**値はあるが信用できない**） | §4-3 | 論理レーン区間長の積み上げで自前に出す |
| **H** | レーンセクションを跨ぐレーン id の追跡（**API はあるが信用できない**） | §4-4（2026-09-24 追記） | レーンリンクを 1 段ずつ辿って自前に出す |

### 4-4. `Road::GetConnectedLaneIdAtS()` はレーンセクション 3 つ以上で行き過ぎる

同じ道路の別 s におけるレーン id を返す API で、署名どおりなら §4-2 H を満たす。
ところが実装のループは、**既に離れたレーンセクションの始端 s** を `s_target` と比べて進む
（[`GT_RoadManager.cpp:3235-3253`](../../src/road/GT_RoadManager.cpp#L3235)）。

```
for (j = lsec_idx; lane_id_tmp != 0 && j < GetNumberOfLaneSections() - 1 && s < s_target; j++)
    lsec        = GetLaneSectionByIdx(j);
    lane_id_tmp = lsec->GetConnectingLaneId(lane_id_tmp, SUCCESSOR);   // ← 1 つ先の id
    s           = lsec->GetS();                                        // ← セクション j の「始端」
```

`s` が常に 1 段遅れるため、ループは**行き過ぎて止まる**。

> **2026-09-24 精度の訂正**: 当初ここは「3 つ以上で壊れる／2 つでも結果的に合う」と書いていたが、
> 条件が**実際より狭い**。ループ条件を追うとこうなる。
>
> - 反復 `j` は `s` を `start(j)` にする。条件は反復 `j` の**直前**に `start(j-1) < s_target` で見る。
> - したがって最後に実行される反復は `j = k+1`（`k` = `s_target` を含むセクション）で、
>   戻り値は**セクション `k+2` の id**。`j < N-1` の上限で `N-1` にクランプされる。
> - つまり **`min(k+2, N-1)` を返す。正解は `k`。**
>
> 一致するのは次の 2 つだけである。
> 1. ループが 1 度も回らない（`s_target <= s_start`、またはレーンセクションが 1 つ）
> 2. **`s_target` が最終レーンセクションにある**（`k == N-1`）
>
> したがって「レーンセクション 2 つなら安全」は成り立たない。`s_start` と `s_target` が
> **同じセクション 0 にある**呼び方（= 同一セクション内の別 s を訊く）でもループは 1 回回り、
> セクション 1 の id を返す。2 つのセクションでレーン番号が同じなら偶然一致するだけである。
> `N=3` で「最終セクションに張り付く」ように見えるのは `k+2 >= N-1` が成立するからで、
> `N=5`・`k=1` なら返るのはセクション 3（最終の 4 ではない）。
>
> 総じて **`s_target = -1`（道路端）で呼ぶ前提の実装**であり、それ以外の呼び方は
> セクション数によらず信用できない。

`EvaluateRouteLaneStatus`（`RouteLanePlan.cpp:397`）と バンド構築（`:200`）もこの関数を使っている。
**中間 s を訊く呼び方をしている限り、レーンセクションが複数ある道路では `ego_lane` の正規化が
同じ癖を持つ。** 既存の挙動であり S4 では触っていないが、レーン正規化まわりを疑うときの
第一候補になる。`signal:route_lane_conformance` / `gate:route-lane-regression` が観測しているのは
まさにこの経路である。

S4 の `MapLaneIdAcrossSections()`（`RouteToOsiRoute.cpp`）は
`LaneSection::GetConnectingLaneId()` を 1 段ずつ辿って自前に解いている（1 パス O(n)、
各段で相手セクションにそのレーンが実在するかを確認する — リンクが無いとき
`GetConnectingLaneId` は**入力 id をそのまま返す**ため）。

### 4-3. `Position::GetRouteS()` は物理駆動の車では凍る

欄そのものは存在する。`Position::GetRouteS()` は `Route::GetPathS()` を返し、ヘッダのコメントも
`Longitudinal distance along the route from start of route` と書いている。

**しかしこの値は物理駆動の車では更新されない。** `path_s_` を書く `Position::CalcRoutePosition()`
の呼び出し元は次の 2 つだけである。

- `Position::SetRoute()` — 経路を割り当てた瞬間に 1 回（[`GT_RoadManager.cpp:12407`](../../src/road/GT_RoadManager.cpp#L12407)）
- `Position::TeleportTo()` — テレポート時（[`同:10553`](../../src/road/GT_RoadManager.cpp#L10553)）

VirtualDriver / RealDriver / PythonDriver はいずれも `SetInertiaPos*` で姿勢を書き戻しており、
この経路では `CalcRoutePosition()` が呼ばれない。したがって
**`GetRouteS()` は経路割り当て時の値で凍る**。値は入っているので、素直に読むと
「ずっと出発点にいる」ように見える。

さらに `CalcRoutePosition()` は `route_->path_s_` / `waypoint_idx_` / `currentPos_` を書き換える。
**観測側から呼ぶとシミュレーション状態を変えてしまう**ので、OSI レポータからは呼べない
（`RouteLanePlan` が `never mutates roadmanager state` を明示しているのと同じ理由）。

---

## 5. 影響範囲

### 5-1. コード

新規に書くもの（概算）。

| 対象 | 行数 |
| :-- | :-- |
| ReferenceLine ビルダ（道路単位） | ~120 |
| LogicalLane ビルダ（レーンセクション×レーン、交差点内含む） | ~350 |
| LogicalLaneBoundary ビルダ（ST 化・欠落時の合成・`passing_rule`） | ~300 |
| 連結性解決（pred / succ / adjacent） | ~250 |
| バンド → RouteSegment 列（§4-2 E/F） | ~200 |
| `LogicalLaneAssignment` の emit（§2-5） | ~60 |
| 経路進捗の積み上げ（§4-3 の迂回） | ~60 |
| ユニットテスト | ~450 |

既存改修で済むもの。

| ファイル | 改修 |
| :-- | :-- |
| `GT_OSIReporter.cpp` `CreateOSIStaticGroundTruthFromODR` | 呼び出し 1 箇所（~6 行）。**2026-09-24 是正**: 当初 `UpdateOSIStaticGroundTruth` と書いていたが別関数だった（そちらは stationary misc object 専用で毎フレーム走る。設計書 §1 の注） |
| `GT_OSIReporter_Moving.cpp` `UpdateOSIMovingObject` | `logical_lane_assignment` の呼び出し（`:979` の直後、4 行） |
| `GT_HostVehicleReporter.cpp` `UpdateFromObjectState` | `mutable_route()` の充填（~40 行）＋ UDP 分割送信（~40 行） |
| `VirtualDriverTelemetryJson.cpp` | 経路進捗を `route_lane` ブロックへ（~8 行） |
| `GT_esmini/test/CMakeLists.txt` | 新ユニットテストの登録 |

### 5-2. R1 Clean Core への当たり

GT-original の OSI コードは 2 つの置き場に分かれており、**どちらに置くかで R1 の当たり方が変わる。**

- `GT_OSI_SOURCES`（[`GT_esmini/CMakeLists.txt:282-284`](../../CMakeLists.txt#L282-L284)）は
  **GT_esminiLib 側**。GT_esminiLib → ScenarioEngine の一方向リンクなので、ScenarioEngine 側の
  `GT_OSIReporter.cpp` から呼ぶと循環になる。
- `obj_osi_internal` は ScenarioEngine 側の TU（`GT_OSIReporter.cpp`）で定義されている。

既存の解決例は `PlannedPathRegistry` で、宣言は GT ヘッダに置き、**実体はスワップゾーン側の
`GT_OSIReporter_Moving.cpp:1189` に定義**している。したがって選択肢は次の 2 つになる。

| 案 | R1 | 代償 |
| :-- | :-- | :-- |
| **(a) 新ファイルをスワップゾーンへ追加** ← **採用（2026-09-24 承認・実施）** | `EnvironmentSimulator/Modules/ScenarioEngine/CMakeLists.txt` に 4 行。ガードフック Rule 1 で ask。**新規例外ではなく既存例外の拡張**（同ファイルの `# GT_esmini Modification: Swap OSIReporter` ブロックは既に `GT_OSIReporter*.cpp` を 7 本 APPEND している。8 本目を足す形）。前例は RoadManager/CMakeLists.txt への odr_side **10 本**追加（2026-07-02 承認。調査時は 6 本と書いたが実数は 10） | コア CMake 改変の件数は増えない。新ファイルは `lineage:gt_osireporter` の `fork_paths` に入らないので inbound 差分も増えない |
| (b) 既存 `GT_OSIReporter_Geometry.cpp` へ足す | コア改変ゼロ | 同ファイルは `lineage:gt_osireporter` の**フォーク系譜ファイル**。GT 固有の約 1000 行を足すと `check_fork_sync.py` の inbound 差分が恒久的に膨らむ |

`OSIReporter.hpp` には触れない。メンバ関数ではなく GT 自由関数として書く
（`spine-work:osi-assigned-lane-driving` で確立済みの形）。

### 5-3. テストとゴールデン — **既定 OFF はここの保護にならない**

`EnvironmentSimulator/Unittest/ScenarioEngineDll_test.cpp` には OSI ファイルサイズの
ハードコード assert が 10 箇所ある（`st_size == 185928` ほか）。静的 GroundTruth に 1 バイト足せば
全部ずれる。

**しかし、これらを含む 7 テストは GT では既にスキップされている。**
[`scripts/run_tests.sh:138-159`](../../../scripts/run_tests.sh#L138-L159) が理由ごと記録している。

> these 7 tests assert BYTE-EXACT serialized OSI sizes. GT emits the same entities, point counts and
> field VALUES as upstream (verified message-by-message), but GT links OSI 3.7.0 whose protos use
> proto3 explicit field presence: every field the reporter explicitly sets to 0.0 serializes at
> ~9 bytes, while upstream's OSI 3.5.0 (implicit presence) omits them. Measured on cut-in_simple:
> static+dynamic msg 11288 (GT) vs 7661 (pristine upstream) — identical content, ~3.6 KB of
> zero-valued-field encoding. The OSI 3.7.0 upgrade is a permanent, intentional GT platform
> divergence, so these exact-size assertions are structurally non-satisfiable for GT.

対象は `GroundTruthTests.check_GroundTruth_including_init_state` /
`check_frequency_explicit` / `check_frequency_implicit` / `check_update_gt_twice_same_frame` /
`check_update_osi_ground_truth_api` / `check_update_osi_ground_truth_api_and_log` /
`GetOSIRoadLaneTest.lane_no_obj` の 7 本。

**したがって「ゴールデンを更新しないために既定 OFF にする」という論拠は成立しない。**
守るべき assert はそもそも走っていない。フラグを置くなら別の理由が要る（設計書 §7）。

他の検証資産への当たりは次のとおり。

| 資産 | 影響 | 根拠 |
| :-- | :-- | :-- |
| `GT_esmini/test/odr_fixtures/golden/osi/*.json` | **id を動かさない限り影響なし** | 抽出はホワイトリスト（`lane_count` / `lanes[].id` / `lane_boundary_count` 等、[`run_odr_conformance.py:508-531`](../../../scripts/run_odr_conformance.py#L508-L531)）。新フィールドは拾わない。ただし `lanes[].id` は**生のグローバル id** |
| `GT_esmini/test/regression_baseline/*.yaml`（7 本） | 影響なし | matcher 名と status しか持たない。オブジェクト id もグローバル id も入っていない |
| `gt_sim_test` の `scene` 射影 | 影響なし | `_gt_to_scene` は厳密なホワイトリスト |
| 統合テスト `osi_junction_{lht,rht}` | 影響なし | `--osi` で GT を要求するが件数 assert はない |

**id 採番の規律が唯一の実質的な制約である。** RM / OSI のグローバル id は
`CommonMini::GetNewGlobalId()` の単一の単調カウンタから出ている。`ApplyAuthoredJunctionBoundaries`
のコメントが既にこの性質を利用している。

> all real ids are already assigned, so a fresh GetNewGlobalId() is guaranteed collision-free against
> every existing boundary id — no documented offset needed.

論理レーン / 論理境界 / 参照線の id も**静的 GT 構築の後段パスで引く限り既存 id は 1 つも動かない**。
逆に OpenDRIVE ロード中に引くと ODR ゴールデンが全滅し、`lane_map` の join も崩れる。

### 5-4. ストリーム消費側

| 消費側 | 影響 |
| :-- | :-- |
| **HVD の UDP 送信** | ~~**フラグメンテーション未実装**。`serialized_data_.size > 8192` で `LOG_WARN` を出し**そのフレームを丸ごと捨てる**。`route` は毎フレーム載るので、長い経路で HVD が静かに止まる。**本件で最も危険な箇所**~~ → **解消（2026-09-24、設計書 §6-1）**。GroundTruth と同じ counter 規約で分割送信する。収まるメッセージは従来どおり `counter == 0` の単一パケットなので消費側の変更はゼロ |
| GroundTruth の UDP 送信 | チャンク済み（counter ベース、`OSI_MAX_UDP_DATA_SIZE 8192`）なので動く。パケット数が増える分、`osi_bridge` の再組み立て取りこぼし確率は上がる |
| `web/backend/api/osi_stream.py` | `_gt_to_json` / `_hvd_to_json` ともホワイトリスト射影。壊れないが、何も見えない |
| `road_geometry_service.py` | OSI を迂回して xodr を直読しているため影響なし |
| Python バインディング | `scripts/osi3/` と `DriverScript/osi3/` の双方に `osi_logicallane_pb2` / `osi_route_pb2` が**生成済み**。再生成不要 |
| 配布 ZIP | DLL 差し替えのみ。`build_package.ps1` に変更なし |
| GT_OSMP_FMU | OSI は v11 共通。protobuf の未知フィールドは無視されるので、古い OSI でビルドされた消費側も壊れない |
| Linux CI | フル USE_OSI ビルドは Windows 限定。なお `externals/osi/v10` の HostVehicleData には **`route` フィールドが無い**が、v10 経路では GT 自体がビルドされないため不問 |

### 5-5. ペイロードのサイズ（**S0 実測、2026-09-24**）

> **この節は 2026-09-24 に書き換えた。** 元の版は upstream の `st_size == 185928` という
> assert からの逆算で「レーンあたり約 12.4 KB」「静的 GroundTruth は 2〜3 倍」と書いていたが、
> **どちらも実測ではなく、前者は誤りだった**。実測すると e6mini の静的バイトの **67% は
> `stationary_object`（ガードレール）**で、道路網（`lane` + `lane_boundary`）は 33% しかない。
> レーン数で割った「レーンあたり」は、論理レーンが触りもしない量を分子に含んでいた。

計測は `scripts/probe_osi_logical_lane_size.py`（in-process、`SE_GetOSIGroundTruth` と
`.osi` 第 1 レコードの両方）。バイト数はフィールドごとに単独で再直列化して測っている。

| 資産 | 静的 GT | うち lane | うち lane_boundary | うち stationary/sign/light | 毎フレーム動的 | osi lane | 論理レーン相当 |
| :-- | --: | --: | --: | --: | --: | --: | --: |
| e6mini | 221,425 | 21,475 | 52,163 | 147,787 | 2,121 | 14 | 14 |
| fabriksgatan | 26,547 | 7,943 | 18,604 | 0 | 2,121 | 25 | 44 |
| multi_intersections | 262,582 | 73,924 | 154,043 | 34,615 | 2,125 | 171 | 242 |
| soderleden | 48,596 | 17,173 | 31,423 | 0 | 2,121 | 33 | 33 |
| highway_merge_split | 45,555 | 15,456 | 30,099 | 0 | 2,123 | 43 | 53 |

「論理レーン相当」は xodr のレーン数（センターレーン除く）。**osi lane との差が交差点である** —
接続路レーンは今 1 本の `TYPE_INTERSECTION` レーンへ融合されているが、論理レーンでは 1 本ずつ出る
（§3-2）。fabriksgatan で 25 → 44、multi_intersections で 171 → 242 になるのがそれ。

upstream の assert 値との対応: `cut-in.xosc` / `e6mini.xodr` の `.osi` 第 1 レコードは
GT で **223,761 B**（upstream pristine は 185,928 B）。差 +20% は OSI 3.7.0 の
explicit field presence によるもので、§5-3 に記録済みの恒久差分と整合する。

**増分の投影**（S1/S3 実装後の値であって、まだ実測ではない）。モデルは「論理レーンは
OpenDRIVE レーンと 1:1 なので、論理面は物理面の m = 論理レーン数 / osi lane 数 倍の道路を覆う」。
`LogicalLaneBoundary` の点は 5 double（Vector3d + s + t）で、物理 `BoundaryPoint` の 5 double
（Vector3d + width + height）と同じ重さなので、効くのは点数だけである。

| 資産 | m | 道路網バイト | 追加見込み | 静的 GT 全体比 | 道路網だけの比 |
| :-- | --: | --: | --: | --: | --: |
| e6mini | 1.00 | 73,638 | 57,171 | **1.26x** | 1.78x |
| fabriksgatan | 1.76 | 26,547 | 45,870 | **2.73x** | 2.73x |
| multi_intersections | 1.42 | 227,967 | 293,687 | **2.12x** | 2.29x |
| soderleden | 1.00 | 48,596 | 40,964 | **1.84x** | 1.84x |
| highway_merge_split | 1.23 | 45,555 | 50,721 | **2.11x** | 2.11x |

> **2026-09-24（S3 実測）**: 投影は 1.26x〜2.73x、**実測は 1.26x〜2.85x**
> （e6mini 1.26 / fabriksgatan 2.85 / multi_intersections 2.48 / soderleden 2.04 /
> highway_merge_split 1.96）。3 倍に届く資産は無く、既定 ON で確定した（設計書 §7-3）。
> 投影が最も外れたのは multi_intersections（+0.36）で、**向きは予想と逆**だった —
> 接続路は短いが曲率半径が 2.2 m まで落ちるので、偏差で刻む境界はそこで本線より密になる。

**静的 GroundTruth は 1.3〜2.7 倍になる見込みで、3 倍に届く資産は無い。** 2 つの比を併記したのは
別の問いに答えるからで、消費側のペイロード全体が問題なら左、道路網の表現コストが問題なら右を見る。
e6mini で両者が大きく割れるのは、ガードレールが静的バイトの大半を占めているためである。

### 5-5a. 毎フレーム側 — 静的レイヤは既定では 1 回しか流れない

**論理レーン・参照線・論理境界は静的 GroundTruth にしか載らず、既定では初回レコードに 1 回出るだけ
である。** したがって上表の 1.26x〜2.73x は**ロード時 1 回**のコストで、毎フレームの帯域ではない。

毎フレーム増えるのは次の 3 つだけ。動的 GroundTruth は **1 台のシーンで実測 2.1 KB、
10 台のシーンで 9.2 KB**（＝1 台あたり約 900 B）。

| 量 | 1 フレームあたり | 効き方 |
| :-- | :-- | :-- |
| `logical_lane_assignment`（L1、§2-5） | **1 レーン割り当てあたり実測 34.00 B**（Identifier + double 3 本）。車線跨ぎ中は 2 本で 68 B | オブジェクト数に比例するが、**比例先が小さい**。実測（`scripts/probe_osi_logical_lane_assignment.py`）で 1 台 2,174→2,208 B（**1.016x**）、2 台 2,967→3,038 B（**1.024x**）、10 台 9,166→9,511 B（**1.038x**） |
| `HostVehicleData.route` | 経路長に比例。`LogicalLaneSegment` 1 本 ≒ 25〜30 B。20 セクション × 並列 3 レーンで ≒ 1.8 KB | 自車 1 台ぶん。長経路では 8192 B を超えるが分割送信で解決済み（§5-4） |
| L2 経路進捗 | 数十 B | 無視できる |

> **2026-09-24（S2.5 実測で是正）**: この表は当初「2〜3 台なら +100 B（+5%）、20 台なら
> +0.7〜1.4 KB（**動的 GT が 1.3〜1.7 倍**）」と書いていた。**1 オブジェクトあたりのバイト数
> （35 B / 跨ぎ 70 B）は当たっていた（実数 34 B / 68 B）が、倍率が 1 桁ちがっていた。**
> 母数を取り違えていたため — 動的 GroundTruth は 1 台あたり約 900 B あるので、
> 1 台 34 B の追加は **+3.8%** にしかならない。**台数を増やしても比率は動かない**
> （分母も分子も台数に比例する）ので、20 台でも 1.04 倍前後である。
> 「オブジェクト数に比例する」という定性は正しく、「だから倍率が伸びる」が誤りだった。

GroundTruth の UDP はチャンク済みなので上限には当たらない。

> **例外があり、これが唯一の落とし穴である。** `OSIStaticReportMode`
> （[`OSIReporter.hpp:53`](../../../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSIReporter.hpp#L53)）
> は 3 値で、既定は `DEFAULT`（静的は初回のみログ・送信）。ここを **`API_AND_LOG` にすると
> 静的 GroundTruth が毎フレーム丸ごとログ・送信される**（`GT_OSIReporter.cpp:360` 付近の switch）。
> そのモードでは上表の **1.26x〜2.73x がそのまま毎フレームの帯域増**になり、
> multi_intersections なら 1 フレーム 556 KB を流すことになる。
> 論理レーン層を入れる前から同じ性質だったが、**層が増えたぶん影響が数倍になる**。
> 既定を変えない限り無関係だが、変える側は知っておく必要がある。

---

## 6. 未決事項

1. ~~**R1 例外の承認** — §5-2 の (a) 案。`ScenarioEngine/CMakeLists.txt` への 4 行追加。~~
   → **決着（2026-09-24 承認、S0 で実施）**。既存スワップブロックの拡張。
2. ~~**知識グラフのノード型** — `feature` 名前空間の `id_pattern` は `F[1-9]` で、F10 は正規表現に
   当たらない。例外採番（F8/F9 の前例）か、face-1 の work-item 名前空間新設か。~~
   → **決着（2026-09-24 ユーザー判断）**: `spine-work:osi-logical-lane`。設計書 §11 の表を見よ。
3. ~~**HVD の UDP 8192 B 上限** — §5-4。`route` を毎フレーム送ると長経路で HVD が静かに落ちる。~~
   → **決着（2026-09-24 実装済み）**。設計書 §6-1。
4. **`overlapping_lane` をスコープに入れるか** — §4-2 A。唯一 RoadManager から導けない項目。
5. ~~**車線跨ぎの複数割り当てを初版に入れるか** — §2-5。規格は 5cm 以上重なるレーン全部への
   割り当てを要求している。初版を 1 本に絞ると、車線変更中でも 1 本しか出ない。~~
   → **決着（2026-09-24、S2.5 で実装）**。入れた。車線変更シナリオで割り当て数が
   1 → 2 → 1 と推移すること、直進車は全フレーム 1 本のままであることを実データで確認済み
   （設計書 §8-0 S2.5）。
6. **経路帯からの符号付き横距離の用途** — OSI に欄が無く、基準の取り方（最近傍レーン中心か
   帯の端か、[m] かレーン幅比か）が用途で変わる。

1・2・3・5 は決着済み。残るのは **4（`overlapping_lane`）と 6（経路帯からの符号付き横距離）**で、
どちらも着手時にユーザー判断が要る（設計書 §11 に判断材料つきでまとめてある）。
