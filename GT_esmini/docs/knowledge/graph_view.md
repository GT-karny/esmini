# Knowledge Graph View

> **GENERATED — do not edit.** Source of truth: `graph.yaml` / `namespaces.yaml`.
> Regenerate: `DriverScript/.venv/Scripts/python.exe scripts/check_knowledge_graph.py --render`

<!-- generated-from: sha256:0a0ef2a47c36abed -->

ノード 117・辺 92（curatedのみ。commit由来の辺は `--extract-commits` で別途抽出）

```mermaid
flowchart LR
  subgraph sg_proposal["proposal｜機能提案 P1-P45 (2026-06)"]
    n_proposal_P24["P24"]
    n_proposal_P15["P15"]
    n_proposal_P39["P39"]
    n_proposal_P13["P13"]
    n_proposal_P8["P8"]
    n_proposal_P2["P2"]
    n_proposal_P5["P5"]
    n_proposal_P40["P40"]
    n_proposal_P41["P41"]
    n_proposal_P1["P1"]
    n_proposal_P23["P23"]
    n_proposal_P7["P7"]
    n_proposal_P11["P11"]
    n_proposal_P26["P26"]
    n_proposal_P12["P12"]
  end
  subgraph sg_feature["feature｜機能ロードマップ F1-F6"]
    n_feature_F2["F2"]
    n_feature_F3["F3"]
    n_feature_F6["F6"]
  end
  subgraph sg_debt_phase["debt-phase｜負債返済ロードマップ R0-R5（R5はU1-U4に細分）"]
    n_debt_phase_R5_U4["R5-U4"]
    n_debt_phase_R5_U3["R5-U3"]
  end
  subgraph sg_odr_plan["odr-plan｜ODR 1.6-1.9 プログラムフェーズ P0-P10（P9a/P9b含む）"]
    n_odr_plan_P5["P5"]
  end
  subgraph sg_fork_patch["fork-patch｜フォークパッチ番号（1-A/1-D〜1-H, 2〜17）"]
    n_fork_patch_3["3"]
    n_fork_patch_7["7"]
    n_fork_patch_8["8"]
    n_fork_patch_13["13"]
    n_fork_patch_10["10"]
    n_fork_patch_17["17"]
  end
  subgraph sg_odr_upstream_pr["odr-upstream-pr｜ODR upstream還元候補 PR-1〜PR-5（PR-1b含む）"]
    n_odr_upstream_pr_PR_1["PR-1"]
    n_odr_upstream_pr_PR_2["PR-2"]
    n_odr_upstream_pr_PR_1b["PR-1b"]
    n_odr_upstream_pr_PR_3["PR-3"]
    n_odr_upstream_pr_PR_4["PR-4"]
    n_odr_upstream_pr_PR_5["PR-5"]
  end
  subgraph sg_openx["openx｜ASAM OpenX Ontology 概念（コミット済みスナップショット）"]
    n_openx_Domain_VehicleLighting["VehicleLighting"]
    n_openx_Domain_IlluminationCondition["IlluminationCondition"]
    n_openx_Domain_Tunnel["Tunnel"]
    n_openx_Domain_NightLightingCondition["NightLightingCondition"]
    n_openx_Domain_PedestrianCrossing["PedestrianCrossing"]
    n_openx_Domain_DynamicTrafficSign["DynamicTrafficSign"]
    n_openx_Domain_Junction["Junction"]
    n_openx_Domain_RegulatorySign["RegulatorySign"]
    n_openx_Domain_FollowRoadUser["FollowRoadUser"]
    n_openx_Domain_IntersectionAtGrade["IntersectionAtGrade"]
    n_openx_Domain_Road["Road"]
    n_openx_Domain_KeepLane["KeepLane"]
    n_openx_Domain_FollowTargetSpeed["FollowTargetSpeed"]
    n_openx_Domain_ChangeLane["ChangeLane"]
    n_openx_Domain_Overtake["Overtake"]
    n_openx_Domain_CutIn["CutIn"]
    n_openx_Domain_CutOut["CutOut"]
    n_openx_Domain_Decelerate["Decelerate"]
    n_openx_Domain_Stop["Stop"]
    n_openx_Domain_CrossRoad["CrossRoad"]
    n_openx_Domain_MakeATurn["MakeATurn"]
    n_openx_Domain_Roundabout["Roundabout"]
    n_openx_Domain_Motorway["Motorway"]
    n_openx_Domain_Pedestrian["Pedestrian"]
    n_openx_Domain_VRU["VRU"]
    n_openx_Domain_Bicycle["Bicycle"]
    n_openx_Domain_Motorcycle["Motorcycle"]
    n_openx_Domain_CycleLane["CycleLane"]
    n_openx_Domain_Bridge["Bridge"]
    n_openx_Domain_RailCrossing["RailCrossing"]
    n_openx_Domain_TollPlaza["TollPlaza"]
    n_openx_Domain_RoadWork["RoadWork"]
    n_openx_Domain_TemporaryRoadStructure["TemporaryRoadStructure"]
    n_openx_Domain_Debris["Debris"]
    n_openx_Domain_EmergencyVehicle["EmergencyVehicle"]
    n_openx_Domain_SoundSiren["SoundSiren"]
    n_openx_Domain_PublicTransportationVehicle["PublicTransportationVehicle"]
    n_openx_Domain_Parking["Parking"]
    n_openx_Domain_MoveBackward["MoveBackward"]
    n_openx_Domain_SharedSpace["SharedSpace"]
    n_openx_Domain_PedestrianZone["PedestrianZone"]
    n_openx_Domain_SchoolZone["SchoolZone"]
    n_openx_Domain_drivingDirection["drivingDirection"]
    n_openx_Domain_VehicleCommunicationActivity["VehicleCommunicationActivity"]
    n_openx_Domain_UseTurnIndicator["UseTurnIndicator"]
    n_openx_Domain_UseHazardLight["UseHazardLight"]
    n_openx_Domain_Animal["Animal"]
    n_openx_Domain_EnvironmentalCondition["EnvironmentalCondition"]
    n_openx_Domain_RoadTopologyAndTrafficInfrastructure["RoadTopologyAndTrafficInfrastructure"]
    n_openx_Domain_TrafficParticipantAndBehavior["TrafficParticipantAndBehavior"]
  end
  subgraph sg_policy["policy｜VirtualDriver 交通ポリシー"]
    n_policy_crosswalk["crosswalk"]
    n_policy_traffic_light["traffic_light"]
    n_policy_junction_priority["junction_priority"]
    n_policy_stop_yield["stop_yield"]
    n_policy_lead["lead"]
    n_policy_conflict["conflict"]
  end
  subgraph sg_scene["scene｜VD自動運転 対応シーンカタログ（SCN-001..018 = ファミリA-R）"]
    n_scene_SCN_001["SCN-001"]
    n_scene_SCN_002["SCN-002"]
    n_scene_SCN_003["SCN-003"]
    n_scene_SCN_004["SCN-004"]
    n_scene_SCN_005["SCN-005"]
    n_scene_SCN_006["SCN-006"]
    n_scene_SCN_007["SCN-007"]
    n_scene_SCN_008["SCN-008"]
    n_scene_SCN_009["SCN-009"]
    n_scene_SCN_010["SCN-010"]
    n_scene_SCN_011["SCN-011"]
    n_scene_SCN_012["SCN-012"]
    n_scene_SCN_013["SCN-013"]
    n_scene_SCN_014["SCN-014"]
    n_scene_SCN_015["SCN-015"]
    n_scene_SCN_016["SCN-016"]
    n_scene_SCN_017["SCN-017"]
    n_scene_SCN_018["SCN-018"]
  end
  subgraph sg_req_vd_ad["req-vd-ad｜VirtualDriver 自動運転/ADAS 対応シーン要求（機能軸 安全/快適/法規遵守/譲り合い）"]
    n_req_vd_ad_REQ_AD_002["REQ-AD-002"]
    n_req_vd_ad_REQ_AD_003["REQ-AD-003"]
    n_req_vd_ad_REQ_AD_004["REQ-AD-004"]
    n_req_vd_ad_REQ_AD_005["REQ-AD-005"]
    n_req_vd_ad_REQ_AD_006["REQ-AD-006"]
    n_req_vd_ad_REQ_AD_001["REQ-AD-001"]
  end
  subgraph sg_matcher["matcher｜検証matcher（vd_metrics event語彙）"]
    n_matcher_maintained_following_distance["maintained_following_distance"]
    n_matcher_stopped_at_signal["stopped_at_signal"]
    n_matcher_stopped_at_stop_sign["stopped_at_stop_sign"]
    n_matcher_min_obb_separation_above["min_obb_separation_above"]
  end
  n_proposal_P24 -->|merged-into| n_proposal_P15
  n_proposal_P39 -->|merged-into| n_proposal_P13
  n_proposal_P8 -->|merged-into| n_proposal_P2
  n_proposal_P5 -->|shares-design-with| n_proposal_P40
  n_proposal_P41 -->|depends-on| n_proposal_P5
  n_proposal_P1 -->|shares-design-with| n_proposal_P23
  n_proposal_P7 -->|depends-on| n_feature_F2
  n_proposal_P7 -->|depends-on| n_feature_F3
  n_proposal_P11 -->|complements| n_feature_F2
  n_proposal_P26 -->|depends-on| n_debt_phase_R5_U4
  n_feature_F6 -->|depends-on| n_debt_phase_R5_U3
  n_feature_F3 -->|depends-on| n_odr_plan_P5
  n_fork_patch_3 -->|upstream-candidate| n_odr_upstream_pr_PR_1
  n_fork_patch_7 -->|upstream-candidate| n_odr_upstream_pr_PR_2
  n_fork_patch_8 -->|upstream-candidate| n_odr_upstream_pr_PR_1b
  n_fork_patch_13 -->|upstream-candidate| n_odr_upstream_pr_PR_3
  n_fork_patch_10 -->|upstream-candidate| n_odr_upstream_pr_PR_4
  n_fork_patch_17 -->|upstream-candidate| n_odr_upstream_pr_PR_5
  n_feature_F6 -. concerns .-> n_openx_Domain_VehicleLighting
  n_feature_F6 -. concerns .-> n_openx_Domain_IlluminationCondition
  n_feature_F6 -. concerns .-> n_openx_Domain_Tunnel
  n_feature_F6 -. concerns .-> n_openx_Domain_NightLightingCondition
  n_policy_crosswalk -. concerns .-> n_openx_Domain_PedestrianCrossing
  n_policy_traffic_light -. concerns .-> n_openx_Domain_DynamicTrafficSign
  n_policy_junction_priority -. concerns .-> n_openx_Domain_Junction
  n_policy_stop_yield -. concerns .-> n_openx_Domain_RegulatorySign
  n_policy_lead -. concerns .-> n_openx_Domain_FollowRoadUser
  n_policy_conflict -. concerns .-> n_openx_Domain_IntersectionAtGrade
  n_scene_SCN_001 -. concerns .-> n_openx_Domain_Road
  n_scene_SCN_001 -. concerns .-> n_openx_Domain_KeepLane
  n_scene_SCN_001 -. concerns .-> n_openx_Domain_FollowTargetSpeed
  n_scene_SCN_002 -. concerns .-> n_openx_Domain_ChangeLane
  n_scene_SCN_002 -. concerns .-> n_openx_Domain_Overtake
  n_scene_SCN_002 -. concerns .-> n_openx_Domain_CutIn
  n_scene_SCN_002 -. concerns .-> n_openx_Domain_CutOut
  n_scene_SCN_003 -. concerns .-> n_openx_Domain_FollowRoadUser
  n_scene_SCN_003 -. concerns .-> n_openx_Domain_Decelerate
  n_scene_SCN_003 -. concerns .-> n_openx_Domain_Stop
  n_scene_SCN_004 -. concerns .-> n_openx_Domain_DynamicTrafficSign
  n_scene_SCN_004 -. concerns .-> n_openx_Domain_CrossRoad
  n_scene_SCN_004 -. concerns .-> n_openx_Domain_MakeATurn
  n_scene_SCN_005 -. concerns .-> n_openx_Domain_IntersectionAtGrade
  n_scene_SCN_005 -. concerns .-> n_openx_Domain_RegulatorySign
  n_scene_SCN_005 -. concerns .-> n_openx_Domain_MakeATurn
  n_scene_SCN_006 -. concerns .-> n_openx_Domain_Junction
  n_scene_SCN_006 -. concerns .-> n_openx_Domain_MakeATurn
  n_scene_SCN_007 -. concerns .-> n_openx_Domain_Roundabout
  n_scene_SCN_008 -. concerns .-> n_openx_Domain_Motorway
  n_scene_SCN_008 -. concerns .-> n_openx_Domain_ChangeLane
  n_scene_SCN_009 -. concerns .-> n_openx_Domain_PedestrianCrossing
  n_scene_SCN_009 -. concerns .-> n_openx_Domain_Pedestrian
  n_scene_SCN_009 -. concerns .-> n_openx_Domain_VRU
  n_scene_SCN_010 -. concerns .-> n_openx_Domain_Bicycle
  n_scene_SCN_010 -. concerns .-> n_openx_Domain_Motorcycle
  n_scene_SCN_010 -. concerns .-> n_openx_Domain_CycleLane
  n_scene_SCN_011 -. concerns .-> n_openx_Domain_Tunnel
  n_scene_SCN_011 -. concerns .-> n_openx_Domain_Bridge
  n_scene_SCN_011 -. concerns .-> n_openx_Domain_RailCrossing
  n_scene_SCN_011 -. concerns .-> n_openx_Domain_TollPlaza
  n_scene_SCN_012 -. concerns .-> n_openx_Domain_RoadWork
  n_scene_SCN_012 -. concerns .-> n_openx_Domain_TemporaryRoadStructure
  n_scene_SCN_012 -. concerns .-> n_openx_Domain_Debris
  n_scene_SCN_013 -. concerns .-> n_openx_Domain_EmergencyVehicle
  n_scene_SCN_013 -. concerns .-> n_openx_Domain_SoundSiren
  n_scene_SCN_013 -. concerns .-> n_openx_Domain_PublicTransportationVehicle
  n_scene_SCN_014 -. concerns .-> n_openx_Domain_Parking
  n_scene_SCN_014 -. concerns .-> n_openx_Domain_MoveBackward
  n_scene_SCN_015 -. concerns .-> n_openx_Domain_SharedSpace
  n_scene_SCN_015 -. concerns .-> n_openx_Domain_PedestrianZone
  n_scene_SCN_015 -. concerns .-> n_openx_Domain_SchoolZone
  n_scene_SCN_016 -. concerns .-> n_openx_Domain_drivingDirection
  n_scene_SCN_017 -. concerns .-> n_openx_Domain_VehicleCommunicationActivity
  n_scene_SCN_017 -. concerns .-> n_openx_Domain_UseTurnIndicator
  n_scene_SCN_017 -. concerns .-> n_openx_Domain_UseHazardLight
  n_scene_SCN_018 -. concerns .-> n_openx_Domain_Animal
  n_proposal_P13 -. concerns .-> n_openx_Domain_EnvironmentalCondition
  n_proposal_P13 -. concerns .-> n_openx_Domain_RoadTopologyAndTrafficInfrastructure
  n_proposal_P13 -. concerns .-> n_openx_Domain_TrafficParticipantAndBehavior
  n_policy_lead -->|realizes| n_req_vd_ad_REQ_AD_002
  n_matcher_maintained_following_distance -->|verifies| n_req_vd_ad_REQ_AD_002
  n_req_vd_ad_REQ_AD_002 -. concerns .-> n_openx_Domain_FollowRoadUser
  n_policy_traffic_light -->|realizes| n_req_vd_ad_REQ_AD_003
  n_matcher_stopped_at_signal -->|verifies| n_req_vd_ad_REQ_AD_003
  n_policy_stop_yield -->|realizes| n_req_vd_ad_REQ_AD_004
  n_matcher_stopped_at_stop_sign -->|verifies| n_req_vd_ad_REQ_AD_004
  n_policy_crosswalk -->|realizes| n_req_vd_ad_REQ_AD_005
  n_matcher_min_obb_separation_above -->|verifies| n_req_vd_ad_REQ_AD_005
  n_policy_conflict -->|realizes| n_req_vd_ad_REQ_AD_006
  n_policy_junction_priority -->|realizes| n_req_vd_ad_REQ_AD_006
  n_matcher_min_obb_separation_above -->|verifies| n_req_vd_ad_REQ_AD_001
  n_req_vd_ad_REQ_AD_001 -->|depends-on| n_proposal_P12
  n_req_vd_ad_REQ_AD_001 -->|depends-on| n_proposal_P11
```

