# OpenX Ontology シーン空間の洗い出し（シーンカタログ第1段・分析ドラフト）

> **位置づけ**: README「将来拡張: VD自動運転対応シーン要求」第1段（シーンカタログ）の入力資料。
> `scene_catalog_vd_ad.yaml`（`scene:SCN-<nnn>`）へ起こすシーンノードの母集団を、
> ASAM OpenX Ontology v1.0.0-PR1-43-g0248cfe（`thirdparty/`、346クラス）の全数調査から導出する。
> **本ファイルは分析ドラフトであり、lint対象（namespaces/graph/concept_vocabulary/graph_view）ではない。**
>
> 作成: 2026-07-17。抽出スクリプト: TTL全クラス+subClassOf階層のパース（Core 71 / Domain 274クラス）。

## 0. 構造上の事実（列挙の前提）

1. **3本の傘クラス（`EnvironmentalCondition` / `RoadTopologyAndTrafficInfrastructure` / `TrafficParticipantAndBehavior`）は subClassOf 階層の根ではない**。傘は「a set of …」と定義された分類マーカーで、実体クラスは Core 側の上位（`PhysicalProperty`, `WholeLifeFunctionalSystem`, `Participant`, `ActivityState` 等）にぶら下がる。→ 本書の分類は意味的グルーピング（concept_vocabulary.yaml の `branch` と同じ流儀）。
2. Core 71クラスはBORO系の抽象上位（State/Event/Set/Relationship等）で、シーン列挙には直接使わない。**シーンの語彙は Domain 274クラスでほぼ完結する**。
3. 既知のcaveats（concept_vocabulary.yaml 冒頭）: OSI未整合、`TrafficLight`クラス無し（`DynamicTrafficSign`で暫定）、OpenDRIVE 1.6 / OpenSCENARIO 1.0 世代の整合。

## 1. 軸1 — 道路トポロジー・交通インフラ（road）

### 1.1 道路種別 `Road`
Motorway / MinorRoad / DistributorRoad（幹線接続路）/ RadialRoad（放射道路）/ DividedRoad（中央分離あり）/ UndividedRoad / Parking / SharedSpace（歩車共存）

### 1.2 交差部 `Junction`
- `IntersectionAtGrade`（平面交差）: CrossRoad（十字）/ TIntersection / YIntersection / StaggeredIntersection（食い違い）
- `GradeSeparatedIntersection`（立体交差）
- `Roundabout`

### 1.3 特殊構造 `SpecialStructure`
Tunnel / Bridge / PedestrianCrossing（横断歩道）/ RailCrossing（踏切）/ TollPlaza（料金所）/ BorderCrossing / AutomaticAccessControlSystem（ゲート）

### 1.4 車線・区画 `LaneElement`
- 特定参加者用: BusLane / CycleLane / Sidewalk
- 規制付き: RestrictedLane / ServiceLane / StopLane（路肩停車帯）/ TrafficLane
- 構造属性: GrassShoulder / PavedShoulder
- 区画線 `LaneMarking`: SolidLine / BrokenLine / DoubleSolidLine
- 物理分離 `PhysicalDivider`: Curb / GuardRail

### 1.5 路面 `RoadSurface` / `RoadSurfaceFeature`
UniformSurface / SegmentedSurface / LooseSurface（未舗装系）; 欠陥: Cracks / Potholes / Ruts（轍）/ Swells / Manhole

### 1.6 一時的構造 `TemporaryRoadStructure`
RoadWork / ConstructionSiteDetour / Roadblocks / TrafficCone / Debris（落下物）/ ConstructionSign / RoadSignage / RefuseCollection

### 1.7 ゾーン `TrafficZone`
SchoolZone / PedestrianZone / GeofencedZone / InterferenceZone / TrafficManagementZone / VehicleRestrictionArea

### 1.8 標識 `TrafficSign`
- 機能別: RegulatorySign（停止・譲れ等）/ WarningSign / InformationSign
- 状態別: StaticTrafficSign / DynamicTrafficSign（**信号機の暫定クラス**）

