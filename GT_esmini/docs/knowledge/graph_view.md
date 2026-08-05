# Knowledge Graph View

> **GENERATED — do not edit.** Source of truth: `graph.yaml` / `namespaces.yaml`.
> Regenerate: `DriverScript/.venv/Scripts/python.exe scripts/check_knowledge_graph.py --render`

<!-- generated-from: sha256:c6f9bafc4ea4b173 -->

ノード 236・辺 266（curatedのみ。commit由来の辺は `--extract-commits` で別途抽出）

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
    n_proposal_P17["P17"]
    n_proposal_P18["P18"]
  end
  subgraph sg_feature["feature｜機能ロードマップ F1-F7"]
    n_feature_F2["F2"]
    n_feature_F3["F3"]
    n_feature_F6["F6"]
    n_feature_F7["F7"]
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
    n_policy_aeb["aeb"]
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
  subgraph sg_vd_func["vd-func｜VirtualDriver ADAS/AD 機能カタログ（安全/快適/法規遵守/譲り合い × AD/ADAS）"]
    n_vd_func_FUNC_013["FUNC-013"]
    n_vd_func_FUNC_023["FUNC-023"]
    n_vd_func_FUNC_024["FUNC-024"]
    n_vd_func_FUNC_025["FUNC-025"]
    n_vd_func_FUNC_027["FUNC-027"]
    n_vd_func_FUNC_037["FUNC-037"]
    n_vd_func_FUNC_041["FUNC-041"]
    n_vd_func_FUNC_038["FUNC-038"]
    n_vd_func_FUNC_029["FUNC-029"]
    n_vd_func_FUNC_001["FUNC-001"]
    n_vd_func_FUNC_049["FUNC-049"]
    n_vd_func_FUNC_050["FUNC-050"]
    n_vd_func_FUNC_055["FUNC-055"]
    n_vd_func_FUNC_052["FUNC-052"]
    n_vd_func_FUNC_054["FUNC-054"]
    n_vd_func_FUNC_061["FUNC-061"]
    n_vd_func_FUNC_076["FUNC-076"]
    n_vd_func_FUNC_077["FUNC-077"]
    n_vd_func_FUNC_078["FUNC-078"]
    n_vd_func_FUNC_075["FUNC-075"]
    n_vd_func_FUNC_079["FUNC-079"]
    n_vd_func_FUNC_080["FUNC-080"]
    n_vd_func_FUNC_081["FUNC-081"]
    n_vd_func_FUNC_002["FUNC-002"]
    n_vd_func_FUNC_056["FUNC-056"]
  end
  subgraph sg_req_vd_ad["req-vd-ad｜VirtualDriver 自動運転/ADAS 対応シーン要求（機能軸 安全/快適/法規遵守/譲り合い）"]
    n_req_vd_ad_REQ_AD_001["REQ-AD-001"]
    n_req_vd_ad_REQ_AD_002["REQ-AD-002"]
    n_req_vd_ad_REQ_AD_003["REQ-AD-003"]
    n_req_vd_ad_REQ_AD_004["REQ-AD-004"]
    n_req_vd_ad_REQ_AD_005["REQ-AD-005"]
    n_req_vd_ad_REQ_AD_006["REQ-AD-006"]
    n_req_vd_ad_REQ_AD_016["REQ-AD-016"]
    n_req_vd_ad_REQ_AD_017["REQ-AD-017"]
    n_req_vd_ad_REQ_AD_013["REQ-AD-013"]
    n_req_vd_ad_REQ_AD_021["REQ-AD-021"]
    n_req_vd_ad_REQ_AD_018["REQ-AD-018"]
    n_req_vd_ad_REQ_AD_019["REQ-AD-019"]
    n_req_vd_ad_REQ_AD_020["REQ-AD-020"]
    n_req_vd_ad_REQ_AD_022["REQ-AD-022"]
    n_req_vd_ad_REQ_AD_025["REQ-AD-025"]
    n_req_vd_ad_REQ_AD_028["REQ-AD-028"]
    n_req_vd_ad_REQ_AD_026["REQ-AD-026"]
    n_req_vd_ad_REQ_AD_027["REQ-AD-027"]
    n_req_vd_ad_REQ_AD_029["REQ-AD-029"]
    n_req_vd_ad_REQ_AD_030["REQ-AD-030"]
    n_req_vd_ad_REQ_AD_031["REQ-AD-031"]
    n_req_vd_ad_REQ_AD_010["REQ-AD-010"]
    n_req_vd_ad_REQ_AD_011["REQ-AD-011"]
    n_req_vd_ad_REQ_AD_012["REQ-AD-012"]
    n_req_vd_ad_REQ_AD_014["REQ-AD-014"]
    n_req_vd_ad_REQ_AD_015["REQ-AD-015"]
    n_req_vd_ad_REQ_AD_023["REQ-AD-023"]
    n_req_vd_ad_REQ_AD_024["REQ-AD-024"]
  end
  subgraph sg_matcher["matcher｜検証matcher（vd_metrics event語彙）"]
    n_matcher_maintained_following_distance["maintained_following_distance"]
    n_matcher_stopped_at_signal["stopped_at_signal"]
    n_matcher_stopped_at_stop_sign["stopped_at_stop_sign"]
    n_matcher_min_obb_separation_above["min_obb_separation_above"]
    n_matcher_impact_speed_below["impact_speed_below"]
    n_matcher_no_emergency_without_conflict["no_emergency_without_conflict"]
    n_matcher_route_lane_plan_holds["route_lane_plan_holds"]
    n_matcher_indicator_leads_junction_turn["indicator_leads_junction_turn"]
    n_matcher_indicator_leads_lane_change["indicator_leads_lane_change"]
    n_matcher_deceleration_profile_smooth["deceleration_profile_smooth"]
    n_matcher_speed_above["speed_above"]
    n_matcher_speed_below["speed_below"]
    n_matcher_lane_keep["lane_keep"]
    n_matcher_lane_change_count["lane_change_count"]
    n_matcher_min_speed_above["min_speed_above"]
    n_matcher_speed_reduction_before_landmark["speed_reduction_before_landmark"]
    n_matcher_steer_not_saturated["steer_not_saturated"]
    n_matcher_no_constraint_kind["no_constraint_kind"]
    n_matcher_overtake_decision_holds["overtake_decision_holds"]
    n_matcher_manual_aeb_fires["manual_aeb_fires"]
    n_matcher_no_intervention_in_window["no_intervention_in_window"]
    n_matcher_brake_not_stacked["brake_not_stacked"]
    n_matcher_fcw_leads_intervention["fcw_leads_intervention"]
    n_matcher_adas_state_matches["adas_state_matches"]
    n_matcher_driver_override_reported["driver_override_reported"]
    n_matcher_setting_reflected["setting_reflected"]
    n_matcher_adas_state_sequence["adas_state_sequence"]
    n_matcher_speed_capped_at["speed_capped_at"]
    n_matcher_no_brake_output["no_brake_output"]
    n_matcher_stop_hold_stationary["stop_hold_stationary"]
    n_matcher_restart_after_trigger["restart_after_trigger"]
    n_matcher_lane_kept_within["lane_kept_within"]
    n_matcher_steer_output_absent["steer_output_absent"]
  end
  subgraph sg_vd_component["vd-component｜VirtualDriver 実装ユニット（ITrafficPolicy 以外の層）"]
    n_vd_component_route_lane_plan["route-lane-plan"]
    n_vd_component_lane_change_initiation["lane-change-initiation"]
    n_vd_component_overtake_maneuver["overtake-maneuver"]
    n_vd_component_lane_keep_assist["lane-keep-assist"]
  end
  subgraph sg_scenario_variant["scenario-variant｜生成シナリオ変体（NN_topic__pNNN）"]
    n_scenario_variant_09_crosswalk_pedestrian__p005["09_crosswalk_pedestrian__p005"]
    n_scenario_variant_07_oncoming_yield__p017["07_oncoming_yield__p017"]
    n_scenario_variant_08_unsignalized_junction__p004["08_unsignalized_junction__p004"]
  end
  subgraph sg_signal["signal｜観測可能量（OSI GroundTruth / HostVehicleData が canonical）"]
    n_signal_ego_speed["ego_speed"]
    n_signal_ego_lane["ego_lane"]
    n_signal_ego_pose["ego_pose"]
    n_signal_traffic_light_state["traffic_light_state"]
    n_signal_traffic_sign_classification["traffic_sign_classification"]
    n_signal_traffic_light_assigned_lane["traffic_light_assigned_lane"]
    n_signal_pedestrian_velocity_vector["pedestrian_velocity_vector"]
    n_signal_object_poses["object_poses"]
    n_signal_obb_separation["obb_separation"]
    n_signal_aeb_trigger_flag["aeb_trigger_flag"]
    n_signal_route_lane_conformance["route_lane_conformance"]
    n_signal_lane_change_signal_timing["lane_change_signal_timing"]
    n_signal_junction_turn_signal_distance["junction_turn_signal_distance"]
    n_signal_overtake_decision["overtake_decision"]
    n_signal_manualdrive_adas_states["manualdrive_adas_states"]
    n_signal_manualdrive_driver_override["manualdrive_driver_override"]
    n_signal_manualdrive_acc_settings["manualdrive_acc_settings"]
    n_signal_manualdrive_msl_cap["manualdrive_msl_cap"]
    n_signal_manualdrive_lka_lateral["manualdrive_lka_lateral"]
  end
  subgraph sg_gate["gate｜常設検証ゲート（回帰で恒久的に走る単位）"]
    n_gate_vd_behavior_regression["vd-behavior-regression"]
    n_gate_aeb_safety_regression["aeb-safety-regression"]
    n_gate_anticipation_driving_regression["anticipation-driving-regression"]
    n_gate_integration_ctest["integration-ctest"]
    n_gate_regression_gate["regression-gate"]
    n_gate_unit_ctest["unit-ctest"]
    n_gate_odr_conformance_quick["odr-conformance-quick"]
    n_gate_fork_census["fork-census"]
    n_gate_fork_drift["fork-drift"]
    n_gate_resync_guards["resync-guards"]
    n_gate_odr_conformance_schema_ci["odr-conformance-schema-ci"]
    n_gate_fork_sync["fork-sync"]
    n_gate_odr_conformance_full["odr-conformance-full"]
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
  n_policy_lead -->|realizes| n_vd_func_FUNC_013
  n_policy_traffic_light -->|realizes| n_vd_func_FUNC_023
  n_policy_stop_yield -->|realizes| n_vd_func_FUNC_024
  n_policy_stop_yield -->|realizes| n_vd_func_FUNC_025
  n_policy_crosswalk -->|realizes| n_vd_func_FUNC_027
  n_policy_crosswalk -->|realizes| n_vd_func_FUNC_037
  n_policy_crosswalk -->|realizes| n_vd_func_FUNC_041
  n_policy_conflict -->|realizes| n_vd_func_FUNC_038
  n_policy_junction_priority -->|realizes| n_vd_func_FUNC_029
  n_policy_aeb -->|realizes| n_vd_func_FUNC_001
  n_vd_func_FUNC_001 -->|realizes| n_req_vd_ad_REQ_AD_001
  n_vd_func_FUNC_013 -->|realizes| n_req_vd_ad_REQ_AD_002
  n_vd_func_FUNC_023 -->|realizes| n_req_vd_ad_REQ_AD_003
  n_vd_func_FUNC_024 -->|realizes| n_req_vd_ad_REQ_AD_004
  n_vd_func_FUNC_027 -->|realizes| n_req_vd_ad_REQ_AD_005
  n_vd_func_FUNC_038 -->|realizes| n_req_vd_ad_REQ_AD_006
  n_vd_func_FUNC_029 -->|realizes| n_req_vd_ad_REQ_AD_006
  n_vd_func_FUNC_049 -->|realizes| n_req_vd_ad_REQ_AD_016
  n_vd_func_FUNC_050 -->|realizes| n_req_vd_ad_REQ_AD_017
  n_vd_func_FUNC_055 -->|realizes| n_req_vd_ad_REQ_AD_017
  n_vd_func_FUNC_052 -->|realizes| n_req_vd_ad_REQ_AD_017
  n_vd_func_FUNC_054 -->|realizes| n_req_vd_ad_REQ_AD_017
  n_matcher_maintained_following_distance -->|verifies| n_req_vd_ad_REQ_AD_002
  n_matcher_stopped_at_signal -->|verifies| n_req_vd_ad_REQ_AD_003
  n_matcher_stopped_at_stop_sign -->|verifies| n_req_vd_ad_REQ_AD_004
  n_matcher_min_obb_separation_above -->|verifies| n_req_vd_ad_REQ_AD_005
  n_matcher_min_obb_separation_above -->|verifies| n_req_vd_ad_REQ_AD_001
  n_matcher_impact_speed_below -->|verifies| n_req_vd_ad_REQ_AD_001
  n_matcher_no_emergency_without_conflict -->|verifies| n_req_vd_ad_REQ_AD_013
  n_vd_component_route_lane_plan -->|realizes| n_vd_func_FUNC_050
  n_vd_component_lane_change_initiation -->|realizes| n_vd_func_FUNC_055
  n_vd_component_lane_change_initiation -->|realizes| n_vd_func_FUNC_061
  n_matcher_route_lane_plan_holds -->|verifies| n_req_vd_ad_REQ_AD_017
  n_matcher_indicator_leads_junction_turn -->|verifies| n_req_vd_ad_REQ_AD_021
  n_matcher_indicator_leads_lane_change -->|verifies| n_req_vd_ad_REQ_AD_018
  n_vd_func_FUNC_061 -->|realizes| n_req_vd_ad_REQ_AD_021
  n_vd_func_FUNC_061 -->|realizes| n_req_vd_ad_REQ_AD_018
  n_matcher_route_lane_plan_holds -->|verifies| n_req_vd_ad_REQ_AD_016
  n_vd_func_FUNC_076 -->|realizes| n_req_vd_ad_REQ_AD_019
  n_vd_func_FUNC_077 -->|realizes| n_req_vd_ad_REQ_AD_020
  n_req_vd_ad_REQ_AD_019 -. concerns .-> n_openx_Domain_Parking
  n_req_vd_ad_REQ_AD_020 -. concerns .-> n_openx_Domain_Parking
  n_req_vd_ad_REQ_AD_020 -. concerns .-> n_openx_Domain_MoveBackward
  n_vd_func_FUNC_078 -->|realizes| n_req_vd_ad_REQ_AD_022
  n_req_vd_ad_REQ_AD_022 -. concerns .-> n_openx_Domain_Parking
  n_req_vd_ad_REQ_AD_022 -. concerns .-> n_openx_Domain_MoveBackward
  n_vd_func_FUNC_075 -->|realizes| n_req_vd_ad_REQ_AD_025
  n_vd_func_FUNC_075 -->|realizes| n_req_vd_ad_REQ_AD_028
  n_vd_func_FUNC_079 -->|realizes| n_req_vd_ad_REQ_AD_026
  n_vd_func_FUNC_080 -->|realizes| n_req_vd_ad_REQ_AD_027
  n_req_vd_ad_REQ_AD_026 -. concerns .-> n_openx_Domain_FollowRoadUser
  n_req_vd_ad_REQ_AD_027 -. concerns .-> n_openx_Domain_KeepLane
  n_vd_func_FUNC_075 -->|realizes| n_req_vd_ad_REQ_AD_029
  n_vd_func_FUNC_081 -->|realizes| n_req_vd_ad_REQ_AD_030
  n_vd_func_FUNC_079 -->|realizes| n_req_vd_ad_REQ_AD_031
  n_req_vd_ad_REQ_AD_002 -. concerns .-> n_openx_Domain_FollowRoadUser
  n_req_vd_ad_REQ_AD_001 -->|depends-on| n_proposal_P12
  n_req_vd_ad_REQ_AD_001 -->|depends-on| n_proposal_P11
  n_vd_func_FUNC_001 -->|realizes| n_req_vd_ad_REQ_AD_010
  n_vd_func_FUNC_001 -->|realizes| n_req_vd_ad_REQ_AD_011
  n_vd_func_FUNC_002 -->|realizes| n_req_vd_ad_REQ_AD_012
  n_vd_func_FUNC_001 -->|realizes| n_req_vd_ad_REQ_AD_013
  n_vd_func_FUNC_001 -->|realizes| n_req_vd_ad_REQ_AD_014
  n_vd_func_FUNC_001 -->|realizes| n_req_vd_ad_REQ_AD_015
  n_matcher_min_obb_separation_above -->|verifies| n_req_vd_ad_REQ_AD_010
  n_matcher_min_obb_separation_above -->|verifies| n_req_vd_ad_REQ_AD_011
  n_matcher_min_obb_separation_above -->|verifies| n_req_vd_ad_REQ_AD_012
  n_matcher_deceleration_profile_smooth -->|verifies| n_req_vd_ad_REQ_AD_014
  n_req_vd_ad_REQ_AD_010 -->|depends-on| n_proposal_P12
  n_req_vd_ad_REQ_AD_011 -->|depends-on| n_proposal_P11
  n_req_vd_ad_REQ_AD_011 -->|depends-on| n_proposal_P12
  n_req_vd_ad_REQ_AD_012 -->|depends-on| n_proposal_P12
  n_req_vd_ad_REQ_AD_013 -->|depends-on| n_proposal_P11
  n_req_vd_ad_REQ_AD_001 -->|stimulated-by| n_policy_aeb
  n_req_vd_ad_REQ_AD_013 -->|stimulated-by| n_policy_aeb
  n_req_vd_ad_REQ_AD_010 -->|stimulated-by| n_policy_aeb
  n_req_vd_ad_REQ_AD_011 -->|stimulated-by| n_policy_aeb
  n_req_vd_ad_REQ_AD_002 -->|stimulated-by| n_policy_lead
  n_req_vd_ad_REQ_AD_003 -->|stimulated-by| n_policy_traffic_light
  n_req_vd_ad_REQ_AD_004 -->|stimulated-by| n_policy_stop_yield
  n_req_vd_ad_REQ_AD_005 -->|stimulated-by| n_scenario_variant_09_crosswalk_pedestrian__p005
  n_req_vd_ad_REQ_AD_006 -->|stimulated-by| n_scenario_variant_07_oncoming_yield__p017
  n_req_vd_ad_REQ_AD_006 -->|stimulated-by| n_scenario_variant_08_unsignalized_junction__p004
  n_matcher_speed_above -->|observes| n_signal_ego_speed
  n_matcher_speed_below -->|observes| n_signal_ego_speed
  n_matcher_lane_keep -->|observes| n_signal_ego_lane
  n_matcher_lane_change_count -->|observes| n_signal_ego_lane
  n_matcher_min_speed_above -->|observes| n_signal_ego_speed
  n_matcher_speed_reduction_before_landmark -->|observes| n_signal_ego_speed
  n_matcher_speed_reduction_before_landmark -->|observes| n_signal_ego_pose
  n_matcher_deceleration_profile_smooth -->|observes| n_signal_ego_speed
  n_matcher_stopped_at_stop_sign -->|observes| n_signal_ego_speed
  n_matcher_stopped_at_signal -->|observes| n_signal_ego_speed
  n_matcher_stopped_at_signal -->|observes| n_signal_traffic_light_state
  n_matcher_stopped_at_stop_sign -->|observes| n_signal_traffic_sign_classification
  n_matcher_stopped_at_signal -->|observes| n_signal_traffic_light_assigned_lane
  n_matcher_impact_speed_below -->|observes| n_signal_pedestrian_velocity_vector
  n_matcher_maintained_following_distance -->|observes| n_signal_object_poses
  n_matcher_maintained_following_distance -->|observes| n_signal_ego_speed
  n_matcher_min_obb_separation_above -->|observes| n_signal_obb_separation
  n_matcher_impact_speed_below -->|observes| n_signal_obb_separation
  n_matcher_impact_speed_below -->|observes| n_signal_object_poses
  n_matcher_no_emergency_without_conflict -->|observes| n_signal_aeb_trigger_flag
  n_matcher_route_lane_plan_holds -->|observes| n_signal_route_lane_conformance
  n_matcher_indicator_leads_lane_change -->|observes| n_signal_lane_change_signal_timing
  n_matcher_indicator_leads_junction_turn -->|observes| n_signal_junction_turn_signal_distance
  n_matcher_speed_above -->|sustained-by| n_gate_vd_behavior_regression
  n_matcher_speed_below -->|sustained-by| n_gate_vd_behavior_regression
  n_matcher_min_speed_above -->|sustained-by| n_gate_vd_behavior_regression
  n_matcher_maintained_following_distance -->|sustained-by| n_gate_vd_behavior_regression
  n_matcher_stopped_at_signal -->|sustained-by| n_gate_vd_behavior_regression
  n_matcher_stopped_at_stop_sign -->|sustained-by| n_gate_vd_behavior_regression
  n_matcher_impact_speed_below -->|sustained-by| n_gate_aeb_safety_regression
  n_matcher_min_obb_separation_above -->|sustained-by| n_gate_aeb_safety_regression
  n_matcher_no_emergency_without_conflict -->|sustained-by| n_gate_aeb_safety_regression
  n_req_vd_ad_REQ_AD_001 -->|sustained-by| n_gate_aeb_safety_regression
  n_req_vd_ad_REQ_AD_013 -->|sustained-by| n_gate_aeb_safety_regression
  n_matcher_deceleration_profile_smooth -->|sustained-by| n_gate_anticipation_driving_regression
  n_matcher_speed_reduction_before_landmark -->|sustained-by| n_gate_anticipation_driving_regression
  n_matcher_indicator_leads_junction_turn -->|sustained-by| n_gate_anticipation_driving_regression
  n_matcher_lane_keep -->|sustained-by| n_gate_anticipation_driving_regression
  n_matcher_steer_not_saturated -->|sustained-by| n_gate_anticipation_driving_regression
  n_matcher_no_constraint_kind -->|sustained-by| n_gate_anticipation_driving_regression
  n_feature_F6 -->|sustained-by| n_gate_integration_ctest
  n_gate_regression_gate -->|depends-on| n_gate_unit_ctest
  n_gate_regression_gate -->|depends-on| n_gate_odr_conformance_quick
  n_gate_regression_gate -->|depends-on| n_gate_vd_behavior_regression
  n_gate_regression_gate -->|depends-on| n_gate_aeb_safety_regression
  n_gate_regression_gate -->|depends-on| n_gate_anticipation_driving_regression
  n_gate_odr_conformance_quick -->|depends-on| n_gate_fork_census
  n_gate_odr_conformance_quick -->|depends-on| n_gate_fork_drift
  n_gate_odr_conformance_quick -->|depends-on| n_gate_resync_guards
  n_gate_odr_conformance_schema_ci -->|depends-on| n_gate_fork_census
  n_gate_fork_sync -->|complements| n_gate_fork_census
  n_gate_odr_conformance_full -->|supersedes| n_gate_odr_conformance_quick
  n_proposal_P17 -->|depends-on| n_feature_F7
  n_proposal_P18 -->|depends-on| n_feature_F7
  n_feature_F7 -->|complements| n_vd_func_FUNC_075
  n_feature_F7 -->|sustained-by| n_gate_unit_ctest
  n_vd_func_FUNC_056 -->|realizes| n_req_vd_ad_REQ_AD_023
  n_vd_func_FUNC_056 -->|realizes| n_req_vd_ad_REQ_AD_024
  n_vd_component_overtake_maneuver -->|realizes| n_vd_func_FUNC_056
  n_vd_component_overtake_maneuver -->|depends-on| n_vd_component_lane_change_initiation
  n_vd_component_overtake_maneuver -->|realizes| n_vd_func_FUNC_061
  n_matcher_overtake_decision_holds -->|observes| n_signal_overtake_decision
  n_matcher_overtake_decision_holds -->|verifies| n_req_vd_ad_REQ_AD_023
  n_matcher_overtake_decision_holds -->|verifies| n_req_vd_ad_REQ_AD_024
  n_matcher_indicator_leads_lane_change -->|verifies| n_req_vd_ad_REQ_AD_023
  n_req_vd_ad_REQ_AD_024 -->|depends-on| n_req_vd_ad_REQ_AD_017
  n_req_vd_ad_REQ_AD_025 -->|stimulated-by| n_policy_aeb
  n_req_vd_ad_REQ_AD_028 -->|stimulated-by| n_policy_aeb
  n_matcher_manual_aeb_fires -->|observes| n_signal_manualdrive_adas_states
  n_matcher_no_intervention_in_window -->|observes| n_signal_manualdrive_adas_states
  n_matcher_brake_not_stacked -->|observes| n_signal_manualdrive_adas_states
  n_matcher_fcw_leads_intervention -->|observes| n_signal_manualdrive_adas_states
  n_matcher_adas_state_matches -->|observes| n_signal_manualdrive_adas_states
  n_matcher_manual_aeb_fires -->|verifies| n_req_vd_ad_REQ_AD_025
  n_matcher_no_intervention_in_window -->|verifies| n_req_vd_ad_REQ_AD_025
  n_matcher_brake_not_stacked -->|verifies| n_req_vd_ad_REQ_AD_025
  n_matcher_fcw_leads_intervention -->|verifies| n_req_vd_ad_REQ_AD_025
  n_matcher_adas_state_matches -->|verifies| n_req_vd_ad_REQ_AD_025
  n_matcher_adas_state_matches -->|verifies| n_req_vd_ad_REQ_AD_028
  n_matcher_driver_override_reported -->|observes| n_signal_manualdrive_driver_override
  n_matcher_driver_override_reported -->|verifies| n_req_vd_ad_REQ_AD_028
  n_matcher_setting_reflected -->|observes| n_signal_manualdrive_acc_settings
  n_matcher_adas_state_sequence -->|observes| n_signal_manualdrive_adas_states
  n_matcher_speed_capped_at -->|observes| n_signal_manualdrive_msl_cap
  n_matcher_no_brake_output -->|observes| n_signal_manualdrive_msl_cap
  n_matcher_stop_hold_stationary -->|observes| n_signal_manualdrive_acc_settings
  n_matcher_setting_reflected -->|verifies| n_req_vd_ad_REQ_AD_026
  n_matcher_adas_state_sequence -->|verifies| n_req_vd_ad_REQ_AD_026
  n_matcher_speed_capped_at -->|verifies| n_req_vd_ad_REQ_AD_026
  n_matcher_speed_capped_at -->|verifies| n_req_vd_ad_REQ_AD_030
  n_matcher_no_brake_output -->|verifies| n_req_vd_ad_REQ_AD_030
  n_matcher_stop_hold_stationary -->|verifies| n_req_vd_ad_REQ_AD_031
  n_matcher_restart_after_trigger -->|verifies| n_req_vd_ad_REQ_AD_031
  n_matcher_driver_override_reported -->|verifies| n_req_vd_ad_REQ_AD_026
  n_req_vd_ad_REQ_AD_026 -->|stimulated-by| n_policy_lead
  n_req_vd_ad_REQ_AD_031 -->|stimulated-by| n_policy_stop_yield
  n_vd_func_FUNC_079 -->|sustained-by| n_gate_unit_ctest
  n_vd_func_FUNC_081 -->|sustained-by| n_gate_unit_ctest
  n_vd_func_FUNC_075 -->|sustained-by| n_gate_unit_ctest
  n_matcher_lane_kept_within -->|observes| n_signal_manualdrive_lka_lateral
  n_matcher_steer_output_absent -->|observes| n_signal_manualdrive_lka_lateral
  n_matcher_lane_kept_within -->|verifies| n_req_vd_ad_REQ_AD_027
  n_matcher_steer_output_absent -->|verifies| n_req_vd_ad_REQ_AD_027
  n_vd_component_lane_keep_assist -->|realizes| n_vd_func_FUNC_080
  n_vd_func_FUNC_080 -->|sustained-by| n_gate_unit_ctest
