# OpenSCENARIO 1.4.0 対応ギャップ監査

> 監査日: 2026-07-05　|　基準スキーマ: `thirdparty/openscenario/schema_1.4/OpenSCENARIO.xsd`（ASAM OpenSCENARIO XML 1.4.0, 2026-05-19 リリース）
> 対象パーサ: **esmini core**（`EnvironmentSimulator/Modules/ScenarioEngine`）＋ **GT_esmini** hooks（`GT_esmini/src/scenario/GT_ScenarioReader.cpp`）
> 手法: 1.4スキーマの全 291 complexType ＋ 48 enum を実コードと照合し、各項目を敵対的に再検証（対応を裏付ける実装を探して誤検出を排除）。

## 1. サマリ

| 指標 | 値 |
| :--- | ---: |
| 総 complexType | 291 |
| 総 enum | 48 |
| 完全対応 | 202 |
| **未対応（UNSUPPORTED）** | **82** |
| **部分対応（PARTIAL）** | **54** |
| 合計（未／部分対応） | 136 |
| うち 1.4 新規 | 8 |
| うち 🔴 ロード中断級 | 11 |

**判定区分**
- **UNSUPPORTED**: どこにもパース経路が無い（黙って破棄、またはハードエラーで中断）。
- **PARTIAL**: 要素はパースするが一部の属性・子要素・enum値を無視、または効果が無い。

**重大度**
- 🔴 **ロード中断**: その要素が現れると `throw` / `LOG_ERROR_AND_QUIT` でシナリオ読込自体が失敗する（最優先）。
- 🟠 **要素拒否**: `LOG_ERROR` 等でその要素/アクションが生成されずスキップ。
- ⚪ **黙って無視**: 警告のみ、または無言でドロップ。

> 注: 本監査は**パーサ（読み取り）層**の対応可否である。実行時セマンティクス（例: レーンレイヤに基づくルーティング挙動）まで含めるとギャップはさらに広がる。

## 2. 結論: 1.4 新機能 6領域はすべて未対応

ユーザが 1.4 スキーマを導入した目的である「1.4 新機能」は、6領域すべてが未対応。以下は専任検証エージェントによる確定結果。

### [UNSUPPORTED] レーンレイヤ／工事区間（LaneLayerType, PreferredLaneLayerAction）
レーンレイヤ関連を一切認識しない。`PreferredLaneLayerAction` は RoutingAction ディスパッチに無く、出現すると `throw std::runtime_error("Action is not supported")` でシナリオ読込が中断する。`LaneLayerType` enum はどこからも参照されず、`layer` 属性はこれを持つどの要素（PreferredLaneLayerAction / AbsoluteTargetLane / RelativeTargetLane / LanePosition）でも読まれない。RoadManager 側にもレーンレイヤ（permanent/temporary）の概念が無く、既定ルーティングもレイヤを考慮しない。

### [UNSUPPORTED] 軌跡の運動プロファイル（Motion: speed_longitudinal / acceleration_longitudinal）
`Motion` 型と2属性（speed_longitudinal / acceleration_longitudinal）はどこでもパースされない。軌跡形状パーサは Vertex / Clothoid / ClothoidSpline のジオメトリと時刻は読むが、Motion / MotionStart / MotionEnd 子要素を一切参照しない。Vertex と ClothoidSplineSegment では Motion 子は黙って破棄され、ClothoidSpline 直下の MotionEnd は LOG_ERROR_AND_QUIT を引き起こす。

### [UNSUPPORTED] 軌跡の補間（Polyline の Interpolation 要素）
1.4 で Polyline に許された Interpolation 子要素はパースされない。Polyline パーサは全子要素を Vertex 前提で走査し、Interpolation 子は Position 子を持たないため `throw` で読込中断する。esmini の補間モードは非標準の車両プロパティ `plineInterpolation` で制御されており、標準の Interpolation 要素とは無関係。

### [UNSUPPORTED] 信号フェーズのセマンティクス（TrafficSignalSemantics: go/stop/caution/attention_go/attention_stop/fallback）
`Phase/@semantics` 属性はどこでもパース・使用されない。信号フェーズ処理は完全に name / duration / state 文字列ベース。コア esmini は Phase / TrafficSignalController を全くパースせず、GT_esmini 拡張はこれらをパースするが name / duration / trafficSignalId / state のみで semantics 属性を黙って捨てる。結果、意味状態（go/stop）と観測状態（red/amber/green）の分離という本機能の趣旨は成立しない。

### [PARTIAL] トラフィックパーティシパント調和（VehicleCategory / Role / PedestrianCategory の新・改名値）
1.4 で調和された VehicleCategory / Role の新値はまったく処理されない。パーサは生文字列を読み if/else 連鎖でハードコード対応する。新 VehicleCategory 値（aircraft / heavyTruck / micromobilityDevice / standupScooter / watercraft / wheelchair / workMachine / landVehicle）と正称 motorcycle は未対応で LOG_ERROR→既定 CAR。新 Role 値（fireBrigade / roadsideAssistance ほか）は無言で NONE。加えて deprecated の綴り（motorbike / truck / fire）を期待し続け、Role では snake_case（public_transport / road_assistance）で比較するため XSD camelCase の publicTransport / roadAssistance すら NONE に落ちる。PedestrianCategory（pedestrian / wheelchair / animal）のみ完全対応。

### [UNSUPPORTED] カタログ再利用（TrafficDistributionEntry の CatalogElement 化・TrafficDistributionEntryCatalogLocation）
TrafficDistributionEntry のカタログ再利用は完全未対応。TrafficDistributionEntry / TrafficDistribution / EntityDistribution / TrafficDistributionEntryCatalog のいずれもパーサに存在しない（esmini の交通機能は旧 TrafficSwarmAction のみ）。カタログ位置は 9 種中 7 種（Vehicle / Controller / Pedestrian / MiscObject / Maneuver / Trajectory / Route）を汎用登録するが、EnvironmentCatalog と TrafficDistributionEntryCatalog は未マップで CATALOG_UNDEFINED に落ちる（Environment はエントリ解決のみ成立する惜しい状態）。

## 3. 🔴 ロード中断級（最優先）

以下の要素は「未対応」で済まず、**正当な 1.4 シナリオの読み込みを失敗させる**。1.4 対応に着手するなら、まずこの握り潰し（未知要素を安全にスキップ）から。