### 1.9 道路属性（property系、シーンのパラメータ軸）
- `drivingDirection`（**LHT/RHT**）/ `numberOfLanes` / `speedLimitForLane` / `roadFrictionScaleFactor`
- 幾何: straightLine / arc / spiral（horizontalGeometry）、upSlope / downSlope / levelPlane（verticalGeometry）、superElevation / lateralProfile（transverseGeometry）
- **誘発路面状態 `inducedRoadSurfaceCondition`**: wet / icy / snowy / flooded / standingWater / contaminating / mirage

## 2. 軸2 — 環境条件（env）

- **気象 `WeatherCondition`**
  - 降雨 `RainfallCondition`: Light / Moderate / Heavy / Violent / Cloudburst（5段階）
  - 降雪 `SnowfallCondition`: Light / Moderate / Heavy
  - 風 `WindCondition`: CalmWind〜Hurricane（ボーフォート風力階級で12クラス: LightAir, LightBreeze, GentleBreeze, ModerateBreeze, FreshBreeze, StrongBreeze, NearGale, Gale, StrongGale, Storm, ViolentStorm, Hurricane）
  - 雲量 `CloudCondition`: Clear / FewClouds / ScatteredClouds / BrokenClouds / Overcast
- **粒子状物質 `ParticulatesCondition`**: FoggyCondition / SandCondition / SmokeAndDustCondition / MarineCondition（海塩）
- **照明 `IlluminationCondition`**: DayLightingCondition / NightLightingCondition / ArtificialLightingCondition（StreetLighting / VehicleLighting）
- **時刻 `TimeCondition`**
- **環境属性（パラメータ軸）**: temperature / visibility（視程）/ sunAzimuth / sunElevation（**逆光シーンの軸**）/ precipitationIntensity / snowfallIntensity / windSpeed / windDirection / atmosphericPressure / cloudinessLevel / lightingIntensity
- **接続性 `ConnectivityCondition`**（参考・スコープ外候補）: CommunicationCondition（V2V / V2I）、PositioningCondition（GPS / GLONASS / Galileo）

## 3. 軸3 — 交通参加者・行動（tpb）

### 3.1 参加者 `TrafficParticipant`
- **種別軸** `TrafficParticipantByParticipantType`:
  - Vehicle: GoodsVehicle（Truck / Van）/ OfficialVehicle / PrivateVehicle（**Bicycle / Motorcycle / Wheelchair**）/ PublicTransportationVehicle
  - Human: Driver / Pedestrian / Rider
  - Animal
- **脆弱性軸** `TrafficParticipantByVulnerability`: VRU / NonVRU（NonVRU直下に Car / Bus / EmergencyVehicle / AgricultureVehicle / ConstructionVehicle / Trailer）
  - ※ 車種の多重分類あり（例: Car は NonVRU 側、Bicycle は PrivateVehicle 側）。シーン記述では両軸を併用する。

### 3.2 行動 `TrafficActivity`（4つの直交する分類軸）
- **抽象度** `ActivityByLevel`: MissionLevel / **ManeuverLevel**（FollowRoadUser / FollowTargetSpeed / FollowTrajectory）/ PrimitiveLevel
- **状態変化** `ActivityByStateChange`:
  - `MovingActivity`: Accelerate / Decelerate / Start / Stop / NotMove / Turn / MoveBackward（後退）/ LateralActivity / LongitudinalActivity
    - `DrivingActivity`: KeepLane / ChangeLane（ChangeToLeftLane / ChangeToRightLane）/ **CutIn / CutOut** / MakeATurn（Left / Right）/ **Overtake / Pass**
  - `CommunicationActivity` → `VehicleCommunicationActivity`: UseTurnIndicator / UseHazardLight / FlashHeadlights / HonkHorn / SoundSiren、`HumanCommunicationActivity`: Wave（手信号）
- **参加者数** `ActivityByNumberOfParticipants`: SingleParticipantActivity / MultiParticipantActivity
- **参加者種別** `ActivityByTrafficParticipantType`: VehicleActivity / HumanActivity（PedestrianActivity: Walk / Run / Stand / Slide、RiderActivity）/ **CloseUp（接近）/ Cross（横断）/ MoveAway（離脱）**

## 4. シーンの定式化（組合せ爆発への処方）

