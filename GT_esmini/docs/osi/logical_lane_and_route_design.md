# OSI 論理レーンと HostVehicleData.route — 実装設計

> ステータス: **全段完了（T / S0 / S1 / S4 / S2.5 / S2.5b / S3 / S2 / S5、2026-09-24）。リリース待ち**。
> 現状と規格の突き合わせは
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
> [`CreateOSIStaticGroundTruthFromODR()`](../../src/osi/GT_OSIReporter.cpp#L503) で、
> `UpdateOSIGroundTruth()` の `!osi_initialized_` 分岐から**1 度だけ**呼ばれる。
> `UpdateOSIStaticGroundTruth()`（[`:576`](../../src/osi/GT_OSIReporter.cpp#L576)）は別物で、
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
| `traffic_rule[].speed_limit` | `Road::GetSpeedByS(start_s) * 3.6`（**km/h へ変換**）。`value_unit = UNIT_KILOMETER_PER_HOUR`。走行系レーンにのみ出す |
| `traffic_rule[].traffic_rule_validity` | 走行方向に沿って `start_s → end_s`。`DECREASING_S` のレーンでは両者が入れ替わる。`BOTH_ALLOWED` では出さない |

> **単位（2026-09-24、S1 実装時に追記）**: OSI の `TrafficSignValue.Unit` に**速度の m/s は無い**
> （速度は km/h と mph の 2 つだけ）。一方 `Road::GetSpeedByS()` は m/s を返す — パーサが
> `<speed @unit>` の km/h も mph も m/s へ正規化してしまうためである
> （[`GT_RoadManager.cpp:3990-3999`](../../src/road/GT_RoadManager.cpp#L3990)）。
> 変換せずに km/h タグで出すと**全ての制限速度が 3.6 分の 1 になり、しかも速度制限として
> 成立して見える**。`virtual_junction_23.xodr`（`<speed max="50" unit="km/h"/>`）を
> 単体テストの固定値に使い、出てくる数が 13.9 ではなく 50 であることを直接押さえてある。

> **走行系レーンに限る理由**: 道路単位の制限速度を歩道や中央分離帯に付けても意味が無い。
> 判定は `move_direction` と同じ `LANE_TYPE_ANY_DRIVING` ビットで行う。

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
| `boundary_line[].s_position` | 参照線の s グリッドを**種**にして、偏差で再帰分割した s |
| `boundary_line[].t_position` | `Position::SetLanePos(road, lane, s, SIGN(lane)*width/2)` の `GetT()`。`GetInnerOffset(s,k) == GetOuterOffset(s,k-sign(k))` が恒等式なので、隣接レーンの見る t は構成的に一致する |
| `boundary_line[].position` | 同じ `SetLanePos` の世界座標。**レーンの上で**評価するのが肝で、`SetTrackPos` を edge の t で呼ぶと `Track2Lane` がどちらのレーンに落ちるかで z が変わる（下の高さ分割） |
| `passing_rule` | §2-3-1 |
| `physical_boundary_id[]` | 同じ edge にある物理境界。**`gt->lane_boundary()` に実在する id だけ**を入れる（後段パスなので既に完成しており、ここで会員判定すれば参照の閉包が構成的になる）。無ければ空（規格が空を許容） |

**s グリッドは参照線のものだけでは足りない。** 参照線の刻みはその曲率で決まっており、
外側の境界は別の半径・別の幅多項式・（縁石の向こう側では）途中で段のつく `<height>` に乗る。
そこで種グリッドを中点で再帰分割する。**分割判定は中点だけで見てはいけない** — 区間の中央に
対して対称に膨らむ幅多項式は中点での偏差がちょうど 0 になり、中点だけの判定は収束したと
言って止まる（soderleden road 0 で実測 0.33 m）。u = 0.25 / 0.5 / 0.75 の 3 点で見る。

**1 つの edge が 2 本の境界になる場合がある。** 規格の但し書き:

> if two lanes have different Z heights (e.g. a driving lane is beside a sidewalk, where the
> sidewalk is 10cm higher than the road), then these lanes cannot share a boundary, since their
> boundaries have different Z heights.

OpenDRIVE の `<lane><height inner outer>` がまさにこれで、出荷資産の歩道・縁石は 0.12 m 上にある。
**XY は 1 本、z が 2 つ**なので、点列は 1 回だけ作り（縁石は鉛直面であって 2 本の線ではない）、
side ごとの z を同じ点で読む。独立に 2 回リファインすると、どちらも理想線に対しては budget 内
なのに**互いに最大 14 mm 離れる**（multi_intersections 実測）。

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
| 道路間（junction 外） | 同じ `lane->GetLink()` が相手道路のレーン id を指す。端は `RoadLink::GetContactPointType()` | `CONTACT_POINT_START` なら `true` |
| junction 経由 | `Junction::GetConnectionByIdx()` → `Connection::GetLaneLink()` | `Connection::GetContactPoint()` が `CONTACT_POINT_START` なら `true` |

3 番目が `osi_lane` では潰れている部分で、**論理レーン側では接続路レーンが個別に存在するので
交差点の中を 1 レーンずつ繋げる**。

> **2026-09-24（S2 実装時に是正）**: 2 番目は当初「既存の `UpdateOSIRoadLane` 後半の前後セクション
> 解決をそのまま流用」と書いていた。**流用しなかった。** あの既存コードは *相手道路が自分を指し返して
> いるか* から接触端を推論しているが、`@contactPoint` はリンク自身が持っている情報で、
> 直接読むほうが短く、相手道路が長くても端を取り違えない。
>
> **3 番目が「重い」という見積りも外れた。** multi_intersections の実測内訳は road-link 経由 326 件・
> junction 経由 152 件で、多いのは 2 番目である。接続路自身の 2 つの端は road-link 経路で解けるので、
> junction 経路が要るのは **incoming 道路から外を見るときだけ**。

**OpenDRIVE は junction の出会いを片側からしか宣言しない。** `<connection>` は incomingRoad しか
名指しせず、出口側の道路は接続路を指すリンクを持たない。各レーンが「自分のリンクが言うこと」だけを
書くと、**交差点の出口で必ずグラフが途切れる**。1 つの出会いから常に両方向を書くこと。

**ゼロ幅の端は繋がない。** 規格は `Both lanes have a non-zero width at the connection point` を
要求する。合流・分流でテーパ 0 まで細った端にも `<link>` は残っているので、そのまま写すと
「幅 0 のレーンを通り抜けられる」と言うことになる。

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

最初のセグメントの `start_s` は**自車の参照点（bbox 中心）の s**、最後のセグメントの `end_s` は
終点 WP の s に詰める。参照点であって entity origin ではないのは §2-6-1 注 2 のとおりで、
**ここを揃えないと `route` × L1 の合成が継ぎ目で車長分（カタログ車で 1.4 m）跳ねる**。

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

// ★追加（2026-09-24 実装。フラグ判定は呼ばれた側に置いた — フォークファイルに
//   条件分岐を持ち込まないため。バウンディングボックスは L1-b が使う）
gt_esmini::osi::EmitLogicalLaneAssignment(obj_osi_internal.mobj->mutable_moving_object_classification(),
                                          objectState.pos_,
                                          {objectState.boundingbox_.dimensions_.length_,
                                           objectState.boundingbox_.dimensions_.width_,
                                           objectState.boundingbox_.center_.x_,
                                           objectState.boundingbox_.center_.y_});
```

`angle_to_lane` と `assigned_lane_id` は追加の幾何計算なしで揃う — **論理レーンの参照線 s を
road s と同一にした（§2-1）ことの直接の見返り**である。`s_position` / `t_position` だけは
参照点（bbox 中心）を road 座標へ落とす 1 回の解決が要る（注 2）。

| OSI | GT 側 |
| :-- | :-- |
| `s_position` | **参照点（bbox 中心）の s**。`ResolveOsiReferencePoint()`（下の注 2） |
| `t_position` | **参照点の t**。同上 |
| `angle_to_lane` | `GetAngleInIntervalMinusPIPlusPI(pos.GetHRelative())`（下の注 1） |
| `assigned_lane_id` | 索引 `(pos.GetTrackId(), road->GetLaneSectionIdxByS(pos.GetS()), pos.GetLaneId())` ＝ **entity origin 基準**（下の注 2） |

> **注 2 — 2026-09-24（S2.5b で是正）: 出すのは entity origin ではなく参照点である。**
> `osi_common.proto` は `BaseMoving.position` を「the center (x,y,z) of the bounding box」、
> `LogicalLaneAssignment.s_position` を「S position of **the object reference point** on the
> lane」と定義している。MovingObject ではこの 2 つは同じ点なので、**`pos.GetS()` / `pos.GetT()`
> を入れるのは誤り**だった（カタログ車で車長方向に 1.4 m ずれる）。同じ規約は
> `signal_catalog.yaml` の `ego_planned_path` で既に決着しており（「base.position と同じ bbox
> 中心 = `Position::GetOsiX/Y/Z` = 原点 + R(h,p,r)·bbox center」）、新しい判断はしていない。
>
> 解決は `ResolveOsiReferencePoint()`（`GT_OsiLogicalLane.hpp`）。`Position` を複製し
> （road キャッシュを引き継ぐので探索が局所で済む）、bbox 中心のワールド座標を
> **自車の road に固定して** `XYZ2TrackPos` する。1 次近似は使わない — 曲路では足りない
> （R=100 の弧、レーン中心 t=-1.535 で実測: 参照点の road s は origin+1.4 ではなく
> **origin+1.37875**、t は **-0.00965 m** 動く）。
>
> **どちらの点を使うかは欄ごとに違う。** 出力（`s_position` / `t_position`、`route` の先頭
> `start_s`、L2 の `s_along_route`）は参照点。**どのレーンに割り当てるか**（`assigned_lane_id`、
> レーンセクションの選択、L1-b の 5cm 重なり判定）は entity origin のまま — 物理
> `assigned_lane_id` が同じ origin から出ているので、そこを揃えておかないと 2 つの面の
> 先頭が食い違う。規格はこの乖離を明示的に想定している（`s_position` は「might be outside
> [s_start,s_end] of the lane ... if the reference point is outside the lane」）。

> **2026-09-24（S2.5 実装時に是正）**: ここは当初 `pos.GetHRelative()` を素で書いていたが、
> **`Position` はこれを `[0, 2pi)` で持っている**。素のまま出すと、レーン方向からわずかに
> 右へ向いた車が `6.28` rad、わずかに左へ向いた車が `0.00` rad になり、
> **この欄が比較のために存在するまさにその場所に 2pi の段差が入る**。同じリポータが出す
> 他の角度（`base.orientation` の roll/pitch/yaw）はすべて `[-pi, pi]` に畳んであるので、
> ここも畳む。単体で両符号を固定してある（`OsiLogicalLane.AssignmentAngleIsWrappedAndSigned`）。

**索引の引き方は `ResolveMovingObjectAssignedLaneGlobalId` とは違う**。既存のリゾルバは OSI 交差点の
接続路上にいる物体に対して**junction のグローバル id を返す**（物理レーンが 1 本へ融合されている
ため、それが正しい）。論理レーンは接続路レーンごとに存在するので、融合せず
`(road, laneSection, lane)` で直接引く。

> つまり L1 は **`signal:ego_lane` の join 欠け（交差点内で `assigned_lane_id` が `lane_map` に
> 無く、実測 171/15800 フレームで外れる）を論理レーン面で解消する**。物理レーン側の融合は
> upstream 由来の正しい振る舞いなので触らず、論理レーン面に穴のない経路を用意する形になる。

`repeated` の扱い（**L1-b**）: 規格は「5cm 以上重なったレーンには全部割り当てよ」と要求している。
**走行レーン 1 本だけを出す実装にしない。** 車体幅と隣接レーン境界から重なりを判定し、5cm を超えて
重なるレーンすべてに割り当てる。1 本しか出さないと**車線変更の途中でも 1 本しか出ず、消費側が
「跨いでいない」と誤読する**。誤読が起きても形は正しく見えるので、症状が出にくい種類の欠陥になる。

> 2026-09-24: 当初これを L1-b として S2.5 の外へ出していたが、+0.5 日で規格の不足が 1 つ閉じる
> ため S2.5 に畳んだ（§8-α）。検証は「車線変更シナリオで割り当て数が 1 → 2 → 1 と推移する」
> ことを実データで示す。全フレーム 1 本のままなら判定器が効いていない。

**重なりの取り方（2026-09-24 実装で確定）**: 「車体幅と隣接レーン端」では 2 つ足りなかった。

1. **車体は原点ではなくボックス中心のまわりにある。** esmini は車両を entity origin
   （カタログ車は後軸）で置き、`boundingbox_.center_.x_` （カタログ車で 1.4 m）だけ前に
   ボックスがある。レーン方向に対して `h_rel` 傾いていると、この前方オフセットが
   `center_x * sin(h_rel)` だけ**横**にずれる。`t` をそのままボックス中心として扱うと、
   車線変更中に片側の重なりを取りこぼす。
2. **ヨーが横の張り出しを広げる。** t 軸への OBB の射影は
   `0.5 * (|length * sin(h_rel)| + |width * cos(h_rel)|)`。5 m × 2 m の車が 20° 傾くと
   片側 1.00 m ではなく 1.80 m 張り出す。幅だけで判定すると、**第 2 の割り当てが最も
   意味を持つ場面（車線変更の最中）でだけ過小評価する**。

レーン端の t は `LaneSection::Get{Inner,Outer}Offset(s, lane_id)` から取るが、これは
**セクション内の符号なし累積幅**なので、`Position::GetT()` と同じ枠に戻すには
`road->GetLaneOffset(s)` を足して `lane_id` の符号を掛ける必要がある。

**アンカーレーン**（`Position::GetLaneId()` が返すレーン＝物理 `assigned_lane_id` の出どころ）は
**重なり量によらず常に、かつ先頭に**入れる。規格の 5cm 則は「重なったら足せ」であって
「重なりが浅ければ外せ」ではなく、ここで物理面と論理面の先頭を一致させておくほうが、
片方しか読まない消費側にとって安全なため。

対象は自車だけでなく `UpdateOSIMovingObject` を通る全オブジェクトである。同じコストで出るし、
「どの車がどの論理レーンにいるか」は経路上の競合判定に使える。

#### 2-6-2. L2 — 経路始点からの累積距離（telemetry へ）

`Position::GetRouteS()` は**使わない**。物理駆動の車では経路割り当て時の値で凍り、
更新するには `CalcRoutePosition()` を呼ぶしかないが、それは route の状態を書き換える
（現状記録 §4-3）。**観測が対象を変えてはいけない。**

代わりにレーンセクション展開の副産物として積み上げる。区間長は全セグメントで既に持っている。
自車の突き合わせは **`route` と同じ参照点（bbox 中心）** で行う（§2-6-1 注 2）。

> **2026-09-24（S4 実装時に是正）**: ここは当初「`BuildOsiRoute()` の副産物」と書いていたが、
> **同じ展開は使えない**。`route` が載せるのは自車から先の区間で、最初のセグメントは必ず
> 自車 s から始まる（§2-5）。その列から積み上げると `s_along_route` は毎フレーム 0 になり、
> `route_length` は走るほど縮む。どちらも名前が意味するものではない。
> 展開の起点を `RouteExpansionStart::{EgoPosition, RouteStart}` の 2 値で切り替え、
> **`route` は `EgoPosition`、L2 は `RouteStart`** を使う。それ以外は同じ関数・同じ規約である。

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
        for edge_owner in [0] + [lane.id for lane in section if not lane.is_center]:   # n+1 本
            sides = sides_of(edge_owner)                  # (lane, outer|inner) を 1 つか 2 つ
            split_z = sides differ in z by more than 1cm  # 縁石・歩道
            for [lo, hi] in split_by_roadmark(section, edge_owner):
                pts = sample(lo) + [refine(a, b) for consecutive seeds] + sample(hi)
                for group in (sides if split_z else [sides[0]]):
                    lb = static_gt.add_logical_lane_boundary()
                    lb.id = GetNewGlobalId(); lb.reference_line_id = ref_line_id[road]
                    for pt in pts:                        # XY/s/t は共通、z だけ side 別
                        lb.add_boundary_line(position=(pt.x, pt.y, pt.z[group]), s=pt.s, t=pt.t)
                    lb.passing_rule = map_passing_rule(roadmark_covering(lo))
                    lb.physical_boundary_id = [id for id in physical_at(edge_owner)
                                               if id in static_gt.lane_boundary]   # 無ければ空
                    boundary_id[road, section, edge_owner, viewing_lane] = lb.id

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
// GT_esmini/src/osi/RouteToOsiRoute.{hpp,cpp}   純関数・ログを出さない・roadmanager を書き換えない
//
// (1) バンド（道路単位）→ セグメント（レーンセクション単位）。protobuf 非依存
// `ego` には ORIGIN ではなく参照点を渡す（ResolveOsiReferencePoint、§2-6-1 注 2）
std::vector<RouteSectionSegment> ExpandRouteLanePlan(const roadmanager::Route&, const roadmanager::Position& ego,
                                                    const RouteLanePlan&, RouteExpansionStart);
// (2) L2。同じく protobuf 非依存なので VD telemetry から直接呼べる
RouteProgress ComputeRouteProgress(const std::vector<RouteSectionSegment>&, const roadmanager::Position& ego);
// (3) 索引を引いて osi3 へ。出力先は引数（S1 引継ぎ 2 と同じ理由）
void BuildOsiRouteInto(const std::vector<RouteSectionSegment>&, const LogicalLaneIndex&,
                       std::uint64_t route_id, osi3::Route* out);
```

3 本に割った理由は §8-0 の S4 差分 2 を見よ（1 本だと VD が L2 のために protobuf を抱え込み、
リポータ抜きで実 xodr を検証できなくなる）。

`UpdateFromObjectState(egoObj)` から `egoObj->pos_.GetRoute()` に届く。VD の
`ControllerVirtualDriver` を経由しない（手動運転や DefaultController の ego でも経路があれば出る。
実測: `routing-test.xosc` はコントローラを一切持たない ego で `route` が出る）。

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

署名が変わったときだけ `BuildRouteLanePlan` を回し、`route_id` を進める。
`route_id` は規格が `must be unique within all route messages exchanged with one traffic participant`
と要求しているので、**単調増加のカウンタ**にする（再計算のたびに +1。シナリオを開き直しても
戻さない）。

**キャッシュするのはプランまでで、`osi3::Route` メッセージはしない**（§8-0 S4 差分 3）。展開は
自車 s で切るので経路が同じでも毎フレーム変わり、論理レーン id はシナリオ再ロードで振り直される。
したがって展開と id 解決は毎フレーム走り、署名が守るのは `BuildRouteLanePlan`（失敗経路では
`LaneIndependentRouter` の再探索まで走る）だけである。**`route_id` がラン中ずっと定数であることが、
プランが再構築されていないことの外から見える証拠**になる（同じ分岐で進むため）。

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

> **2026-09-24 実測（S4）**: 「大きい地図の経路は実際に上限を超える」は、**この repo にある
> 地図では起きない**。最大は `routing-test.xosc`（`multi_intersections`、63 道路 / 論理レーン 242）の
> **817 B / lane_segment 16 本**で、8192 B には遠く届かない。上限を踏むには 1 道路 400 レーン
> セクションの生成資産が要った（HVD 11,212 B）。分割送信は必要な備えだが、**現実的な経路の
> サイズ主張としてこの節の見積もりを引かないこと**。

**解決はフラグメンテーションの実装とする。** 送信側を GroundTruth 側と同じ形に揃える。

- `GT_OSIReporter.cpp:373-382`: counter は 1 始まり、最後のパケットは負の counter。
- `osi_bridge.py` `_OSIProtocol.datagram_received`: `counter == 0` を単一パケット、
  `counter >= 1` を分割として扱う。**GT ストリームと HVD ストリームで同じクラスを使っている。**
- `DriverScript/realdriver/udp_common.py`: 分割（1..N / 末尾負）を再組み立てできる。

したがって送信側 ~40 行の追加で、消費側の変更はゼロになる。単一パケットに収まる場合の
`counter = 0` は現状の挙動なので、既存の受信側は何も変わらない。

> **2026-09-24 是正（S4 実測）**: 当初ここに「**受信側は両方とも既に対応済み**」「udp_common は
> 0 始まり / 1 始まり両対応」と書いていたが、**単一パケットについては誤り**だった。
> `udp_common.OSIReceiver.receive()` は `counter < 0` でしか `done` にならないので、
> `counter == 0` の 1 パケットを受け取ると続きを待ち続ける（実測 **0/61 完了**、`osi_bridge` は
> 61/61）。GroundTruth 送信側は counter を必ず 1 から振って末尾を負にするため udp_common 自身の
> 用途では踏まないが、**HVD の単一パケットは踏む**。分割経路は両方とも通る（実測 40/40 対 40/40）。

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

1. ~~**段階的に着地させるため。**~~ **S3 で失効（2026-09-24）。** S1〜S2 の時点では境界が無く、
   規格として不完全な中間状態を既定で外に出したくなかった。境界が入ってモデルが閉じたので、
   この理由はもう無い。残るのは 2 と 3 である。
2. **A/B 帰属のため。** 同一バイナリで ON/OFF を切り替えられないと、挙動差やクラッシュが
   本機能由来かを二分探索できない。計器に新しい出力を足すときの既知の落とし穴である。
3. **ペイロードの選択権のため。** 静的 GT が **1.3〜2.7 倍**になる見込み（S0 実測に基づく投影、
   現状記録 §5-5）。論理レーンを読まない消費側にとっては純粋な増分でしかない。

### 7-3. 既定値と、それを変える条件

| 段 | 既定 | 理由 |
| :-- | :-- | :-- |
| S3 より前（実行順で T / S0 / S1 / S4 / S2.5 / S2.5b） | **OFF** | 理由 1（境界が無く、モデルが規格として不完全） |
| **S3 完了以降（実施済み 2026-09-24）** | **ON**（`GT_OSI_LOGICAL_LANE=0` で opt-out） | モデルが規格として完結した。**既定 OFF のまま残さない** |

**反転の実装上の注意**: 変数の意味が opt-in から **opt-out** に変わる。`EnvIsTruthy(getenv(...))`
のままだと未設定が OFF になるので、**未設定かどうかを先に見る**こと
（`v == nullptr || v[0] == '\0' ? true : EnvIsTruthy(v)`）。同居している
`gt_esmini::odr::GetUseAuthoredJunctionBoundary()` は opt-in のままなので、
**同じ形に見えて意味が逆の関数が 2 つ並ぶ**。

**プローブの両極性も同時に反転させる。** 4 本のプローブはどれも「ON = `1` を設定 /
OFF = 未設定」で書かれていた。そのままだと**両方の run が ON になり、OFF 側の判定が
全部 vacuous に通る**。「ON = 未設定（＝既定を踏む）/ OFF = `0`（＝逃げ道を踏む）」へ
書き換えた。こうすると同じ 2 本の run で「既定は ON」と「opt-out は効く」の両方が実証される。

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
> **2026-09-24（S3 実測、反転実施）: 覆す条件に当たらなかったので既定 ON へ反転した。**
> 静的 GT 全体に対して **1.26x〜2.85x**（最大は fabriksgatan）。**3 倍に届く資産は無い。**
>
> | 資産 | S0 投影 | S3 実測 | 差 |
> | :-- | --: | --: | --: |
> | e6mini | 1.26x | 1.26x | ±0.00 |
> | fabriksgatan | 2.73x | **2.85x** | +0.12 |
> | multi_intersections | 2.12x | 2.48x | **+0.36** |
> | soderleden | 1.84x | 2.04x | +0.20 |
> | highway_merge_split | 2.11x | 1.96x | −0.15 |
>
> **投影が外れる向きとして名指ししていた 2 つのうち、交差点側は予想と逆に出た。**
> 「接続路レーンの点密度を本線と同じと仮定＝短いので過大評価のはず」と書いていたが、
> 実際には multi_intersections が**最も過小**だった（+0.36）。理由は測ってみれば明らかで、
> 接続路の曲率半径は 2.2 m まで落ちる（プローブ実測）ので、偏差で刻む境界はそこで
> **本線よりずっと密**になる。「短い＝安い」は点密度が一定なら正しいが、点密度は曲率で決まる。
> もう一方（境界の共有による過小評価）は highway_merge_split の −0.15 に出ている。
>
> 条件の残り半分「OSI 記録が日常のワークフローか」は評価するまでもない: 仮に日常だとしても
> 1 実行あたり 0.4 MB（multi_intersections）の**一度きり**の増分である。毎フレーム流れる
> 動的側は論理レーン割り当ての 34 B/台だけで、10 台でも +3.8%（S2.5 実測）。
>
> **2026-09-24（S2 実測、既定 ON 維持）**: 連結性を足して **1.27x〜2.97x**。**3 倍に届く資産は無い**が、
> **fabriksgatan が 2.97x** で余裕は 1% を切った。反転条件は「3 倍超 **かつ** OSI 記録が日常の
> ワークフロー」の AND であり、後半が成立しないので既定 ON を変えない。分母が小さい
> （静的 GT 26.5 KB）ための比で、絶対値は 1 回きりの +52 KB である。
> **次に層へ何かを足すときは fabriksgatan を先に測ること** — 3x に最初に触るのはこの資産で、
> 次点の multi_intersections（2.56x）とは 0.4 の開きがある。

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
| ~~**S1**~~ **✅ 完了 2026-09-24** | 参照線 + 論理レーン本体（境界・接続なし） | 2〜3 日 | `GroundTruth.logical_lane[]` が出る。xodr のレーンと 1:1 対応していることを OSI 直読で確認できる。**交差点内レーンが初めて個別に見える** |
| ~~**S2**~~ **✅ 完了 2026-09-24** | 連結性（pred / succ / adjacent） | 2〜3 日 | **論理レーンの列として経路をたどれる**。`route` の参照先が実在し連結していることが保証される |
| ~~**S2.5**~~ **✅ 完了 2026-09-24** | L1 `LogicalLaneAssignment`（§2-6-1）＋ **L1-b 車線跨ぎの複数割り当て**（§10-7） | 1 日 | **全オブジェクトの論理レーン相対 s / t / 向きが出る**。交差点内でも車線が個別に引ける（`signal:ego_lane` の join 欠けが論理レーン面で埋まる）。車線変更中は両方のレーンに割り当たる |
| ~~**S3**~~ **✅ 完了 2026-09-24** | 論理境界（ST 化・合成・`passing_rule`）＋ 既定 ON へ反転 | 2〜3 日 | **規格の必須参照が全部埋まる**。外部の OSI 準拠チェッカを通せる。サイズはここで最大になる |
| ~~**S4**~~ **✅ 完了 2026-09-24** | HVD `route` ＋ L2 経路進捗（§2-6-2）。**T が入っていること** | 2 日 | **目的達成**。`capability_model.md` W4 の `route` が閉じる。S2.5 と揃えば `route` × L1 の合成で経路相対位置が外から出せる |
| ~~**S5**~~ **✅ 完了 2026-09-24** | signal 登録 / matcher / ゲート常設化（§9） | 1.5〜2 日 | **回帰で守られる**。両極性を実証してから緑にする |

**合計 11.5〜15.5 日**（`overlapping_lane` / L3 を除く）。**全段完了（2026-09-24）。**

段の ID は実行順ではない（§7.1 の「順序は ID でなく依存で表す」）。依存は §8-1、実行順は次節。

### 8-α. 実行順とリリース線（2026-09-24 決定）

**S4 → S2.5 → S2.5b → S3 → S2 → S5 → リリース。** **全段完了（2026-09-24）。次はリリース。**

一度は「S2 を後回しにして早期リリース」も検討したが、**論理レーン層を規格として完結させてから
1 度で出す**ことにした。段ごとの根拠は次のとおり。

| 順 | 段 | なぜここか |
| :-- | :-- | :-- |
| 1 | **S4** | S1 完了時点で成立する（§8-1）。目的である `route` を最初に通し、以降の段が「既に動いているものを壊していないか」で測れるようにする |
| 2 | **S2.5** | S1 にしか依存しない。`route` と揃って経路相対位置が合成できるようになる（§2-6） |
| 3 | **S3** | **S2 より先。** 規格は隣接レーンについて `The XY positions of the polyline generated by the LogicalLaneBoundaries of adjacent lanes must match up to a small error (5cm)` という整合要件を課している。S3 が先なら S2 の隣接を張った瞬間にこれを検査でき、逆順だと検査が S3 まで待つ。ここで既定 ON へ反転（§7-3） |
| 4 | **S2** | 最後の実装段。**S3 が入ると隣接は冗長になる**（隣接する 2 レーンは `LogicalLaneBoundary` を共有するので、消費側は境界 id の一致から隣接を復元できる）。S2 に残る固有の価値は**前後接続**で、これは OSI の中に代替の手がかりが無い。なお工数は減らない — 隣接は S2 の最も安い部分で、重いのは交差点の前後接続（multi_intersections で接続路 76 本）だから |
| 5 | **S5** | 全部揃ってから常設化する。縮小版にしない |

**S2 / S2.5 / S3 は同じファイル（`GT_OSIReporter_LogicalLane.cpp`）を触る**ので、依存が無くても
どのみち直列になる（§8-1）。

`overlapping_lane`（§10-1）と L3（§10-8）は**入れない**。前者は唯一 RoadManager から導けず
新規の幾何計算が要る（+2〜3 日）。後者は OSI に欄が無いうえ基準の取り方が用途で変わるので、
用途が決まるまで定義を与えない。L1-b（§10-7）だけは 0.5 日で規格の不足を 1 つ閉じるので
S2.5 に畳んだ。

### 8-0. 各段の実測結果

#### S0（2026-09-24）

置いたもの:

| 追加/変更 | 何 |
| :-- | :-- |
| 新規 `GT_esmini/include/gt_esmini/osi/GT_OsiLogicalLane.hpp` | `LogicalLaneKey` / `LogicalLaneIndex` / フラグ / `BuildOsiLogicalLanes()` / `GetLogicalLaneIndex()` の宣言。namespace は `gt_esmini::osi` |
| 新規 `GT_esmini/src/osi/GT_OSIReporter_LogicalLane.cpp` | フラグ実体と**何も emit しない**後段パス。スワップゾーン側 |
| 変更 `GT_esmini/src/osi/GT_OSIReporter.cpp` | `CreateOSIStaticGroundTruthFromODR()` の `ApplyAuthoredJunctionBoundaries()` 直後で呼ぶ（§1 の注） |
| 変更 `EnvironmentSimulator/Modules/ScenarioEngine/CMakeLists.txt` | 既存スワップブロックへ 4 行（R1、承認済み） |
| 新規 `GT_esmini/test/unit/osi/test_OsiLogicalLane.cpp` | 傘バイナリへ登録。フラグ両極性と S0 不変量（索引が空） |
| 新規 `scripts/probe_osi_logical_lane_size.py` | 実測プローブ。出力 `test_results/osi_logical_lane/size_probe.json`（S1 で `s0_` 接頭辞を外した — 恒久資産に工程名を残さない） |

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
2. **交差点の増分が数で裏付いた。** osi lane → 論理レーン相当は fabriksgatan 25 → 44、
   multi_intersections 171 → 242。**増えた分はすべて接続路レーン**である。

   > **2026-09-24 訂正（S1 プロンプト作成時）**: S0 報告はここに「流用できる既存 OSI 点が
   > 1 つも無い」と書いていたが、**面を取り違えている**。無いのは **OSI メッセージ面**だけで、
   > **RoadManager の `OSIPoints` は接続路レーンにも全部ある**。
   >
   > - `OpenDrive::SetLaneOSIPoints()` は junction road を含む**全道路**を回り、全レーンの点を
   >   計算したうえで最後に `lane->SetOSIIntersection(...)` で**タグを付けるだけ**である
   >   （[`GT_RoadManager.cpp:8316-8551`](../../src/road/GT_RoadManager.cpp#L8316) に junction スキップは無い）。
   > - `SetLaneBoundaryPoints()` も同様で、条件は `n_roadmarks == 0` だけである。
   > - 落ちているのは出力側。`UpdateOSIRoadLane()` が `IsOSIIntersection()` を除外し
   >   （[`GT_OSIReporter_Geometry.cpp:1043`](../../src/osi/GT_OSIReporter_Geometry.cpp#L1043)）、
   >   `UpdateOSIIntersection()` が 1 本の `TYPE_INTERSECTION` へ融合している。
   >
   > したがって **S1 と S3 にとって接続路レーンは通常路と同じ扱いでよい**。
   > `GetRefLineOSIPoints()` も `lane->GetOSIPoints()` も引ける。増えるのは反復回数であって
   > 機構ではない。**交差点が本当に塊になるのは S2（連結性）** で、そこだけは
   > `Junction::GetConnectionByIdx()` / `Connection::GetLaneLink()` を辿る別経路が要る。
   >
   > **2026-09-24 実測（S1 冒頭、`OsiLogicalLane.ConnectingRoadLanesCarryRoadManagerOsiPoints`）:
   > 訂正は正しかった。** multi_intersections で接続路レーン 76 本すべてに OSI 点があり
   > （計 682 点）、接続路のレーンセクション 42 すべてに参照線 OSI 点がある（計 390 点）。
   > 通常路側も 166 本 / 21 セクションで欠けゼロ。**接続路は通常路と同じ扱いでよい**ことが
   > コード読みでなく実データで確定した。この測定はコメントではなく assert としてテストに残してある。
3. **`obj_osi_internal.static_gt` は OSIReporter のコンストラクタで確保される**（静的初期化時ではない）。
   ユニットテストから後段パスを直接叩くと null になりうるので、S1 以降も null ガードを外さないこと。
4. **`.osi` 第 1 レコードが静的 GroundTruth を読む唯一の口である。** `SE_GetOSIGroundTruth` は
   初期化後は `dynamic_gt` のコピーしか返さない（`UpdateOSIGroundTruth` の else 分岐）ので、
   静的側の回帰をこれで測ろうとすると**約 2 KB の動的ペイロードを比べているだけになる**。
   S0 のプローブは最初これを踏んで、lane 数 0 のまま「一致」を報告した。プローブ側には
   「測ったレコードに lane が 1 本も無ければ FAIL」を入れてある。

#### S1（2026-09-24）

pass 1（参照線）と pass 3（論理レーン本体）＋索引。pass 2（境界）と pass 4（連結性）は未実施なので
`left_boundary_id` / `right_boundary_id` / `predecessor_lane` / `successor_lane` /
`left_adjacent_lane` / `right_adjacent_lane` は**空のまま**である。いずれも `repeated` なのでメッセージは
壊れないが、厳密な OSI バリデータは通らない。既定 OFF はそのため（§7-2 理由 1）。

置いたもの:

| 追加/変更 | 何 |
| :-- | :-- |
| 変更 `GT_esmini/src/osi/GT_OSIReporter_LogicalLane.cpp` | 後段パス本体。`BuildOsiLogicalLanesInto(opendrive, gt, index)` に実装し、`BuildOsiLogicalLanes()` はフラグ判定と `obj_osi_internal.static_gt` の解決だけを行う薄い包み |
| 変更 `GT_esmini/include/gt_esmini/osi/GT_OsiLogicalLane.hpp` | `BuildOsiLogicalLanesInto()` と純写像 2 本（`MapLaneTypeToLogicalLaneType` / `MapMoveDirection`）の宣言。`osi3::GroundTruth` は**前方宣言のみ**でヘッダに protobuf を持ち込まない |
| 変更 `GT_esmini/test/unit/osi/test_OsiLogicalLane.cpp` | 14 テスト（下記） |
| 変更 `scripts/probe_osi_logical_lane_size.py` | 受入判定を「全バイト一致」から「**既存フィールドが 1 つも動いていない**」へ。出力名から `s0_` を外した |

**なぜ `BuildOsiLogicalLanesInto()` を分けたか**: 出力先を引数で受けると、`OSIReporter` を立てずに
実 xodr で emit を検証できる。引継ぎ 3（`static_gt` は reporter のコンストラクタで確保される）が
効いているのはここで、ユニットテストのプロセスでは `static_gt` が常に null だから、
包みの側だけをテストしても**中身は 1 行も踏めない**。

実測（`test_ScenarioReaderParsing --gtest_filter=OsiLogicalLane.*`、14/14 緑）:

| 受入基準 | 実測 |
| :-- | :-- |
| xodr レーンと 1:1（中央レーン除く） | 5 資産すべてで一致。multi_intersections **242 = 242**、e6mini 14、fabriksgatan 44、soderleden 33、highway_merge_split 53 |
| 接続路レーンが個別に出る | multi_intersections **論理 242 > osi lane 171**、S0 の 242 と一致。内訳は `171 = 通常路 166 + 融合交差点 5`、`242 = 通常路 166 + 接続路 76`。接続路 76 本は融合物理レーン 5 本を共有して指す（`physical_lane_reference` の多対一が実データで 5 件） |
| 参照線は道路 1 本に 1 本 | 5 資産すべてで `reference_line == roads`（63 / 1 / 16 / 5 / 9） |
| 非走行レーンも全部出る | multi_intersections の型内訳 `NORMAL 86 / SIDEWALK 59 / BORDER 59 / OTHER 38`（合計 242）。`TYPE_UNKNOWN` はゼロ |
| ON にしても既存 `lane[].id` が動かない | 5 資産すべてで `lane` / `lane_boundary` の**id 集合が OFF と完全一致**し、`lane` `lane_boundary` `traffic_sign` `traffic_light` `road_marking` `stationary_object` の**各フィールドが SHA-256 まで一致**。論理層の id は既存 id 集合と互いに素 |
| OFF で `logical_lane == 0` かつ `reference_line == 0` | 5 資産すべて 0、論理層のバイト数も 0。ON でのみ後段パスのログ行が出ることも 5/5 で確認（死んだフラグとの区別） |
| `t_axis_yaw` の極性 | e6mini 48 点で `agree=48 / 48`、**符号を反転させた対照は `0 / 48`**。世界座標で `(s, t=+3m)` と突き合わせている |
| `move_direction` の 4 象限 | (RHT, LHT) × (負, 正) で `INCREASING_S` / `DECREASING_S` が入れ替わることを単体で固定。双方向・非走行は `BOTH_ALLOWED` |
| 速度制限の単位 | virtual_junction_23（`50 km/h` 記載）で **50 が出る**（内部値 13.889 m/s ではない）。§2-2 の注を参照 |
| レーンセクション継ぎ目 | soderleden（5 道路 / 7 セクション）で内部継ぎ目 2 に対し**畳まれた点がちょうど 2**。全参照線で s は狭義単調増加 |

**参照線 + 論理レーンだけのサイズ増分（境界抜き・実測、`scripts/probe_osi_logical_lane_size.py`）**:

| 資産 | 論理レーン | 参照線 | 参照線点 | refLine B | logLane B | 増分 B | static x | roadnet x |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: |
| e6mini | 14 | 1 | 48 | 2,363 | 1,771 | **4,134** | 1.02x | 1.06x |
| fabriksgatan | 44 | 16 | 131 | 6,588 | 4,987 | **11,575** | 1.44x | 1.44x |
| multi_intersections | 242 | 63 | 587 | 29,442 | 28,207 | **57,649** | 1.22x | 1.25x |
| soderleden | 33 | 5 | 83 | 4,117 | 3,762 | **7,879** | 1.16x | 1.16x |
| highway_merge_split | 53 | 9 | 98 | 4,899 | 6,069 | **10,968** | 1.24x | 1.24x |

S0 の投影（**層まるごと**で 1.26x〜2.73x）に対し、**本体だけなら 1.02x〜1.44x**。
差分はすべて S3 の論理境界が持っていく — つまり §7-3 の既定 ON 判断を左右するのは境界であって
本体ではない、というのがこの中間値の意味である。最大規模の multi_intersections でも
静的 262 KB → 320 KB（+56 KB）で、ロード時 1 回。毎フレームの動的側は 2.1 KB のまま不変
（論理レーン面は静的 GT にしか載らない）。

**S4 / S2.5 へ引き継ぐ発見**:

1. **索引の粒度は `(road_id, laneSection idx, lane_id)` で足りている。** 5 資産すべてで
   `index.size() == logical_lane_size()`、かつ索引の指す先が必ず emit 済みという閉包が成立した。
   S4 は `RouteLanePlan` のホップを `(road, section, lane)` へ落とせればそのまま引ける。
2. **接続路も通常路と同じ経路で引ける**（引継ぎ 2 が実データで確定）。S4 の `route` が交差点を
   跨ぐときも、特別扱いが要るのは連結性（S2）だけで、id 解決は要らない。
3. **`osi3::GroundTruth` の前方宣言だけでヘッダが書ける。** S4 の `RouteToOsiRoute.hpp` も
   同じ形にできる（`GT_esminiLib` 側は protobuf を持っているので必須ではないが、
   ユニットテストから叩ける形が保てる）。
4. **`road_s` の `source_reference` は `fmt::format("{}", double)` で書かれている**（既存 `osi_lane`
   と同一）。S4 でこの文字列を突き合わせるなら、**数値へ戻してから比較すること**。
   同じ s でも書式が一致する保証はない。

#### S4（2026-09-24）

`HostVehicleData.route` と L2 経路進捗。置いたもの:

| 追加/変更 | 何 |
| :-- | :-- |
| 新規 `GT_esmini/include/gt_esmini/osi/RouteToOsiRoute.hpp` / `src/osi/RouteToOsiRoute.cpp` | 純関数 3 本 + `RouteSignature`。詳細は下の「設計との差分」 |
| 変更 `GT_esmini/src/osi/GT_HostVehicleReporter.{hpp,cpp}` | `FillRoute()` + 経路キャッシュ（pimpl）+ `route_id` カウンタ |
| 変更 `GT_esmini/src/control/ControllerVirtualDriver.cpp` ほか | L2 を telemetry `route_lane` ブロックへ（`on_route` / `s_along_route` / `route_length` / `segment_index`） |
| 新規 `GT_esmini/test/unit/osi/test_RouteToOsiRoute.cpp` | 9 テスト（傘バイナリ） |
| 新規 `scripts/probe_hvd_route.py` | 実測プローブ。出力 `test_results/osi_logical_lane/hvd_route_probe.json` |

**設計との差分（コードが正、本書をコードに合わせた）**:

1. **展開の起点が 2 つ要る。** 本書 §2-6-2 は L2 を「`BuildOsiRoute()` の副産物」と書いていたが、
   これは成立しない。`route` は**自車から先**を載せる（受入基準）ので最初のセグメントが必ず
   自車 s から始まり、同じ展開から積むと `s_along_route` は**構造的に恒久 0** になる。
   `ExpandRouteLanePlan(..., RouteExpansionStart)` の 2 値で分けた —
   `EgoPosition` が `route` 用、`RouteStart` が L2 用。両者はそれ以外すべて同一。
2. **`BuildOsiRoute` は 3 本に割れた。** `ExpandRouteLanePlan`（バンド→レーンセクション、protobuf 非依存）
   / `ComputeRouteProgress`（L2、同じく非依存）/ `BuildOsiRouteInto`（索引を引いて `osi3::Route` へ）。
   VD が protobuf に触らず L2 を出せるのと、S1 引継ぎ 2 の「出力先を引数で受ければリポータ抜きで
   実 xodr を検証できる」の両方がこれで満たせる。
3. **キャッシュするのは `RouteLanePlan` だけで、`osi3::Route` メッセージはしない。** 署名が同じでも
   自車が動けば展開結果は変わるうえ、シナリオ再ロードで論理レーン id が振り直されるため。
   展開と id 解決は毎フレーム、`BuildRouteLanePlan` だけが署名で守られる。
4. **`Road::GetConnectedLaneIdAtS()` は使えない。** レーンセクションを跨ぐ id 追跡の実装は
   「既に離れたセクションの**始端** s」と `s_target` を比べて進むため
   （[`GT_RoadManager.cpp:3235-3253`](../../src/road/GT_RoadManager.cpp#L3235)）、
   **3 セクション以上ある道路では行き過ぎて常に最終セクションの id を返す**。
   `EvaluateRouteLaneStatus` も同じ関数を使っている（既存の挙動なので本段では触らない）。
   S4 はレーンリンクを 1 段ずつ辿る `MapLaneIdAcrossSections()` を自前に持ち、1 パス O(n) で解いた。
5. **`Position` は渡された `Route*` を所有する。** `Position::SetRoute(Route*)` はポインタを持つだけだが
   `~Position()` が**無条件に `delete` する**（[`GT_RoadManager.cpp:7942`](../../src/road/GT_RoadManager.cpp#L7942)）。
   スタック上の `Route` を渡すとヒープ破壊（実測: 終了コード `0xC0000374`）。ユニットテストで
   `SetRoute` を使うときは `new Route` を渡す。
6. **`route` は静的 GroundTruth が一度も組まれていないと空になる。** 索引を埋める後段パスは
   `OSIReporter::UpdateOSIGroundTruth()` の初回にしか走らないので、**HVD だけを有効にした
   セッションでは全ルックアップが外れる**。実測: OSI 出力なしの同一シナリオで HVD 384 B
   （セグメント 0）、`GT_OpenOSISocket` を足すと 11,212 B。空の `route` は「経路が尽きた」と
   区別できないので、`GT_HostVehicleReporter` に一度だけ出る `LOG_WARN` を入れた。
7. **§6 の「受信側は両方とも既に対応済み」は分割経路についてのみ正しい。** 実測で
   `DriverScript/realdriver/udp_common.py` の `OSIReceiver` は**負の counter でしか受信を終えない**ため、
   `counter == 0` の単一パケット HVD を延々待ち続ける（実測 0/61 完了。`osi_bridge.py` は
   `counter == 0` を明示的に単一メッセージとして扱い 61/61）。GroundTruth 送信側は counter を
   必ず 1 始まり・末尾負で振るので udp_common の実運用では踏まないが、**HVD 単一パケットは踏む**。
   分割経路（1..N / -N）は両方とも通る（実測 40/40 対 40/40）。

**実測（受入基準ごと）**:

| 受入基準 | 実測 |
| :-- | :-- |
| 参照の閉包 | 3 資産合計 **19,297 / 19,297** の `lane_segment` が実在する `logical_lane` を指す（routing-test 2,946 / exit-ramp 796 / 長経路 15,555） |
| 向きの両極性 | 昇順 18,443・**降順 854**。降順は routing-test（`multi_intersections`、RHT の正レーン `laneId=1` から始まる実カタログ経路）で出た。単体側は同一経路内で昇順 1・降順 1 を固定 |
| 最初のセグメント = 自車 s | on-route フレーム 102 本で最大偏差 **4.98e-10 m**。off-route 97 本では経路が自車の **最大 40 m 先**から始まる — road 0 のレーン -4 が s=50 からしか存在しないため（下の §10-10） |
| 最後のセグメント = 終点 WP の s | 3 資産とも全フレームで単一値（20.0 / 40.0 / 15.0）、シナリオの最終 Waypoint と一致 |
| `route_id` と キャッシュ | 1 ラン内で**定数**（1 / 2 / 3）＝ `BuildRouteLanePlan` が再実行されていない証拠（id はプラン再構築と同じ分岐で進むため）。同一プロセスで 3 シナリオを続けて開くと 1 → 2 → 3 と単調増加 |
| 8192 B 超で落ちない | HVD **11,212 B** が 8200 B + 3020 B の 2 パケットで出る。`osi_bridge` 40/40・`udp_common` 40/40 が再組み立て。**実地図の経路は 8192 B に届かない**（routing-test 最大 817 B / 16 lane_segment）ため、超過側は生成資産（1 道路 400 レーンセクション）で踏んだ — 転送層の worst case であって現実的なペイロードの主張ではない |
| L2 単調増加 + 負の対照 | `s_along_route` 0.29 → 71.28 m、逆行 0 / 102 フレーム。単体で同一走行の `GetRouteS()` が割当時の値から動かないことを併せて固定（`SetInertiaPos` 駆動、15 サンプル） |
| 経路外 | off-route 97 フレームすべてで `on_route=false` かつ `s_along_route == -1`（0 ではない） |
| `GT_OSI_LOGICAL_LANE=0` | 3 資産・全 477 フレームで `route` フィールド自体が不在。`logical_lane[] == 0` |

---

#### S2.5（2026-09-24）

L1 `LogicalLaneAssignment` と L1-b（車線跨ぎの複数割り当て）。置いたもの:

| 追加/変更 | 何 |
| :-- | :-- |
| 変更 `GT_esmini/src/osi/GT_OSIReporter_LogicalLane.cpp` | `ComputeLogicalLaneAssignments()`（純関数、protobuf 非依存）と `EmitLogicalLaneAssignment()`（薄い emit 包み） |
| 変更 `GT_esmini/include/gt_esmini/osi/GT_OsiLogicalLane.hpp` | `ObjectBox` / `LogicalLaneAssignmentEntry` / `kLogicalLaneOverlapThresholdM` と上記 2 本の宣言。`osi3::MovingObject_MovingObjectClassification` は**前方宣言のみ** |
| 変更 `GT_esmini/src/osi/GT_OSIReporter_Moving.cpp` | **呼び出し 1 か所（実質 6 行、うちコメント 3 行）＋ include 1 行**。フォーク系譜なので条件分岐も本体も持ち込まない |
| 変更 `GT_esmini/test/unit/osi/test_OsiLogicalLane.cpp` | S2.5 の 7 テスト（S1 の 14 と合わせて 21、傘バイナリで全緑） |
| 新規 `scripts/probe_osi_logical_lane_assignment.py` | 実測プローブ。出力 `test_results/osi_logical_lane/assignment_probe.json` |
| 変更 `scripts/probe_osi_logical_lane_size.py` | **毎フレーム側の受入判定を「バイト一致」から「logical_lane_assignment を剥がせば一致」へ**（下の差分 5） |

**設計との差分（コードが正、本書をコードに合わせた）**:

1. **`angle_to_lane` は畳む必要があった。** §2-6-1 は `pos.GetHRelative()` を素で書いていたが、
   `Position` はこれを `[0, 2pi)` で持っている。素のまま出すと、レーン方向からわずかに右へ
   向いた車が `6.28`、左へ向いた車が `0.00` になり、**この欄が比較のために存在するまさに
   その場所に 2pi の段差が入る**。同じリポータの他の角度と揃えて `[-pi, pi]` に畳んだ。
   §2-6-1 の表と注を是正済み。
2. **「車体幅と隣接レーン端」では重なりが正しく出ない。** 2 つ足りなかった。
   (a) 車体は entity origin ではなく**ボックス中心**のまわりにあり、レーン方向に対して
   傾いているとその前方オフセットが `center_x * sin(h_rel)` だけ横へずれる。
   (b) ヨーは横の張り出しを広げる（t 軸への OBB 射影 =
   `0.5 * (|L*sin| + |W*cos|)`）。5 m × 2 m の車が 20° 傾くと片側 1.00 m ではなく 1.80 m。
   **幅だけで判定すると、第 2 の割り当てが最も意味を持つ場面でだけ過小評価する**。
   §2-6-1 に追記済み。
3. **フラグ判定は呼ばれた側に置いた。** §2-6-1 の擬似コードは呼び出し側に `if` を書いて
   いたが、`GT_OSIReporter_Moving.cpp` は `lineage:gt_osireporter` のフォーク系譜なので、
   条件分岐を含めて GT 側の TU に寄せた。フォーク差分は呼び出し 1 か所だけになる。
4. **`OSCBoundingBox` は引数型にできない。** 無名 struct の `typedef` なので前方宣言も
   名前付けもできず、ヘッダに ScenarioEngine を引き込まずには受け取れない。
   4 つの double を持つ `ObjectBox` へ平らにして渡す。
5. **毎フレーム側の不変量を「narrow」した（緩めたのではない）。** S0/S1 の
   `probe_osi_logical_lane_size.py` は「毎フレーム GroundTruth が OFF/ON でバイト一致」を
   受入条件にしていた。L1 は**動的メッセージ**に載るので、この条件は S2.5 で構造的に
   成立しなくなる。落とさず絞った — **ON 側から `logical_lane_assignment` だけを剥がして
   再直列化し、OFF 側と SHA 一致すること**。「差分は割り当てが全部である」がそのまま条件になる。
   **負の対照も取った**: ON 側だけ `moving_object.base.velocity.x` に +1 した細工版では
   （**剥がした後のバイト長は 2121 B で OFF と同じまま**）この判定が FAIL する。
   長さ比較では捕まらない改変を SHA が捕まえている。

**実測（受入基準ごと）**:

| 受入基準 | 実測 |
| :-- | :-- |
| `s_position == pos.GetS()` / `t_position == pos.GetT()` が全フレーム | 3 資産・**4,825 比較すべてで差 0.0**（`max |s_position - SE.s| = 0`、`max |t_position - SE.t| = 0`）。突き合わせ相手は `SE_GetObjectState`、つまり**メッセージを埋めたのとは別の DLL 経路**。内訳 cut-in 502 / routing-test 279 / highway_driver 4,044 |
| 交差点の接続路で論理 id が junction id でない | routing-test の**物理側が融合 junction レーンを指している 23 オブジェクトフレーム**で、論理側が junction id を指した回数 **0/23**、かつ **23/23** が「その車が実際に乗っている road のレーン」に解決した。単体側は multi_intersections の接続路走行レーン **42 本すべて**で「論理 = 接続路レーン」と「物理 = junction グローバル id」を**同じテストの中で**固定 |
| L1-b の両極性（1 → 2 → 1） | cut-in.xosc（e6mini・2 台）を 239 フレーム。車線変更する `OverTaker` が **1（146 フレーム）→ 2（24）→ 1（69）**、直進する `Ego` が **239 フレーム全部 1**。単体側は 5cm しきい値の両側を固定: レーン -3（幅 3.50）で横オフセット **0.79 m → 1 本 / 0.81 m → 2 本**（境界は 0.75 + 0.05 = 0.80 m ちょうど） |
| 参照の閉包 | **4,825 / 4,825** の `assigned_lane_id` が静的 GT の `logical_lane[]` に実在。単体側は e6mini / fabriksgatan / multi_intersections の全レーンを掃いて 549 割り当てで同じ検査 |
| `GT_OSI_LOGICAL_LANE=0` で空 | 3 資産・**4,747 オブジェクトフレームで割り当て 0**、`logical_lane[] = 0` |
| `angle_to_lane` が畳まれている | 実測レンジ cut-in `[-0.0523, 0.0000]` / routing-test `[0.0000, 3.1416]` / highway_driver `[-0.3066, 0.1125]` rad。routing-test の `3.1416` は正レーンを -s 方向へ走る車（畳んでいなければ 2pi 近傍が出る） |
| 動的 GroundTruth の増分 | 下表 |

**動的 GroundTruth の増分（実測、`scripts/probe_osi_logical_lane_assignment.py`）**:

| 資産 | 台数/フレーム | 割り当て/フレーム | 動的 GT OFF | 動的 GT ON | 増分 | 倍率 | 1 割り当てあたり |
| :-- | --: | --: | --: | --: | --: | --: | --: |
| cut-in | 2.00 | 2.10 | 2,967 B | 3,038 B | +71.4 B | 1.024x | **34.00 B** |
| routing-test | 1.00 | 1.00 | 2,174 B | 2,208 B | +34.0 B | 1.016x | **34.00 B** |
| highway_driver | 10.00 | 10.14 | 9,166 B | 9,511 B | +344.6 B | **1.038x** | **34.00 B** |

**見積り 35 B / 跨ぎ 70 B は当たっていた（実数 34 B / 68 B）。外れていたのは倍率のほうである。**
現状記録 §5-5a は「20 台なら動的 GT が 1.3〜1.7 倍」と書いていたが、**動的 GroundTruth は
1 台あたり約 900 B ある**（highway_driver で 10 台 9,166 B）ので、1 台 34 B の追加は
**+3.8% にしかならない**。20 台でも 1.04 倍前後で、1.3〜1.7 倍には届かない。現状記録を是正済み。

**S3 へ引き継ぐ発見**:

1. **レーン端の t の出し方が S3 の論理境界と共有の基準になる。**
   `road->GetLaneOffset(s) + SIGN(lane_id) * LaneSection::Get{Inner,Outer}Offset(s, lane_id)` が
   `Position::GetT()` と同じ枠の値である（`Get{Inner,Outer}Offset` は**セクション内の符号なし
   累積幅**なので、road 単位の `<laneOffset>` を足して符号を掛けないと枠が合わない）。
   S3 の `LogicalLaneBoundary` がこの t から外れたら、**同じレーンの端が面ごとに 2 つある**
   ことになる。規格が隣接レーン境界に課している 5cm 整合は、S3 でこの式を基準に測れる。
2. **毎フレーム側の受入条件はもう「バイト一致」ではない（差分 5）。** S3 が増やすのは
   静的バイトだけなので、**S3 は narrow 後の判定をそのまま緑で通せなければならない**。
   将来さらに動的フィールドを足す段は、長さ比較へ緩めるのではなく同じやり方で絞ること
   （負の対照が示したとおり、長さが同じまま中身だけ動く改変が実在する）。
3. **`Position::GetHRelative()` は畳まれていない（差分 1）。** 論理レーン層で角度を出す
   のは S3 の境界にはないが、`overlapping_lane` など将来の欄で同じ罠を踏む。
4. **アンカーレーンは重なり量によらず常に入る（§2-6-1）。** したがって「割り当て 1 本」は
   L1-b が死んでいる証拠にならない。**跨ぐはずのエンティティを名指しで 2 本要求する**検査が
   要る（プローブは `OverTaker` を名指ししている）。
5. **重なり判定は物体のレーンセクション内に閉じている**（§10-14）。S3 の境界はセクション
   境界で切れるので、同じ制約を共有する。

#### S2.5b（2026-09-24）— 参照点の是正

`LogicalLaneAssignment` / `route` 先頭 / L2 の基準点を entity origin から **bbox 中心**へ揃えた。
**設計書が誤っていた側**（§2-6-1 の表がこの 3 つとも origin を指定していた）。置いたもの:

| 追加/変更 | 何 |
| :-- | :-- |
| 変更 `GT_OsiLogicalLane.hpp` | `ResolveOsiReferencePoint()` の宣言、`ObjectBox` に `center_z`、`LogicalLaneAssignmentEntry::ref_point_ok` |
| 変更 `GT_OSIReporter_LogicalLane.cpp` | `ResolveOsiReferencePoint()` 実装（複製 + road 固定 `XYZ2TrackPos`）、`ComputeLogicalLaneAssignments` の出力だけを参照点へ |
| 変更 `GT_OSIReporter_Moving.cpp` | 呼び出しに `center_z` を 1 行追加（フォーク差分はこれだけ） |
| 変更 `GT_HostVehicleReporter.cpp` | `ExpandRouteLanePlan` に参照点を渡す |
| 変更 `ControllerVirtualDriver.cpp` | L2 の突き合わせを参照点で |
| 変更 `test_OsiLogicalLane.cpp` | S2.5 の `AssignmentCarriesPositionStVerbatim` を廃し 5 本を新設（直線の量、bbox 中心＝原点の縮退、ヨー、曲路の閉形式、road 固定）。傘バイナリで **OsiLogicalLane 25/25 緑** |
| 変更 `scripts/probe_osi_logical_lane_assignment.py` | ST 検証を **esminiRMLib 経由の往復**へ（下の実測） |
| 変更 `scripts/probe_hvd_route.py` | 先頭 `start_s` の突き合わせ相手を telemetry `ego.s` から**同一フレームの L1 `s_position`** へ |

**計測器（S2.5 のときと同じ規律で、対象と別経路）**: `esminiRMLib.dll` — **別 DLL・別
RoadManager インスタンス**を同一プロセスに載せ、`(roadId, s_position, t_position)` を
`RM_SetRoadPosition` でワールド XY に戻して `moving_object.base.position`（リポータが
`Position::GetOsiX/Y` から別経路で書いた値）と突き合わせる。

> **床（noise floor）を取り違えかけた。** 最初は「同じ往復を entity origin の s/t で回した残差」
> を床にしたが、これは **恒等式**だった — origin の XY は s/t から導かれているので必ず 0 に
> なり、何も測っていない。実際 3 資産中 2 つで床が 0.000 と出て、床より大きい主張値が
> 不合格になった。正しい床は**同じ点における計測器自身の XY→ST→XY 残差**である
> （ST→XY は厳密幾何、XY→ST は OSI ポリライン上の歩行なので構造的に閉じない）。

**実測（受入基準ごと）**:

| 受入基準 | 実測 |
| :-- | :-- |
| `s_position` / `t_position` が bbox 中心 | 往復誤差 vs `base.position`: cut-in **1.616 mm** / routing-test **51.58 mm** / highway_driver **2.362 mm**。**いずれも計測器自身の床と小数 6 桁まで同値**（cut-in 1.616 mm / routing-test 77.23 mm / highway 2.362 mm）＝残差は全部 OSI ポリライン近似で、こちらが足した誤差は 0。最悪は multi_intersections の road 205（長さ 17.7 m の接続路）で 51.6 mm |
| 同じ点を 2 つの RoadManager が解く | `\|esminiRMLib の s(base.position) − s_position\| = 0` （4,743 フレーム全部、3 資産） |
| **負の対照** | 同じ往復に **origin の s/t** を食わせると `base.position` を最小でも **1.301〜1.401 m** 外す（`centerOffsetX ≥ 1.0` の 4,747 フレーム）。判定が 2 点を区別できることを同じループで実証 |
| 曲路で 1 次近似では足りない | 単体（`curve_r100.xodr`、R=100 の弧、レーン中心 t=−1.535）: 参照点は origin+1.4 ではなく **origin+1.37875**（ずれ 21.25 mm）、t は **−9.65 mm** 動く。閉形式 `R·atan(d/r)` / `R−√(r²+d²)` と 5e-4 m 以内で一致 |
| route 先頭と L1 が同一フレームで一致 | `route_lane_exit_ramp` の on-route 104 フレームで `\|first_start_s − s_position\| = 0 m`。**負の対照**: 同じフレームを telemetry の `ego.s`（entity origin）と比べると最小 **1.395 m** 外れる |
| 既存の受入が維持されている | 交差点の論理 id 0/23 が junction id・23/23 が実在の road、L1-b が 1→2→1（`OverTaker`）/ 全 239 フレーム 1（`Ego`）、参照の閉包 4,825/4,825、OFF で割り当て 0、毎フレーム増分 34.00 B/割り当て（S2.5 と同値） |
| 参照点が自車の road から出ない | 単体で multi_intersections の長さ 10 m 超の全 road（`checked > 5`）の終端 0.5 m 手前を掃き、`ref.GetTrackId() == road` かつ `pos.GetS() ≤ ref.GetS() ≤ road.GetLength()` |
| 回帰ゲート | **PASS**（unit 緑 / ODR quick 緑 / behavioral 67 シナリオ 0 deviation） |

**探索が局所で済むか（実測）**: 済む。`roadId` を渡した `XYZ2TrackPos` は `nrOfRoads = 0` に
落として `current_road` 1 本しか見ない（`GT_RoadManager.cpp` の `if (roadId == ID_UNDEFINED)`
分岐）ので、これは構造で保証されている。裏付けの計測（ON/OFF の 1 ステップあたり差、
論理レーン層まるごと＝割り当て計算＋emit＋直列化を含む）:

| 資産 | 道路数 | 台数 | OFF | ON | 差 | 1 台あたり |
| :-- | --: | --: | --: | --: | --: | --: |
| routing-test (multi_intersections) | 63 | 1 | 0.0684 ms | 0.0902 ms | +0.0218 ms | 21.8 us |
| highway_driver (e6mini) | 1 | 10 | 0.0698 ms | 0.1613 ms | +0.0915 ms | 9.2 us |

道路数 63 倍に対して 1 台あたりのコストは **2.4 倍**にしかならない。全探索なら 63 倍側に
乗るので、局所探索であることの傍証になっている。

**S3 へ引き継ぐ発見**:

1. **計測器の「床」は、対象と同じ向きの変換で取らなければ床にならない。** 逆向きの往復は
   恒等式になりうる。S3 の境界偏差も同じ罠がある — **構築に使った s で測ると自己確認**に
   なるので、独立した細かい s グリッドで測ること。
2. **`RM_PositionData` に t が無い。** 往復は `RM_SetRoadPosition`（road, s, t）か
   `RM_SetLanePosition`（road, lane, offset, s）で閉じる。前者は GT 側の s/t をそのまま
   食わせられるので主張の検証に、後者は計測器自身の往復（床）に使う。
3. **esmini は roadmark ライン の OSI 点を「レーンの外側端」に置く**（`SetRoadMarkOSIPoints` が
   `SetRoadMarkPos(..., offset=0, ...)` を呼び、`SetRoadMarkPos` は
   `offset_ = SIGN(lane_id)*width/2 + offset`）。つまり `LaneRoadMarkTypeLine::GetTOffset()` は
   OSI 出力には効いていない。S3 の `physical_boundary_id` で「その t 位置に物理境界があるか」
   を判定するとき、**esmini のモデルでは roadmark ラインは常にそのレーンの外側端にある**。
4. **`LaneBoundaryOSI` もレーンの外側端にある**（`SetLaneBoundaryPos` が
   `offset_ = SIGN(lane_id)*GetWidth(s,lane_id)/2`）。ただし作られるのは
   `n_roadmarks == 0` のレーンだけ（§2-3）。

#### S3（2026-09-24）— 論理境界

`LogicalLaneBoundary` と `left/right_boundary_id`。置いたもの:

| 追加/変更 | 何 |
| :-- | :-- |
| 変更 `GT_OSIReporter_LogicalLane.cpp` | pass 2（境界）と、pass 3 での左右境界の結線。`EdgeSide` / `EdgeSample` / `EdgeBuilder` / `MapPassingRule` / `BoundaryKey` |
| 変更 `test_OsiLogicalLane.cpp` | S3 の 4 テスト（被覆・共有と高さ例外の両極性・passing_rule・5cm）。傘バイナリで **OsiLogicalLane 29/29 緑** |
| 新規 `scripts/probe_osi_logical_lane_boundary.py` | 実測プローブ。出力 `test_results/osi_logical_lane/boundary_probe.json` |
| 変更 `scripts/probe_osi_logical_lane_size.py` | S1 段の「境界は 0 本」判定を S3 の「レーン端ごとに 1 本以上」へ。増分表を層まるごとに |

**設計との差分（コードが正、本書をコードに合わせた）**:

1. **レーン端の t は `SetLanePos` から取るほうが良い。** S2.5 引き継ぎ 1 の式
   （`GetLaneOffset(s) + SIGN*GetOuterOffset(s,k)`）は正しいが、`SetLanePos(road, k, s,
   SIGN(k)*width/2)` が同じ t を返し、**かつ物理境界（`SetLaneBoundaryPos`）と同じ呼び方**になる。
   隣接レーンの見る t が一致するのは `GetInnerOffset(s,k) == GetOuterOffset(s,k-sign(k))` が
   esmini の中で恒等式（`GetOuterOffset` は 1 ずつ戻る再帰）だからで、近似ではない。
2. **`SetTrackPos` を edge の t で呼んではいけない。** `Track2XYZ` は `EvaluateLaneHeight()` を
   通すので、edge 上では `Track2Lane` がどちらのレーンに落ちるかで z が変わる。**レーンを指定して
   評価する**こと。
3. **中点だけの分割判定では足りない**（上記 §2-3）。
4. **`Refine(a, b, out, ...)` の a/b は値渡し。** 呼び出し側は `out.back()` を渡すのが自然で、
   再帰の中の `push_back` が同じ vector を再確保すると参照が浮く。
5. **分割判定は全 side の z を見る。** 幾何の由来側だけで見ると、multi_intersections road 276
   （22 m の区間で歩道の高さが s=3 で 0.02 → 0.12 に段をつける）は 2 点で収束し、**もう一方の
   側の段をまたいで線形補間される**（実測 0.10 m ずれ）。
6. **`physical_boundary_id` は `gt->lane_boundary()` の会員に限る。** 後段パスなので既に完成して
   おり、ここで判定すれば参照の閉包が構成的に保証される。roadmark ラインのうち
   `GetSOffset() > 0` のものは「参照先は短くてはならない」に反するので落とす。

**実測（受入基準ごと）**:

| 受入基準 | 実測 |
| :-- | :-- |
| 左右境界が `[start_s, end_s]` を隙間・重複なく覆う | 5 資産・**386 論理レーン × 2 辺すべて**で、先頭境界の始 s == `start_s`、末尾境界の終 s == `end_s`、連結点の s 差 0（許容 1e-6） |
| 隣接境界の接点が一致 | 道路標示で分割された **63 か所**（multi_intersections）で、前の境界の最終点と次の始点のワールド距離 **最大 0 m** |
| 最大横偏差 ≤ 5cm | **0.0250 m**（multi_intersections road 210/200 の接続路、R≈19 m 付近の別区間）。5 資産の全 14,812 サンプル。縦は **0.0139 m**（規格 0.02）。**測ったのは構築に使っていない s**（黄金比刻み）で、**別 DLL（esminiRMLib）が独立に組み立てた理想レーン端**と比べている |
| 曲率の大きい区間を実際に踏んだか | サンプル中の最大曲率は soderleden **0.833 1/m（R=1.2 m）**、multi_intersections 0.465（R=2.2 m）、fabriksgatan 0.370（R=2.7 m）。**最悪偏差はそこではなく緩い曲率側に出る** — 急曲率は budget まで分割されるので、残るのは種グリッドがたまたま長い区間だけ |
| 隣接 2 レーンが同じ境界 id を共有 | **126 組が共有**。残り **120 組は高さで分割**（0.12 m の歩道・縁石）。両方が実データに出ている（e6mini / highway_merge_split は共有のみ、fabriksgatan / soderleden / multi_intersections は両方） |
| 物理境界の参照閉包 | **639/639** の `physical_boundary_id` が `lane_boundary[]` に実在（5 資産） |
| `GT_OSI_LOGICAL_LANE=0` で空 | 5 資産すべてで `logical_lane_boundary[] = 0`、`logical_lane[] = 0` |
| 毎フレーム側が増えていない | S2.5 が narrow した判定（ON 側から `logical_lane_assignment` だけ剥がして再直列化し OFF と SHA 一致）を **そのまま緑で通過**。S3 は静的側にしか触っていない |
| 回帰ゲート | **PASS**（unit 29/29 緑 / ODR quick 緑 / behavioral 67 シナリオ 0 deviation） |

**サイズ実測（境界込み、`scripts/probe_osi_logical_lane_size.py`）**:

| 資産 | 論理レーン | 境界 | 境界点 | 参照線 B | 論理レーン B | 境界 B | 増分 B | static x | roadnet x |
| :-- | --: | --: | --: | --: | --: | --: | --: | --: | --: |
| e6mini | 14 | 15 | 1,084 | 2,363 | 1,925 | 53,416 | 57,704 | 1.26x | 1.78x |
| fabriksgatan | 44 | 72 | 729 | 6,588 | 5,427 | 37,105 | 49,120 | **2.85x** | 2.85x |
| multi_intersections | 242 | 440 | 6,514 | 29,442 | 30,992 | 328,307 | 388,741 | 2.48x | 2.71x |
| soderleden | 33 | 51 | 847 | 4,117 | 4,074 | 42,431 | 50,622 | 2.04x | 2.04x |
| highway_merge_split | 53 | 66 | 633 | 4,899 | 6,599 | 32,318 | 43,816 | 1.96x | 1.96x |

**境界が層の 84〜93% を占める**（設計の予想どおり）。1 境界点あたり約 50 B。

> **点密度と budget の取引**: 構築 budget は規格値の**半分**（横 0.025 m / 縦 0.01 m）にしてある。
> 実測の最大偏差はちょうどその budget に張り付く（0.0249〜0.0250 m）ので、budget を規格値の
> 5cm にすれば点数は約 1/√2 に、境界バイト（層の 9 割）も同じだけ減り、fabriksgatan の 2.85x は
> 2.3x 前後まで下がる。**取らなかった**: 偏差が規格値ちょうどに張り付く設計は、まだ測っていない
> 資産や、サンプル運で規格を超える。§7-3 の閾値 3x に対して 2.85x はまだ余裕の側にあるので、
> 精度をバイトと交換する理由がない。必要になったら `--osi_lateral_deviation` で両方が同時に動く。

#### S2（2026-09-24）— 連結性

`predecessor_lane` / `successor_lane` / `left_adjacent_lane` / `right_adjacent_lane`。置いたもの:

| 追加/変更 | 何 |
| :-- | :-- |
| 変更 `GT_OSIReporter_LogicalLane.cpp` | pass 4（前後接続）と pass 4b（横隣接）。`LaneEnd` / `PathStats` / `connect()` / `lane_width_at()` |
| 変更 `test_OsiLogicalLane.cpp` | S2 の 6 テスト。傘バイナリで **OsiLogicalLane 35/35 緑** |
| 新規 `scripts/probe_osi_logical_lane_connectivity.py` | 実測プローブ。出力 `test_results/osi_logical_lane/connectivity_probe.json` |
| 変更 `scripts/probe_osi_logical_lane_boundary.py` | 両極性の見出し文字列が S1 のまま（`=1` / `unset (default OFF)`）で**コードと逆のことを言っていた**ので是正。判定側は最初から正しい |

**設計との差分（コードが正、本書をコードに合わせた）**:

1. **道路間（junction 外）は `UpdateOSIRoadLane` の流用ではなく、リンク自身の `@contactPoint` で解く。**
   §2-4-1 の表は「既存の前後セクション解決をそのまま流用」と書いていたが、あの既存コードは
   *相手道路が自分を指し返しているか*から接触端を推論している。`RoadLink::GetContactPointType()` が
   同じことを直接言っており、相手道路が長くても端を取り違えない。**流用しなかった**。
   `@contactPoint` が欠けている（`CONTACT_POINT_UNDEFINED`）ときは推論せずに落とす — 実測 5 資産で 0 件。
2. **重いのは junction 経由、という見積りは外れた。** multi_intersections の内訳は
   road-link 経由 326 件・junction 経由 152 件で、**多いのは road-link 側**である。理由は接続路自身の
   2 つの端が road-link 経路で解けるからで、junction 経路が要るのは
   **incoming 道路から外を見るときだけ**。所要時間はどちらも計測できる量ではない（静的 1 回）。
3. **OpenDRIVE は junction の接続を片側からしか宣言しないので、ミラーを自分で書く必要がある。**
   `<connection>` は incomingRoad しか名指ししない。出口側の道路は接続路を指すリンクを持たない。
   各レーンが「自分のリンクが言うこと」だけを emit すると、**交差点の出口で必ずグラフが途切れる**。
   `connect()` は 1 つの出会いから常に両方向を書く。
4. **`Connection` の逆向きは esmini が自分で合成している。** `GT_RoadManager.cpp:7066` 付近が
   `Connection(connecting, incoming, new_contact_point)` を `from_`/`to_` ごと入れ替えて足す。
   **つまり junction 経路と road-link 経路は同じ出会いを 2 度、別経路から導出する。**
   これは偶然の冗長ではなく相互検証に使える（下の「両経路の一致」）。
5. **ゼロ幅の端は繋がない。** 規格は `Both lanes have a non-zero width at the connection point` を
   要求している。合流・分流でテーパ 0 まで細った端にも OpenDRIVE の `<link>` は残るので、
   写すと「幅 0 のレーンを通り抜けられる」と言うことになる。実測 soderleden で 1 件落ちた。
6. **`at_begin_of_other_lane` の両極性は、同一道路内では構造的に 50/50 で出るので証拠にならない。**
   次のレーンセクションは必ずその始端から入るからである。**接触点由来なのは道路境界を跨ぐ接続だけ**なので、
   プローブは cross-road とそれ以外を分けて数える。

**実測（受入基準ごと）**:

| 受入基準 | 実測 |
| :-- | :-- |
| 参照の閉包 | **1,268/1,268**（5 資産、pred+succ+left+right の全 `other_lane_id`）が `logical_lane[]` に実在。dangling 0 |
| `at_begin` の両極性（接触点由来のものだけ） | 道路境界を跨ぐ接続 **332 true / 300 false**。うち接続路に触れるもの **292 true / 140 false**。経路別（C++ ログ）では road-link `[T=180 F=146]`（multi_intersections）、junction `[T=76 F=76]`。**両経路とも両極性が出る** |
| 交差点の連結 | multi_intersections **接続路レーン 76/76 に pred と succ の両方**。fabriksgatan 20/20、highway_merge_split 12/12 |
| 隣接と境界の整合（規格 5cm） | 隣接ペア **286 組**：166 組は境界 id を共有（構造上 0 m）、**120 組は高さで分割**され実測対象になる。**最大 0.0000 m** |
| 上の測定が vacuous でないこと | 同じ関数を**意図的に誤ったペア**（自分の右境界 vs 左隣の左境界）に当てると **223 組で最大 21.516 m** を返す。5cm の閾値をはるかに超える＝計器は値を出せる |
| 並び順 | 違反 0。ただし**各リストは最大 1 件**（セクション内で片側 1 本）なので順序は構成上自明。`end_s > start_s` 違反も 0 |
| 両経路の一致 | 同じ相手を逆の `at_begin` で 2 度名指しした例 **0 件**（5 資産）。road-link 由来と junction 由来が食い違えばここに出る。multi_intersections は 240 出会いのうち **238 が両側から独立に宣言**された |
| ミラーの正しさ | ユニット `EveryConnectionIsMirroredWithTheOppositeEnd` が 3 資産の全接続について「相手も自分を、逆の端で名指している」を検査 |
| `GT_OSI_LOGICAL_LANE=0` で空 | 5 資産すべてで `logical_lane[] = 0`、接続 0。ON 側は非空 |
| 毎フレーム側が増えていない | S2.5 が narrow した判定（ON から `logical_lane_assignment` だけ剥がして再直列化し OFF と SHA 一致）を**そのまま緑で通過**。S2 は静的側にしか触っていない |
| 回帰ゲート | **PASS**（unit 35/35 緑 / ODR quick 緑 / behavioral 0 deviation） |

**検知器が反転していないことの実証（意図的な欠陥を 2 つ注入）**:

| 注入した欠陥 | 赤になったテスト |
| :-- | :-- |
| 出力時に `at_begin_of_other_lane` を反転 | `SuccessorIsTheHigherSNeighbourOnBothMoveDirections` / `EveryConnectionIsMirroredWithTheOppositeEnd` |
| `right_adjacent` と `left_adjacent` を入れ替え | `AdjacencyIsInReferenceLineDirectionInBothTrafficHands` / `LaneMinusOneAndPlusOneAreNeighboursAcrossTheCentreLane` |

残り 31 件は両方とも緑のままだった。**プローブ側はどちらの欠陥も捕まえない**（全体反転では両極性の
集計が入れ替わるだけ、左右入替は対称性を保つ）。層の役割分担として記録しておく:
**プローブは「接触点の分岐が生きているか」を、ユニットは「向きが正しいか」を見ている。**

**サイズ実測（連結性込み、`scripts/probe_osi_logical_lane_size.py`）**:

| 資産 | 論理レーン B（S3→S2） | 層の増分 B（S3→S2） | 連結性ぶん | static x |
| :-- | --: | --: | --: | --: |
| e6mini | 1,925 → 3,043 | 57,704 → 58,822 | +1,118 | 1.26x → **1.27x** |
| fabriksgatan | 5,427 → 8,599 | 49,120 → 52,292 | +3,172 | 2.85x → **2.97x** |
| multi_intersections | 30,992 → 50,898 | 388,741 → 408,647 | +19,906 | 2.48x → **2.56x** |
| soderleden | 4,074 → 6,735 | 50,622 → 53,283 | +2,661 | 2.04x → **2.10x** |
| highway_merge_split | 6,599 → 10,920 | 43,816 → 48,137 | +4,321 | 1.96x → **2.06x** |

**予想は「連結性は id の列なので小さいはず」だった。小さいのは当たったが、理由は外れている。**
層全体に対して 2〜5% で、境界（84〜93%）に比べれば確かに小さい。しかし**バイトの出どころは id ではない**:
multi_intersections の内訳は接続 480 件（`LaneConnection` = id + bool、約 11 B）に対し隣接 358 件
（`LaneRelation` = id + **double 4 本**、約 45 B）で、**隣接が連結性バイトの 8 割**を占める。
`start_s` / `end_s` / `start_s_other` / `end_s_other` は同一セクション内では 4 つとも同じレーン範囲の
繰り返しだが、規格が要求するフィールドなので削れない。

> **fabriksgatan が 2.97x に達した（§7-3 の反転条件は 3x）。** 条件は「3 倍超 **かつ** OSI 記録が
> 日常のワークフロー」の AND なので**既定 ON を変えない**が、余裕は 1% を切った。分母が小さい
> （静的 GT 26.5 KB）ための比であって、絶対値は 1 回きりの +52 KB である。次に層へ何かを足すときは、
> **fabriksgatan を先に測る**こと。3x に最初に触るのはこの資産で、次点の multi_intersections
> （2.56x）とは 0.4 の開きがある。

**S5 へ引き継ぐ発見**:

1. **`signal:logical_lane_topology` の観測経路は静的 GT の第 1 レコードだけ。** 連結性は
   `logical_lane[]` にしか載らず、毎フレームの GroundTruth には出ない（S0 以来の性質）。
   matcher は `.osi` の第 1 レコードを読む形にしないと、常に空を見て緑になる。
2. **「数が増えた」型の matcher にしないこと。** 連結性は 4 つの `repeated` で、どれも
   欠けていても message は well-formed である。閾値を「> 0」に置くと、片側だけ壊れた実装が通る。
   常設ゲートに載せるなら**閉包（dangling 0）と接続路レーンの被覆率**を見る形にする — この 2 つは
   分母が真実源（xodr のレーン数）から出るので、実装が縮んだときに分子が減る。
3. **両極性の実証は「ON=未設定 / OFF=`0`」で書く。** 既定 ON 反転以降、「ON=`1` / OFF=未設定」の
   ままのプローブは両 run が ON になる。S3 で踏んだ罠で、本段でも boundary プローブの
   **見出し文字列だけ**が S1 のまま残っていた（判定は正しかった）。文字列も証拠の一部である。
4. **隣接は消費側から復元できるので、ゲートで守る価値が高いのは前後接続のほう。**
   隣接する 2 レーンは 166/286 組で境界 id を共有しており、残り 120 組も XY が一致する。
   つまり隣接は境界から再構成できる（§8-α で S2 を最後に置いた理由そのもの）。
   OSI の中に代替の手がかりが無いのは前後接続だけである。

#### S5（2026-09-24）— 常設化

置いたもの（3 コミットに分けた。KG の lint と回帰ゲートは別々に落ちうるため）:

| コミット | 追加/変更 | 何 |
| :-- | :-- | :-- |
| ② | `signal_catalog.yaml` | `logical_lane_topology` / `logical_lane_assignment` / `ego_route_lane_segments` を exposure / state 付きで収載 |
| ② | `namespaces.yaml` | `matcher` の列挙 `id_pattern` に `route_matches_plan`、`count: 35 → 36` |
| ② | `vd_metrics.py` | matcher `route_matches_plan` 本体 |
| ② | `gt_sim_test.py` | 静的 scene キーに `logical_lanes` を追加（前方充填）、VirtualDriver 側でも osi capture 時に HVD を取る |
| ② | 新規 `test_route_matches_plan.py` | 単体両極性 17 ケース（緑 11・赤 6） |
| ③ | `route_lane_batch.yaml` | `osi: false → true` |
| ③ | `06_route_lane/*.expectations.yaml` | `route_matches_plan` の must を 6 本へ配分（うち 1 本は負の対照） |
| ③ | `route_lane_expected.yaml` | ベースライン再凍結（15 matcher 追加） |
| ③ | `gate_catalog.yaml` | `gate:route-lane-regression` の covers / not_covers / requires / 昇格手順 |
| ③ | `run_odr_conformance.py` | OSI 抽出の拡張 C（`osi_dump_logical_lanes`、opt-in） |
| ④ | `graph.yaml` | 縦串 5 辺（observes ×3 / sustained-by / verifies） |
| ④ | `capability_model.md` | §2.2a **W4 の `route` 行を解消**、`spine-work:osi-logical-lane` の進捗 |

**設計との差分（コードが正、本書をコードに合わせた）**:

1. **matcher は 1 本だが、読む signal は 3 つになった。** §9 は
   「HVD の `route` と telemetry の `route_lane` が同じ経路を指していること」とだけ書いていたが、
   それだけでは**参照先が実在するか**を見ていない。`route` は論理レーン id の列なので、
   id が実在しなくても well-formed な message になる。`signal:logical_lane_topology` を
   同時に読んで閉包と連結を見ないと、S2 で入れた連結性は常設で 1 バイトも踏まれない。
2. **ハーネスに配線が 2 本要った。** §9 はこれを見積もっていなかった。
   - `GroundTruth.logical_lane[]` は**静的 GT の第 1 レコードにしか出ない**ので、
     `gt_sim_test.py` の `_STATIC_SCENE_KEYS` に入れて前方充填しないと matcher は常に空を見る。
   - `HostVehicleData` は **ManualDrive 経路でしか取っていなかった**。VirtualDriver 側でも
     osi capture 時に取るようにした（`route` は HVD にしか載らないため）。
3. **ODR 適合の拡張 C は入れたが、どのフィクスチャでも有効にしていない**（§9 の注を参照）。
   有効化＝そのフィクスチャのゴールデンの書き直しで、いま 13 件が本作業と無関係に stale なため。
   そもそも OSI 層は `--profile full` でしか走らず、回帰ゲート Step 1.5 は quick・CI は schema 層
   だけなので、**有効化しても常設ゲートにはならない**。常設で踏むのは ③ の matcher のほう。

**実測（受入基準ごと）**:

| 受入基準 | 実測 |
| :-- | :-- |
| 新 matcher が ON で緑 | 6 シナリオ 15 must すべて pass。実データで 53 論理レーン・経路 4 セグメント（road0 lane-4 ×2 セクション → road4 接続路 → road2）、閉包は最大 3,320 id / run、連結は最大 2,480 seam / run |
| **意図的な違反データで赤になる** | `test_route_matches_plan.py` に 6 赤。中核は **「経路はそのまま・トポロジから `succ`/`pred` を抜く」**で、経路の形も長さも変わらないのに fail になる＝件数型の判定が見逃す欠陥を捕まえている。ほかに dangling id・セクション飛ばし・両面のレーン集合不一致・経路が出ない・`expect_route_present: false` の逆向き |
| vacuous pass の封じ | 4 ケースが skip（`must` が何も指定しない／`scene.logical_lanes` が無い＝`osi: false`／`hvd.route` が無い／セグメントが 1 本しかなく継ぎ目を 1 つも見ていない）。**いずれも pass にしない** |
| 負の対照が実バッチにあること | `merge_required_for_exit_ramp` が `expect_route_present: false`。実測 **0/840 フレーム**が経路を持たず、兄弟 5 本は 533〜840 フレームで持つ。esmini が粗い 2 WP 経路を無効と判定するため |
| ベースライン凍結の前提（自己決定論性） | バッチを **3 回連続実行**し、6 シナリオ × 全 matcher の `(event, status, detail)` が**完全一致**（`detail` はフレーム数を含む文字列なので、数が 1 つでも動けば差が出る） |
| ベースライン凍結の前提（比較器の発火） | 凍結前に 15 件の `matcher_added` を検出（exit 1）。凍結後、**別 run** に対して deviations=0。そのうえで記録済み `route_matches_plan` の status を 1 つ反転させて **deviations=1 / exit 1**（`regression (expected=pass actual=fail)`） |
| ODR 拡張 C の両極性 | fabriksgatan で OFF＝鍵が 1 つも増えない（抽出はバイト同一）、ON＝`logical_lane_count=44 / logical_lane_boundary_count=72 / reference_line_count=16`（連結性プローブの実測と一致） |
| `--spine-report` の動き | ④観測欠 (b) が **19 → 20**（総数 118 → 119）。**予想と逆に増えた**。下の「台帳の数え方」を参照 |
| lint / `--render` | 両方グリーン |
| 既定 ON で `/gates` | **PASS**（unit 35/35 / ODR quick / behavioral 67/67 deviation 0、route_lane 6/6 を含む） |

**台帳の数え方を取り違えた（実測して直した）**:

`--spine-report` の ④(b) は **`signal_catalog.yaml` の `state` 欄だけで数えている**。
`observes` 辺の有無は見ていない（見ているのは「`state` が (b) なのに配線がある」の側だけ）。
当初 3 signal をすべて `state: "●"` で収載したため、**どれ 1 つとして台帳に現れず**、
辺を 5 本張っても件数は 19 のまま動かなかった。

そこで `logical_lane_assignment` を **`(b)` へ直した**。emit はされているが読む matcher が
無いのは事実で、`"●"` と書くのは穴を台帳から消す行為だった。結果 19 → 20。
**増えたのが正しい動き**である。残り 2 本（`logical_lane_topology` /
`ego_route_lane_segments`）は matcher と gate まで届いているので `"●"` のままで、
台帳には元々現れない。

> **検知器の非対称**: 台帳は「`(a)` なのに観測する matcher がある」（楽観の逆向き）は検査するが、
> **「`"●"` なのに誰も読んでいない」は検査しない**。実測すると `"●"` 31 本のうち 10 本に
> `observes` 辺が無く（`ego_orientation` のように `_ego_state` 経由で判定へ届いている正当な例を含む）、
> 一律の lint にすると警報疲れを招く。**今回は lint を足さず、記帳を正した。**
> ただし「`"●"` は自己申告であり、台帳はそれを検査していない」ことは覚えておくこと。

**常設ゲートが踏まないもの（`gate_catalog.yaml` の `not_covers` と同じ内容。ここに書いておく）**:

- 境界（`logical_lane_boundary[]` の被覆・共有・5cm 整合）と `at_begin_of_other_lane` の両極性は
  **手動プローブだけ**が持つ（`probe_osi_logical_lane_{boundary,connectivity}.py`、5 資産）。
- 横隣接と向きの正しさはユニット（傘バイナリ＝Step 1 で常設）が持つ。
- 常設ゲートが踏む道路は `highway_example_with_merge_and_split.xodr` **1 資産だけ**。
  multi_intersections 規模の交差点連結は常設では守られていない。
- `signal:logical_lane_assignment`（L1）は読む matcher が無く ④(b) のまま。

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
| 新規 | `GT_esmini/include/gt_esmini/osi/RouteToOsiRoute.hpp` | **`control` の `RouteLanePlan.hpp` を include する** — 下の注を見よ |
| 変更 | `GT_esmini/src/osi/GT_OSIReporter.cpp` | 後段パスの呼び出し 1 行 |
| 変更 | `GT_esmini/src/osi/GT_OSIReporter_Moving.cpp` | L1 の**呼び出しのみ** 4 行（`:979` の直後）。本体は `GT_OSIReporter_LogicalLane.cpp` 側に置く — 同ファイルは `lineage:gt_osireporter` のフォーク系譜なので、inbound 差分を増やさないため |
| 変更 | `GT_esmini/src/osi/GT_HostVehicleReporter.cpp` | `route` 充填 + `Send()` の分割送信 |
| 変更 | `GT_esmini/src/control/virtualdriver/VirtualDriverTelemetryJson.cpp` | L2 経路進捗を `route_lane` ブロックへ追加（~8 行） |
| 変更 | **`EnvironmentSimulator/Modules/ScenarioEngine/CMakeLists.txt`** | **R1 承認が要る 4 行**（新 .cpp をスワップリストへ）。**2026-09-24 承認・実施済み**（S0） |
| 変更 | `GT_esmini/CMakeLists.txt` | `RouteToOsiRoute.cpp` を `GT_OSI_SOURCES` へ |
| 変更 | `GT_esmini/test/CMakeLists.txt` | 新ユニットテストの登録 |

R1 の当たりは `ScenarioEngine/CMakeLists.txt` の 4 行のみ。`OSIReporter.hpp` は触らない
（メンバ関数ではなく GT 自由関数として書く）。

> **モジュール依存の注（2026-09-24、S4）**: `RouteToOsiRoute.hpp` は `osi` モジュールにありながら
> `control` の `RouteLanePlan.hpp` を include する。`GT_esmini/CLAUDE.md` §2 の `osi -> core, scenario`
> には無い向きである。**`RouteLanePlan` と `osi3::Route` の両方を見る関数に合法な置き場が無い**ため
> で、`core` は両方より下、`osi` 側に `RouteLaneBand` を写すと同じ経路バンドの定義が 2 つになる
> （手で同期し続ける必要が出る）。`RouteLanePlan.hpp` 自体は roadmanager にしか依存しない道路
> トポロジのヘルパで、制御パイプラインではない。ファイル単位の逆向き依存は既に 1 件あり
> （`GT_OSIReporter_Moving.cpp` → `control/common/TransitionDynamics.hpp`）、新種ではない。
> 逆向き（`control` → `osi`）は `PlannedPathBuilder.hpp` → `GT_PlannedPathRegistry.hpp` の前例がある。
> **`RouteLanePlan` を中立モジュールへ移すのが本筋だが、`vd-component:route-lane-plan` として
> 知識グラフに登録済みなので、移動は記帳側の変更とセットで別途判断する。**

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
8. **L1 の s/t が参照点（bbox 中心）であること** — entity origin ではないこと。直線では
   `s_position == pos.GetS() + center_x` / `t_position == pos.GetT()`、ヨーがあれば
   `center_x·sin(h_rel)` だけ横へ動き、曲路では 1 次近似から外れること（§2-6-1 注 2）。
   **origin を入れた版が落ちること**を同じテストの中で固定する（負の対照）。
10. **境界の被覆と共有** — 各論理レーンの `left/right_boundary_id` が `[start_s, end_s]` を
   隙間・重複なく覆うこと。隣接 2 レーンが同じ境界 id を名指すこと、**ただし高さの違う
   レーン同士（縁石・歩道）は規格の但し書きどおり分かれ、XY 一致・z 相違であること**。
   両方の結果が実データに出ることを要求する（片方しか起きない資産では検査が半分死ぬ）。
11. **`passing_rule` が `UNKNOWN` を出さない**こと、かつ**全部が同じ値でない**こと
   （全部 OTHER の実装は「UNKNOWN を出さない」検査を素通りする）。
12. **境界の横偏差 ≤ 5cm** — 構築に使っていない s で測ること。重い測定
   （別 DLL・5 資産・曲率つき）は `scripts/probe_osi_logical_lane_boundary.py`。
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

#### S5b — プローブを常設ゲートへ（2026-09-24）

`gate:route-lane-regression` **だけでは足りない**ことが S5 完了時に判明した。同ゲートが踏む
道路は **1 資産（highway_merge_split）** で、S2 の主眼だった**交差点の接続路連結**
（multi_intersections 76 本）と S3 の**境界 5cm 整合**は、そこから構造的に到達できない。

手動プローブのまま置くと `gate:odr-conformance-full` の OSI ゴールデンと同じ経路で腐る。
**同じ日にその実例を検出している** — `--profile full` は CI にも回帰ゲートにも起動点が無く、
ゴールデン 13 件が 3 ヶ月 stale だった。

そこで回帰ゲートに **Step 1.6（HARD、`-SkipOsiProbes`）** を新設し、5 本のプローブを常設した。
詳細な covers / not_covers は `gate_catalog.yaml` の `gate:osi-logical-lane-probes`。

| 検証 | 結果 |
| :-- | :-- |
| 緑側 | 5 本とも PASS、合計 **13 秒**（connectivity 2s / boundary 3s / assignment 3s / size 3s / hvd_route 2s） |
| **赤側（配線の負の対照）** | 存在しないプローブを 1 本混ぜた同一ロジックで `$osiFailed` が埋まり `overallOk=False`、exit 1。**緑と赤の両方を実証してから常設化した** |
| 引数の展開 | `@($probe.Args)` が空配列で無引数、`@("--skip-udp")` で 1 引数に展開されることを実測（PowerShell の配列アンロール） |

**`probe_hvd_route` は `--skip-udp` で入れている。** UDP 分割送信の正しさは S4 の手動実測
（`osi_bridge` 40/40・`udp_common` 40/40）が一次証拠で、ソケット経路は環境 flaky のため
常設からは外した（CI で UDP を外した前例と同じ判断）。

> **外側の環境変数でプローブを赤にしようとしても赤にならない。** 各プローブは ON/OFF を
> **自分の子プロセスごとに**作る（`env.pop(ENV_FLAG)` で ON、`"0"` で OFF）ので、親の
> `GT_OSI_LOGICAL_LANE` に免疫がある。これは正しい設計であって腐敗ではない。
> 負の対照はプローブの内部（各プローブが既に持つ）か、ゲート配線側（上表）で取ること。

### ODR 適合

`run_odr_conformance.py` の OSI 抽出に `logical_lane_count` / `reference_line_count` /
`logical_lane_boundary_count` を追加する（`DUMP_POLYGONS` と同じ opt-in 形にして、
フラグ無しのフィクスチャは byte-identical に保つ）。

> **2026-09-24（S5 実施）**: 機構は入れた（マニフェストの `osi_dump_logical_lanes: true`。
> 拡張 C）。**どのフィクスチャでも有効にしていない。** 有効化はそのフィクスチャのゴールデンを
> 書き直すことを意味し、いま 13 件のゴールデンが本作業と無関係に stale なので、
> 自分の追加と 3 ヶ月ぶんのドリフトが同じコミットで凍結されてしまう。
> 動くことは実測で示した: fabriksgatan に対して OFF では鍵が 1 つも増えず、ON では
> `logical_lane_count=44 / logical_lane_boundary_count=72 / reference_line_count=16`
> （連結性プローブの実測値と一致）。
> **残作業**: ゴールデン再生成の Issue が片付いた後、版・機能ごとに 1 本ずつ有効化する。
> なお OSI 層は `--profile full` でしか走らず、回帰ゲート Step 1.5 は quick、CI は schema 層だけ
> なので、**ここを有効化しても常設ゲートにはならない**（常設で踏むのは
> `gate:route-lane-regression` の matcher:route_matches_plan のほう）。

---

## 10. 既知の非充足と将来課題

1. **`overlapping_lane` を出さない。** 交差点内で経路が交差する区間の s レンジ。幾何計算が要り、
   RoadManager に概念がない。`repeated` なので規格違反ではないが、交差点の competing path を
   OSI から読む消費側には情報が足りない。+2〜3 日で別途。
2. ~~**境界の 5cm 精度が参照線の s グリッド依存。**~~ **解消（S3）。** 種グリッドを 3 点判定で
   再帰分割する（§2-3）。実測 **0.0250 m / 0.0139 m**（規格 5cm / 2cm）で、測定は構築に使って
   いない s と別 DLL による。ただし**この偏差は budget（規格値の半分）で頭打ちになる設計**で、
   さらに詰めたければ `--osi_lateral_deviation` を下げる（境界も同じ値に追従する）。
3. **Z 方向 2cm は実測で満たしているが構成的な保証ではない。** 分割判定は z も見るので
   勾配・カント・`<height>` の段は拾うが、budget は `kBoundaryBudgetZ = 0.01` の固定値であり
   `OSI_MAX_LATERAL_DEVIATION` のような設定項目に繋がっていない。カント厳密化と同じ土俵の課題。
4. **速度制限が道路単位。** OpenDRIVE の `<lane><speed>` はパーサが読んでいない。
   `traffic_rule[].speed_limit` には `Road::GetSpeedByS()` の値が入る。
5. **連続レーンの併合をしない。** 規格は同型・単一後続のレーンを 1 本の論理レーンへ併合することを
   許しているが、行わない。論理レーンは常に 1 レーンセクションで切れる。消費側から見ると
   セグメント数が多くなるだけで、意味は変わらない。
6. **`source_reference` が規格本文の素の id 形式ではなく GT の接頭辞付き形式。** §2-2。
7. ~~**L1 の割り当てが 1 レーンのみ。**~~ **2026-09-24 にスコープへ戻した**（L1-b、§2-6-1 / §8-α）。
   S2.5 で車体幅から重なりを判定し、5cm を超えるレーンすべてに割り当てる。
8. **L3（経路レーン帯からの符号付き横距離）を出さない。** §2-6-3。OSI に欄がなく、基準の取り方も
   未定。`on_target_lane` の bool と L1 の `t_position` で「どのレーンにいて経路は何を許すか」は
   外から判定できるので、必要になった時点で用途と一緒に設計する。
9. **L2 を OSI の型付き欄へ出さない（ただし規格上の不足ではない）。** §2-6-2。

   > **2026-09-24 訂正**: ここは当初「OSI しか読まない消費側からは経路進捗が見えない」と
   > 書いていたが、**過大だった**。L2 も L3 も**公式フィールドの組み合わせから消費側が導ける**。
   >
   > - **L2** = `route.route_segment[]` は順序付きで各 `lane_segment` が `start_s`/`end_s` を持つ。
   >   L1 の `assigned_lane_id` で自車がどの segment にいるか分かるので、
   >   「そこまでの segment 長の累積 + (`s_position` − `segment.start_s`)」で出る。
   > - **L3** = segment に含まれるレーン集合が「許されるレーン」。L1 の `t_position` が今いる
   >   レーンでの横位置。S3 の境界からレーン幅が引けるので帯の最寄りまでの距離も出る。
   >
   > OSI が持っているのは **L1 と `route` という原材料**で、L2 / L3 はそこからの導出値である。
   > `Route` が `high level path information, similar to that of a map or a navigation system` と
   > 定義され、そこに自車位置の欄をわざと持たせていないのも同じ思想。
   > **レーン相対は公式、経路相対は導出**という切れ目は規格の切り方であって GT の都合ではない。
   >
   > したがって GT が L2 を出すのは**計算の肩代わり（便宜）**であり、置き場の選択は
   > 「規格の不足をどこに逃がすか」ではなく「消費側に計算させるか、させないなら
   > どの範囲の ego に出すか」である。`custom_detail`
   > （`repeated KeyValuePair`、規格自身が `An opaque set of key-value pairs` と定義）へ
   > 載せても公式フィールドにはならず、telemetry と性質は同じ。

   **既知の提供範囲**: L2 は VD telemetry にしか出ないので、**VD 以外の ego（手動運転 /
   DefaultController）では取れない**。`route` 自体は `UpdateFromObjectState` が
   コントローラに関係なく呼ばれるため全 ego で出るので、ここだけ非対称になる。
   手動運転で経路進捗が要るようになったら `custom_detail` へ足す（4 つの KV:
   `s_along_route` / `route_length` / `segment_index` / `on_route`。キー名に単位を埋め、
   受け側のパースは 1 か所に閉じる）。**不足ではなく便宜の提供範囲**として記録する。

10. **経路のレーンが自車位置にまだ存在しないとき、`route` は自車より先から始まる。**（S4 実測）
    合流・分岐路では経路が指すレーンが途中から開くことがある。`highway_example_with_merge_and_split`
    の road 0 はレーンセクションが s=0 / 50 / 175 で、**レーン -4 は s=50 から**しかない。
    自車が s=10 にいる間、指すべき論理レーンが存在しないので最初の `RouteSegment` は s=50 から
    始まり、**自車と経路の間に 40 m の空白ができる**。参照が壊れているわけではない（存在しない
    レーンの id を書くよりは正しい）が、規格の
    `Consecutive segments should be connected without gaps` を自車〜経路始点については満たさない。
    L2 の `on_route` はこの間 false になるので、消費側は区別できる。

11. **`route_id` は「計画」の同一性であって、メッセージ内容の同一性ではない。** 経路が変わらない
    限り id は据え置くが、内容は自車 s で切るぶん毎フレーム変わる（先頭セグメントが縮み、
    通過した道路が落ちる）。**id をキーにメッセージをキャッシュしてはいけない。** 毎フレーム
    送っているので読み直せばよい（§5）。

12. **`route` は OSI GroundTruth を一度も出していないセッションでは空になる。** 索引を埋める
    後段パスが `OSIReporter::UpdateOSIGroundTruth()` の初回にしか走らないため
    （§8-0 S4 差分 6）。`GT_HostVehicleReporter` が一度だけ `LOG_WARN` を出すが、
    設定で解くほうが筋なら S5 で見直す。

13. ~~**L1 の `s_position` / `t_position` は OSI の「オブジェクト参照点」ではない。**~~
    **解消（S2.5b、2026-09-24）。** 出力は参照点（bbox 中心）へ揃えた。§2-6-1 注 2 と
    §8-0 S2.5b。§2-1 の「追加の幾何計算なしで揃う」という主張はこの欄については失効し、
    代わりに `ResolveOsiReferencePoint()` が 1 道路に固定した `XYZ2TrackPos` を 1 回回す。

14. **重なり判定は物体のいるレーンセクション内に閉じている。**（S2.5）長い車両がレーンセクション
    の継ぎ目を跨いでいても、隣のセクションのレーンには割り当てない。規格は面積の重なりで
    定義しているので厳密には不足だが、継ぎ目でのみ、かつ車長の一部でのみ起きる。

15. **参照点は道路の端で飽和する。**（S2.5b 実測）参照点は**自車がいる road に固定して**
    解いている — `s_position` は `assigned_lane_id` が指すレーンの参照線上の値でなければ
    ならず、自由探索させると bbox 中心が次の road に載った瞬間に座標系ごと入れ替わって
    しまうからである。代償として、中心が road の端を越えている間は `s_position` がその
    road の長さで頭打ちになる。誤差の上限は `center_x`（カタログ車 1.4 m）、発生するのは
    road 遷移の 1〜2 フレームだけ。実測: `routing-test.xosc`（multi_intersections、
    交差点を 4 つ通過）279 オブジェクトフレーム中 **4 フレーム**、そこでの最大ずれ
    **1.08 m**。残り 275 フレームは後述の計測器の分解能内で一致する。

16. **単一道路だけの経路は進行方向を +s と決め打ちする。** `RouteLanePlan` は道路が 1 本の
    プランで `exit_at_road_end = true` を固定するため（`RouteLanePlan.cpp` の `skeleton.size()==1`
    分岐）、-s 方向へ進む 1 道路経路は逆向きに展開される。ホップが 1 つでもあれば方向は
    トポロジから決まるので、この穴は 1 道路経路に限られる。

---

## 11. 着手前に決めること

| # | 決めること | 提案 |
| :-- | :-- | :-- |
| 1 | `ScenarioEngine/CMakeLists.txt` への 4 行（R1 例外） | **決着（2026-09-24 承認、S0 で実施）**。既存スワップブロックの拡張（§8 の注を見よ）。前例は RoadManager/CMakeLists.txt への odr_side **10 本**追加（2026-07-02 承認。調査時点で「6 本」と書いていたが実数は 10） |
| 2 | 知識グラフのノード型 | **決着（2026-09-24 ユーザー判断）**: 既存の `spine-work` 名前空間へ `spine-work:osi-logical-lane` として起こす。`feature:F10` は採らない（`F1..F9` はユーザーに見える機能で本件と性格が違ううえ、凍結体系の `id_pattern` 拡張が要る）。face-1 work-item 名前空間の新設も採らない（実体 0 件。`spine-work` は face タグが "3" だが `ego-anchor-face1-migration` / `osi-assigned-lane-driving` という face-1 の実体を既に 2 件収容している） |
| 3 | `overlapping_lane` をスコープに入れるか | 入れない（§10-1）。必要なら別工程 |
| 4 | S3 完了時に既定 ON へ倒すか | 倒す（§7-3）。S0 の実測で 3 倍超かつ OSI 記録が日常なら再検討 |
| 5 | L1-b（車線跨ぎの複数割り当て）を初版に入れるか | **決着（2026-09-24）: 入れる。** S2.5 に畳んだ（§8-α）。+0.5 日で規格の不足が 1 つ閉じるため |
| 6 | L3（経路帯からの符号付き横距離）の用途 | 未定のうちは設計しない（§10-8）。「誰が何のために読むか」が決まった時点で基準の取り方を決める |