| 型 | 状態 | 1.4新規 | 概要 |
| :--- | :--- | :---: | :--- |
| `ByObjectType` | PARTIAL |  | CollisionConditionのByTypeで external を throw 拒否（pedestrian/vehicle/miscellaneousのみ）。 |
| `ClothoidSpline` | PARTIAL |  | MotionEnd 子要素を読まない。出現すると LOG_ERROR_AND_QUIT。 |
| `CollisionCondition` | PARTIAL |  | ByTypeで external を throw 拒否。EntityRef と pedestrian/vehicle/miscellaneous は対応。 |
| `EntityCondition` | PARTIAL |  | AngleConditionは対応だが RelativeAngleCondition choice を未ディスパッチ→LOG_ERROR_AND_QUIT。 |
| `ExternalObjectReference` | UNSUPPORTED |  | Entities配下で 'not supported yet' の LOG_ERROR＋return -1（パース中断側）。 |
| `Interpolation` | UNSUPPORTED | ★ | Polylineの子要素として未パース。Polyline走査が全子をVertex前提のため出現すると throw で読込中断。 |
| `LateralDisplacement` | PARTIAL |  | 明示 'any' が LOG_ERROR_AND_QUIT（空/省略時のみANY既定）。left/rightは対応。 |
| `ObjectType` | PARTIAL |  | external 未対応（pedestrian/vehicle/miscellaneousのみ、CollisionConditionで throw）。 |
| `Position` | PARTIAL |  | GeoPosition choice が未ディスパッチで、出現すると throw 'Failed parse position'。他Positionは対応。 |
| `RelativeAngleCondition` | UNSUPPORTED |  | EntityCondition ディスパッチに分岐なし→else の LOG_ERROR_AND_QUIT（読込中断）。 |
| `RoutingAction` | PARTIAL |  | PreferredLaneLayerAction（1.4新）を未処理→else の throw で読込中断。他Routingは対応。 |

## 4. 全未対応・部分対応カタログ（カテゴリ別・全136件）

各項目の `根拠` は該当箇所（`ファイル:行`）。パスは `EnvironmentSimulator/Modules/ScenarioEngine/` 配下、または基準スキーマ `schema_1.4/OpenSCENARIO.xsd`。

### レーンレイヤ（1.4新規） (5件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `LaneLayerType` ★ | UNSUPPORTED | ⚪ 黙って無視 | enum permanent/temporary をOSCパーサが一切参照しない（レーンレイヤ概念が無い）。 | grep一致なし（未実装） |
| `PreferredLaneLayerAction` ★ | UNSUPPORTED | ⚪ 黙って無視 | RoutingActionディスパッチに分岐が無く、出現すると throw で読込中断。layer属性も未読。 | grep一致なし（未実装） |
| `AbsoluteTargetLane` | PARTIAL | ⚪ 黙って無視 | 1.4の layer 属性（LaneLayerType）を読まず value のみパース。 | `ScenarioReader.cpp:3000-3005` |
| `LanePosition` | PARTIAL | ⚪ 黙って無視 | 1.4の layer 属性を読まない（roadId/laneId/s/offset/Orientation のみ）。 | `ScenarioReader.cpp:2065-2100, OpenSCENARIO.xsd:1458` |
| `RelativeTargetLane` | PARTIAL | ⚪ 黙って無視 | 1.4の layer 属性を読まない（entityRef/value のみ）。 | `ScenarioReader.cpp:2988-2998` |

### 軌跡の運動プロファイル・補間（1.4新規） (7件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `Interpolation` ★ | UNSUPPORTED | 🔴 ロード中断 | Polylineの子要素として未パース。Polyline走査が全子をVertex前提のため出現すると throw で読込中断。 | `ScenarioReader.cpp:1331-1338, OpenSCENARIO.xsd:1412-1413, Entities.cpp:61,` |
| `Motion` ★ | UNSUPPORTED | ⚪ 黙って無視 | speed_longitudinal/acceleration_longitudinal を全く読まない。Vertex/Segmentでは黙って破棄、ClothoidSpline/MotionEnd は LOG_ERROR_AND_QUIT。 | `ScenarioReader.cpp:1347-1383, OSCPrivateAction.cpp:342-346` |
| `Clothoid` | PARTIAL | ⚪ 黙って無視 | MotionStart/MotionEnd 子要素を読まない（Position＋曲率/長さ/時刻のみ）。 | `ScenarioReader.cpp:1347-1383` |
| `ClothoidSpline` | PARTIAL | 🔴 ロード中断 | MotionEnd 子要素を読まない。出現すると LOG_ERROR_AND_QUIT。 | `ScenarioReader.cpp:1385-1437` |
| `ClothoidSplineSegment` | PARTIAL | ⚪ 黙って無視 | MotionStart 子要素を読まない（PositionStart＋曲率/長さ/hOffset/timeStart のみ）。 | `ScenarioReader.cpp:1394-1430` |
| `Polyline` | PARTIAL | ⚪ 黙って無視 | Interpolation 子要素を読まない。補間は非標準の plineInterpolation プロパティ由来。 | `ScenarioReader.cpp:1328-1345, OSCPrivateAction.cpp:371-400` |
| `Vertex` | PARTIAL | ⚪ 黙って無視 | 1.4の Motion 子要素を読まない（Position と time のみ）。 | `ScenarioReader.cpp:1331-1343` |

### 信号フェーズのセマンティクス（1.4新規） (3件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `TrafficSignalGroupState` | UNSUPPORTED | ⚪ 黙って無視 | どこにもパース箇所なし。state 属性未読。 | grep一致なし（未実装） |
| `TrafficSignalSemantics` ★ | UNSUPPORTED | ⚪ 黙って無視 | 6値をenumとして扱わず、状態は state 文字列を素通し。意味状態と観測状態の分離は不在。 | `ScenarioReader.cpp:2560` |
| `Phase` | PARTIAL | ⚪ 黙って無視 | semantics 属性と TrafficSignalGroupState 子を読まない（GT拡張が name/duration/TrafficSignalState のみ）。コアはPhase自体を未パース。 | `GT_ScenarioReader.cpp:208-227` |