```

## 辺の一覧（type別）

### complements (3)

| from | to | note |
| :--- | :--- | :--- |
| `proposal:P11` | `feature:F2` | 安全マージン評価に直結・F2と並走前提 |
| `gate:fork-sync` | `gate:fork-census` | INBOUND（上流未取込）と OUTBOUND（フォーク会計）の対。どちらもLHTの*正しさ*は見ない |
| `feature:F7` | `vd-func:FUNC-075` | 運転主体遷移の隣接スコープ・非重複（F7=切替そのもの、FUNC-075=手動運転中のADAS並行稼働）。混同防止で明記 |

### concerns (68)

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
| `req-vd-ad:REQ-AD-019` | `openx:Domain#Parking` | 駐車枠の探索・選定が対象とするODD軸。scene:SCN-014 と同じ openx 概念を共有 |
| `req-vd-ad:REQ-AD-020` | `openx:Domain#Parking` | 駐車マヌーバ実行が対象とするODD軸。scene:SCN-014 と同じ openx 概念を共有 |
| `req-vd-ad:REQ-AD-020` | `openx:Domain#MoveBackward` | バック駐車（後退を含むマヌーバ）が対象とするODD軸 |
| `req-vd-ad:REQ-AD-022` | `openx:Domain#Parking` | 出庫マヌーバの実行が対象とするODD軸。scene:SCN-014・REQ-AD-019/020 と同じ openx 概念を共有 |
| `req-vd-ad:REQ-AD-022` | `openx:Domain#MoveBackward` | 前進入庫状態からの後退出庫（段b）が対象とするODD軸 |
| `req-vd-ad:REQ-AD-026` | `openx:Domain#FollowRoadUser` | 手動運転中 ACC の先行車追従が対象とする ODD 軸。REQ-AD-002（AD 文脈）と同じ openx 概念を運転主体違いで共有 |
| `req-vd-ad:REQ-AD-027` | `openx:Domain#KeepLane` | 手動運転中 LKA の車線維持補正が対象とする ODD 軸 |
| `req-vd-ad:REQ-AD-002` | `openx:Domain#FollowRoadUser` | 先行車追従のODD軸 |