## 辺の一覧（type別）

### complements (1)

| from | to | note |
| :--- | :--- | :--- |
| `proposal:P11` | `feature:F2` | 安全マージン評価に直結・F2と並走前提 |

### concerns (61)

| from | to | note |
| :--- | :--- | :--- |
| `feature:F6` | `openx:Domain#VehicleLighting` |  |
| `feature:F6` | `openx:Domain#IlluminationCondition` |  |
| `feature:F6` | `openx:Domain#Tunnel` |  |
| `feature:F6` | `openx:Domain#NightLightingCondition` |  |
| `policy:crosswalk` | `openx:Domain#PedestrianCrossing` |  |
| `policy:traffic_light` | `openx:Domain#DynamicTrafficSign` | OpenX v1.0にTrafficLightクラスは存在しない。DynamicTrafficSignが最近縁（暫定） |
| `policy:junction_priority` | `openx:Domain#Junction` |  |
| `policy:stop_yield` | `openx:Domain#RegulatorySign` |  |
| `policy:lead` | `openx:Domain#FollowRoadUser` |  |
| `policy:conflict` | `openx:Domain#IntersectionAtGrade` | 交差点内交錯（コンフリクトポイント）の暫定対応クラス |
| `scene:SCN-001` | `openx:Domain#Road` |  |
| `scene:SCN-001` | `openx:Domain#KeepLane` |  |
| `scene:SCN-001` | `openx:Domain#FollowTargetSpeed` |  |
| `scene:SCN-002` | `openx:Domain#ChangeLane` |  |
| `scene:SCN-002` | `openx:Domain#Overtake` |  |
| `scene:SCN-002` | `openx:Domain#CutIn` |  |
| `scene:SCN-002` | `openx:Domain#CutOut` |  |
| `scene:SCN-003` | `openx:Domain#FollowRoadUser` |  |
| `scene:SCN-003` | `openx:Domain#Decelerate` |  |
| `scene:SCN-003` | `openx:Domain#Stop` |  |
| `scene:SCN-004` | `openx:Domain#DynamicTrafficSign` | 信号交差点。TrafficLightクラスはv1.0に無くDynamicTrafficSignで暫定 |
| `scene:SCN-004` | `openx:Domain#CrossRoad` |  |
| `scene:SCN-004` | `openx:Domain#MakeATurn` |  |
| `scene:SCN-005` | `openx:Domain#IntersectionAtGrade` |  |
| `scene:SCN-005` | `openx:Domain#RegulatorySign` |  |
| `scene:SCN-005` | `openx:Domain#MakeATurn` |  |
| `scene:SCN-006` | `openx:Domain#Junction` | 対向車ギャップ受容（右折/RHT左折の対向待ち）。MakeATurn単独と区別 |
| `scene:SCN-006` | `openx:Domain#MakeATurn` |  |
| `scene:SCN-007` | `openx:Domain#Roundabout` |  |
| `scene:SCN-008` | `openx:Domain#Motorway` |  |
| `scene:SCN-008` | `openx:Domain#ChangeLane` |  |
| `scene:SCN-009` | `openx:Domain#PedestrianCrossing` |  |
| `scene:SCN-009` | `openx:Domain#Pedestrian` |  |
| `scene:SCN-009` | `openx:Domain#VRU` |  |
| `scene:SCN-010` | `openx:Domain#Bicycle` |  |
| `scene:SCN-010` | `openx:Domain#Motorcycle` |  |
| `scene:SCN-010` | `openx:Domain#CycleLane` |  |
| `scene:SCN-011` | `openx:Domain#Tunnel` |  |
| `scene:SCN-011` | `openx:Domain#Bridge` |  |
| `scene:SCN-011` | `openx:Domain#RailCrossing` |  |
| `scene:SCN-011` | `openx:Domain#TollPlaza` |  |
| `scene:SCN-012` | `openx:Domain#RoadWork` |  |
| `scene:SCN-012` | `openx:Domain#TemporaryRoadStructure` |  |
| `scene:SCN-012` | `openx:Domain#Debris` |  |
| `scene:SCN-013` | `openx:Domain#EmergencyVehicle` |  |
| `scene:SCN-013` | `openx:Domain#SoundSiren` |  |
| `scene:SCN-013` | `openx:Domain#PublicTransportationVehicle` |  |
| `scene:SCN-014` | `openx:Domain#Parking` |  |
| `scene:SCN-014` | `openx:Domain#MoveBackward` |  |
| `scene:SCN-015` | `openx:Domain#SharedSpace` |  |
| `scene:SCN-015` | `openx:Domain#PedestrianZone` |  |
| `scene:SCN-015` | `openx:Domain#SchoolZone` |  |
| `scene:SCN-016` | `openx:Domain#drivingDirection` | LHT/RHT両建て。全シーンファミリの横断バリアント軸 |
| `scene:SCN-017` | `openx:Domain#VehicleCommunicationActivity` |  |
| `scene:SCN-017` | `openx:Domain#UseTurnIndicator` |  |
| `scene:SCN-017` | `openx:Domain#UseHazardLight` |  |
| `scene:SCN-018` | `openx:Domain#Animal` |  |
| `proposal:P13` | `openx:Domain#EnvironmentalCondition` | ODDカバレッジ台帳の環境軸はOpenX傘構造（ISO 34503整合）を土台にする方針 |
| `proposal:P13` | `openx:Domain#RoadTopologyAndTrafficInfrastructure` | 同・道路トポロジー軸 |
| `proposal:P13` | `openx:Domain#TrafficParticipantAndBehavior` | 同・交通参加者/行動軸 |
| `req-vd-ad:REQ-AD-002` | `openx:Domain#FollowRoadUser` | 先行車追従のODD軸 |