オントロジーが与えるのは**軸**であり、シーンは軸の直積から選ぶ。全直積は数百万通りになるため、次の2層で構成する:

```
ベースシーン  = 道路コンテキスト（§1） × インタラクション（Ego行動 × 相手参加者 × 相手行動、§3）
モディファイア = 環境条件（§2） × 路面状態（§1.9） × LHT/RHT × 交通規制状態（信号phase等）
```

- ベースシーンを `scene:SCN-<nnn>` として採番し、モディファイアは**シーンノードの属性（またはバリアント）**として持つ。天候×照明×路面をシーンに焼き込むと採番が爆発する。
- これは P13（ODDカバレッジ台帳）の軸定義と同一基盤 — 既に `proposal:P13 -[concerns]-> openx:` 傘3概念の辺が graph.yaml にある。

## 5. ベースシーン・ファミリの洗い出し（SCN候補の母集団）

既存資産との突合: 道路類型 G1-G17・シチュエーション S1-S7 は `GT_esmini/docs/virtualdriver/scenario_authoring_foundation.md` §2、検証カテゴリは `resources/xosc/verification/01〜09`。
**凡例**: ✅=検証資産あり / 🔶=部分的（資産はあるが検証未整備）/ ❌=未着手

| # | ファミリ | 主なOpenX概念 | 代表シーン候補 | 既存カバー |
| :--- | :--- | :--- | :--- | :--- |
| A | 単路走行 | Road, KeepLane, FollowTargetSpeed, straightLine/arc/spiral, up/downSlope | 直線巡航、単一/連続カーブ、勾配×曲率、速度制限変化点 | ✅ G1/G2, 01/02 🔶 G3/G15/G16 |
| B | 車線変更・追越し | ChangeLane, Overtake, Pass, CutIn, CutOut, LaneMarking | 車線変更（左/右）、追越し、被カットイン/カットアウト | 🔶 S1(LC), S7(割込み)は層2 |
| C | 先行車追従 | FollowRoadUser, Decelerate, Stop, CloseUp | 定速追従、減速停止、急停止対応 | ✅ 06, S2 |
| D | 信号交差点 | CrossRoad, DynamicTrafficSign, MakeATurn | 赤保持/赤→青/黄判断/青通過 × 直進/左折/右折 | ✅ 03, G10 |
| E | 無信号交差点（標識・優先） | TIntersection/CrossRoad/YIntersection/Staggered, RegulatorySign, MakeATurn, Cross | STOP/YIELD/priority × 自車優先/非優先 × 交差車タイミング | ✅ 04/08, G4/G5/G11-G13 🔶 G6/G7 |
| F | 対向車ギャップ | MakeATurn, CloseUp, MultiParticipantActivity | 右折（RHT左折）対向車待ち、ギャップ判断 | ✅ 07 (3d) |
| G | ラウンドアバウト | Roundabout | 進入譲り・周回・退出 | ❌ G8=層3 |
| H | 高速道路 | Motorway, ChangeLane, CutIn | 合流、分岐、車線減少、本線譲り | 🔶 G9=層2 |
| I | 横断歩道・VRU | PedestrianCrossing, Pedestrian, Walk/Run/Stand, Cross, SchoolZone | 歩行者横断/待機/飛び出し、信号付き横断歩道 | ✅ 09 (crosswalk) 🔶 飛び出し=S7層3 |
| J | 自転車・二輪・低速VRU | Bicycle, Motorcycle, Wheelchair, CycleLane, RiderActivity | 自転車並走・追越し、二輪すり抜け、自転車レーン横断 | ❌ |
| K | 特殊構造 | Tunnel, Bridge, RailCrossing, TollPlaza | トンネル出入り（照明遷移）、踏切一時停止、料金所 | 🔶 トンネル=F6視点のみ、踏切/料金所 ❌ |
| L | 一時的障害・道路工事 | RoadWork, TrafficCone, Roadblocks, Debris, ConstructionSiteDetour | 工事帯回避、落下物回避、車線規制通過 | ❌ |
| M | 緊急・特殊車両 | EmergencyVehicle, SoundSiren, Bus, PublicTransportationVehicle | 緊急車両接近時の譲路、バス停発進譲り | ❌ |
| N | 駐停車・後退 | Parking, StopLane, MoveBackward, Start | 路上駐車回避、駐車場、後退、路肩停車からの合流 | ❌ |
| O | 共存空間・ゾーン | SharedSpace, PedestrianZone, SchoolZone, TrafficZone | 歩車共存路の徐行、スクールゾーン | ❌ |
| P | 通行方向 | drivingDirection (LHT/RHT) | LHT/RHT両建て（全ファミリの横断バリアント） | 🔶 G14、LHT修正済（lht_road_transition） |
| Q | 車両間コミュニケーション | UseTurnIndicator, UseHazardLight, FlashHeadlights, HonkHorn, Wave | 指示器提示（Ego側は実装済: AutoIndicator/F6）、ハザード渋滞末尾 | 🔶 出力側のみ、認知側 ❌ |
| R | 動物・想定外参加者 | Animal | 動物飛び出し | ❌ |