### depends-on (26)

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
| `req-vd-ad:REQ-AD-010` | `proposal:P12` | 衝突検出(collision-free)が検証前提 |
| `req-vd-ad:REQ-AD-011` | `proposal:P11` | 必要減速度/衝突速度低減メトリクスが検証前提 |
| `req-vd-ad:REQ-AD-011` | `proposal:P12` | 衝突検出が検証前提 |
| `req-vd-ad:REQ-AD-012` | `proposal:P12` | 衝突検出が検証前提 |
| `req-vd-ad:REQ-AD-013` | `proposal:P11` | 誤作動ゼロ判定に緊急制動発火メトリクスが必要 |
| `gate:regression-gate` | `gate:unit-ctest` | Step 1（ハード） |
| `gate:regression-gate` | `gate:odr-conformance-quick` | Step 1.5（ハード、-SkipOdr で除外可） |
| `gate:regression-gate` | `gate:vd-behavior-regression` | Step 2（既定 WARN、-FailOnBehavioral でハード化） |
| `gate:regression-gate` | `gate:aeb-safety-regression` | Step 2.6（既定 WARN、-FailOnBehavioral でハード化、-SkipAeb で単独スキップ）。 Step 2 と同じ recipe（共有関数 Invoke-BehavioralBatch）を別マニフェスト・別ベースラインで回す。 |
| `gate:regression-gate` | `gate:anticipation-driving-regression` | Step 2.7（既定 WARN、-FailOnBehavioral でハード化、-SkipAnticipation で単独スキップ）。 Step 2/2.6 と同じ共有関数 Invoke-BehavioralBatch を別マニフェスト・別ベースラインで回す。 |
| `gate:odr-conformance-quick` | `gate:fork-census` | :1556-1558 はプロファイル分岐より前で無条件＝CI の schema-only 起動でも走る。 census/drift/resync-guards が「独立スクリプト」ではなく適合ハーネスに内包された 常設ゲートであることは、名前からは読めない事実。 |
| `gate:odr-conformance-quick` | `gate:fork-drift` |  |
| `gate:odr-conformance-quick` | `gate:resync-guards` |  |
| `gate:odr-conformance-schema-ci` | `gate:fork-census` | schema層のみの CI 起動でも census は走る（同上） |
| `proposal:P17` | `feature:F7` | TORトリガ実験（DiL束）はAD⇄手動切替の実基盤が前提。F7がその土台を提供 |
| `proposal:P18` | `feature:F7` | 被験者応答テレメトリの反応時間計測は切替イベント（manual/auto_transition エッジ）が基準点 |
| `vd-component:overtake-maneuver` | `vd-component:lane-change-initiation` | 追い越しは車線変更の「動機」を1つ足す層であり、ホップ機構 （ArmLaneChangeHop / ArmResumeMerge / ScanAdjacentLaneGap / EvaluateGapAcceptance / RequiredLaneChangeDistance / ShouldSignalLaneChangeHop）を丸ごと借りている。 ただし**enable フラグは独立**（overtake_enabled と lane_change_initiation_enabled）。 同居させると「経路追従の車線変更だけ欲しい」利用者が追い越しまで有効化してしまうため。 安全弁が ShouldAttemptLaneChangeHop / ShouldSignalLaneChangeHop を呼ぶときだけ cfg.enabled=true に上書きしたコピーを渡す（両関数が enabled を内部で見るため）。 |
| `req-vd-ad:REQ-AD-024` | `req-vd-ad:REQ-AD-017` | 経路ガードは RouteLanePlan が出す target_lanes / dist_to_connection にそのまま依存する。 **現在バンドの target_lanes には下流の要求が既に畳み込まれている**（BuildRouteLanePlan が 最終ウェイポイントから後ろ向きに伝播させるため）ので、追い越しガードは次のバンドを 先読みする必要が無い。REQ-AD-017 段 a/b が成立していなければこのガードは成立しない。 |

### merged-into (3)

| from | to | note |
| :--- | :--- | :--- |
| `proposal:P24` | `proposal:P15` | 同一提案としてP15に一本化 |
| `proposal:P39` | `proposal:P13` | ODDカバレッジ台帳部分はP13と統合が前提（log2xosc由来meta拡張は残件） |
| `proposal:P8` | `proposal:P2` | 配信部が同一のためP2に吸収 |

### observes (37)