### depends-on (8)

| from | to | note |
| :--- | :--- | :--- |
| `proposal:P41` | `proposal:P5` | 入力再生機構の統一が前提（UDP独自再生案は却下） |
| `proposal:P7` | `feature:F2` | F2/F3完了後に着手 |
| `proposal:P7` | `feature:F3` |  |
| `proposal:P26` | `debt-phase:R5-U4` | OSIポート設計のすり合わせが前提（R5-U4は完了済み） |
| `feature:F6` | `debt-phase:R5-U3` | ライトストレージ統合（vehLghtStsList一本化）完了後に着手（両方完了済み） |
| `feature:F3` | `odr-plan:P5` | junction priorityデータはODRプランP5で着地、消費はF3（Phase3e） |
| `req-vd-ad:REQ-AD-001` | `proposal:P12` | collision-free 不変条件（衝突検出）が回帰固化の前提 |
| `req-vd-ad:REQ-AD-001` | `proposal:P11` | 必要減速度/TTC メトリクスが緊急介入判定・検証の前提 |

### merged-into (3)

| from | to | note |
| :--- | :--- | :--- |
| `proposal:P24` | `proposal:P15` | 同一提案としてP15に一本化 |
| `proposal:P39` | `proposal:P13` | ODDカバレッジ台帳部分はP13と統合が前提（log2xosc由来meta拡張は残件） |
| `proposal:P8` | `proposal:P2` | 配信部が同一のためP2に吸収 |

