# OSI 論理レーンと HostVehicleData.route — 実装設計

> ステータス: **S0 完了（2026-09-24）、S1 以降は未着手**。現状と規格の突き合わせは
> [`logical_lane_and_route.md`](logical_lane_and_route.md)。本書はそれを前提に、
> 何をどこへどう書くかを決める。
> 知識グラフ: `capability_model.md` §2.2a **W4** の `route` 行を閉じる作業。
> 材料は `vd-component:route-lane-plan` / `req-vd-ad:REQ-AD-016/017`。
> 作成: 2026-09-23。

---

## 1. 方式の骨子

**静的 GroundTruth 構築の後段パスとして、GT 自由関数で組む。** `RoadManager` にも
`OSIReporter.hpp` にも触れない。既存の `ApplyAuthoredJunctionBoundaries`
（[`GT_OSIReporter.cpp:436`](../../src/osi/GT_OSIReporter.cpp#L436)）と同じ形である。

> **2026-09-24（S0 実装時に是正）**: 当初この節は後段パスの置き場を
> `UpdateOSIStaticGroundTruth()` と書いていたが、**誤り**だった。道路網を組むのは
> [`CreateOSIStaticGroundTruthFromODR()`](../../src/osi/GT_OSIReporter.cpp#L502) で、
> `UpdateOSIGroundTruth()` の `!osi_initialized_` 分岐から**1 度だけ**呼ばれる。
> `UpdateOSIStaticGroundTruth()`（[`:570`](../../src/osi/GT_OSIReporter.cpp#L570)）は別物で、
> OpenSCENARIO 由来の stationary misc object だけを扱い、**毎フレーム**（初期化分岐と通常分岐の
> 両方から）呼ばれる。後者に吊ると後段パスが毎フレーム走り、`GetNewGlobalId()` を引くように
> なった S1 以降で id が際限なく増える。

```
OpenDrive ロード（id 採番はここで完了）
        ↓
CreateOSIStaticGroundTruthFromODR()   1 度だけ（!osi_initialized_ 分岐）
   ├ UpdateOSIRoadLane()        既存: lane[]
   ├ UpdateOSILaneBoundary()    既存: lane_boundary[]
   ├ UpdateOSIIntersection()    既存: 交差点を1本へ融合
   ├ ApplyAuthoredJunctionBoundaries()   既存 GT 後段パス
   └ BuildOsiLogicalLanes()     ★新規 GT 後段パス（1度だけ）
        ├ reference_line[]
        ├ logical_lane_boundary[]
        ├ logical_lane[]
        └ 索引 (road, laneSection, lane) -> logical lane id
        ↓
UpdateOSIMovingObject()         毎フレーム・全オブジェクト
   └ EmitLogicalLaneAssignment() ★新規（L1、§2-6-1）
        moving_object_classification.logical_lane_assignment[]{s, t, angle}
        ↓
GT_HostVehicleReporter::UpdateFromObjectState()
   └ BuildOsiRoute()            ★新規（純関数）
        RouteLanePlan → Route{route_segment[]{lane_segment[]}}
        副産物 → RouteProgress（L2、telemetry 行き・§2-6-2）
```

**「自車が経路のどこにいるか」は L1 と `route` の合成でしか出ない。** 片方だけでは出ない（§2-6）。

分割の理由は所有権にある。論理レーン側は道路網から決まる静的な量なので、静的 GT と同じ寿命で
1 度だけ組む。`route` と L1 は自車／各車の状態から決まる動的な量なので毎フレーム組む。
静的側と動的側を繋ぐのは
**論理レーンの id を (road_id, laneSection index, lane_id) から引く索引 1 つだけ**である。

---

## 2. データモデルの写像

### 2-1. ReferenceLine — 道路 1 本につき 1 本

規格は隣接レーンが同じ参照線を共有することを強く推奨している
（`Neighboring lanes ... are strongly encouraged to reference the same ReferenceLine`）ので、
**道路単位**にする。s は OpenDRIVE の road s をそのまま使う。

| フィールド | 値 |
| :-- | :-- |
| `id` | 後段パスで `GetNewGlobalId()`（§3） |
| `type` | **`TYPE_POLYLINE_WITH_T_AXIS`**。`TYPE_POLYLINE` は proto 本文で DEPRECATED（4.0.0 で削除）と明記されている |
| `poly_line[].world_position` | `PointStruct.{x,y,z}` |
| `poly_line[].s_position` | `PointStruct.s`（road s） |
| `poly_line[].t_axis_yaw` | `atan2(PointStruct.ny, PointStruct.nx)` |

点列は `LaneSection::GetRefLineOSIPoints()` をレーンセクション順に連結し、継ぎ目の重複点
（同一 s）を 1 点に畳む。

`t_axis_yaw` の根拠を明示しておく。proto 本文は
`the yaw angle is equal to the angle of the normal to the reference line in the sampled point`
と定義しており、esmini 側の法線は
`RotateVec3d(point.h, point.p, point.r, 0.0, 1.0, 0.0, point.nx, point.ny, unused)`
（[`GT_RoadManager.cpp:8539`](../../src/road/GT_RoadManager.cpp#L8539)）で**ローカル +Y を姿勢で回した
ベクトル**である。+Y は t が増える側なので、そのまま `t_axis_yaw` になる。

> これは「符号の取り違えが起きやすく、かつ取り違えても形は正しく見える」箇所である。
> 実装時に**正の t のレーンが +90°側に来ることをデータで示す**こと（§9 の単体 1）。

### 2-2. LogicalLane — (road, laneSection, lane) につき 1 本

中央レーン（id 0、幅ゼロ）は面積を持たないので出さない。それ以外は**走行レーンに限らず全部出す**
（規格が「路面に隙間を作るな」と要求しているため）。

| フィールド | 値 |
| :-- | :-- |
| `id` | 後段パスで `GetNewGlobalId()` |
| `type` | §2-2-1 の写像 |
| `reference_line_id` | その道路の参照線 |
| `start_s` / `end_s` | `laneSection.GetS()` .. 次セクションの s（最終セクションは `road->GetLength()`）。`end_s > start_s` は非退化セクションなら常に成立 |
| `move_direction` | §2-2-2 |
| `source_reference` | `type = "net.asam.opendrive"`, `identifier = ["road_id:<id>", "road_s:<s>", "lane_id:<id>"]` |
| `physical_lane_reference` | §2-2-3 |
| `street_name` | `Road::GetName()`（空なら省略） |
| `traffic_rule[].speed_limit` | `Road::GetSpeedByS(start_s)`（道路単位。レーン単位は未パース） |

`source_reference` は規格本文だと `identifier[0]` が素の road id だが、**GT の既存 `osi_lane` と
同じ接頭辞付き形式に揃える**（[`GT_OSIReporter_Geometry.cpp:1335-1344`](../../src/osi/GT_OSIReporter_Geometry.cpp#L1335-L1344)）。
理由は `_gt_to_scene` の `lane_map` がこの形式を既にパースしており、同じ 1 本のパーサで
物理レーンと論理レーンの双方を引けるようにするため。規格本文からの逸脱としてここに記録する。

#### 2-2-1. 型の写像

| `Lane::LaneType` | `LogicalLane::Type` | 現行 `osi_lane` の Subtype |
| :-- | :-- | :-- |
| `DRIVING` / `BIDIRECTIONAL` | `TYPE_NORMAL` | `SUBTYPE_NORMAL` |
| `BIKING` | `TYPE_BIKING` | `SUBTYPE_BIKING` |
| `SIDEWALK` | `TYPE_SIDEWALK` | `SUBTYPE_SIDEWALK` |
| `PARKING` | `TYPE_PARKING` | `SUBTYPE_PARKING` |
| `STOP` | `TYPE_STOP` | `SUBTYPE_STOP` |
| `RESTRICTED` | `TYPE_RESTRICTED` | `SUBTYPE_RESTRICTED` |
| `BORDER` | `TYPE_BORDER` | `SUBTYPE_BORDER` |
| `SHOULDER` | `TYPE_SHOULDER` | `SUBTYPE_SHOULDER` |
| `ENTRY` / `EXIT` | `TYPE_ENTRY` / `TYPE_EXIT` | `SUBTYPE_ENTRY` / `SUBTYPE_EXIT` |
| `ON_RAMP` / `OFF_RAMP` / `CONNECTING_RAMP` | `TYPE_ONRAMP` / `TYPE_OFFRAMP` / `TYPE_CONNECTINGRAMP` | 同名 Subtype |
| **`MEDIAN`** | **`TYPE_MEDIAN`** | `SUBTYPE_OTHER`（情報が落ちている） |
| **`CURB`** | **`TYPE_CURB`** | `SUBTYPE_BORDER`（別物に丸められている） |
| **`RAIL`** / **`TRAM`** | **`TYPE_RAIL`** / **`TYPE_TRAM`** | どちらも `SUBTYPE_OTHER` |
| `ROADWORKS` / `SPECIAL1-3` / `NONE` | `TYPE_OTHER` | `SUBTYPE_OTHER` / `SUBTYPE_UNKNOWN` |

太字の 4 種は **`osi_lane` の Subtype 列挙に対応がなく、現行出力では区別が消えている**。
論理レーンはここで情報量が増える（副次的な利得）。

#### 2-2-2. `move_direction`

既存の `centerline_is_driving_direction` と同じ判定式を使う。

```cpp
// RHT: 負レーンは s 増加方向へ走る / LHT: 正レーンが s 増加方向
const bool along_s = (lane_id < 0) == (rule == RoadRule::RIGHT_HAND_TRAFFIC);

if (type == LANE_TYPE_BIDIRECTIONAL)        MOVE_DIRECTION_BOTH_ALLOWED
else if (!IsDrivingLikeType(type))          MOVE_DIRECTION_BOTH_ALLOWED  // 歩道など
else if (along_s)                           MOVE_DIRECTION_INCREASING_S
else                                        MOVE_DIRECTION_DECREASING_S
```

#### 2-2-3. `physical_lane_reference`

| レーンの所在 | `physical_lane_id` | 備考 |
| :-- | :-- | :-- |
| 通常路 | `lane->GetGlobalId()` | 1:1 |
| OSI 交差点の中 | **junction のグローバル id** | 融合された `TYPE_INTERSECTION` レーン。複数の論理レーンが同じ物理レーンを指す（規格が明示的に許容） |
| `TYPE_MEDIAN` / `CURB` / `TRAM` / `RAIL` | **出さない** | 規格本文: `For LogicalLanes without a correspondence to a Lane.Classification.Subtype ... this field has no value.` |

`start_s` / `end_s` は「論理レーン上の s」なので、論理レーン自身の `[start_s, end_s]` と同じ。

### 2-3. LogicalLaneBoundary — 物理境界を再利用せず合成する

**既存の物理境界を流用しない。** 理由は、流用できる単一のソースが存在しないためである。
`SetLaneBoundaryPoints()` は `n_roadmarks == 0` のレーンにしか `LaneBoundaryOSI` を作らず
（[`GT_RoadManager.cpp:8605-8703`](../../src/road/GT_RoadManager.cpp#L8605-L8703)）、路面標示のある
レーンは roadmark のライン点列しか持たない。しかも roadmark の t はレーン端と一致するとは限らず、
1 本のレーンに複数ラインが付くこともある。つまり「レーン端のポリライン」は**どちらの経路からも
一様には取れない**。

したがって (s, t) から直接合成する。ST 座標が構成的に正確になるという副次的な利点がある。

境界の置き場所は、レーンセクションの**レーン端すべて**である。

```
t が大きい側                                          t が小さい側
  |   lane +2   |   lane +1   |  (center)  |   lane -1   |   lane -2   |
  B4            B3            B2           B1            B0           B(-1)
```

`n` 本のレーンを持つセクションには `n + 1` 本の境界が立ち、隣接する 2 レーンは 1 本を共有する。

| フィールド | 値 |
| :-- | :-- |
| `id` | 後段パスで `GetNewGlobalId()` |
| `reference_line_id` | その道路の参照線（規格要件: レーンと同一であること） |
| `boundary_line[].s_position` | 参照線の s グリッドをそのまま使う |
| `boundary_line[].t_position` | `LaneSection::GetOuterOffset(s, lane_id)` に側の符号を付けたもの。中央は `GetCenterOffset(s, 0)` |
| `boundary_line[].position` | `Position::SetLanePos(road, lane, s, offset)` で解決した世界座標 |
| `passing_rule` | §2-3-1 |
| `physical_boundary_id[]` | その t 位置に物理境界（`GetLaneBoundaryGlobalId()` または roadmark ライン）があれば入れる。無ければ空（規格が空を許容） |

s グリッドを参照線から借りる点には制約がある。参照線の刻みはその曲率で決まっており、外側の
境界のほうが曲率半径が大きい／小さい。**5cm 要件が外側境界で破れうる**ので、S3 実装時に
`CheckAndAddOSIPoint` と同じ偏差判定を境界ごとに回して点を追加する（§10-2）。

#### 2-3-1. `passing_rule`

OpenDRIVE の `<roadMark @laneChange>` が `LaneRoadMark::GetLaneChange()` として取れる。

| `RoadMarkLaneChange` | `PassingRule` |
| :-- | :-- |
| `BOTH` | `PASSING_RULE_BOTH_ALLOWED` |
| `NONE` | `PASSING_RULE_NONE_ALLOWED` |
| `INCREASE` | `PASSING_RULE_INCREASING_T` |
| `DECREASE` | `PASSING_RULE_DECREASING_T` |
| roadmark 無し | `PASSING_RULE_OTHER` |

`PASSING_RULE_UNKNOWN` は規格本文が `must not be used in ground truth` としているので出さない。
標示の無い境界（路肩と縁石の間など）は `PASSING_RULE_OTHER` に落とす — これも本文が
明示している用法である。

### 2-4. 連結性

#### 2-4-1. 前後接続

**OpenDRIVE の predecessor/successor はどちらも s 方向で定義されており、OSI の
predecessor/successor も参照線方向で定義されている。反転は無い。** 直接写せる。

> `"End" is relative to the reference line, so connections at #end_s.`

RHT の正レーン（s 減少方向へ走る）では、OSI の `successor_lane` が**車両の後方**にあることになる。
これは規格どおりで、走行方向で読み替えてはいけない。

解決経路は 3 つある。

| ケース | 解決 | `at_begin_of_other_lane` |
| :-- | :-- | :-- |
| 同一道路内のセクション間 | `lane->GetLink(SUCCESSOR)->GetId()` を次セクションで引く | 常に `true`（次セクションの始端） |
| 道路間（junction 外） | 既存の `UpdateOSIRoadLane` 後半の前後セクション解決をそのまま流用 | 相手道路の contact point から決める |
| junction 経由 | `Junction::GetConnectionByIdx()` → `Connection::GetLaneLink()` | `Connection::GetContactPoint()` が `CONTACT_POINT_START` なら `true` |

3 番目が `osi_lane` では潰れている部分で、**論理レーン側では接続路レーンが個別に存在するので
交差点の中を 1 レーンずつ繋げる**。既存の DIRECT / VIRTUAL ジャンクション処理
（[`GT_OSIReporter_Geometry.cpp:73-220`](../../src/osi/GT_OSIReporter_Geometry.cpp#L73-L220)）が
同じ材料を同じ分岐で扱っているので、判定ロジックは流用できる。

#### 2-4-2. 横隣接

同一レーンセクション内では**隣接は常に全長**である。レーンの出現・消滅はレーンセクション境界で
しか起きず、論理レーンもそこで切れるからである。したがって `LaneRelation` の 4 つの s は
すべて当該レーンの `[start_s, end_s]` になり、「途中から隣接が始まる」ケースは構造上発生しない。

```
left_adjacent_lane  : lane_id + 1（0 を飛ばして +2）
right_adjacent_lane : lane_id - 1（0 を飛ばして -2）
```

中央レーンは論理レーンを持たないので、lane -1 と lane +1 は互いに隣接する。
`"Right" is in definition direction` なので **t が小さい側が right**、すなわち lane_id が小さい側である。
LHT でも反転しない。

#### 2-4-3. `overlapping_lane`

**本設計のスコープ外**（§10-1）。交差点内で経路が交差する区間の s レンジは幾何計算が要り、
RoadManager に概念がない。`repeated` なので空でも規格違反ではない。

### 2-5. Route

```
RouteSegment       ←→ (経路上の road, laneSection) の組 1 つ
  LogicalLaneSegment ←→ そのセクションで経路が許すレーン 1 本
```

`RouteLaneBand` が道路単位なのに対し `RouteSegment` はレーンセクション単位なので、**バンドを
セクション列へ展開する層**が要る（現状記録 §4-2 E/F）。展開のとき、バンドの `lanes` は
「出口端で評価されたレーン id」なので、セクションを遡るたびにレーンリンクで引き直す。
道路内で基準レーンを跨ぐとレーン id が振り直されるため、id をそのまま持ち回してはいけない。

s の向きは §2-4-1 とは逆に**走行方向で決める**。

```
進行が s 増加方向:  start_s = セクション始端,  end_s = セクション終端
進行が s 減少方向:  start_s = セクション終端,  end_s = セクション始端   ← start_s > end_s
```

最初のセグメントの `start_s` は自車の現在 s、最後のセグメントの `end_s` は終点 WP の s に詰める。

### 2-6. 自車が経路のどこにいるか — 3 層に分けて扱う

「経路に対する縦横オフセット」は 1 つの量ではない。**OSI に欄があるのは 1 層目だけ**である
（現状記録 §2-5）。層を混ぜると「規格の欄に GT 独自の意味を入れる」ことになるので分けて扱う。

| 層 | 量 | OSI の欄 | 本設計での扱い |
| :-- | :-- | :-- | :-- |
| **L1** | 論理レーン相対の s / t / 向き | **`LogicalLaneAssignment`（有る）** | §2-6-1。**S2.5 として実装する** |
| **L2** | 経路始点からの累積距離 | 無い | §2-6-2。telemetry へ出す。OSI へは出さない |
| **L3** | 経路レーン帯からの符号付き横距離 | 無い | §2-6-3。定義から設計が要る。**本設計のスコープ外** |

#### 2-6-1. L1 — `LogicalLaneAssignment`（実装する）

出す先は既存の emit 行の真横である。

```cpp
// GT_OSIReporter_Moving.cpp:977-979 の直後
const id_t assigned_lane_gid = ResolveMovingObjectAssignedLaneGlobalId(objectState.pos_);
obj_osi_internal.mobj->add_assigned_lane_id()->set_value(assigned_lane_gid);
obj_osi_internal.mobj->mutable_moving_object_classification()->add_assigned_lane_id()->set_value(assigned_lane_gid);

// ★追加
if (GetUseOsiLogicalLane()) {
    EmitLogicalLaneAssignment(obj_osi_internal.mobj->mutable_moving_object_classification(),
                              objectState.pos_);
}
```

値は追加の幾何計算なしで揃う。**論理レーンの参照線 s を road s と同一にした（§2-1）ことの
直接の見返り**である。

| OSI | GT 側 |
| :-- | :-- |
| `s_position` | `pos.GetS()` |
| `t_position` | `pos.GetT()` |
| `angle_to_lane` | `pos.GetHRelative()` |
| `assigned_lane_id` | 索引 `(pos.GetTrackId(), road->GetLaneSectionIdxByS(pos.GetS()), pos.GetLaneId())` |

**索引の引き方は `ResolveMovingObjectAssignedLaneGlobalId` とは違う**。既存のリゾルバは OSI 交差点の
接続路上にいる物体に対して**junction のグローバル id を返す**（物理レーンが 1 本へ融合されている
ため、それが正しい）。論理レーンは接続路レーンごとに存在するので、融合せず
`(road, laneSection, lane)` で直接引く。

> つまり L1 は **`signal:ego_lane` の join 欠け（交差点内で `assigned_lane_id` が `lane_map` に
> 無く、実測 171/15800 フレームで外れる）を論理レーン面で解消する**。物理レーン側の融合は
> upstream 由来の正しい振る舞いなので触らず、論理レーン面に穴のない経路を用意する形になる。

`repeated` の扱い: 規格は「5cm 以上重なったレーンには全部割り当てよ」と要求している。
初版は**各オブジェクトのキャッシュ済み走行レーン 1 本のみ**を出す。車体幅から隣接レーンへの重なりを
判定して複数出すのは L1-b として分けた（§10-7）。1 本しか出さないことは規格からの不足であり、
消費側が「跨いでいない」と誤読しうるので、**不足として §10 に明記する**。

対象は自車だけでなく `UpdateOSIMovingObject` を通る全オブジェクトである。同じコストで出るし、
「どの車がどの論理レーンにいるか」は経路上の競合判定に使える。

#### 2-6-2. L2 — 経路始点からの累積距離（telemetry へ）

`Position::GetRouteS()` は**使わない**。物理駆動の車では経路割り当て時の値で凍り、
更新するには `CalcRoutePosition()` を呼ぶしかないが、それは route の状態を書き換える
（現状記録 §4-3）。**観測が対象を変えてはいけない。**

代わりに `BuildOsiRoute()` の副産物として積み上げる。区間長は全セグメントで既に持っている。

```cpp
struct RouteProgress {
    double  s_along_route;   // [m] 経路始点からの累積距離
    double  route_length;    // [m] 経路全長
    size_t  segment_index;   // 自車が乗っている RouteSegment の index
    bool    on_route;        // どのセグメントにも乗っていなければ false
};
```

**OSI へは出さない。** 該当する欄がなく、`custom_state` のような自由欄へ入れると
「GT にしか読めない値が規格の欄に入っている」状態になる。出し先は VD telemetry の
`route_lane` ブロック（既存）とする。`signal:route_lane_conformance` の追加フィールドとして
扱い、**新しい signal は起こさない**。

`on_route == false` を必ず持つ。経路から外れているときに `s_along_route` を 0 や直前値で埋めると、
「出発点にいる」と区別できなくなる — `GetRouteS()` が今まさに陥っている形である。

#### 2-6-3. L3 — 経路レーン帯からの符号付き横距離（スコープ外）

`t_position`（L1）は「**今いるレーン**に対する横位置」であって「経路が許すレーンからどれだけ
外れているか」ではない。後者は `RouteLaneStatus.on_target_lane` が bool でしか持っていない。

符号付き距離にするには定義を決める必要がある。少なくとも次が未定である。

- 基準は `target_lanes` の**最近傍レーンの中心**か、**帯の最も近い端**か
- 帯の中にいるときは 0 なのか、最近傍中心までの距離なのか
- レーン幅が変化する区間での正規化（[m] か レーン幅比か）

**用途が決まっていないものに定義を与えると、後から意味が動く。** L1 と L2 が入れば
「どのレーンにいて、経路のどのレーンが許されているか」は外から判定できるので、
L3 は必要になった時点で用途と一緒に設計する。§10-8 に未実装として記録する。

---

## 3. id 採番 — 既存 id を 1 つも動かさないこと

RM / OSI のグローバル id は `CommonMini::GetNewGlobalId()` の単一の単調カウンタから出る。

**規律**: 論理レーン / 論理境界 / 参照線の id は、**静的 GT 構築の後段パスでのみ** `GetNewGlobalId()`
から引く。OpenDRIVE ロード中やシナリオ初期化中に引いてはならない。

- 守れば: 既存のレーン id・オブジェクト id は 1 つも動かず、ODR 適合の OSI ゴールデン
  （`lanes[].id` が生のグローバル id）も `_gt_to_scene` の `lane_map` join も無傷。
- 破ると: ODR ゴールデンが全滅し、`signal:ego_lane` の join が静かにずれる。

これは仕様ではなく規律なので、**ユニットテストで固定する**（§9 の単体 4）。
`ApplyAuthoredJunctionBoundaries` が同じ性質を既にコメントで根拠づけている。

索引（HVD 側が引く）は後段パスの副産物として持つ。

```cpp
// (road_id, lane_section_idx, lane_id) -> logical lane global id
std::map<std::tuple<id_t, unsigned, int>, uint64_t> g_logical_lane_index;
```

---

## 4. 静的パスのアルゴリズム

```
BuildOsiLogicalLanes(opendrive, static_gt):
    if (!GetUseOsiLogicalLane()) return;          // §7
    index.clear()

    # --- pass 1: 参照線 ---
    for road in roads:
        rl = static_gt.add_reference_line()
        rl.id = GetNewGlobalId()
        rl.type = TYPE_POLYLINE_WITH_T_AXIS
        for p in concat(section.GetRefLineOSIPoints() for section in road):
            if p.s == last_s: continue            # 継ぎ目の重複を畳む
            rl.add_poly_line(world={p.x,p.y,p.z}, s=p.s, t_axis_yaw=atan2(p.ny,p.nx))
        ref_line_id[road] = rl.id

    # --- pass 2: 境界 ---
    for road, section in sections:
        for edge_t in lane_edges(section):        # n+1 本
            lb = static_gt.add_logical_lane_boundary()
            lb.id = GetNewGlobalId(); lb.reference_line_id = ref_line_id[road]
            for s in s_grid(section) + refine_for_5cm(edge_t):
                lb.add_boundary_line(position=world(road,s,edge_t), s=s, t=edge_t(s))
            lb.passing_rule = map_passing_rule(roadmark_at(edge_t))
            lb.physical_boundary_id = physical_ids_at(edge_t)   # 無ければ空
            boundary_id[road, section, edge] = lb.id

    # --- pass 3: 論理レーン本体（接続は張らない） ---
    for road, section, lane in lanes where !lane.IsCenter():
        ll = static_gt.add_logical_lane()
        ll.id = GetNewGlobalId()
        ll.type = map_type(lane.GetLaneType())
        ll.reference_line_id = ref_line_id[road]
        ll.start_s, ll.end_s = section_range(section)
        ll.move_direction = map_move_direction(lane, road.GetRule())
        ll.source_reference = odr_ref(road, section, lane)
        ll.physical_lane_reference = map_physical(lane)          # §2-2-3
        ll.left_boundary_id  = [boundary_id[.., inner_or_outer(lane, LEFT)]]
        ll.right_boundary_id = [boundary_id[.., inner_or_outer(lane, RIGHT)]]
        index[road.id, section_idx, lane.id] = ll.id

    # --- pass 4: 連結性（全 id が確定した後）---
    for each logical lane:
        successor_lane   += resolve_s_forward(...)   # §2-4-1
        predecessor_lane += resolve_s_backward(...)
        left_adjacent_lane / right_adjacent_lane += same_section_neighbours(...)  # §2-4-2
```

4 パスに分ける理由は、接続が id を前方参照するためである。`UpdateOSIRoadLane` が
「全レーンを作ってから連結を解決する」のと同じ理由・同じ構造にしてある。

---

## 5. HVD 側 — `route` の構築とキャッシュ

```cpp
// GT_esmini/src/osi/RouteToOsiRoute.{hpp,cpp}   純関数・ログを出さない
osi3::Route BuildOsiRoute(const roadmanager::Route&     route,
                          const roadmanager::Position&  ego,
                          const gt_esmini::RouteLanePlan& plan,
                          const LogicalLaneIndex&       index,
                          uint64_t                      route_id);
```

`UpdateFromObjectState(egoObj)` から `egoObj->pos_.GetRoute()` に届く。VD の
`ControllerVirtualDriver` を経由しない（手動運転や DefaultController の ego でも経路があれば出る）。

**キャッシュ**: `BuildRouteLanePlan` は失敗経路で `LaneIndependentRouter` の再探索まで走るので、
毎フレーム呼んではいけない。キーには **`Route` のポインタやクローンを使わない**
（`CopyRoute` のクローンは別アドレスになる）。代わりに経路の署名を使う。

```cpp
struct RouteSignature {                 // 全部 minimal_waypoints_ から取れる
    size_t n_waypoints;
    id_t   first_road, last_road;
    int    first_lane, last_lane;
    double first_s,    last_s;
};
```

署名が変わったときだけ `BuildRouteLanePlan` + `BuildOsiRoute` を回し、`route_id` を進める。
`route_id` は規格が `must be unique within all route messages exchanged with one traffic participant`
と要求しているので、**単調増加のカウンタ**にする（再計算のたびに +1）。

**`route` を毎フレーム載せるか** — 載せる。「変化時だけ送る」は採らない。フィールドが
間欠的に消えると、受け側は「経路が無い」と「今回は送っていない」を区別できない。
値が入っていないこと自体が情報になってしまう形は、この repo が既に一度踏んだ失敗
（3 値で持つべきチャネルを 2 値で持った件）と同じである。サイズは §6 で構造的に解く。

---

## 6. HVD の UDP 8192 B 上限を塞ぐ

現状、`GT_HostVehicleReporter::Send()` は `serialized_data_.size > MAX_UDP_DATA_SIZE` のとき
`LOG_WARN` を出して**そのフレームを丸ごと捨てる**
（[`GT_HostVehicleReporter.cpp:474-480`](../../src/osi/GT_HostVehicleReporter.cpp#L474-L480)）。
`route` は経路長に比例するので、この上限に当たる。

規模の目安: `LogicalLaneSegment` 1 本が Identifier + double 2 本でおよそ 25〜30 B。
20 セクション × 並列 3 レーン = 60 本で約 1.8 KB、100 セクション × 4 レーンなら約 12 KB。
**大きい地図の経路は実際に上限を超える。**

**解決はフラグメンテーションの実装とする。** 送信側を GroundTruth 側と同じ形に揃えるだけで、
**受信側は両方とも既に対応済み**である。

- `GT_OSIReporter.cpp:373-382`: counter は 1 始まり、最後のパケットは負の counter。
- `osi_bridge.py` `_OSIProtocol.datagram_received`: `counter == 0` を単一パケット、
  `counter >= 1` を分割として扱う。**GT ストリームと HVD ストリームで同じクラスを使っている。**
- `DriverScript/realdriver/udp_common.py`: 同じプロトコルを 0 始まり / 1 始まり両対応で実装。

したがって送信側 ~40 行の追加で、消費側の変更はゼロになる。単一パケットに収まる場合の
`counter = 0` は現状の挙動なので、既存の受信側は何も変わらない。

> 経路の先読み区間で打ち切る案（horizon truncation）は採らない。OSI に「ここで切った」を
> 表す欄が無く、切った経路と本当に終わる経路が区別できないため。必要になったら
> 製品判断として別途決める。

### 6-1. 実装（2026-09-24、S0 とは別コミット）

分割そのものは**純関数に切り出した**。`Send()` の中に直接書くと、この経路を踏めるのは
`route` が載る S4 以降になり、それまで**一度も実行されないコードが常設ゲートの下に居座る**。

```cpp
// gt_esmini/osi/GT_HostVehicleReporter.hpp
struct UdpChunk { int counter; unsigned int offset; unsigned int datasize; };
std::vector<UdpChunk> PlanHostVehicleUdpChunks(unsigned int total_size, unsigned int max_payload);
```

`Send()` はこの計画を舐めるだけになった。**収まるメッセージは今までどおり
`counter == 0` の単一パケット**で出る（消費側の挙動を一切変えない）。超えた場合だけ
`1, 2, … N` を振り、最後を負にする。

検証は `GT_esmini/test/unit/osi/test_HostVehicleUdpChunks.cpp`（傘バイナリ、5 テスト）。
両極性を実データで踏んでいる — 1 B / 8192 B は 1 パケット `counter=0`、8193 B は 2 パケットで
末尾 `-2`、40,000 B は 5 パケットで末尾 `-5`、8192×3 は空の末尾パケットを作らない。
`counter` の 1 始まりと末尾負値は `osi_bridge.py` / `udp_common.py` の双方が再組み立ての
起点と終端に使っているので、どちらかの端で 1 ずれるとストリームが黙って止まる。

送信途中で失敗したときはそのフレームを**打ち切る**（短いパケットを出さない）。負の counter を
見なかった受信側は次フレームで再同期するだけだが、短いパケットは壊れた
HostVehicleData として dispatch されてしまう。

---

## 7. フラグの設計と、その寿命

### 7-1. 「ゴールデンを守るため」は論拠にならない

当初、静的 GroundTruth のバイト数を assert している upstream テスト 10 箇所を理由に
「既定 OFF が前提条件」と書いたが、これは誤りだった。

**該当する 7 テストは GT では既にスキップされている。**
[`scripts/run_tests.sh:138-159`](../../../scripts/run_tests.sh#L138-L159) が理由を記録している —
GT は OSI 3.7.0（proto3 explicit presence）をリンクしており、reporter が明示的に 0.0 を入れた
フィールドが約 9 B ずつ直列化されるため、upstream の OSI 3.5.0 とバイト数が構造的に一致しない
（cut-in_simple で実測 11288 B vs 7661 B、内容は一致）。OSI 3.7.0 への移行は意図的な恒久差分なので、
これらの厳密サイズ assert は GT では満たせない、という判断で issue #37 G4 以来スキップされている。

つまり**守るべき assert はそもそも走っていない**。ODR 適合の OSI ゴールデンはホワイトリスト抽出で
新フィールドを拾わず、id を動かさない限り（§3）無傷。回帰ベースラインは matcher 名と status しか
持たない。**フラグの有無でゴールデンは 1 つも変わらない。**

### 7-2. では、フラグを置く理由は何か

3 つある。いずれも「テストを避けるため」ではない。

1. **段階的に着地させるため。** S1〜S2 の時点では境界が無く、S4 を S3 より先にやれば
   `route` が境界の無い論理レーンを指す。規格として不完全な中間状態を、既定で外に出したくない。
2. **A/B 帰属のため。** 同一バイナリで ON/OFF を切り替えられないと、挙動差やクラッシュが
   本機能由来かを二分探索できない。計器に新しい出力を足すときの既知の落とし穴である。
3. **ペイロードの選択権のため。** 静的 GT が **1.3〜2.7 倍**になる見込み（S0 実測に基づく投影、
   現状記録 §5-5）。論理レーンを読まない消費側にとっては純粋な増分でしかない。

### 7-3. 既定値と、それを変える条件

| 段 | 既定 | 理由 |
| :-- | :-- | :-- |
| S0〜S2 | **OFF** | 理由 1（モデルが不完全） |
| S3 完了コミット以降 | **ON**（`GT_OSI_LOGICAL_LANE=0` で opt-out） | モデルが規格として完結する。**既定 OFF のまま残さない** |

既定 OFF のまま置き去りにすると、どのゲートも通らない経路になって腐る。この repo には
「構造上一度も赤にできないゲート」を抱え込んだ前例があり、既定 OFF の未検証出力は同じ形をしている。
S5 は **ON 状態を常設ゲートで踏む**（§9）。

**この既定 ON 判断を覆しうる唯一の入力は、S0 のサイズ実測である。** 静的 GT が実測で 3 倍を超え、
かつ OSI 記録が日常のワークフローだと分かった場合は、既定 OFF（opt-in）に倒してよい。
そのときも「ゲートが ON を踏む」ことは変えない。

> **2026-09-24（S0 実測、`scripts/probe_osi_logical_lane_size.py`）: 覆す条件に当たらなかったので
> 既定 ON を維持する。** 代表 5 資産の投影は静的 GT 全体に対して **1.26x〜2.73x**（最大は
> fabriksgatan）で、**3 倍に届く資産は 1 つも無い**。最大規模の multi_intersections でも 2.12x で、
> 増分は静的 262 KB → 約 556 KB。しかもこれは**ロード時 1 回**の量で、毎フレーム流れる動的側は
> 実測 2.1 KB のまま変わらない（論理レーン面は静的 GT にしか載らない）。GroundTruth の UDP は
> チャンク済みなので上限にも当たらない。
>
> 条件の残り半分「OSI 記録が日常のワークフローか」は評価するまでもないが、記録しておく:
> 仮に日常だとしても、1 実行あたり 0.3 MB の一度きりの増分である。
>
> **ただし S3 で反転する前に確かめること**: 上の投影は S0 時点で emit がまだ無いためのもので、
> S3 の実装後に**同じプローブを回して実測に置き換える**。投影が外れる向きは 2 つあり、
> どちらも交差点にある — 接続路レーンの点密度を本線と同じと仮定していること（短いので
> 過大評価のはず）と、論理境界の共有がどこまで効くか（過小評価になりうる）。

実装は既存 idiom をそのまま使う（`GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY` と同型）。

```cpp
bool GetUseOsiLogicalLane()
{
    if (!g_inited) { g_enabled = EnvIsTruthy(std::getenv("GT_OSI_LOGICAL_LANE")); g_inited = true; }
    return g_enabled;
}
```

---

## 8. 段取り

各段で「何が動く状態になるか」を先に書く。

| 段 | 内容 | 工数 | 終わると何が動くか |
| :-- | :-- | :-- | :-- |
| ~~**T**~~ **✅ 完了 2026-09-24** | HVD の UDP 分割送信（§6）。**何にも依存しない独立タスク** | 0.5 日 | 8192 B を超える HostVehicleData が落ちなくなる。論理レーンとは無関係に単体で価値がある |
| ~~**S0**~~ **✅ 完了 2026-09-24** | サイズ・レーン数・境界点数の実測プローブ、env ゲート（既定 OFF）、空の後段パス、CMake の R1 承認 | 0.5 日 | **ON/OFF で 1 バイトも変わらないことが実証できる**。§7-3 の既定値判断に使う実測値が出る → §8-3 |
| **S1** | 参照線 + 論理レーン本体（境界・接続なし） | 2〜3 日 | `GroundTruth.logical_lane[]` が出る。xodr のレーンと 1:1 対応していることを OSI 直読で確認できる。**交差点内レーンが初めて個別に見える** |
| **S2** | 連結性（pred / succ / adjacent） | 2〜3 日 | **論理レーンの列として経路をたどれる**。`route` の参照先が実在し連結していることが保証される |
| **S2.5** | L1 `LogicalLaneAssignment`（§2-6-1） | 0.5 日 | **全オブジェクトの論理レーン相対 s / t / 向きが出る**。交差点内でも車線が個別に引ける（`signal:ego_lane` の join 欠けが論理レーン面で埋まる） |
| **S3** | 論理境界（ST 化・合成・`passing_rule`）＋ 既定 ON へ反転 | 2〜3 日 | **規格の必須参照が全部埋まる**。外部の OSI 準拠チェッカを通せる。サイズはここで最大になる |
| **S4** | HVD `route` ＋ L2 経路進捗（§2-6-2）。**T が入っていること** | 2 日 | **目的達成**。`capability_model.md` W4 の `route` が閉じる。S2.5 と揃えば `route` × L1 の合成で経路相対位置が外から出せる |
| **S5** | signal 登録 / matcher / ゲート常設化（§9） | 1.5〜2 日 | **回帰で守られる**。両極性を実証してから緑にする |

**合計 11〜15 日**（`overlapping_lane` / L1-b / L3 を除く）。

**S4 は S2 も S3 も待たずに実施できる**（§8-1）。`right/left_boundary_id` は repeated なので、
**S1 完了時点で** `route` → `logical_lane` の参照は成立する。厳密な OSI バリデータには落ちるが、
参照が無言で壊れている状態にはならない。目的を最短で出すなら **S0(+T)→S1→S4** で
6〜7 日、その後 S2 → S2.5 → S3 → S5。S3 までは既定 OFF を維持する（§7-3）。

### 8-0. S0 の結果（2026-09-24）

置いたもの:

| 追加/変更 | 何 |
| :-- | :-- |
| 新規 `GT_esmini/include/gt_esmini/osi/GT_OsiLogicalLane.hpp` | `LogicalLaneKey` / `LogicalLaneIndex` / フラグ / `BuildOsiLogicalLanes()` / `GetLogicalLaneIndex()` の宣言。namespace は `gt_esmini::osi` |
| 新規 `GT_esmini/src/osi/GT_OSIReporter_LogicalLane.cpp` | フラグ実体と**何も emit しない**後段パス。スワップゾーン側 |
| 変更 `GT_esmini/src/osi/GT_OSIReporter.cpp` | `CreateOSIStaticGroundTruthFromODR()` の `ApplyAuthoredJunctionBoundaries()` 直後で呼ぶ（§1 の注） |
| 変更 `EnvironmentSimulator/Modules/ScenarioEngine/CMakeLists.txt` | 既存スワップブロックへ 4 行（R1、承認済み） |
| 新規 `GT_esmini/test/unit/osi/test_OsiLogicalLane.cpp` | 傘バイナリへ登録。フラグ両極性と S0 不変量（索引が空） |
| 新規 `scripts/probe_osi_logical_lane_size.py` | 実測プローブ。出力 `test_results/osi_logical_lane/s0_size_probe.json` |

実測（5 資産、それぞれ OFF / ON を別プロセスで 2 回ロード）:

- **静的 GroundTruth は OFF と ON で SHA-256 まで一致**した（`.osi` 第 1 レコード = static + dynamic、
  e6mini で 223,761 B）。毎フレームの外部 GroundTruth も一致。
- **それが「フラグが読まれていないだけ」ではないことを同時に示した。** バイト一致だけなら
  死んだフラグでも成立するので、ON のときだけ出る後段パスのログ行を負の対照に使い、
  OFF 5/5 で不在・ON 5/5 で存在を確認している。
- サイズの内訳と投影は現状記録 §5-5。**静的 GT の増分は 1.26x〜2.73x で 3 倍に届かない**ため、
  §7-3 の「S3 で既定 ON へ倒す」は維持（§7-3 の注）。

**S1 へ引き継ぐ発見**（いずれも S0 で実測したもの）:

1. **後段パスの置き場は `CreateOSIStaticGroundTruthFromODR()`**（§1 の注）。`UpdateOSIStaticGroundTruth()`
   は毎フレーム走る別物で、そこに吊ると `GetNewGlobalId()` がフレームごとに消費される。
2. **交差点が実装量の主な塊であることが数で裏付いた。** osi lane → 論理レーン相当は
   fabriksgatan 25 → 44、multi_intersections 171 → 242。**増えた分はすべて接続路レーン**で、
   これらには今 `lane_boundary` も centerline も無い（融合されているため）。つまり S1 で
   交差点内の論理レーンを出すとき、**流用できる既存 OSI 点が 1 つも無い**。
3. **`obj_osi_internal.static_gt` は OSIReporter のコンストラクタで確保される**（静的初期化時ではない）。
   ユニットテストから後段パスを直接叩くと null になりうるので、S1 以降も null ガードを外さないこと。
4. **`.osi` 第 1 レコードが静的 GroundTruth を読む唯一の口である。** `SE_GetOSIGroundTruth` は
   初期化後は `dynamic_gt` のコピーしか返さない（`UpdateOSIGroundTruth` の else 分岐）ので、
   静的側の回帰をこれで測ろうとすると**約 2 KB の動的ペイロードを比べているだけになる**。
   S0 のプローブは最初これを踏んで、lane 数 0 のまま「一致」を報告した。プローブ側には
   「測ったレコードに lane が 1 本も無ければ FAIL」を入れてある。

---

### 8-1. 依存関係 — 逐次なのは S0 → S1 までで、その先は扇形に開く

```
T  (HVD の UDP 分割送信・§6)   ← 何にも依存しない。単独で先行できる
S0 (足場・計測)
    └ S1 (参照線 + 論理レーン + 索引)     ← 唯一のボトルネック
         ├ S2   (連結性)
         ├ S2.5 (L1 assignment)
         ├ S3   (境界)
         └ S4   (route + L2)              ← T が入っていること
              ↓
             S5 (常設化)   ← 上の全部に依存
```

**S2 / S2.5 / S3 / S4 はいずれも S1 にしか依存しない。互いには依存しない。** 根拠は
「何を必要とするか」を辿れば出る。

| 段 | 必要とするもの | S2（連結性）を必要とするか |
| :-- | :-- | :-- |
| S2.5 | 索引（S1）と `Position` の s/t/h だけ | **しない** |
| S3 | 論理レーンの `[start_s, end_s]` と参照線の s グリッド（S1） | **しない** |
| S4 | 索引（S1）と `RouteLanePlan`。バンドのセクション展開は **RoadManager のレーンリンク**を辿るのであって、OSI の論理レーン連結は読まない（§2-5） | **しない** |

**T（UDP 分割送信）は論理レーンと無関係**な純粋な転送層の修正であり、S0 より前にでも着手できる。
S4 の前に入っていればよい。

ただし S4 を S2 より先に出す場合の**品質上の欠け**は記録しておく。規格は
`Consecutive segments should be connected without gaps` と述べており、消費側がその連続性を
検証する手段は `predecessor_lane` / `successor_lane` である。S2 が無い段階の `route` は
**参照は健全だが連続性が検証できない**。壊れてはいないが、外部の消費側に出すなら S2 を先に入れる。

### 8-2. 「レーンのどこにいるか」と「経路のどこにいるか」は別の段で揃う

L1（S2.5）だけでは経路相対位置は出ない。`route`（S4）が無ければ「この論理レーンが経路の
どのセグメントか」を引けないからである。逆に `route` だけでも出ない。**この 2 つが揃って
初めて経路相対位置が合成できる**（§2-6）。どちらを先にやっても構わないが、片方だけで
「経路相対が出た」と報告しないこと。

### ファイル構成

| 追加/変更 | パス | 種別 |
| :-- | :-- | :-- |
| 新規 | `GT_esmini/src/osi/GT_OSIReporter_LogicalLane.cpp` | **スワップゾーン側**（`obj_osi_internal` を触るため。§8 R1） |
| 新規 | `GT_esmini/include/gt_esmini/osi/GT_OsiLogicalLane.hpp` | 索引型 + `GetUseOsiLogicalLane()` の宣言 |
| 新規 | `GT_esmini/src/osi/RouteToOsiRoute.cpp` | GT_esminiLib 側（純関数、`obj_osi_internal` を触らない） |
| 新規 | `GT_esmini/include/gt_esmini/osi/RouteToOsiRoute.hpp` | |
| 変更 | `GT_esmini/src/osi/GT_OSIReporter.cpp` | 後段パスの呼び出し 1 行 |
| 変更 | `GT_esmini/src/osi/GT_OSIReporter_Moving.cpp` | L1 の**呼び出しのみ** 4 行（`:979` の直後）。本体は `GT_OSIReporter_LogicalLane.cpp` 側に置く — 同ファイルは `lineage:gt_osireporter` のフォーク系譜なので、inbound 差分を増やさないため |
| 変更 | `GT_esmini/src/osi/GT_HostVehicleReporter.cpp` | `route` 充填 + `Send()` の分割送信 |
| 変更 | `GT_esmini/src/control/virtualdriver/VirtualDriverTelemetryJson.cpp` | L2 経路進捗を `route_lane` ブロックへ追加（~8 行） |
| 変更 | **`EnvironmentSimulator/Modules/ScenarioEngine/CMakeLists.txt`** | **R1 承認が要る 4 行**（新 .cpp をスワップリストへ）。**2026-09-24 承認・実施済み**（S0） |
| 変更 | `GT_esmini/CMakeLists.txt` | `RouteToOsiRoute.cpp` を `GT_OSI_SOURCES` へ |
| 変更 | `GT_esmini/test/CMakeLists.txt` | 新ユニットテストの登録 |

R1 の当たりは `ScenarioEngine/CMakeLists.txt` の 4 行のみ。`OSIReporter.hpp` は触らない
（メンバ関数ではなく GT 自由関数として書く）。

> **2026-09-24（S0 実装時に是正）**: この 4 行は**新規の R1 例外ではなく既存例外の拡張**である。
> 同ファイルには既に `# GT_esmini Modification: Swap OSIReporter` ブロックがあり、
> `GT_OSIReporter*.cpp` を 7 本 `list(APPEND)` している。8 本目を足す形になる。
> 新ファイルは upstream に対応物が無いので `fork_sync_manifest.yaml` の
> `lineage:gt_osireporter` の `fork_paths` には**入らない**（GT-original 扱い、
> `GT_HostVehicleReporter.cpp` と同じ）。`check_core_census.py` は CMakeLists.txt を
> 追跡していないため行数予算にも当たらない。

---

## 9. 検証設計

### 単体（`GT_esmini/test/unit/osi/test_OsiLogicalLane.cpp`、傘バイナリへ）

1. **`t_axis_yaw` の極性** — 正の t 側のレーン中心が、参照線点の `t_axis_yaw` 方向にあることを
   世界座標で確認する。符号を反転させると落ちること（負の対照）まで示す。
2. **`move_direction` の 4 象限** — (RHT, LHT) × (正レーン, 負レーン) で
   `INCREASING_S` / `DECREASING_S` が入れ替わること。
3. **`start_s > end_s` の向き規約** — s 減少方向へ進む経路で `RouteSegment` の
   `start_s > end_s` になること。正順の経路では逆になること（両極性）。
4. **id 採番の規律** — 論理レーンを ON にしても**既存の `lane[].id` が 1 つも変わらない**こと。
   OFF 時の id 集合と ON 時の id 集合を突き合わせる。**この機能の一番重要な不変量。**
5. **OFF の無害性** — `GT_OSI_LOGICAL_LANE=0` で `logical_lane_size() == 0` かつ
   `reference_line_size() == 0` かつ静的 GT のバイト数が OFF ビルドと一致すること。
6. **参照の閉包** — すべての `route.lane_segment[].logical_lane_id` が `logical_lane[]` に実在し、
   すべての `logical_lane[].{left,right}_boundary_id` が `logical_lane_boundary[]` に実在し、
   すべての `logical_lane_assignment[].assigned_lane_id` が `logical_lane[]` に実在すること。
   **これが「参照が無言で壊れていない」の機械化である。**
7. **L1 の交差点カバレッジ** — OSI 交差点の接続路を走行中、`logical_lane_assignment` が
   **junction id ではなく接続路レーンの論理レーン id** を指すこと。物理側の
   `assigned_lane_id` が junction id のままであることも同時に確認する（片方だけ直すと
   既存の消費側が壊れる）。`signal:ego_lane` の join 欠けが埋まったことの直接の証拠になる。
8. **L1 の s/t と Position の一致** — `s_position == pos.GetS()`、`t_position == pos.GetT()` が
   全フレームで成立すること。参照線 s を road s と同一にした設計判断（§2-1）の不変量。
9. **L2 の生存性** — 物理駆動で走らせたとき `s_along_route` が単調増加すること。
   **`Position::GetRouteS()` が同じ走行で凍ることも併せて示す**（現状記録 §4-3 の負の対照。
   「凍る値を使っていない」ことを実証しないと、将来また同じ欄に手が伸びる）。
   経路外へ出したとき `on_route == false` になること。

### 観測量（`signal_catalog.yaml`）

| id | title | exposure | emit |
| :-- | :-- | :-- | :-- |
| `logical_lane_topology` | 論理レーンの網（id / 型 / 前後・隣接） | `[osi]` | `GT_OSIReporter_LogicalLane.cpp` |
| `logical_lane_assignment` | オブジェクトの論理レーン相対 s / t / 向き（L1） | `[osi]` | `GT_OSIReporter_LogicalLane.cpp` |
| `ego_route_lane_segments` | 自車の経路（論理レーン区間の列） | `[hvd]` | `GT_HostVehicleReporter.cpp` |

L2（経路進捗）は**新しい signal を起こさない**。既存の `signal:route_lane_conformance` の
`route_lane` ブロックに追加フィールドとして載せる（§2-6-2）。OSI に欄が無い量を signal 台帳の
`[osi]` 面へ登録すると、台帳の canonical が OSI であるという前提が崩れるため。

`signal:route_lane_conformance` は telemetry 面のまま残す。**同じ量を 2 面で持つのではなく、
「VD が内部でどう見ているか」（telemetry）と「外へ何を宣言しているか」（HVD / GroundTruth）を
別の観測量として持つ。** この 2 つが食い違うことこそ検出したい事象なので、統合してはいけない。

### matcher とゲート

新 matcher 1 本 `route_matches_plan`（HVD の `route` と telemetry の `route_lane` が同じ経路を
指していることを判定）。`namespaces.yaml` の `matcher` は**列挙型の `id_pattern`** なので、
パターンと `count: 35` の更新が必須である（更新しないと lint が新 id の辺を拒否する）。

常設化は既存の `gate:route-lane-regression`（`route_lane_batch.yaml` 6 シナリオ）に
`--osi` 付きで相乗りさせる。新規バッチは作らない。**ゲートは ON 状態を踏む。**

ベースラインの凍結は、3 回連続実行で自己決定論性を確認し、かつ比較器が負の対照で発火することを
示してから行う。

### ODR 適合

`run_odr_conformance.py` の OSI 抽出に `logical_lane_count` / `reference_line_count` /
`logical_lane_boundary_count` を追加する（`DUMP_POLYGONS` と同じ opt-in 形にして、
フラグ無しのフィクスチャは byte-identical に保つ）。ゴールデンの更新は S5 で 1 回だけ行う。

---

## 10. 既知の非充足と将来課題

1. **`overlapping_lane` を出さない。** 交差点内で経路が交差する区間の s レンジ。幾何計算が要り、
   RoadManager に概念がない。`repeated` なので規格違反ではないが、交差点の competing path を
   OSI から読む消費側には情報が足りない。+2〜3 日で別途。
2. **境界の 5cm 精度が参照線の s グリッド依存。** §2-3 のとおり、外側境界では偏差が広がりうる。
   S3 で境界ごとの偏差判定を入れるが、入れ損ねても「それらしい線」は出るため症状が出にくい。
   単体テストで最大偏差を直接測ること。
3. **Z 方向 2cm 精度を保証しない。** `OSI_MAX_LATERAL_DEVIATION` は XY 平面の判定であり、
   勾配・カントのある道路では Z 誤差が規格値を超えうる。カント厳密化と同じ土俵の課題。
4. **速度制限が道路単位。** OpenDRIVE の `<lane><speed>` はパーサが読んでいない。
   `traffic_rule[].speed_limit` には `Road::GetSpeedByS()` の値が入る。
5. **連続レーンの併合をしない。** 規格は同型・単一後続のレーンを 1 本の論理レーンへ併合することを
   許しているが、行わない。論理レーンは常に 1 レーンセクションで切れる。消費側から見ると
   セグメント数が多くなるだけで、意味は変わらない。
6. **`source_reference` が規格本文の素の id 形式ではなく GT の接頭辞付き形式。** §2-2。
7. **L1 の割り当てが 1 レーンのみ（L1-b 未実装）。** 規格は「5cm 以上重なったレーンには全部
   割り当てよ」と要求しているが、初版はキャッシュ済み走行レーン 1 本しか出さない。
   **車線変更の途中でも 1 本しか出ないので、消費側が「跨いでいない」と誤読しうる。**
   車体幅と隣接レーン境界から重なりを判定すれば出せる（+0.5 日）。規格からの明確な不足である。
8. **L3（経路レーン帯からの符号付き横距離）を出さない。** §2-6-3。OSI に欄がなく、基準の取り方も
   未定。`on_target_lane` の bool と L1 の `t_position` で「どのレーンにいて経路は何を許すか」は
   外から判定できるので、必要になった時点で用途と一緒に設計する。
9. **L2 を OSI へ出さない。** §2-6-2。経路始点からの累積距離に該当する欄が OSI に無いため
   telemetry 止まり。OSI しか読まない消費側からは経路進捗が見えない。

---

## 11. 着手前に決めること

| # | 決めること | 提案 |
| :-- | :-- | :-- |
| 1 | `ScenarioEngine/CMakeLists.txt` への 4 行（R1 例外） | **決着（2026-09-24 承認、S0 で実施）**。既存スワップブロックの拡張（§8 の注を見よ）。前例は RoadManager/CMakeLists.txt への odr_side **10 本**追加（2026-07-02 承認。調査時点で「6 本」と書いていたが実数は 10） |
| 2 | 知識グラフのノード型 | **決着（2026-09-24 ユーザー判断）**: 既存の `spine-work` 名前空間へ `spine-work:osi-logical-lane` として起こす。`feature:F10` は採らない（`F1..F9` はユーザーに見える機能で本件と性格が違ううえ、凍結体系の `id_pattern` 拡張が要る）。face-1 work-item 名前空間の新設も採らない（実体 0 件。`spine-work` は face タグが "3" だが `ego-anchor-face1-migration` / `osi-assigned-lane-driving` という face-1 の実体を既に 2 件収容している） |
| 3 | `overlapping_lane` をスコープに入れるか | 入れない（§10-1）。必要なら別工程 |
| 4 | S3 完了時に既定 ON へ倒すか | 倒す（§7-3）。S0 の実測で 3 倍超かつ OSI 記録が日常なら再検討 |
| 5 | L1-b（車線跨ぎの複数割り当て）を初版に入れるか | 入れない（§10-7）。**ただし車線変更の検証に L1 を使うなら初版から必要**。用途次第なので S2.5 着手時に再判定する |
| 6 | L3（経路帯からの符号付き横距離）の用途 | 未定のうちは設計しない（§10-8）。「誰が何のために読むか」が決まった時点で基準の取り方を決める |
