# 有効な経路だが目標レーン帯に乗っていない状態のオフレーン診断

merge_required_for_exit_ramp.xoscと同じ道路・出発点・目的地を使う。
違いはRouteのWaypointに接続道路road4を明示している点で、esmini側ではRoute::IsValid()==trueとなる。
自車はレーン変更をせずlane-1のまま走行し続けるため、目標レーン帯{-4}には乗らない。

## 検証の狙い

esmini自身が検出できない「経路は有効だが目標レーン帯に乗っていない」状態を、RouteLanePlanのtarget_lanes/on_target_lane/deviation_countで診断できることを確認する。
接続道路road4という一跳ねのWaypointを明示すると、RoadPath::Calculateの「nextRoad == targetRoad」高速経路が発火してレーン接続性チェックがスキップされるため（route_lane_plan_design.md §2「穴1」）、esmini自体はWARNを一切出さない。
vd-func:FUNC-055（自発的レーンチェンジ）は本シナリオの範囲外設計であり、自車は自ら車線変更しない。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `highway_example_with_merge_and_split.xodr`（junction 1。merge_required_for_exit_rampと同一） |
| 自車 | road0/lane-1/s=10出発、目標速度13.889 m/s（Cruiseイベント、t=0開始、3.0秒のlinearランプ）。Route: road0/-1/s=10 -> road4/-1/s=50 -> road2/-1/s=40（接続道路road4を明示した3 Waypoint） |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間28 s |

## 進行

1. t=0: road4を明示した3 WaypointのRouteが割り当てられ、Route::IsValid()==trueとなる。ただし実際にはlane-1からroad4へは接続されていない（road0のlane-4のみが接続）。
2. t=0〜: Cruiseイベントにより13.889 m/sへ3.0秒でランプアップ。自車はレーン変更せずlane-1のまま直進する。
3. 実測（expectations.yaml notes）ではt=15.70sにroad=3（直進側のthrough connector）へ遷移し、deviation_count=1が記録される。
4. t=28s: StopTrigger成立。

## 期待する挙動

- road0滞在中、RouteLanePlanはtarget_lanes==[-4]を報告し、自車はlane-1にいるためon_target_lane==falseとなる。
- diagnosticは空文字列で、rerouted==false（band解決が最初のパスで完了し、LaneIndependentRouterによる再経路は発生しない）。
- 自車がroad0を離れる際に少なくとも1回のdeviationが記録される。
- expectations.yaml記載の実測（600 frames, dt=0.05）: t=0.05でroad=0, ego_lane=-1, target=[-4], on_target=false, dist_to_connection=190.00。t=15.70でroad=3, reason=off_plan_road, deviation_count=1, last_deviation_road_id=0。esmini自身の「Entity Ego moved away from route」ログより約15秒・190m早い段階で診断が完了している。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| route_lane_plan_holds | expect_diagnostic="", expect_rerouted=false | 車線帯解決が最初のパスで完了し、再経路も発生しないことを確認 |
| route_lane_plan_holds | window=[0.0, 14.0], expect_target_lanes=[-4], expect_on_target_lane=false | road0滞在中、目標レーン帯{-4}と、自車がそこに乗っていないことを確認 |
| route_lane_plan_holds | min_deviations=1 | road0を離れる際に逸脱が記録されることを確認 |

## 関連

- バッチ: `route_lane_batch.yaml`（report: route_lane）
- 期待値: `route_valid_off_target_lane_for_exit_ramp.expectations.yaml`
- 関連ID: `vd-func:FUNC-050` / `vd-func:FUNC-055`