| from | to | note |
| :--- | :--- | :--- |
| `matcher:speed_above` | `signal:ego_speed` | frames[i]["ego"]["speed"]（面2テレメトリ投影経由） |
| `matcher:speed_below` | `signal:ego_speed` | 同上（speed_above と同一分岐） |
| `matcher:lane_keep` | `signal:ego_lane` | ★2026-07-24 結線（c22aeb5d lane 面1化）: _ego_state が is_host の assigned_lane_id （classification 優先・field4 fallback）を scene.lane_map で解決（scene 優先・ telemetry fallback・使用面は verdict の ego_source に記録）。 |
| `matcher:lane_change_count` | `signal:ego_lane` | 同上（lane_keep と同じ _ego_state 経由の判定車線）。 |
| `matcher:min_speed_above` | `signal:ego_speed` | 同上 |
| `matcher:speed_reduction_before_landmark` | `signal:ego_speed` | ego.speed ＋ ego.s（ランドマーク手前の減速率） |
| `matcher:speed_reduction_before_landmark` | `signal:ego_pose` | ego.s（走行距離）で窓を切る |
| `matcher:deceleration_profile_smooth` | `signal:ego_speed` | **本来は signal:ego_accel_long を観測すべき matcher**。OSI に加速度が emit 済み (GT_OSIReporter_Moving.cpp:772-774) なのに speed の中心差分で自前推定している (vd_metrics.py:253-259)。この辺を ego_accel_long に付け替えるのが §2.3a の 「配線差し替えだけで面1経由に移せる最有力候補」。 |
| `matcher:stopped_at_stop_sign` | `signal:ego_speed` | _sustained_stop（速度が閾値未満の継続時間） |
| `matcher:stopped_at_signal` | `signal:ego_speed` | 主判定は面2テレメトリの速度（vd_metrics.py:565-595） |
| `matcher:stopped_at_signal` | `signal:traffic_light_state` | require_red サブチェックのみ OSI GroundTruth 経由（scene.traffic_lights[].color） ＝14 matcher 唯一の混在型。`--osi`/batch `osi: true` が無いと scene=None で skip。 |
| `matcher:stopped_at_stop_sign` | `signal:traffic_sign_classification` | require_sign サブチェック（2026-07-21 新設）。`_gt_to_scene` が静的GTの traffic_sign を scene に載せるようになったため引けた辺（従来は「OSI が出しているのに scene 変換層で せき止められる」(b)の代表例）。分類 stop/give_way を positive にのみ使い、esmini が カタログ未分類を "unmapped:" 番兵で返すため否定側では判定しない。ゲート実測で stop_sign_full_stop / semantic_stop_sign_full_stop（P4 意味論 fallback 経路）の 両方が "stop sign confirmed in scene" を出すことを確認済み。 |
| `matcher:stopped_at_signal` | `signal:traffic_light_assigned_lane` | require_red サブチェックの多灯交差点向け絞り込み（2026-07-21 新設）。must の `lane_id` 指定時のみ classification.assigned_lane_id で信号頭を選ぶ。既存 expectations は lane_id を持たないため現行ゲートの判定は不変（deviation 0 で実証済み）。 |
| `matcher:impact_speed_below` | `signal:pedestrian_velocity_vector` | _closing_speed が scene.objects[].vx,vy（OSI 速度ベクトル）を第一候補として読む （2026-07-21）。従来は速度スカラー＋heading から再構成しており、**自機の heading と 逆向きに動く物体（後退車・車道から後ずさる歩行者）の符号を落としていた**。 heading 再構成は旧テレメトリ向けの後方互換 fallback として残置。 |
| `matcher:maintained_following_distance` | `signal:object_poses` | scene.objects[]（正規IF＝面1 OSI 直結の4件のうち1件） |
| `matcher:maintained_following_distance` | `signal:ego_speed` | THW = gap / speed の分母 |
| `matcher:min_obb_separation_above` | `signal:obb_separation` | SAT による OBB 分離（隣接レーン通過での誤検出を避けるため中心間距離から移行） |
| `matcher:impact_speed_below` | `signal:obb_separation` | OBB 接触判定と接近速度の組み合わせ |
| `matcher:impact_speed_below` | `signal:object_poses` | 相手の位置・方位・速度 |
| `matcher:no_emergency_without_conflict` | `signal:aeb_trigger_flag` | policy.constraints[].source == "aeb"（負matcher＝誤作動ゼロ） |
| `matcher:route_lane_plan_holds` | `signal:route_lane_conformance` | vd_metrics.py:1520-1677 の route_lane_plan_holds 分岐が frames[i]["route_lane"] （VirtualDriverTelemetryJson.cpp 由来の route_lane ブロック）を読む。target_lanes/ on_target_lane/dist_to_connection/deviation_count/diagnostic の各チェックは呼び出し側の must が指定したものだけ評価する（何もチェックしない must は skip 扱い＝「何も評価しない ものを pass にしない」規律、domain_split_holds と同型）。 |
| `matcher:indicator_leads_lane_change` | `signal:lane_change_signal_timing` | vd_metrics.py:1708-1836 の indicator_leads_lane_change 分岐が、frames[i]["lane_change"] の signal_active/armed/direction と frames[i]["indicator"] の left/right を**両方**読む。 片方だけでは判定にならない（lane_change_initiation.md §11-8）: signal_active だけだと 意図が AutoIndicatorPolicy に握り潰されていても真になり、indicator だけだと DetectJunctionTurn の交差点旋回による点灯と区別できない。両ブロックとも欠けていれば skip（「何も評価しないものを pass にしない」規律、route_lane_plan_holds と同型）。 |
| `matcher:indicator_leads_junction_turn` | `signal:junction_turn_signal_distance` | vd_metrics.py:1838- の indicator_leads_junction_turn 分岐が、frames[i]["junction_turn"] の dir/dist_to_entry_m/on_connector と frames[i]["indicator"] の left/right を**両方**読む。 **on_connector が無いと成立しない**: テレメトリ単体では road id から junction 所属を 判定できず、検証ハーネスは xodr を読めないため、「交差点に進入したフレーム」を 機械判定する手段が他に無い（junction_turn_signal.md §3-4）。 距離は ego.x/y のフレーム間ユークリッド距離の積算で測る（domain_split_holds と同型。 speed×dt の積分より頑健で、バッチ末尾の同一 sim_time 重複フレームにも耐える）。 どちらのブロックも欠けていれば skip（「何も評価しないものを pass にしない」規律）。 |
| `matcher:overtake_decision_holds` | `signal:overtake_decision` | vd_metrics.py の overtake_decision_holds 分岐が frames[i]["overtake"] を読む。 must で指定されたキー（expect_considered / expect_blocked_reason / expect_phases / forbid_phases / expect_cleared_lead）だけを評価し、**何も指定しない must は skip** （route_lane_plan_holds / indicator_leads_lane_change と同じ「何も評価しないものを pass にしない」規律）。overtake ブロックを持つフレームが窓に1つも無ければ skip＝ 古い DLL の検出。 |
| `matcher:manual_aeb_fires` | `signal:manualdrive_adas_states` | 窓内でfunctionのstate_nameが一度でもactiveになるかをframe["hvd"]["adas"][function]から判定 |
| `matcher:no_intervention_in_window` | `signal:manualdrive_adas_states` | 窓内でactiveが一度も無いことに加え、reported かつ not-unavailable（＝正しく STANDBYで見張っていた）であることも要求する（REQ-AD-028のSTANDBY/UNAVAILABLE区別に 判定が依存する、vd_metrics.py:1646-1649のコメント）。 |
| `matcher:brake_not_stacked` | `signal:manualdrive_adas_states` | active区間のcustom_detail（driver_brake/brake_request/brake_outキー）を読み、max合成かを判定 |
| `matcher:fcw_leads_intervention` | `signal:manualdrive_adas_states` | warning_function/intervention_functionの2行を読み、最初のactive遷移の時刻差をリードとする |
| `matcher:adas_state_matches` | `signal:manualdrive_adas_states` | functionのstate_name列が窓内でexpectと一致するか（modeパラメータでall/any切替）を判定する汎用matcher |
| `matcher:driver_override_reported` | `signal:manualdrive_driver_override` | frame["hvd"]["adas"][function] の driver_override{present,active,reasons} と custom_state を読み、指定窓で期待どおりの上書きが出ている（または出ていない）かを判定する。 present（＝OSI submessage の有無）を読むのが肝で、正方向は present 不要（行が出ている 時点で計器は生きているので submessage 欠如は真の負観測＝fail）、負方向は present を 必須にする（誰も書かなかったチャネルは「上書きしなかった」証拠にならない）。 |
| `matcher:setting_reflected` | `signal:manualdrive_acc_settings` | 設定キー（gt.acc.set_speed_mps / gt.acc.thw_setting_s）と実効キー （gt.acc.effective_cap_mps / gt.acc.thw_actual_s）を時系列として読み、設定の段差が settle_s 以内に実効値の**同方向の**移動として現れるかを見る。等値でなく方向を見るのは、 実効値が min(設定, policy天井, 制限速度) の合成であり、新しい設定に届かないことが 正当にありうるため。**窓内で設定変更が1回も起きなければ pass ではなく fail** を返す （操作列が壊れた run と、設定を保存して適用しない実装が同じ定数列を作るため）。 |
| `matcher:adas_state_sequence` | `signal:manualdrive_adas_states` | state_name 列をラン長圧縮したうえで expect の**部分列**一致を見る。順序だけを主張し 滞在時間は主張しない（20Hz で数十フレーム続く状態に厳密一致を課すと、誰も書いていない dwell time の assertion になる）。空虚化を防ぐため expect が2要素未満、または隣接重複を 含む場合は skip を返す。 |
| `matcher:speed_capped_at` | `signal:manualdrive_msl_cap` | cap_key（gt.msl.cap_mps）で毎フレームのキャップを読み、自車速がそれ＋許容差を超えない ことを見る。キャップをリテラルで書かず signal から読むのは、制限速度連動モードでは キャップ自体が道路に沿って動くため。未記録のキャップは 0 と読まず frame ごと skip する。 |
| `matcher:no_brake_output` | `signal:manualdrive_msl_cap` | gt.msl.brake_out を読み、窓内で恒等的に 0 であることを見る。**車両の実効ブレーキでは なく機能自身の寄与を読む**のが肝で、人間が踏んだブレーキは主張と無関係に非零になりうる。 チャネルが一度も書かれていなければ skip（誰も書かなかったチャネルは「出さなかった」証拠に ならない）。 |
| `matcher:stop_hold_stationary` | `signal:manualdrive_acc_settings` | gt.acc.stop_hold が真の**連続区間ごとに**変位を測る。区間で分けるのは、1回の run で 停止→再発進→再停止が起きうるため（区間をまたいで測ると再発進の移動をクリープとして 数える）。速度だけを見ないのは「動かなかった」が「誰も保持していない車」にも当てはまる ため——保持中であることを機能自身のフラグで固定して初めて保持の主張になる。 |
| `matcher:lane_kept_within` | `signal:manualdrive_lka_lateral` | `gt.lka.offset_m` と `gt.lka.lane_id` を**対で**読む。偏差だけを読まないのが設計の肝で、 この偏差はレーン相対であり roadmanager の Position::GetOffset() はレーン境界で参照を 張り替える（本プロジェクト実測 -1.7482 → +1.9425 が1フレーム）。つまり逸脱した run ほど |offset| は小さく戻り、偏差だけの matcher は**車線を出た run にこそ最もきれいな pass を 返す**。レーンID不変を併せて要求することでこの穴を塞ぎ、同時に `expect_kept: false` （両極性ペアの逸脱側）に観測できる中身を与えている。 |
| `matcher:steer_output_absent` | `signal:manualdrive_lka_lateral` | `gt.lka.correction`（**機能自身の寄与**）を読み、窓内で恒等的に 0 であることを見る。 車両の実効操舵ではないのが肝で、この matcher が使われる場面——明確な操舵入力・指示器つき 車線変更・速度域外——はいずれも**人間が意図的にハンドルを切っている**。実効操舵を見る matcher は、それを証明したい当の run で必ず赤くなる（no_brake_output が実効ブレーキでなく gt.msl.brake_out を読むのと同型）。チャネルが一度も書かれていなければ skip。 |

### realizes (48)