### realizes (6)

| from | to | note |
| :--- | :--- | :--- |
| `policy:lead` | `req-vd-ad:REQ-AD-002` | 快適機能。LeadVehicleAware(IDM)=ACC相当の定常追従 |
| `policy:traffic_light` | `req-vd-ad:REQ-AD-003` | 法規遵守機能。信号停止 |
| `policy:stop_yield` | `req-vd-ad:REQ-AD-004` | 法規遵守機能。一時停止標識 |
| `policy:crosswalk` | `req-vd-ad:REQ-AD-005` | 法規遵守機能。歩行者優先 |
| `policy:conflict` | `req-vd-ad:REQ-AD-006` | 譲り合い機能。コリドー衝突判定で優先権評価 |
| `policy:junction_priority` | `req-vd-ad:REQ-AD-006` | 譲り合い機能。交差点優先権 |

### shares-design-with (2)

| from | to | note |
| :--- | :--- | :--- |
| `proposal:P5` | `proposal:P40` | ReplayInputSource機構の一本化必須 |
| `proposal:P1` | `proposal:P23` | 外部制御注入の二重資産回避のため設計共有必須 |

### upstream-candidate (6)

| from | to | note |
| :--- | :--- | :--- |
| `fork-patch:3` | `odr-upstream-pr:PR-1` |  |
| `fork-patch:7` | `odr-upstream-pr:PR-2` |  |
| `fork-patch:8` | `odr-upstream-pr:PR-1b` |  |
| `fork-patch:13` | `odr-upstream-pr:PR-3` |  |
| `fork-patch:10` | `odr-upstream-pr:PR-4` |  |
| `fork-patch:17` | `odr-upstream-pr:PR-5` |  |