### モディファイア軸（シーン属性として付与）

| 軸 | OpenX概念 | 値域 | 既存カバー |
| :--- | :--- | :--- | :--- |
| 降雨/降雪 | RainfallCondition / SnowfallCondition | 各5/3段階 | ❌（描画・μ連動なし） |
| 霧・視程 | FoggyCondition, visibility | 連続値 | ❌ |
| 照明 | Day/Night/ArtificialLighting, sunAzimuth/Elevation | 昼/夜/薄暮/逆光/街路灯 | 🔶 F6（夜間・トンネル・ハイビーム） |
| 路面 | inducedRoadSurfaceCondition, roadFrictionScaleFactor | wet/icy/snowy/flooded + μ | ❌ |
| 風 | WindCondition | 12段階 | ❌（横風外乱なし） |
| 通行方向 | drivingDirection | LHT/RHT | ✅ 両対応済 |
| 信号phase | DynamicTrafficSign | 赤保持/赤→青/黄/青 | ✅ 03 |

## 6. スコープ判定案（GT_esminiとして扱わない/保留にする概念）

- **接続性系**（V2V/V2I、GPS/GLONASS/Galileo）: OSI GroundTruth シミュレータの守備範囲外。要求が出るまで語彙にも入れない。
- **MarineCondition / BorderCrossing / AutomaticAccessControlSystem**: 対象ドメイン外。
- **路面欠陥**（Potholes/Cracks/Ruts/Swells/Manhole）: esmini の路面モデル（friction程度）では表現不能。カバレッジ台帳上「表現不能」と明記する扱いが誠実。
- **SandCondition / SmokeAndDust**: 同上（描画・センサモデル無し）。
- Core 71クラス: シーン記述には使わない（オントロジー内部の骨格）。

## 7. 次アクション（第1段の残り）

1. §5 のファミリから **SCN ノードを採番**して `scene_catalog_vd_ad.yaml` に起こす（ファミリ=グループ、シーン=ノード、モディファイア=属性）。優先度は層1相当（A〜F, I, P）から。
2. シーンが参照する OpenX 概念のうち **concept_vocabulary.yaml 未収載のもの**を随時追加（§5で太字級の新顔: CutIn/CutOut は収載済、未収載例: `Domain#Bicycle`, `Domain#EmergencyVehicle`, `Domain#RoadWork`, `Domain#RailCrossing`, `Domain#SharedSpace`, `Domain#Motorway` は収載済 等 — 追加は必要時のみ、一括インポートしない原則を維持）。
3. `namespaces.yaml` の `scene` を `status: active` へ → lint → `--render`。
4. 第2段: シーン→要求導出（`req-vd-ad:REQ-AD-<nnn>`）。

---
*抽出の再現: `scratchpad/parse_ontology.py`（セッション一時ファイル）。TTLソース: `thirdparty/ASAM_OpenXONTOLOGY_BS_V1-0-0-PR1-43-g0248cfe.zip` 内 `ontology/OpenXOntology.ttl`。owl:Class 検出は345/346（`HumanCommunicationActivity` の宣言形式が非定型で1件パーサ非検出、目視で階層確認済み）。*