| from | to | note |
| :--- | :--- | :--- |
| `policy:lead` | `vd-func:FUNC-013` | ACC定常追従(快適)=LeadVehicleAware(IDM) |
| `policy:traffic_light` | `vd-func:FUNC-023` | 信号遵守(法規) |
| `policy:stop_yield` | `vd-func:FUNC-024` | 一時停止標識遵守(法規) |
| `policy:stop_yield` | `vd-func:FUNC-025` | 譲れ標識遵守(法規+譲り合い・部分) |
| `policy:crosswalk` | `vd-func:FUNC-027` | 横断歩道の歩行者優先(法規) |
| `policy:crosswalk` | `vd-func:FUNC-037` | 歩行者信号遵守(法規・部分, signal-aware) |
| `policy:crosswalk` | `vd-func:FUNC-041` | 待ち歩行者への譲り(譲り合い) |
| `policy:conflict` | `vd-func:FUNC-038` | 非制御交差点の譲り(譲り合い) |
| `policy:junction_priority` | `vd-func:FUNC-029` | 交差点優先権規則(法規・部分) |
| `policy:aeb` | `vd-func:FUNC-001` | 前方AEB(安全・ADAS)=AebSafety。早期横侵入検知+TTC/a_reqゲート+SAFETY-tier STOP_AT_S(emergency_decel)。フェーズ1実装辺(issue |
| `vd-func:FUNC-001` | `req-vd-ad:REQ-AD-001` | 前方AEB(安全・未実装)がカットイン衝突回避要求を充足 |
| `vd-func:FUNC-013` | `req-vd-ad:REQ-AD-002` | ACC定常追従(快適)が車間維持要求を充足 |
| `vd-func:FUNC-023` | `req-vd-ad:REQ-AD-003` | 信号遵守が赤信号停止要求を充足 |
| `vd-func:FUNC-024` | `req-vd-ad:REQ-AD-004` | 一時停止標識遵守が停止要求を充足 |
| `vd-func:FUNC-027` | `req-vd-ad:REQ-AD-005` | 横断歩道の歩行者優先が歩行者衝突回避要求を充足 |
| `vd-func:FUNC-038` | `req-vd-ad:REQ-AD-006` | 非制御交差点の譲りが交差点譲り要求を充足 |
| `vd-func:FUNC-029` | `req-vd-ad:REQ-AD-006` | 交差点優先権規則が交差点譲り要求を充足 |
| `vd-func:FUNC-049` | `req-vd-ad:REQ-AD-016` | 目的地ルーティング（road/lane 列の導出と保持）が経路導出要求の中核 |
| `vd-func:FUNC-050` | `req-vd-ad:REQ-AD-017` | レーンレベル経路計画が段 a/b（目標レーンの算出と逸脱検出）を充足 |
| `vd-func:FUNC-055` | `req-vd-ad:REQ-AD-017` | 自発的な車線変更の発起が段 c（接続点までに目標レーンへ移る）を充足。 2026-08-02 実装済み（vd-component:lane-change-initiation、既定 OFF） |
| `vd-func:FUNC-052` | `req-vd-ad:REQ-AD-017` | ルート維持・逸脱復帰が段 d を充足。未実装 |
| `vd-func:FUNC-054` | `req-vd-ad:REQ-AD-017` | 到達判定・ミッション終了が段 e（終点で安全に停車する）を充足。未実装 |
| `vd-component:route-lane-plan` | `vd-func:FUNC-050` | 目標レーン帯の算出と逸脱・ルート解決失敗の可視化。FUNC-050 の実現範囲は診断までで、 寄せる動作（自発的な車線変更）は vd-func:FUNC-055 のスコープ |
| `vd-component:lane-change-initiation` | `vd-func:FUNC-055` | route-lane-plan の目標レーン帯へ自発的に寄せる実装。決断距離（残ホップ数比例）・ ギャップ受容（隣接レーンの前後車）・軌道（ResumeMergeProfile 流用、アンカーを 目標レーンへ）・優先順位（storyboard LC > resume-merge > AD発起）を担う。 FUNC-055 のうち**経路要求を動機とする発起のみ**を実現し、遅い先行車・専用レーン 回避を動機とする発起は範囲外（追い越しは vd-func:FUNC-056） |
| `vd-component:lane-change-initiation` | `vd-func:FUNC-061` | 発起した車線変更に方向指示器を同期させる（DetectManeuverDir を storyboard LC → AD発起LC → **先行合図** → 0 の4段へ拡張し、発起時に方向をラッチ）。2026-08-03 に 先行合図の判定を前進予測形へ置き換えて REQ-AD-018 段 b/c を達成（§11-11）。 FUNC-055 分はこれで埋まり、**2026-08-04 に vd-func:FUNC-056（追い越し）分も埋まった** （vd-component:overtake-maneuver。往路・復路の2回とも発起前から点灯、実測 2.95 / 3.00 s。 ただしそちらは前進予測形ではなく**合図→ドウェル T 秒→発起のタイマ形**で、 追い越しには締切が無いので待てばよいという別機構）。**残るは出庫・非経路の合流** （旧記載の「FUNC-056..059」は括りが誤っていた。REQ-AD-018 の acceptance 参照）。 |
| `vd-func:FUNC-061` | `req-vd-ad:REQ-AD-021` | 同じ「方向指示器の自発操作」が右左折側も担う。ただし**法定の次元が違う**（進路変更＝ 3秒前 / 右左折＝交差点手前の側端から 30 m 手前）ため要求は REQ-AD-018 と分けてある。 実装経路も別で、こちらは DetectJunctionTurn（ControllerVirtualDriver.cpp:1897-1905）が `lookahead = max(15.0, v*indicator_lead_time + 10.0)` で前方を走査する側。 2026-08-03 の指摘: `2v+10 >= 30` すなわち v >= 10 m/s (36 km/h) でしか法定 30 m に 届かない。交差点の実運用域はそれ以下なので低速側で不足する（コード読解＋算術。実測は未）。 |
| `vd-func:FUNC-061` | `req-vd-ad:REQ-AD-018` | 方向指示器の自発操作が段 a/b/c を充足（2026-08-03、§11-11 の前進予測形で段 b/c 達成）。 **段 d は未達**。ただし 2026-08-03 に範囲を切り直しており、以前の 「FUNC-056..059 由来の発起にも同期」は誤った括りだった: FUNC-057（交差点）は右左折であって車線変更でなく req-vd-ad:REQ-AD-021 へ分離、 FUNC-058（発進）は dim:lon の Stop&Go 出口で指示器不要（対象は出庫＝REQ-AD-022）、 FUNC-059（合流）は経路要求として現れる分は既に段 a-c の機構で合図が出る。 段 d に残る実体は vd-func:FUNC-056（追い越し）・出庫・非経路の合流の3つ。 |
| `vd-func:FUNC-076` | `req-vd-ad:REQ-AD-019` | 駐車枠探索・選定が駐車枠の探索・選定要求を充足。未実装 |
| `vd-func:FUNC-077` | `req-vd-ad:REQ-AD-020` | 駐車マヌーバ実行が駐車マヌーバの実行要求を充足。未実装 |
| `vd-func:FUNC-078` | `req-vd-ad:REQ-AD-022` | 出庫マヌーバ実行が出庫マヌーバの実行要求を充足。未実装 |
| `vd-func:FUNC-075` | `req-vd-ad:REQ-AD-025` | 手動運転中の AEB 並行監視・介入（実車型上書きを含む）を充足。未実装 |
| `vd-func:FUNC-075` | `req-vd-ad:REQ-AD-028` | ADAS 状態・DriverOverride の HVD 報告基盤（横断基盤側）が観測性要求を充足。signal:manualdrive_adas_states の (a) を塞ぐ宛先。未実装 |
| `vd-func:FUNC-079` | `req-vd-ad:REQ-AD-026` | 手動運転中の ACC（実車標準操作系）を充足。未実装 |
| `vd-func:FUNC-080` | `req-vd-ad:REQ-AD-027` | 手動運転中の LKA（人間操舵優先）を充足。未実装 |
| `vd-func:FUNC-075` | `req-vd-ad:REQ-AD-029` | HVD 報告基盤＋警報提示チャネル（横断基盤側）が運転者向け HMI 提示要求を充足。表示実体は web フロント（HvdGaugePanel 既存配線の描画拡張）。未実装 |
| `vd-func:FUNC-081` | `req-vd-ad:REQ-AD-030` | 手動運転中の速度リミッター（AEB envelope の縮退実装）を充足。未実装 |
| `vd-func:FUNC-079` | `req-vd-ad:REQ-AD-031` | 手動 ACC の Stop&Go（段a/b=先行車・信号/一時停止への停止と人間トリガ再発進がフェーズCスコープ。段c/d 自動再発進は将来段、段d は vd-func:FUNC-058 依存）。未実装 |
| `vd-func:FUNC-001` | `req-vd-ad:REQ-AD-010` | 前方AEB→停止先行車回避(CCRs) |
| `vd-func:FUNC-001` | `req-vd-ad:REQ-AD-011` | 前方AEB→等速/制動先行車回避(CCRm/CCRb) |
| `vd-func:FUNC-002` | `req-vd-ad:REQ-AD-012` | VRU-AEB→横断歩行者/自転車回避(CPNA/CPFA/CBNA) |
| `vd-func:FUNC-001` | `req-vd-ad:REQ-AD-013` | 前方AEB→誤作動抑止(negative, R152) |
| `vd-func:FUNC-001` | `req-vd-ad:REQ-AD-014` | 前方AEB→快適優先の層調停(arbitration) |
| `vd-func:FUNC-001` | `req-vd-ad:REQ-AD-015` | 前方AEB→作動包絡線/応答フロア(regulatory, R152) |
| `vd-func:FUNC-056` | `req-vd-ad:REQ-AD-023` | 追い越しの発起・完遂が「遅い先行車を追い越して巡航速度へ戻る」を充足。2026-08-04 実装 （vd-component:overtake-maneuver、既定 OFF）。段 a-e まで実測で達成、 段 f（追い越し禁止区間）と段 g（見通し距離）は未達で、それぞれ vd-func:FUNC-030 待ちと 視距モデル不在による。**この辺は要求全体の充足を主張しない** — どの段まで達成したかは 要求側の acceptance_ladder[].verified_by が持つ。 |
| `vd-func:FUNC-056` | `req-vd-ad:REQ-AD-024` | 同じ実装が「追い越しによって経路上の分岐を逃さない」も担う。要求を2本に割ったのは 要素技術が別（先行車認識＋ギャップ受容 / 経路計画との突き合わせ）で片方だけ完成しうるため。 経路ガードは追い越しレーンから目標レーン帯までのホップ数 n_back を RequiredLaneChangeDistance へ渡す形で、**新しい距離の概念を持ち込んでいない**。 段 a-c は実測で達成、段 d（復路の隙間が空かない場合）は刺激が未整備で未観測。 |
| `vd-component:overtake-maneuver` | `vd-func:FUNC-056` | 追い越しの純関数層（trigger / 経路ガード / 対向ギャップ / 追い越しレーン選択 / クリア判定 / 合図ドウェル）と、その上のフェーズ状態機械。 **vd-component:lane-change-initiation の1ホップ機構を無改造で2回まわす**構成で、 軌道生成（ResumeMergeProfile）にも指示器経路（lc_signal_dir_）にも新しい経路を作っていない。 DetectManeuverDir への変更は enable ゲート1行のみ。 |
| `vd-component:overtake-maneuver` | `vd-func:FUNC-061` | 追い越しの往路・復路それぞれに方向指示器を同期させる（req-vd-ad:REQ-AD-018 段 d の **追い越しぶん**）。既存の lc_signal_dir_ に同じ契約で書き込むだけで、 DetectManeuverDir の段構造は変えていない。 **FUNC-055 の前進予測形は流用していない** — あれは dist_to_connection と n_remaining を 引数に取る締切専用の述語で、追い越しには対応する量が存在しない。 追い越しには締切が無いので「合図してから T 秒待って発起する」タイマ形で足り、 法定リードは近似ではなく設計上ちょうど T になる（実測 往路 2.95 s / 復路 3.00 s）。 |
| `vd-component:lane-keep-assist` | `vd-func:FUNC-080` | レーン中心偏差→補正操舵の本体。**FUNC-080 は既存部品からの流用がゼロだった唯一の 手動運転中ADAS機能**で（設計 §5-1 の調査どおり、PIDPurePursuit はトラジェクトリ preview 点列への追従でレーン中心を追わない）、AEB/ACC/Stop&Go が ITrafficPolicy 実装を 無改修で載せ替えるだけだったのに対し、こちらは判定も出力も新規に書いた。 実現範囲は補正と警報の判定・出力まで。FFB 反力として実ハンドルに伝わるか（段d）は このユニットの外＝IFFBSink 側であり、headless では代替できない G29 実機限定観点。 |

### shares-design-with (2)

| from | to | note |
| :--- | :--- | :--- |
| `proposal:P5` | `proposal:P40` | ReplayInputSource機構の一本化必須 |
| `proposal:P1` | `proposal:P23` | 外部制御注入の二重資産回避のため設計共有必須 |

### stimulated-by (14)

| from | to | note |
| :--- | :--- | :--- |
| `req-vd-ad:REQ-AD-001` | `policy:aeb` | cutin_hard_brake(_curve).xosc が policies:[lead,aeb]（:47,:50）で AebSafety を発火させ、 impact_speed_below(直進, 閾10.0)/min_obb_separation_above(カーブ, 閾0.5) で判定。 判定を担うのは lead ではなく aeb 側 matcher。 ※ requirements_vd_ad.yaml:68 の red_asset は policies:[lead] と記すが陳腐化（実バッチは [lead,aeb]）。 |
| `req-vd-ad:REQ-AD-013` | `policy:aeb` | 負例3本 normal_following/benign_cutin/parallel_overtake が policies:[lead,aeb]（:58,:61,:64）で AEB パイプラインの各段（ゲート非発火/候補拒否）を狙い撃ち、no_emergency_without_conflict で 誤発火ゼロを判定。正例(001)と同一バッチ・同一ベースライン（sustained-by:aeb-safety-regression 参照）。 |
| `req-vd-ad:REQ-AD-010` | `policy:aeb` | CCRs 7セル（aeb_c2c_grid/ccrs_ego10..70.xosc, policies:[lead,aeb], osi:true）＝停止先行車への パラメトリック接近（初期ギャップ=閉じ速度x6s）。判定は expectations でなく探索スイープ層の score_aeb_c2c_grid.py（obb_separation/_closing_speed を vd_metrics から流用）で帯分類。 2026-07-24 実測: 全セル avoid(no_aeb)＝AEB は armed だが不発火（min TTC 2.78s > 閾2.5s）、 回避は AD 層（max 3.9 m/s²）。aeb はバックストップとして毎フレーム gt.aeb.* で観測済み。 行列は GT_esmini/docs/virtualdriver/measurements/aeb_c2c_grid_matrix.md に固化。 |
| `req-vd-ad:REQ-AD-011` | `policy:aeb` | CCRm 5セル（ccrm_ego30..70_lead20.xosc）＋ CCRb 4セル（ccrb_hw{12,40}_d{2,6}.xosc, NCAP 車間12/40m・制動-2/-6 m/s²）、policies:[lead,aeb]。判定は score_aeb_c2c_grid.py の帯行列。 2026-07-24 実測: 全セル avoid(no_aeb)。ただし ccrb_hw12 系は max 8.6 m/s² の強制動での回避 ＝本物のニアミス（min TTC 2.83s、Claim B「IDM経路は快適天井を迂回」の実証セル）。 avoid/mitigate 境界は本グリッドの外側＝格子細分化/ギャップ短縮が後続課題。 |
| `req-vd-ad:REQ-AD-002` | `policy:lead` | 06_lead_vehicle/follow_steady.xosc が policies:[lead]（:59）。 maintained_following_distance（min_thw 1.0/max_thw 3.5/percentile 50）で快適車間維持を判定。 |
| `req-vd-ad:REQ-AD-003` | `policy:traffic_light` | 03_traffic_signals/red_stop_green_go.xosc が policies:[traffic_light]（:25）。 stopped_at_signal(require_red=true, signal_id:1/road_id:3) で停止線手前停止を判定。red_hold も同 policy で補強。 |
| `req-vd-ad:REQ-AD-004` | `policy:stop_yield` | 04_traffic_signs/stop_sign_full_stop.xosc が policies:[stop_yield]（:38）。 stopped_at_stop_sign(sign_id:10/road_id:1/min_duration 1.0) で完全停止を判定。 |
| `req-vd-ad:REQ-AD-005` | `scenario-variant:09_crosswalk_pedestrian__p005` | p005（crosswalk_pedestrian_batch.yaml:36-38, policies:[crosswalk]）が横断中歩行者へ発火。 min_obb_separation_above(閾0.3, 歩行者 footprint 0.6x0.5 実寸との分離)＋speed_below で衝突回避を判定。 登録済み scenario-variant を直指し＝face:3 で face-clean（policy:crosswalk 迂回を回避）。 |
| `req-vd-ad:REQ-AD-006` | `scenario-variant:07_oncoming_yield__p017` | p017（junction_conflict_batch.yaml:31-33, policies:[conflict]）＝Ego 左折が 14m/s 対向直進車を横切る ＝要求 title「対向直進車に譲る」に字句一致。min_obb_separation_above/speed_below で判定。 face:3 scenario-variant を直指し。 |
| `req-vd-ad:REQ-AD-006` | `scenario-variant:08_unsignalized_junction__p004` | p004_nonpriority_yield（junction_priority_batch.yaml:38-40, policies:[conflict,junction_priority]）＝ 非優先(MINOR)側が優先側に譲り STOP_AT_S で待つ＝要求 rationale「優先権を評価…非優先側は STOP_AT_S」 および realized_by FUNC-029(交差点優先権) に一致（直交交差車で title の「対向直進」とは別ファセット）。 title=p017／rationale=p004 の2ファセットを別辺で明示（要求が両面を包含）。 |
| `req-vd-ad:REQ-AD-025` | `policy:aeb` | 10_manualdrive_adas/ の5シナリオ（md_aeb_unresponsive / md_aeb_no_conflict / md_aeb_strong_brake_driver / md_aeb_kickdown_suppress / md_fcw_warning_only、 controller: manualdrive）が AdasCoexistenceStack 経由で AebSafety(policy:aeb) を 毎フレーム発火させる（design §2-1、ゼロ改修流用）。実測（test_results/mdadas_run1/ md_aeb_unresponsive/verdict.json）: gt.aeb が t=2.00 で ACTIVE、impact_speed 8.26 m/s まで低減（段a）。段b-eは同バッチの他4シナリオが刺激する。段d（キックダウン抑制）・ 段e（FCWリード）は閾値未校正・実測で design §3-2 の想定と食い違いが判明済み （design doc 訂正・下の verifies 辺の note を参照）。 |
| `req-vd-ad:REQ-AD-028` | `policy:aeb` | 要求自身の scenario 欄が明記するとおり「REQ-AD-025/026/027 の刺激と同一バッチで 判定面として使う」（専用刺激を持たない横断適用）。段a（正規Name列挙でのState報告）は matcher:adas_state_matches が同じ5シナリオの実行から判定する（下の verifies 辺）。 段c（ハーネスがHVDを読めること）は、このバッチが frames=0 エラー無く440フレームずつ 実行できたこと自体で実証済み（test_results/mdadas_run1/*/telemetry.jsonl 実測）。 **段b（DriverOverride）は 2026-08-05 のフェーズBで populate 機構が入り、同じバッチが 刺激になった**（md_aeb_kickdown_suppress のキックダウン保持窓＝正、md_aeb_unresponsive ＝負、同 kickdown 行の gt.fcw＝同一実行内の対照）。ただし刺激できるのは**アクセル経路 だけ**である: 段b の claim が挙げる3経路のうち、brake 理由の producer は ACC （フェーズC）、steering 理由の producer は LKA（フェーズD）にしか無く、本バッチには それらを発火させる資産が原理的に存在しない。 |
| `req-vd-ad:REQ-AD-026` | `policy:lead` | md_acc_follow_lead / md_acc_cancel_resume / md_acc_setting_changes / md_acc_speed_limit_cap（2構成）/ md_acc_speed_range_gate / md_acc_aeb_independence （controller: manualdrive、adas_acc_enabled: true）が AdasCoexistenceStack 経由で LeadVehicleAware(policy:lead) を毎フレーム発火させ、その constraint 列を EvaluateAccCeiling が現在位置1点の速度上限へ畳む（design §4-2、policy 本体はゼロ改修）。 手動運転中ADASの刺激は xosc だけでは決まらない: 入力プロファイル （profiles/acc_ops_*.json）が「運転者」を、xosc が「世界」を決め、両方の組で1観点が 発火する（検証計画§1）。実測 20/20 緑（test_results/mdadas_phasec）。 |
| `req-vd-ad:REQ-AD-031` | `policy:stop_yield` | md_sng_stop_sign を stop_at_stop_sign の true/false 2構成（variant）で回し、 StopYieldSignAware(policy:stop_yield) を手動スタックへ Add する/しないで両極性を作る。 **負側では policy がそもそも生成されない**ので、フィルタの緩和で漏れる余地が構造的に無い （design §4-3）。段a（先行車のみ）は policy:lead 側の同一バッチ行 md_sng_lead_stop_restart が発火させる。 この2構成は同時に design §12 の残確認事項——TrafficLightAware / StopYieldSignAware が コントローラ非依存か——の実走確認でもあった。ManualDrive の ego は Route を持たず、 両 policy は ego の Position から前方を歩いて標識を探す（RouteSignalScan の CopyRoute）ため、 「インタフェースに適合するか」ではなく「Route が無いとき歩行が何か見つけるか」が 未確認だった。実測で見つかる（201フレーム stop_requested、 GT_esmini/docs/virtualdriver/measurements/manualdrive_creep_stop_hold_2026-08-05.md）。 |

### supersedes (1)

| from | to | note |
| :--- | :--- | :--- |
| `gate:odr-conformance-full` | `gate:odr-conformance-quick` | full は quick の上位集合（+OSI層）だが**手動実行のみ**でどのラダーにも配線されていない。 capability_model.md §2.3 D9 が OSI層を (b) と採点している当の理由。 |

### sustained-by (23)

| from | to | note |
| :--- | :--- | :--- |
| `matcher:speed_above` | `gate:vd-behavior-regression` |  |
| `matcher:speed_below` | `gate:vd-behavior-regression` |  |
| `matcher:min_speed_above` | `gate:vd-behavior-regression` |  |
| `matcher:maintained_following_distance` | `gate:vd-behavior-regression` |  |
| `matcher:stopped_at_signal` | `gate:vd-behavior-regression` | OSC拡張パース（TrafficSignalController系）の唯一の常設検出経路でもある |
| `matcher:stopped_at_stop_sign` | `gate:vd-behavior-regression` |  |
| `matcher:impact_speed_below` | `gate:aeb-safety-regression` | cutin_hard_brake（直進）。完全回避不能域での緩和＝閉じ速度 <= 10 m/s |
| `matcher:min_obb_separation_above` | `gate:aeb-safety-regression` | cutin_hard_brake_curve の完全回避判定（0.5m）＋ benign_cutin のサニティ（20m）。 本 matcher は junction/crosswalk 系の手動マニフェストでも使われるが、 **常設で守られるのは AEB 経由のこの2用法のみ**。 |
| `matcher:no_emergency_without_conflict` | `gate:aeb-safety-regression` | 負例3件（normal_following / benign_cutin / parallel_overtake）の主判定 |
| `req-vd-ad:REQ-AD-001` | `gate:aeb-safety-regression` | 正例2シナリオ（直進=緩和 / カーブ=完全回避） |
| `req-vd-ad:REQ-AD-013` | `gate:aeb-safety-regression` | 負例3シナリオ（SOTIF ミラー）。正負を同一ゲート・同一ベースラインに置くのは、 片方だけを守ると「閾値を下げて正例を通し負例を壊す」取引が素通りするため。 |
| `matcher:deceleration_profile_smooth` | `gate:anticipation-driving-regression` | decelerate_for_curve / decelerate_for_left_turn / speed_limit_change の3シナリオ。 osi:true で **a=osi の面1直読加速度**（ego_accel_long ●）から bounded decel/jerk を判定。 |
| `matcher:speed_reduction_before_landmark` | `gate:anticipation-driving-regression` | 4シナリオ（curve/left_turn/speed_limit/traffic_lights）。ランドマーク手前で目標速度到達。 |
| `matcher:indicator_leads_junction_turn` | `gate:anticipation-driving-regression` | 3シナリオ（decelerate_for_left_turn / junction_turn_signal_long_connector / cross_straight_junction）。このバッチは run_regression_gate.ps1 Step 2.7 と CI の vd-behavioral-regression ジョブの**両方**で走るため、新しい gate を起こさずに ⑥常設を満たせる（junction_turn_signal.md §4-3）。 **req-vd-ad:REQ-AD-018 が sustained-by 未結線のまま放置されている状態を再生産しない** ために、matcher の新設と同じサイクルで結線した（あちらの route_lane_batch.yaml は baseline すら無く、ゲートにも CI にも載っていない孤立 matcher になっている）。 |
| `matcher:lane_keep` | `gate:anticipation-driving-regression` | curve / speed_limit / traffic_lights の3シナリオ。road_id / lane とも面1 lane_map (source_reference) 経由（★2026-07-24 更新: 旧「lane は telemetry＝assigned_lane_id は 別量のため」は 7baf202d の走行レーン由来化と c22aeb5d の lane 面1化で解消。 junction 内 171/15800 フレームのみ telemetry fallback）。 |
| `matcher:steer_not_saturated` | `gate:anticipation-driving-regression` | decelerate_for_left_turn / traffic_lights_junction。コーナーで操舵飽和なし（面2 driver.steer）。 |
| `matcher:no_constraint_kind` | `gate:anticipation-driving-regression` | cross_straight_junction。直進通過の接続路で junction 制約を上げない（面2 midlong.constraints）。 |
| `feature:F6` | `gate:integration-ctest` | F6 環境ヘッドライト 5本＋AutoLight/LightStateAction 6本の per-test アサーション（run_gt_tests.ps1 -IncludeIntegration、opt-in＝既定ゲート外）。 「ビューワー目視未」は残る（アサーションは灯火状態変化のみ）。 |
| `feature:F7` | `gate:unit-ctest` | OverrideManagerTest 10ケースが傘バイナリ常設（片方向ラッチ仕様の固定＋RESUMEエッジ復帰）。フルサイクルsmoke(scripts/vd_override_smoke.py)はCI未統合＝手動のため計上しない |
| `vd-func:FUNC-079` | `gate:unit-ctest` | test_AccLonController.cpp / test_ManualAdasPhaseC.cpp が傘バイナリ常設 （**731/731緑**、2026-08-05 フェーズC実測。フェーズB時点は673件）。 matcher 側の赤実証は GT_esmini/scripts/verification/test_manualdrive_matchers.py （フェーズCで6 matcher分を追加、計77テスト緑）。 |
| `vd-func:FUNC-081` | `gate:unit-ctest` | MSL（SpeedLimiter）の単体は test_ManualAdasPhaseC.cpp に同居する（ACC と段の順序を 共有し、順序こそが「リミッターが安全段を拒否できない」を決めるため、別ファイルに 分けると順序のテストが宙に浮く）。 |
| `vd-func:FUNC-075` | `gate:unit-ctest` | test_ManualAdasFunctionReport.cpp / test_KickdownDetector.cpp / test_PedalArbitrator.cpp / test_ManualDriveAdasConfig.cpp / test_AdasCoexistenceStack.cpp / test_ScriptedInputSource.cpp が傘バイナリ常設（ManualDrive ADAS 関連 81ケース、**673/673緑**の内数、2026-08-05 フェーズB実測。フェーズA時点は69ケース/661件で、フェーズBが上書きチャネル5件・ accel producer 5件・FCWゲート config 2件を追加した）。 matcher実装自体の赤実証（Python側）は GT_esmini/scripts/verification/test_manualdrive_matchers.py（50テスト）が別途担うが、 これは gt_sim_test 検証ハーネス側であり ctest 傘バイナリの外＝この辺には含めない。 |
| `vd-func:FUNC-080` | `gate:unit-ctest` | test_LaneKeepAssist.cpp が傘バイナリ常設（**771/771緑**、2026-08-05 フェーズD実測。 フェーズC時点は731件）。matcher 側の赤実証は GT_esmini/scripts/verification/test_manualdrive_matchers.py（フェーズDで lane_kept_within / steer_output_absent の13テストを追加、計90緑）。 ユニットが実装欠陥を2件捕まえている: envelope の振幅上限が rate 段で押し戻される （AdSteeringEnvelope のジャーク段が shipped-disabled のままである当の非対称）と、 抑制時のランプダウンが要求の「即時中断」に反していたこと。 |

### upstream-candidate (6)

| from | to | note |
| :--- | :--- | :--- |
| `fork-patch:3` | `odr-upstream-pr:PR-1` |  |
| `fork-patch:7` | `odr-upstream-pr:PR-2` |  |
| `fork-patch:8` | `odr-upstream-pr:PR-1b` |  |
| `fork-patch:13` | `odr-upstream-pr:PR-3` |  |
| `fork-patch:10` | `odr-upstream-pr:PR-4` |  |
| `fork-patch:17` | `odr-upstream-pr:PR-5` |  |

### verifies (35)

| from | to | note |
| :--- | :--- | :--- |
| `matcher:maintained_following_distance` | `req-vd-ad:REQ-AD-002` | THW車間の維持を検証 |
| `matcher:stopped_at_signal` | `req-vd-ad:REQ-AD-003` | 停止線手前停止を検証 |
| `matcher:stopped_at_stop_sign` | `req-vd-ad:REQ-AD-004` | STOP標識停止を検証 |
| `matcher:min_obb_separation_above` | `req-vd-ad:REQ-AD-005` | 歩行者とのOBB分離（衝突ゼロ）を検証 |
| `matcher:min_obb_separation_above` | `req-vd-ad:REQ-AD-001` | カットイン追突回避=ego-他車OBB分離>0（衝突ゼロ） |
| `matcher:impact_speed_below` | `req-vd-ad:REQ-AD-001` | 回避不能域の緩和=初回接触の閉じ(衝突)速度が床以下（07_aeb直進, NCAPカラーバンド思想） |
| `matcher:no_emergency_without_conflict` | `req-vd-ad:REQ-AD-013` | 誤作動抑止(SOTIF)=衝突コース不在時にsource:"aeb"の緊急制動が不発火（07_aeb負3本） |
| `matcher:route_lane_plan_holds` | `req-vd-ad:REQ-AD-017` | route_lane_batch.yaml の4シナリオで検証。段 a/b は merge_required_for_exit_ramp （invalid_route 診断）と route_valid_off_target_lane_for_exit_ramp（目標レーン帯からの 逸脱を検出。route_lane_plan_design.md §4-4 実測）。**段 c** は lane_change_to_exit_ramp（隣接車なし・3ホップ）と lane_change_to_exit_ramp_with_traffic （隣接車あり・2ホップ）で、max_deviations: 0 が「接続点を目標レーン帯に乗ったまま 通過した」ことを判定する（2026-08-02 実測: 前者は -1→-2→-3→-4 と移り road4→road2 へ、 後者は 7.5s ギャップを拒否してから 2 ホップ、いずれも deviation_count=0）。 **REQ-AD-017 の段 a/b/c まで**。段 d/e（逸脱復帰・終点停車）は未実装で検証対象外＝ この辺は要求全体の充足を主張しない。どの段を検証済みかは要求側の acceptance_ladder[].verified_by が持つ。 |
| `matcher:indicator_leads_junction_turn` | `req-vd-ad:REQ-AD-021` | anticipation_driving_batch.yaml の3本で検証する（junction_turn_signal.md §4-3）。 **接続路長の両極を意図的に取ってある** — 旧実装の故障モードが接続路長に依存して 2つに分かれていたためで、片方だけでは再発を捕まえられない:
- 正・短い接続路: decelerate_for_left_turn（14.87 m。2026-08-04以前は
  decelerate_for_left_turnという名前だった）`min_distance_m: 29.5`,
  `expect_dir: left`。旧実装ではここが 7.18 m しか出なかった。
  **幾何は左折である**（同 §5。接続路 road 13 は
  arc curvature +0.108108・length 14.8696 ＝ heading delta +1.608 rad、東向き→北向き）。
  expect_dir を right に「直す」と赤くなる。
- 正・長い接続路: junction_turn_signal_long_connector（33.2 m）`min_distance_m: 29.5`,
  `expect_dir: right`。旧実装ではここが**点灯フレーム0**だった（接続路長が単独で
  lookahead を超え、走査が出口の腕に届かないため構造的に点灯不能）。
- 負・直進通過: cross_straight_junction `expect_dir: none`。今回の修正は
  「点きやすくする」方向なので偽陽性が出やすく、負の資産が要る。

min_distance_m の 29.5 は法定 30.0 に対する**フレーム量子化の余裕**であって主張の 緩和ではない（同 §4-2）。コード側は dist<=30.0 の最初のフレームで点灯するので実測は 30.0 を僅かに下回る。§11-9 が法定 3.0 秒に min_lead_s: 2.9 を置いたのと同じ流儀。
**段 a/b/c のどれを検証済みかは要求側の acceptance_ladder[].verified_by が持つ。** |
| `matcher:indicator_leads_lane_change` | `req-vd-ad:REQ-AD-018` | route_lane_batch.yaml で lane_change_initiation を有効化する4本で検証。 **REQ-AD-018 の段 a・段 b・段 c**を担う（2026-08-03 に段 b/c を追加）。 段ごとに刺激が違い、min_lead_s の意味も違う:
- 段 a（先行する）: lane_change_to_exit_ramp `min_lead_s: 2.0`（実測 2.30s）、
  lane_change_to_exit_ramp_with_traffic `3.0`（実測 7.00s）。後者を測定値の近くに
  置かないのは、7.00 の大半がギャップ待ち時間で、隣接車の配置を触っただけで
  赤くなるからである（無関係な変更で鳴る検知器は警報疲れを育てる）。法定値 3.0
  そのものを下限に置いてある。
- 段 b（定速で法定3秒）: lane_change_to_exit_ramp_at_constant_speed
  `min_lead_s: 2.9`（実測 3.05s）。2.9 は法定値を下回るが、これは
  **フレーム量子化（dt=0.05）のぶんの余裕**であって主張の緩和ではない。
- 段 c（加速中も縮まない）: lane_change_to_exit_ramp_during_gradual_acceleration
  `min_lead_s: 2.9`（実測 3.05s）。修正前は同じ刺激で 2.10s だった。

**この matcher だけでは段 b が「定速で」を保証しない。** min_lead_s は秒数しか見ず、 刺激が本当に定速かは検査しない。定速性は t_sig 近傍の加速度を別途実測して示す （2026-08-03 実測 0.00066 m/s²）。刺激の素性を matcher に代弁させないこと。 どの段を検証済みかは要求側の acceptance_ladder[].verified_by が持つ。 |
| `matcher:route_lane_plan_holds` | `req-vd-ad:REQ-AD-016` | merge_required_for_exit_ramp シナリオの invalid_route 診断が **REQ-AD-016 の段 b のみ**（経路解決の失敗を検出して外へ出す＝沈黙しない）を検証する。 段 a/c（経路の補完・起終点のみからの探索）は検証対象外。 |
| `matcher:min_obb_separation_above` | `req-vd-ad:REQ-AD-010` | 停止先行車との衝突ゼロ |
| `matcher:min_obb_separation_above` | `req-vd-ad:REQ-AD-011` | 先行車との衝突ゼロ |
| `matcher:min_obb_separation_above` | `req-vd-ad:REQ-AD-012` | 歩行者/自転車との衝突ゼロ |
| `matcher:deceleration_profile_smooth` | `req-vd-ad:REQ-AD-014` | 快適域で滑らかな減速（緊急制動不発火） |
| `matcher:overtake_decision_holds` | `req-vd-ad:REQ-AD-023` | overtake_batch.yaml の4本で検証。段 a は expect_considered、段 b/c は expect_phases （signal_out → out → pass → signal_back → back → idle の部分列）と expect_cleared_lead、 段 e は同じ判定を対向車線シナリオで。**段 d（指示器）はこの matcher では判定しない** — matcher:indicator_leads_lane_change の担当。段 f/g は未達で検証対象外。 どの段を検証済みかは要求側の acceptance_ladder[].verified_by が持つ。 |
| `matcher:overtake_decision_holds` | `req-vd-ad:REQ-AD-024` | 段 b は overtake_declined_before_route_branch で **considered=true かつ blocked_reason=route_budget かつ phase が終始 idle** の3点同時成立を判定する （forbid_phases で「一度も合図すらしていない」ことも押さえる）。 considered を落とすと「そもそも検討していないから緑」＝偽 PASS と区別できない。 段 c は overtake_aborted_for_route_branch で **cleared_lead=false（全フレーム）** と blocked_reason=route_budget を判定する。 この2本は完遂側 overtake_slow_lead_on_two_lane_road と**同じフィールドの逆極性**を 主張しており、それによって検知器の両極性が実証されている。 |
| `matcher:indicator_leads_lane_change` | `req-vd-ad:REQ-AD-023` | **段 d のみ**（往路・復路の両方で進路変更の前から予告する）。追い越しは車線変更が2回なので、 同 matcher を **window で往路・復路に分けて2エントリ**呼ぶ — matcher は窓内の最初の signal_active / armed しか拾わないため（vd_metrics.py の next(...)）。 matcher 自体は変更していない。direction と点灯側の一致検査が入っているので、 復路で左右が反転していることも同時に検査される。 実測 2026-08-04: 往路 t_sig=0.05 / t_arm=3.00 = 2.95 s、復路 t_sig=10.25 / t_arm=13.25 = 3.00 s。 min_lead_s: 2.9 は法定 3.0 s からフレーム量子化（dt=0.05）ぶんを引いた値で主張の緩和ではない。 |
| `matcher:manual_aeb_fires` | `req-vd-ad:REQ-AD-025` | 段a（無反応ドライバ＋衝突コースでAEB介入）の主判定。md_aeb_kickdown_suppressでは キックダウン前区間の前提（段d抑制の対照）としても使う。実測: t=2.00でACTIVE （test_results/mdadas_run1/md_aeb_unresponsive/verdict.json）。 |
| `matcher:no_intervention_in_window` | `req-vd-ad:REQ-AD-025` | 段b（衝突コース不在で誤介入しない）の主判定。md_aeb_kickdown_suppressでは段d （抑制後の窓）、md_fcw_warning_onlyでは段e（警報単独エピソードでAEB非介入）にも使う。 |
| `matcher:brake_not_stacked` | `req-vd-ad:REQ-AD-025` | 段c（人間がAEB要求以上を踏んでいるとき上乗せしない＝max合成）の主判定。この E2E 資産は threshold未校正で needs-review（test_results/mdadas_run1/batch_summary.md）＝緑担保に 留まらず未計測。赤実証は matcher:brake_not_stacked の実装を直接叩く Python ユニット テストが担う（検証計画§4-1の unit-level 許容、verification_plan.md §4-2で訂正済み）。 |
| `matcher:fcw_leads_intervention` | `req-vd-ad:REQ-AD-025` | 段e（警報が介入に先行しリード≥0.8s）の判定。実測ではcut-inにつきリード0.000sでfail （gt.fcw/gt.aebが同一フレームt=1.75でACTIVE、test_results/mdadas_run1/ md_aeb_unresponsive/verdict.json）。design §3-2訂正・検証計画の対象。 **この辺は「matcherが正しく判定を出す」ことを主張するのであって「要求段eが 現状達成されている」ことは主張しない**（現に fail）。 |
| `matcher:adas_state_matches` | `req-vd-ad:REQ-AD-025` | 段b（config OFFでなくSTANDBYで見張っていたことの確認、負系の意味を担保）と 段e（md_fcw_warning_only、gt.fcwが少なくとも一度activeになること）の補助判定。 |
| `matcher:adas_state_matches` | `req-vd-ad:REQ-AD-028` | コード自身のコメント（vd_metrics.py:1853-1854）が「req-vd-ad:REQ-AD-026 step c / REQ-AD-028 step a」の主判定と明記。正規Name列挙のstate_nameが期待どおりに出ることの 確認そのものが観測性要求（3値規律のUNAVAILABLE/STANDBY/ACTIVE区別）の判定にあたる。 |
| `matcher:driver_override_reported` | `req-vd-ad:REQ-AD-028` | 段b（運転者上書き事象が DriverOverride{active,reason} に出る／アクセル起因は custom_state で補完）の主判定。 ~~この辺は「matcher が段bを判定できる」ことを主張し、「段bが全経路で達成されている」 ことは主張しない~~ → **2026-08-05 フェーズDで3経路すべてがそろい、段bは met へ昇格した**。 経緯を残すのは、この辺が長く「器具はあるが producer が無い」状態だったことが記帳の 形として重要だからである: フェーズBが機構とアクセル経路（custom_state= DRIVER_OVERRIDE_ACCEL、md_aeb_kickdown_suppress）、フェーズCがブレーキ経路 （REASON_BRAKE_PEDAL、ACC の解除＝md_acc_cancel_resume）、フェーズDがステア経路 （REASON_STEERING_INPUT、LKA の中断＝md_lka_human_steer）を順に足した。3経路を1つの段で 主張している以上、1経路の達成で met を上げるのは overclaim であり、実際に上げたのは 3つ目が緑になった時点である。 赤実証は GT_esmini/scripts/verification/test_manualdrive_matchers.py（populate停止・ 入力プロファイル時刻ずらしの2赤）。 |
| `matcher:setting_reflected` | `req-vd-ad:REQ-AD-026` | 段e（走行中の設定速度増減が追従目標に反映される）と段h（車間段階の切替が実測THWに 反映される）の主判定。段h は md_acc_follow_lead 側で、1本の刺激が2段階以上を跨ぐ （mid→long→short）ことで判定する。実測 20/20 緑。 |
| `matcher:adas_state_sequence` | `req-vd-ad:REQ-AD-026` | 段c（操作列に対応する状態遷移列がHVDに出る）の主判定。段f（速度域の域外で ACTIVE→STANDBY）にも md_acc_speed_range_gate で使う。設計§6 の ACC/MSL 排他も 同じ matcher で判定する（md_acc_msl_exclusion、gt.acc と gt.msl の2行）。 |
| `matcher:speed_capped_at` | `req-vd-ad:REQ-AD-026` | 段g（制限速度制御の有無で実効上限が変わる）の主判定。**この判定は2構成の対で初めて 主張になる**——respect_limit 側だけでは「常に制限を守る実装」も通り、ignore_limit 側だけ では「道路を一度も読まない実装」も通る。バッチは同一 xosc を variant で2行持つ。 |
| `matcher:speed_capped_at` | `req-vd-ad:REQ-AD-030` | 段a（全開スロットルでも設定速度を超えない）の主判定。cap_key で実効キャップを毎フレーム読む。 |
| `matcher:no_brake_output` | `req-vd-ad:REQ-AD-030` | 段a の**負系**（リミッターはブレーキを出さない）の主判定。検証計画が当初この負系に 当てようとした下り坂資産（MDA-XODR-02）は作らなかった: RealVehicle は道路ピッチを 姿勢にしか使わず（terrain_pitch_ は縦運動に入らない）、勾配路は「下り坂に見えて平地の ように振る舞う」——起こせない現象の名前を持つ資産になる。同じ主張は平坦路で、 クランプが実際に効いている最中に取っている。 |
| `matcher:stop_hold_stationary` | `req-vd-ad:REQ-AD-031` | 段a（v=0 到達後の停止保持でクリープが抑止される）の主判定。段b（信号・一時停止標識を 停止対象に構成した場合）にも md_sng_stop_sign で使う。実測: 保持 11.6 秒で変位 0.032 m（GT_esmini/docs/virtualdriver/measurements/manualdrive_creep_stop_hold_2026-08-05.md）。 |
| `matcher:restart_after_trigger` | `req-vd-ad:REQ-AD-031` | 段a の後半（人間のアクセル操作を唯一の再発進トリガとする）の主判定。先行車は停止した まま run が終わるので、発進の説明はパルス以外に存在しない——「先行車が動いたから 追従した」で通ってしまう構成では、段a が禁じている自動再発進と区別できない。 |
| `matcher:driver_override_reported` | `req-vd-ad:REQ-AD-026` | 段b（ブレーキ踏下で解除、アクセル踏下は一時上書き）の上書きチャネル側の判定。 フェーズBで作った matcher が、フェーズCで初めて **REASON_BRAKE_PEDAL を実際に生む producer** を得た（ACC の解除）。同一 run でアクセル起因の一時上書きも custom_state=DRIVER_OVERRIDE_ACCEL として出るため、規格の2値制約とその補完が 1本の刺激の中で並んで観測できる。 |
| `matcher:lane_kept_within` | `req-vd-ad:REQ-AD-027` | 段a（弱ドリフトで補正が入り車線内に留まる）の主判定。**左右対で回すことが判定の一部** ——補正則の符号誤りは機能を弱めるのではなく車を車線の外へ押すので、片側だけでは 「弱すぎる実装」と区別できない。実測は完全な鏡像（t=5.0 で offset +0.317/補正 +0.004 対 -0.317/-0.004、両極ともピーク |offset| 0.363m でレーン-1不変）。 `expect_kept: false` 方向は同一 xosc の warning_only / below_band 構成で使い、同じ刺激が 補正なしでは |offset| 4.330m・レーン-1→-2 の逸脱になることを示す——これが無いと正例の 「留まった」は刺激が弱かっただけの可能性を排除できない。 |
| `matcher:steer_output_absent` | `req-vd-ad:REQ-AD-027` | 段b（人間の明確な操舵入力・指示器作動中は介入しない/即時中断）と段f（警報のみモードで cmd.steering 不変）と段e（速度域外で介入しない）の負系判定。3段が同一 matcher なのは、 いずれも「機能自身の操舵寄与がゼロ」という同じ観測だからで、違うのは**理由**の方 （gt.lka.suppressed_* / in_speed_band に別々に出る）。 |

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
| `Domain#FollowRoadUser` | 先行者追従 | `policy:lead`, `req-vd-ad:REQ-AD-002`, `req-vd-ad:REQ-AD-026`, `scene:SCN-003` |
| `Domain#FollowTargetSpeed` | 目標速度追従（ManeuverLevelActivity） | `scene:SCN-001` |
| `Domain#IlluminationCondition` | 照明条件（昼光・夜間・人工照明の親クラス） | `feature:F6` |
| `Domain#IntersectionAtGrade` | 平面交差点 | `policy:conflict`, `scene:SCN-005` |
| `Domain#Junction` | 交差点（2つ以上の道路が交わる） | `policy:junction_priority`, `scene:SCN-006` |
| `Domain#KeepLane` | 車線維持 | `req-vd-ad:REQ-AD-027`, `scene:SCN-001` |
| `Domain#MakeATurn` | 右左折（交差点通過） | `scene:SCN-004`, `scene:SCN-005`, `scene:SCN-006` |
| `Domain#Motorcycle` | 二輪車（PrivateVehicle配下） | `scene:SCN-010` |
| `Domain#Motorway` | 高速道路 | `scene:SCN-008` |
| `Domain#MoveBackward` | 後退（LongitudinalActivity） | `req-vd-ad:REQ-AD-020`, `req-vd-ad:REQ-AD-022`, `scene:SCN-014` |
| `Domain#NightLightingCondition` | 夜間照明条件 | `feature:F6` |
| `Domain#Overtake` | 追越し（対象後方に始まり前方で終わる、2回の車線変更を伴う） | `scene:SCN-002` |
| `Domain#Parking` | 駐車場・駐車スペース（Road配下） | `req-vd-ad:REQ-AD-019`, `req-vd-ad:REQ-AD-020`, `req-vd-ad:REQ-AD-022`, `scene:SCN-014` |
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