### verifies (5)

| from | to | note |
| :--- | :--- | :--- |
| `matcher:maintained_following_distance` | `req-vd-ad:REQ-AD-002` | THW車間の維持を検証 |
| `matcher:stopped_at_signal` | `req-vd-ad:REQ-AD-003` | 停止線手前停止を検証 |
| `matcher:stopped_at_stop_sign` | `req-vd-ad:REQ-AD-004` | STOP標識停止を検証 |
| `matcher:min_obb_separation_above` | `req-vd-ad:REQ-AD-005` | 歩行者とのOBB分離（衝突ゼロ）を検証 |
| `matcher:min_obb_separation_above` | `req-vd-ad:REQ-AD-001` | カットイン追突回避=ego-他車OBB分離>0（衝突ゼロ） |

## OpenX概念 逆引き

curated辺のみ。Issue/コミット言及まで含めた逆引きは `--query openx:Domain#<Name> --issues --commits`。

| 概念 | 定義 | 接続ノード |
| :--- | :--- | :--- |
| `Domain#Animal` | 動物（TrafficParticipantByParticipantType） | `scene:SCN-018` |
| `Domain#Bicycle` | 自転車（PrivateVehicle配下・VRU） | `scene:SCN-010` |
| `Domain#Bridge` | 橋梁（SpecialStructure） | `scene:SCN-011` |
| `Domain#ChangeLane` | 車線変更（開始時と終了時で異なる車線） | `scene:SCN-002`, `scene:SCN-008` |
| `Domain#CrossRoad` | 十字路 | `scene:SCN-004` |
| `Domain#CutIn` | カットイン（対象直前への割込み。対象挙動に影響しうる） | `scene:SCN-002` |
| `Domain#CutOut` | カットアウト（対象前方からの離脱） | `scene:SCN-002` |
| `Domain#CycleLane` | 自転車レーン（LaneForSpecificParticipants） | `scene:SCN-010` |
| `Domain#Debris` | 落下物・瓦礫（TemporaryRoadStructure） | `scene:SCN-012` |
| `Domain#Decelerate` | 減速 | `scene:SCN-003` |
| `Domain#DynamicTrafficSign` | 動的交通標識（可変標識。信号機の最近縁クラス — TrafficLightクラスはv1.0に無い） | `policy:traffic_light`, `scene:SCN-004` |
| `Domain#EmergencyVehicle` | 緊急車両（NonVRU） | `scene:SCN-013` |
| `Domain#EnvironmentalCondition` | 環境条件（天候・照明・時刻・粒子状物質等の親クラス） | `proposal:P13` |
| `Domain#FollowRoadUser` | 先行者追従 | `policy:lead`, `req-vd-ad:REQ-AD-002`, `scene:SCN-003` |
| `Domain#FollowTargetSpeed` | 目標速度追従（ManeuverLevelActivity） | `scene:SCN-001` |
| `Domain#IlluminationCondition` | 照明条件（昼光・夜間・人工照明の親クラス） | `feature:F6` |
| `Domain#IntersectionAtGrade` | 平面交差点 | `policy:conflict`, `scene:SCN-005` |
| `Domain#Junction` | 交差点（2つ以上の道路が交わる） | `policy:junction_priority`, `scene:SCN-006` |
| `Domain#KeepLane` | 車線維持 | `scene:SCN-001` |
| `Domain#MakeATurn` | 右左折（交差点通過） | `scene:SCN-004`, `scene:SCN-005`, `scene:SCN-006` |
| `Domain#Motorcycle` | 二輪車（PrivateVehicle配下） | `scene:SCN-010` |
| `Domain#Motorway` | 高速道路 | `scene:SCN-008` |
| `Domain#MoveBackward` | 後退（LongitudinalActivity） | `scene:SCN-014` |
| `Domain#NightLightingCondition` | 夜間照明条件 | `feature:F6` |
| `Domain#Overtake` | 追越し（対象後方に始まり前方で終わる、2回の車線変更を伴う） | `scene:SCN-002` |
| `Domain#Parking` | 駐車場・駐車スペース（Road配下） | `scene:SCN-014` |
| `Domain#Pedestrian` | 歩行者 | `scene:SCN-009` |
| `Domain#PedestrianCrossing` | 横断歩道（歩行者が道路/車線を横断できる特殊構造） | `policy:crosswalk`, `scene:SCN-009` |
| `Domain#PedestrianZone` | 歩行者専用ゾーン | `scene:SCN-015` |
| `Domain#PublicTransportationVehicle` | 公共交通車両（バス等） | `scene:SCN-013` |
| `Domain#RailCrossing` | 踏切（SpecialStructure） | `scene:SCN-011` |
| `Domain#RegulatorySign` | 規制標識（停止・譲れ等） | `policy:stop_yield`, `scene:SCN-005` |
| `Domain#Road` | 道路 | `scene:SCN-001` |
| `Domain#RoadTopologyAndTrafficInfrastructure` | 道路トポロジー・交通インフラの親クラス | `proposal:P13` |
| `Domain#RoadWork` | 道路工事（TemporaryRoadStructure） | `scene:SCN-012` |
| `Domain#Roundabout` | ラウンドアバウト（中央島を一方向に周回する交差点） | `scene:SCN-007` |
| `Domain#SchoolZone` | スクールゾーン（若年歩行者存在確率が高いTrafficZone） | `scene:SCN-015` |
| `Domain#SharedSpace` | 歩車共存道路（Road配下） | `scene:SCN-015` |
| `Domain#SoundSiren` | サイレン吹鳴（VehicleCommunicationActivity） | `scene:SCN-013` |
| `Domain#Stop` | 停止 | `scene:SCN-003` |
| `Domain#TemporaryRoadStructure` | 一時的道路構造（工事帯・コーン・落下物・道路封鎖等の親クラス） | `scene:SCN-012` |
| `Domain#TollPlaza` | 料金所（SpecialStructure） | `scene:SCN-011` |
| `Domain#TrafficParticipantAndBehavior` | 交通参加者と行動の親クラス | `proposal:P13` |
| `Domain#Tunnel` | トンネル | `feature:F6`, `scene:SCN-011` |
| `Domain#UseHazardLight` | ハザードランプ使用 | `scene:SCN-017` |
| `Domain#UseTurnIndicator` | 方向指示器の使用 | `scene:SCN-017` |
| `Domain#VRU` | 脆弱道路利用者（歩行者・自転車等、移動デバイス含む） | `scene:SCN-009` |
| `Domain#VehicleCommunicationActivity` | 車両の意思表示行動（指示器・ハザード・ホーン・パッシング・サイレンの親クラス） | `scene:SCN-017` |
| `Domain#VehicleLighting` | 車両搭載照明（ヘッドライト等）による照明条件 | `feature:F6` |
| `Domain#drivingDirection` | 通行方向属性（LHT/RHT。roadTopologyAndTrafficInfrastructureProperty） | `scene:SCN-016` |