### トラフィックパーティシパント enum（1.4調和） (2件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `Role` ★ | PARTIAL | ⚪ 黙って無視 | ambulance/civil/fire/military/police のみ。新値（fireBrigade,roadsideAssistance 他）全滅。さらに publicTransport/roadAssistance も snake_case判定バグ（public_transport/road_assistance を期待）でNONE落ち。 | `Entities.hpp:655-689` |
| `VehicleCategory` ★ | PARTIAL | 🟠 要素拒否 | 対応は car,van,truck,semitrailer,trailer,bus,motorbike,bicycle,train,tram の10種のみ。新値（aircraft,heavyTruck,micromobilityDevice,standupScooter,watercraft,wheelchair,workMachine,landVehicle）と正称 motorcycle は未対応→LOG_ERRORでカテゴリ未設定（既定CAR）。 | `Entities.hpp:805-817` |

### Traffic Source/Sink/Area＋分布モデル (29件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `ControllerDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（重み付きController選択なし）。 | `ScenarioReader.cpp:15,158-185` |
| `ControllerDistributionEntry` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（親未対応）。 | grep一致なし（未実装） |
| `DirectionOfTravelDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（same/opposite重み破棄）。 | grep一致なし（未実装） |
| `EntityDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（TrafficSource/Sink配下の分布）。 | `ScenarioReader.cpp:2420-2471` |
| `EntityDistributionEntry` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（EntityDistribution配下）。 | grep一致なし（未実装） |
| `Lane` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（RoadCursor配下の id 子）。 | `ScenarioReader.cpp:2423-2470` |
| `Polygon` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（TrafficAreaのarea）。 | `ScenarioReader.cpp:1328` |
| `PositionInLaneCoordinates` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（RoadCursor/RoadRange専用）。 | grep一致なし（未実装） |
| `PositionInRoadCoordinates` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（RoadCursor/RoadRange専用）。 | grep一致なし（未実装） |
| `PositionOfCurrentEntity` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（RelativeClearance/DirectionOfTravel文脈のentityRef）。 | grep一致なし（未実装） |
| `RoadCursor` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（RoadRange配下、roadId/s/Lane）。 | grep一致なし（未実装） |
| `RoadRange` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（length属性/RoadCursor子）。 | grep一致なし（未実装） |
| `ScenarioObjectTemplate` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（EntityDistributionEntry配下）。 | grep一致なし（未実装） |
| `TrafficArea` | UNSUPPORTED | ⚪ 黙って無視 | 未パース。唯一の消費元 TrafficAreaAction 自体が未ディスパッチ。 | `ScenarioReader.cpp:2420-2471` |
| `TrafficAreaAction` | UNSUPPORTED | ⚪ 黙って無視 | TrafficAction ディスパッチに分岐なし（TrafficSwarmActionのみ）→黙って破棄。 | `ScenarioReader.cpp:2420-2471` |
| `TrafficDefinition` | UNSUPPORTED | ⚪ 黙って無視 | 未パース。TrafficSwarmActionは半径/台数/速度のみ読む。 | `ScenarioReader.cpp:2423-2470` |
| `TrafficDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース。TrafficDistributionEntry/CatalogReferences 一式なし。 | `ScenarioReader.cpp:2420-2471` |
| `TrafficDistributionEntry` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（weight/name/EntityDistribution 等）。 | grep一致なし（未実装） |
| `TrafficDistributionEntryCatalogLocation` ★ | UNSUPPORTED | ⚪ 黙って無視 | 対応カタログ型が無く CATALOG_UNDEFINED（TrafficDistributionモデル自体が未実装）。 | `ScenarioReader.cpp:1286-1293, ScenarioReader.cpp:222-260, Catalogs.hpp:27-37, Catalogs.cpp:72-104` |
| `TrafficSinkAction` | UNSUPPORTED | ⚪ 黙って無視 | TrafficAction ディスパッチに分岐なし→黙って破棄。 | `ScenarioReader.cpp:2420-2471` |
| `TrafficSourceAction` | UNSUPPORTED | ⚪ 黙って無視 | 同上（TrafficSwarmActionのみ対応）→黙って破棄。 | `ScenarioReader.cpp:2420-2471` |
| `TrafficStopAction` | UNSUPPORTED | ⚪ 黙って無視 | 同上→黙って破棄。 | `ScenarioReader.cpp:2420-2471` |
| `UsedArea` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（RoadNetwork配下の Position 列）。 | grep一致なし（未実装） |
| `VehicleCategoryDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（swarmのカテゴリ分布）。 | grep一致なし（未実装） |
| `VehicleCategoryDistributionEntry` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（親未対応）。 | grep一致なし（未実装） |
| `VehicleRoleDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（swarmのRole分布）。 | grep一致なし（未実装） |
| `VehicleRoleDistributionEntry` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（親未対応）。 | grep一致なし（未実装） |
| `TrafficAction` | PARTIAL | ⚪ 黙って無視 | TrafficSwarmActionのみディスパッチ。Source/Sink/Area/Stopは分岐もelseも無く黙って破棄。trafficName属性も未読。 | `ScenarioReader.cpp:2420-2471` |
| `TrafficSwarmAction` | PARTIAL | ⚪ 黙って無視 | パースするが innerRadius/軸/台数/deprecated velocity のみ。必須 offset、現行 speed、子（TrafficDistribution/DirectionOfTravel等）を欠く。 | `ScenarioReader.cpp:2441-2467, OSCGlobalAction.hpp:372-445, OSCGlobalAction.cpp:424` |

### Stochastic パラメータ分布 (11件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `Histogram` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（同上）。 | `OSCParameterDistribution.cpp:259` |
| `HistogramBin` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（同上）。 | `OSCParameterDistribution.cpp:259` |
| `LogNormalDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（同上）。 | `OSCParameterDistribution.cpp:250-261` |
| `NormalDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（Stochastic枝スキップ）。 | `OSCParameterDistribution.cpp:250-260` |
| `PoissonDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（同上）。 | grep一致なし（未実装） |
| `ProbabilityDistributionSet` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（同上）。 | `OSCParameterDistribution.cpp:250-260, OpenSCENARIO.xsd:1888` |
| `ProbabilityDistributionSetElement` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（同上）。 | `OpenSCENARIO.xsd:1893, OSCParameterDistribution.cpp:259` |
| `StochasticDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（Stochastic枝がwarn-skip）。 | `OSCParameterDistribution.cpp:250-261` |
| `UniformDistribution` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（同上）。 | `OSCParameterDistribution.cpp:250-261` |
| `UserDefinedDistribution` | UNSUPPORTED | 🟠 要素拒否 | Deterministic配下で明示的に LOG_ERROR＋return -1（拒否）。 | `OSCParameterDistribution.cpp:232-236` |
| `Stochastic` | PARTIAL | ⚪ 黙って無視 | node検出後 'Stochastic distributions not supported yet' で warn-skip。numberOfTestRuns/randomSeed/子分布は未読・無効。 | `OSCParameterDistribution.cpp:250-260` |

### アニメーション／アピアランス (14件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `AnimationAction` | UNSUPPORTED | 🟠 要素拒否 | AppearanceAction配下で 'AnimationAction not supported yet' の LOG_ERROR→return 0（配下すべて未パース）。 | `ScenarioReader.cpp:4011-4015` |
| `AnimationFile` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（未対応AnimationAction配下）。 | grep一致なし（未実装） |
| `AnimationState` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（同上）。 | grep一致なし（未実装） |
| `AnimationType` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（同上）。 | grep一致なし（未実装） |
| `ComponentAnimation` | UNSUPPORTED | 🟠 要素拒否 | 未パース（同上）。 | `ScenarioReader.cpp:4011-4015` |
| `PedestrianAnimation` | UNSUPPORTED | ⚪ 黙って無視 | 未パース。 | grep一致なし（未実装） |
| `PedestrianGesture` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（親PedestrianAnimation未対応）。 | grep一致なし（未実装） |
| `PedestrianGestureType` | UNSUPPORTED | ⚪ 黙って無視 | gesture値を一切パースしない（AnimationActionが拒否）。 | `ScenarioReader.cpp:4011-4014` |
| `PedestrianMotionType` | UNSUPPORTED | ⚪ 黙って無視 | walking/running等をパースしない（AnimationActionが拒否）。 | `ScenarioReader.cpp:4011-4014` |
| `UserDefinedAnimation` | UNSUPPORTED | 🟠 要素拒否 | 未パース（AnimationActionが先に拒否）。 | `ScenarioReader.cpp:4011-4015` |
| `UserDefinedComponent` | UNSUPPORTED | ⚪ 黙って無視 | 未パース。 | grep一致なし（未実装） |
| `VehicleComponent` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（SensorReferenceSet/コンポーネント木）。 | grep一致なし（未実装） |
| `VehicleComponentType` | UNSUPPORTED | ⚪ 黙って無視 | hood/trunk/doors等を未パース（AnimationActionが拒否）。 | `ScenarioReader.cpp:4011-4014` |
| `AppearanceAction` | PARTIAL | 🟠 要素拒否 | LightStateActionは対応。AnimationAction子は LOG_ERROR で拒否（return 0）。 | `ScenarioReader.cpp:4008-4022` |

### モニター（1.3新規） (4件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `MonitorDeclaration` | UNSUPPORTED | ⚪ 黙って無視 | 完全未パース（name/value）。 | grep一致なし（未実装） |
| `MonitorDeclarations` | UNSUPPORTED | ⚪ 黙って無視 | コンテナ未パース。 | grep一致なし（未実装） |
| `SetMonitorAction` | UNSUPPORTED | ⚪ 黙って無視 | monitorRef/value未パース。Monitor機構が皆無。 | grep一致なし（未実装） |
| `GlobalAction` | PARTIAL | ⚪ 黙って無視 | 1.4新の SetMonitorAction choice を処理せず else の 'Unsupported global action' へ。他choiceは対応。 | `ScenarioReader.cpp:2349-2571` |

### エンティティ選択 (5件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `ByType` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（EntitySelection文脈のByType）。 | `ScenarioReader.cpp:1749-1752, ScenarioReader.cpp:4785` |
| `EntitySelection` | UNSUPPORTED | ⚪ 黙って無視 | 'is not implemented yet' のスタブ。members/selector破棄。 | `ScenarioReader.cpp:1749-1751` |
| `ExternalObjectReference` | UNSUPPORTED | 🔴 ロード中断 | Entities配下で 'not supported yet' の LOG_ERROR＋return -1（パース中断側）。 | `ScenarioReader.cpp:1656-1660` |
| `SelectedEntities` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（EntitySelectionがスキップ）。 | `ScenarioReader.cpp:1749-1751` |
| `ByObjectType` | PARTIAL | 🔴 ロード中断 | CollisionConditionのByTypeで external を throw 拒否（pedestrian/vehicle/miscellaneousのみ）。 | `ScenarioReader.cpp:4779-4798` |

### パラメータ／制約 (9件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `ModifyRule` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（ParameterModifyAction経由のみで未到達）。 | `ScenarioReader.cpp:2366-2369` |
| `ParameterAddValueRule` | UNSUPPORTED | ⚪ 黙って無視 | 未到達（Parameter ModifyActionが未対応）。 | `ScenarioReader.cpp:2392` |
| `ParameterModifyAction` | UNSUPPORTED | ⚪ 黙って無視 | ParameterAction は SetAction のみ。ModifyAction系は else の 'not supported yet' で破棄（Modifyは Variable 側のみ実装）。 | `ScenarioReader.cpp:2349-2369` |
| `ParameterMultiplyByValueRule` | UNSUPPORTED | ⚪ 黙って無視 | 未到達（同上）。 | `ScenarioReader.cpp:2402-2411` |
| `ValueConstraint` | UNSUPPORTED | ⚪ 黙って無視 | ParameterDeclaration/ParameterAssignmentで制約を読まない（name/type/valueのみ）。 | `Parameters.cpp:20-23, ScenarioReader.cpp:1530-1543` |
| `ValueConstraintGroup` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（constraintGroups子を走査しない）。 | grep一致なし（未実装） |
| `ParameterAction` | PARTIAL | ⚪ 黙って無視 | SetActionのみ（deprecation警告付きで受理）。ModifyActionは 'not supported yet'。 | `ScenarioReader.cpp:2349-2370` |
| `ParameterDeclaration` | PARTIAL | ⚪ 黙って無視 | ConstraintGroup子を未パース（name/type/valueのみ）。 | grep一致なし（未実装） |
| `ParameterType` | PARTIAL | 🟠 要素拒否 | unsignedInt/unsignedShort/dateTime は LOG_ERROR（型未割当）。boolean/double/integer/string/int は対応。 | `Parameters.cpp:535-566` |

### 条件（Condition） (10件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `RelativeAngleCondition` | UNSUPPORTED | 🔴 ロード中断 | EntityCondition ディスパッチに分岐なし→else の LOG_ERROR_AND_QUIT（読込中断）。 | `ScenarioReader.cpp:4753` |
| `TimeOfDayCondition` | UNSUPPORTED | ⚪ 黙って無視 | ByValueCondition ディスパッチに無く 'not supported yet' で無視。 | `ScenarioReader.cpp:5096-5153, ScenarioReader.cpp:5574-5590` |
| `UserDefinedValueCondition` | UNSUPPORTED | ⚪ 黙って無視 | 同ディスパッチに無く warn で無視。 | `ScenarioReader.cpp:5096-5153` |
| `ByValueCondition` | PARTIAL | ⚪ 黙って無視 | TimeOfDay/UserDefinedValueは warn 無視。TrafficSignalControllerConditionはGT拡張のみ（コア未対応）。 | `ScenarioReader.cpp:5096-5153, GT_ScenarioReader.cpp:253-266` |
| `CollisionCondition` | PARTIAL | 🔴 ロード中断 | ByTypeで external を throw 拒否。EntityRef と pedestrian/vehicle/miscellaneous は対応。 | `ScenarioReader.cpp:4775-4808` |
| `DistanceCondition` | PARTIAL | ⚪ 黙って無視 | 1.2+ の routingAlgorithm 属性を未読（他は対応）。 | `ScenarioReader.cpp:4810-4865` |
| `EntityCondition` | PARTIAL | 🔴 ロード中断 | AngleConditionは対応だが RelativeAngleCondition choice を未ディスパッチ→LOG_ERROR_AND_QUIT。 | `OpenSCENARIO.xsd:1236, ScenarioReader.cpp:4612-5047` |
| `RelativeDistanceCondition` | PARTIAL | ⚪ 黙って無視 | 1.4 の routingAlgorithm 属性を未読。 | `ScenarioReader.cpp:4753-4773` |
| `TimeHeadwayCondition` | PARTIAL | ⚪ 黙って無視 | routingAlgorithm 属性を未読（他は対応）。 | `ScenarioReader.cpp:4615-4666` |
| `TimeToCollisionCondition` | PARTIAL | ⚪ 黙って無視 | routingAlgorithm 属性を未読（他は対応）。 | `ScenarioReader.cpp:4668-4735` |

### 位置（Position） (2件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `GeoPosition` | UNSUPPORTED | ⚪ 黙って無視 | parseOSCPositionのディスパッチに無く未対応。latitude/longitude/altitude/Orientation未読。 | grep一致なし（未実装） |
| `Position` | PARTIAL | 🔴 ロード中断 | GeoPosition choice が未ディスパッチで、出現すると throw 'Failed parse position'。他Positionは対応。 | `ScenarioReader.cpp:1862` |

### 環境・天候 (5件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `DomeImage` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（DomeFile子/azimuthOffset破棄）。Weatherで 'Not valid weather property name' 警告へ。 | `OSCEnvironment.hpp:87-93` |
| `Wetness` | UNSUPPORTED | ⚪ 黙って無視 | dry/moist/wetWithPuddles等を未パース。RoadConditionは frictionScaleFactor のみ。WetnessTypeは 'NOT IMPLEMENTED YET'。 | `ScenarioReader.cpp:5749-5757` |
| `Environment` | PARTIAL | ⚪ 黙って無視 | 必須 name 属性を未読（TimeOfDay/Weather/RoadCondition子は対応）。 | `ScenarioReader.cpp:5565-5763` |
| `RoadCondition` | PARTIAL | ⚪ 黙って無視 | wetness属性/Properties子を未処理（frictionScaleFactor のみ）。 | `ScenarioReader.cpp:5749-5757, OSCEnvironment.cpp:298, OSCEnvironment.hpp:145-146` |
| `Weather` | PARTIAL | ⚪ 黙って無視 | 1.4 の DomeImage 子を未パース。属性/Sun/Fog/Precipitation/Wind は対応。 | `ScenarioReader.cpp:5743-5746` |

### センサ・可視性 (3件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `SensorReference` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（SensorReferenceSet配下、name）。 | grep一致なし（未実装） |
| `SensorReferenceSet` | UNSUPPORTED | ⚪ 黙って無視 | 未パース（SensorReference子）。 | grep一致なし（未実装） |
| `VisibilityAction` | PARTIAL | ⚪ 黙って無視 | 1.4 の SensorReferenceSet 子を未パース（graphics/traffic/sensors bool のみ）。 | `ScenarioReader.cpp:3976-4006` |

### コントローラ (3件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `ControllerType` | UNSUPPORTED | ⚪ 黙って無視 | controllerType 属性（1.3+）を未読。型は 'esminiController' プロパティで決定。 | `ScenarioReader.cpp:1157` |
| `ActivateControllerAction` | PARTIAL | ⚪ 黙って無視 | animation の起動は 'not supported yet' 警告。lateral/longitudinal/lighting/controllerRef は対応。 | `ScenarioReader.cpp:2617-2675` |
| `Controller` | PARTIAL | ⚪ 黙って無視 | XSD の controllerType 属性を未読（esminiControllerプロパティ由来）。 | `ScenarioReader.cpp:1114-1184` |

### ルーティング・ルート (7件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `RoutingAlgorithm` | UNSUPPORTED | ⚪ 黙って無視 | routingAlgorithm 属性（assignedRoute/fastest等）をどこでも未読。 | grep一致なし（未実装） |
| `InRoutePosition` | PARTIAL | 🟠 要素拒否 | FromCurrentEntity子が 'not implemented'（LOG_ERROR）。FromRoad/LaneCoordinatesは対応。 | `ScenarioReader.cpp:2167-2208` |
| `Route` | PARTIAL | ⚪ 黙って無視 | 必須 closed 属性を未適用（コメントアウト 'roadmanager未対応'）。 | `ScenarioReader.cpp:1186-1240, OSCPrivateAction.cpp:521-532` |
| `RouteStrategy` | PARTIAL | ⚪ 黙って無視 | random を区別せず（leastIntersections/fastest のみ、他はSHORTEST既定）。 | `ScenarioReader.cpp:1213-1222` |
| `RoutingAction` | PARTIAL | 🔴 ロード中断 | PreferredLaneLayerAction（1.4新）を未処理→else の throw で読込中断。他Routingは対応。 | grep一致なし（未実装） |
| `SpeedProfileAction` | PARTIAL | ⚪ 黙って無視 | 1.4で属性化された entityRef を未読（存在しない子 'EntityRef' を読む）。DynamicConstraints/Entry/followingModeは対応。 | `ScenarioReader.cpp:2817-2882, OpenSCENARIO.xsd:2162` |
| `Waypoint` | PARTIAL | ⚪ 黙って無視 | routeStrategy の random を区別しない（SHORTESTに落ちる）。 | `ScenarioReader.cpp:1213-1223` |

### 車両・トレーラ・ヘッダ・その他 (17件)

| 型 | 状態 | 重大度 | ギャップ | 根拠 |
| :--- | :--- | :--- | :--- | :--- |
| `CustomContent` | UNSUPPORTED | 🟠 要素拒否 | Properties子として未対応→else の LOG_ERROR で破棄（File/Propertyのみ）。 | `ScenarioReader.cpp:384-401` |
| `License` | UNSUPPORTED | ⚪ 黙って無視 | FileHeaderの License 子を無視（revMajor/revMinor/descriptionのみ）。 | `ScenarioReader.cpp:263-271` |
| `UserDefinedLight` | UNSUPPORTED | ⚪ 黙って無視 | LightStateAction内で warn-skip。userDefinedLightType無視。 | `ScenarioReader.cpp:4055-4064` |
| `Actors` | PARTIAL | ⚪ 黙って無視 | selectTriggeringEntities 属性を未読。ByConditionは 'not implemented' 警告（EntityRefのみ）。 | `ScenarioReader.cpp:5424-5446` |
| `Axles` | PARTIAL | ⚪ 黙って無視 | AdditionalAxle 子を無視（Front/RearAxleのみ）。 | `ScenarioReader.cpp:644-666` |
| `Entities` | PARTIAL | ⚪ 黙って無視 | EntitySelection子は 'not implemented yet' でスキップ。ScenarioObjectは対応。 | `ScenarioReader.cpp:1749-1752` |
| `FileHeader` | PARTIAL | ⚪ 黙って無視 | 必須 author/date を無視、License/Properties子も未パース。 | `ScenarioReader.cpp:263-272` |
| `LateralDisplacement` | PARTIAL | 🔴 ロード中断 | 明示 'any' が LOG_ERROR_AND_QUIT（空/省略時のみANY既定）。left/rightは対応。 | `ScenarioReader.cpp:3132-3148, ScenarioReader.cpp:2926` |
| `LightType` | PARTIAL | ⚪ 黙って無視 | UserDefinedLight choice は warn-skip（ok=false）。VehicleLightのみ対応。 | `ScenarioReader.cpp:4045-4064` |
| `ObjectType` | PARTIAL | 🔴 ロード中断 | external 未対応（pedestrian/vehicle/miscellaneousのみ、CollisionConditionで throw）。 | `ScenarioReader.cpp:4783-4798` |
| `Pedestrian` | PARTIAL | ⚪ 黙って無視 | deprecated model 属性を未読。PedestrianAnimationランタイム無し。 | `ScenarioReader.cpp:907-963` |
| `Performance` | PARTIAL | ⚪ 黙って無視 | maxAccelerationRate/maxDecelerationRate 属性を未読・格納先も無い（maxSpeed/maxAcc/maxDecのみ）。 | `ScenarioReader.cpp:597-631, ScenarioReader.cpp:2687, Entities.hpp:213-215` |
| `Properties` | PARTIAL | 🟠 要素拒否 | CustomContent子を未処理→LOG_ERROR。File/Propertyは対応。 | `ScenarioReader.cpp:379-403` |
| `RoadNetwork` | PARTIAL | ⚪ 黙って無視 | UsedArea子を無視（コアは LogicFile/SceneGraphFileのみ／TrafficSignalsはGT拡張で対応）。 | `ScenarioReader.cpp:360-377, GT_ScenarioReader.cpp:187-231` |
| `TrailerCoupler` | PARTIAL | ⚪ 黙って無視 | 任意 dz を未パース（必須 dx のみ、構造体にdz_無し）。 | `ScenarioReader.cpp:775-780, Entities.hpp:791-801` |
| `TrailerHitch` | PARTIAL | ⚪ 黙って無視 | 任意 dz を未パース（必須 dx のみ）。 | `ScenarioReader.cpp:768-773, Entities.hpp:777-789` |
| `Vehicle` | PARTIAL | ⚪ 黙って無視 | 1.4 の mass 属性を未読（pedestrian/miscObjectのみmass対応）。 | `ScenarioReader.cpp:549-706` |

## 5. enum 調和の罠（1.4 harmonization）

1.4 の VehicleCategory/Role は値がリネーム・追加された。マイグレーション XSLT（`thirdparty/openscenario/migration_1.4/migration1_3to1_4.xslt`）は次のリネームのみ扱う:

- `motorbike` → `motorcycle`
- `truck` → `heavyTruck`
- `fire` → `fireBrigade`
- `roadAssistance` → `roadsideAssistance`
- `wheelchair`（PedestrianCategory）→ `wheelchair`（VehicleCategory）

esmini 側の実装（`Entities.hpp`）は **deprecated 側の綴りを期待し続けている**（`motorbike`/`truck`/`fire`）ため、正称にマイグレーションした 1.4 シナリオはむしろ category/role が落ちる。さらに Role では `publicTransport`/`roadAssistance` を **snake_case（`public_transport`/`road_assistance`）で比較しており、XSD の camelCase と一致せず NONE に落ちる既存バグ**がある。

## 6. 補足・出典

- esmini/GT_esmini は OSC パーサを**フォークしていない**。コアパーサは `EnvironmentSimulator/Modules/ScenarioEngine` のみ（`ScenarioReader.cpp` が主ディスパッチ、`OSCTypeDefs/*.cpp` が Action/Condition/Position/Environment/ParameterDistribution）。GT 側の関与は `GT_ScenarioReader.cpp` の薄い hook（TrafficSignalController のみ）。
- esmini の実質対応上限は 1.3（`AngleCondition`/`BrakeInput` 等で version 分岐）。1.4 要素の version 分岐は皆無。
- リリース資料上の 1.4 新機能（Lane layers / Motion / Interpolation / TrafficSignalSemantics / participant調和 / Catalog再利用）は、追加要素がすべて optional・cardinality 0..1 のため 1.3.1 と完全後方互換。ただし esmini は未知要素を安全にスキップせず、上記のとおり一部はハードエラーになる。

---

## 付録A. 未対応要素を導入バージョン昇順で（汎用性の観点）

「新しさ」ではなく **導入バージョンが低い＝エコシステムが広く対応済み** の順。低版数の穴ほど、他シミュレータ由来のシナリオを取り込む際の非互換に直結する。

> 版数の出典: esmini `osc_coverage.txt`（〜1.3、括弧の版数タグ）を親要素継承込みでパース。同ファイルに載らない子要素・1.4新規は OpenSCENARIO 版数知識で補完（**1.0/1.1 は確度高、1.2/1.3 は近似**）。対応状況は本監査（実コード検証）に従う。

### バージョン別 未対応分布

| 導入バージョン | 未対応(UNSUP) | 部分対応(PARTIAL) | 計 |
| :--- | ---: | ---: | ---: |
| **1.0** | 17 | 45 | 62 |
| **1.1** | 19 | 2 | 21 |
| **1.2** | 35 | 4 | 39 |
| **1.3** | 4 | 3 | 7 |
| **1.4** | 7 | 0 | 7 |

要点: **完全未対応（UNSUPPORTED）の 1.0/1.1 由来だけで約38件**。これらは CARLA/VTD 等も対応済みの"枯れた"標準で、汎用性への影響が最も大きい。

### 完全未対応（UNSUPPORTED）を版数昇順に

#### OpenSCENARIO 1.0 (17件)

| 型 | 重大度 | ギャップ |
| :--- | :---: | :--- |
| `ByType` | ⚪ | 未パース（EntitySelection文脈のByType）。 |
| `ControllerDistribution` | ⚪ | 未パース（重み付きController選択なし）。 |
| `ControllerDistributionEntry` | ⚪ | 未パース（親未対応）。 |
| `EntitySelection` | ⚪ | 'is not implemented yet' のスタブ。members/selector破棄。 |
| `ModifyRule` | ⚪ | 未パース（ParameterModifyAction経由のみで未到達）。 |
| `ParameterAddValueRule` | ⚪ | 未到達（Parameter ModifyActionが未対応）。 |
| `ParameterModifyAction` | ⚪ | ParameterAction は SetAction のみ。ModifyAction系は else の 'not supported yet' で破棄（Modifyは Variable 側のみ実装）。 |
| `ParameterMultiplyByValueRule` | ⚪ | 未到達（同上）。 |
| `SelectedEntities` | ⚪ | 未パース（EntitySelectionがスキップ）。 |
| `TimeOfDayCondition` | ⚪ | ByValueCondition ディスパッチに無く 'not supported yet' で無視。 |
| `TrafficDefinition` | ⚪ | 未パース。TrafficSwarmActionは半径/台数/速度のみ読む。 |
| `TrafficSinkAction` | ⚪ | TrafficAction ディスパッチに分岐なし→黙って破棄。 |
| `TrafficSourceAction` | ⚪ | 同上（TrafficSwarmActionのみ対応）→黙って破棄。 |
| `TrafficStopAction` | ⚪ | 同上→黙って破棄。 |
| `UserDefinedValueCondition` | ⚪ | 同ディスパッチに無く warn で無視。 |
| `VehicleCategoryDistribution` | ⚪ | 未パース（swarmのカテゴリ分布）。 |
| `VehicleCategoryDistributionEntry` | ⚪ | 未パース（親未対応）。 |

#### OpenSCENARIO 1.1 (19件)

| 型 | 重大度 | ギャップ |
| :--- | :---: | :--- |
| `AnimationFile` | ⚪ | 未パース（未対応AnimationAction配下）。 |
| `AnimationState` | ⚪ | 未パース（同上）。 |
| `AnimationType` | ⚪ | 未パース（同上）。 |
| `ExternalObjectReference` | 🔴 | Entities配下で 'not supported yet' の LOG_ERROR＋return -1（パース中断側）。 |
| `GeoPosition` | ⚪ | parseOSCPositionのディスパッチに無く未対応。latitude/longitude/altitude/Orientation未読。 |
| `Histogram` | ⚪ | 未パース（同上）。 |
| `HistogramBin` | ⚪ | 未パース（同上）。 |
| `License` | ⚪ | FileHeaderの License 子を無視（revMajor/revMinor/descriptionのみ）。 |
| `LogNormalDistribution` | ⚪ | 未パース（同上）。 |
| `NormalDistribution` | ⚪ | 未パース（Stochastic枝スキップ）。 |
| `PoissonDistribution` | ⚪ | 未パース（同上）。 |
| `ProbabilityDistributionSet` | ⚪ | 未パース（同上）。 |
| `ProbabilityDistributionSetElement` | ⚪ | 未パース（同上）。 |
| `StochasticDistribution` | ⚪ | 未パース（Stochastic枝がwarn-skip）。 |
| `UniformDistribution` | ⚪ | 未パース（同上）。 |
| `UsedArea` | ⚪ | 未パース（RoadNetwork配下の Position 列）。 |
| `UserDefinedDistribution` | 🟠 | Deterministic配下で明示的に LOG_ERROR＋return -1（拒否）。 |
| `ValueConstraint` | ⚪ | ParameterDeclaration/ParameterAssignmentで制約を読まない（name/type/valueのみ）。 |
| `ValueConstraintGroup` | ⚪ | 未パース（constraintGroups子を走査しない）。 |

#### OpenSCENARIO 1.2 (35件)

| 型 | 重大度 | ギャップ |
| :--- | :---: | :--- |
| `AnimationAction` | 🟠 | AppearanceAction配下で 'AnimationAction not supported yet' の LOG_ERROR→return 0（配下すべて未パース）。 |
| `ComponentAnimation` | 🟠 | 未パース（同上）。 |
| `ControllerType` | ⚪ | controllerType 属性（1.3+）を未読。型は 'esminiController' プロパティで決定。 |
| `CustomContent` | 🟠 | Properties子として未対応→else の LOG_ERROR で破棄（File/Propertyのみ）。 |
| `DirectionOfTravelDistribution` | ⚪ | 未パース（same/opposite重み破棄）。 |
| `DomeImage` | ⚪ | 未パース（DomeFile子/azimuthOffset破棄）。Weatherで 'Not valid weather property name' 警告へ。 |
| `EntityDistribution` | ⚪ | 未パース（TrafficSource/Sink配下の分布）。 |
| `EntityDistributionEntry` | ⚪ | 未パース（EntityDistribution配下）。 |
| `Lane` | ⚪ | 未パース（RoadCursor配下の id 子）。 |
| `PedestrianAnimation` | ⚪ | 未パース。 |
| `PedestrianGesture` | ⚪ | 未パース（親PedestrianAnimation未対応）。 |
| `PedestrianGestureType` | ⚪ | gesture値を一切パースしない（AnimationActionが拒否）。 |
| `PedestrianMotionType` | ⚪ | walking/running等をパースしない（AnimationActionが拒否）。 |
| `Polygon` | ⚪ | 未パース（TrafficAreaのarea）。 |
| `PositionInLaneCoordinates` | ⚪ | 未パース（RoadCursor/RoadRange専用）。 |
| `PositionInRoadCoordinates` | ⚪ | 未パース（RoadCursor/RoadRange専用）。 |
| `PositionOfCurrentEntity` | ⚪ | 未パース（RelativeClearance/DirectionOfTravel文脈のentityRef）。 |
| `RoadCursor` | ⚪ | 未パース（RoadRange配下、roadId/s/Lane）。 |
| `RoadRange` | ⚪ | 未パース（length属性/RoadCursor子）。 |
| `RoutingAlgorithm` | ⚪ | routingAlgorithm 属性（assignedRoute/fastest等）をどこでも未読。 |
| `ScenarioObjectTemplate` | ⚪ | 未パース（EntityDistributionEntry配下）。 |
| `SensorReference` | ⚪ | 未パース（SensorReferenceSet配下、name）。 |
| `SensorReferenceSet` | ⚪ | 未パース（SensorReference子）。 |
| `TrafficArea` | ⚪ | 未パース。唯一の消費元 TrafficAreaAction 自体が未ディスパッチ。 |
| `TrafficAreaAction` | ⚪ | TrafficAction ディスパッチに分岐なし（TrafficSwarmActionのみ）→黙って破棄。 |
| `TrafficDistribution` | ⚪ | 未パース。TrafficDistributionEntry/CatalogReferences 一式なし。 |
| `TrafficDistributionEntry` | ⚪ | 未パース（weight/name/EntityDistribution 等）。 |
| `UserDefinedAnimation` | 🟠 | 未パース（AnimationActionが先に拒否）。 |
| `UserDefinedComponent` | ⚪ | 未パース。 |
| `UserDefinedLight` | ⚪ | LightStateAction内で warn-skip。userDefinedLightType無視。 |
| `VehicleComponent` | ⚪ | 未パース（SensorReferenceSet/コンポーネント木）。 |
| `VehicleComponentType` | ⚪ | hood/trunk/doors等を未パース（AnimationActionが拒否）。 |
| `VehicleRoleDistribution` | ⚪ | 未パース（swarmのRole分布）。 |
| `VehicleRoleDistributionEntry` | ⚪ | 未パース（親未対応）。 |
| `Wetness` | ⚪ | dry/moist/wetWithPuddles等を未パース。RoadConditionは frictionScaleFactor のみ。WetnessTypeは 'NOT IMPLEMENTED YET'。 |

#### OpenSCENARIO 1.3 (4件)

| 型 | 重大度 | ギャップ |
| :--- | :---: | :--- |
| `MonitorDeclaration` | ⚪ | 完全未パース（name/value）。 |
| `MonitorDeclarations` | ⚪ | コンテナ未パース。 |
| `RelativeAngleCondition` | 🔴 | EntityCondition ディスパッチに分岐なし→else の LOG_ERROR_AND_QUIT（読込中断）。 |
| `SetMonitorAction` | ⚪ | monitorRef/value未パース。Monitor機構が皆無。 |

#### OpenSCENARIO 1.4 (7件)

| 型 | 重大度 | ギャップ |
| :--- | :---: | :--- |
| `Interpolation` | 🔴 | Polylineの子要素として未パース。Polyline走査が全子をVertex前提のため出現すると throw で読込中断。 |
| `LaneLayerType` | ⚪ | enum permanent/temporary をOSCパーサが一切参照しない（レーンレイヤ概念が無い）。 |
| `Motion` | ⚪ | speed_longitudinal/acceleration_longitudinal を全く読まない。Vertex/Segmentでは黙って破棄、ClothoidSpline/MotionEnd は LOG_ERROR_AND_QUIT。 |
| `PreferredLaneLayerAction` | ⚪ | RoutingActionディスパッチに分岐が無く、出現すると throw で読込中断。layer属性も未読。 |
| `TrafficDistributionEntryCatalogLocation` | ⚪ | 対応カタログ型が無く CATALOG_UNDEFINED（TrafficDistributionモデル自体が未実装）。 |
| `TrafficSignalGroupState` | ⚪ | どこにもパース箇所なし。state 属性未読。 |
| `TrafficSignalSemantics` | ⚪ | 6値をenumとして扱わず、状態は state 文字列を素通し。意味状態と観測状態の分離は不在。 |

### 参考: 部分対応（PARTIAL）を版数昇順に

要素自体は解釈するが一部の属性・子要素・enum値を落とすもの。低版数ほど"取りこぼし"が古い標準に及ぶ。

- **1.0** (45): `AbsoluteTargetLane`, `ActivateControllerAction`, `Actors`, `Axles`, `ByObjectType`, `ByValueCondition`, `Clothoid`, `ClothoidSpline`, `CollisionCondition`, `Controller`, `DistanceCondition`, `Entities`, `EntityCondition`, `Environment`, `FileHeader`, `GlobalAction`, `InRoutePosition`, `LanePosition`, `ObjectType`, `ParameterAction`, `ParameterDeclaration`, `ParameterType`, `Pedestrian`, `Performance`, `Phase`, `Polyline`, `Position`, `Properties`, `RelativeDistanceCondition`, `RelativeTargetLane`, `RoadCondition`, `RoadNetwork`, `Route`, `RouteStrategy`, `RoutingAction`, `TimeHeadwayCondition`, `TimeToCollisionCondition`, `TrafficAction`, `TrafficSwarmAction`, `Vehicle`, `VehicleCategory`, `Vertex`, `VisibilityAction`, `Waypoint`, `Weather`
- **1.1** (2): `LateralDisplacement`, `Stochastic`
- **1.2** (4): `AppearanceAction`, `LightType`, `Role`, `SpeedProfileAction`
- **1.3** (3): `ClothoidSplineSegment`, `TrailerCoupler`, `TrailerHitch`

