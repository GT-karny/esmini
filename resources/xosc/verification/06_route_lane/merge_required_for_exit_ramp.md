# 粗いルートによる出口ランプ接続のinvalid_route診断

自車はhighway_example_with_merge_and_split.xodrのjunction 1で、road0/lane-1/s=10からroad2/lane-1/s=40へ向かう、始点と終点だけの2 WaypointのRouteを持つ。
lane-1はjunction 1の出口ランプ側接続（connection id=3、laneLinkは"-4"のみ）には繋がっておらず、esmini自身がこのRouteをinvalidと判定して破棄する。
自車はレーン-1のまま直進を続ける。

## 検証の狙い

このシナリオは自車が出口ランプへ到達することを検証するものではない。
逆に到達「しない」ことと、その経路解決失敗がVDテレメトリのdiagnostic=="invalid_route"として正しく報告されることを確認する、失敗を期待するシナリオである。
esmini自身はこの経路破棄をWARNログ（`Route::AddWaypoint Skip waypoint ... path not found`等）でしか可視化しない。
`BuildRouteLanePlan()`がRoute::IsValid()を確認してdiagnostic=="invalid_route"へ短絡することで、この失敗が毎フレームのテレメトリからも見えるようになった、というのがこの資産の主眼である。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `highway_example_with_merge_and_split.xodr`（junction 1。road0のlane-1,-2,-3は直進側connection id=1へ、lane-4のみが出口ランプ側connection id=3経由でroad2へ接続） |
| 自車 | road0/lane-1/s=10出発、目標速度13.889 m/s（Cruiseイベント、t=0開始、3.0秒のlinearランプ）。Route: road0/-1/s=10 -> road2/-1/s=40の2 Waypointのみ（接続道路road4を明示しない） |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間28 s |

## 進行

1. t=0: AssignRouteActionでRouteが割り当てられる。lane-1から出口ランプへ到達する経路が存在しないため、esminiがRouteをinvalidと判定して破棄する。
2. t=0〜: Cruiseイベントにより自車は13.889 m/sへ3.0秒でランプアップし、以後lane-1のまま直進を続ける。
3. t=28s: StopTrigger成立。

## 期待する挙動

- 経路が破棄され、自車はlane-1のまま直進し続ける（出口ランプへは到達しない）。
- route_lane_plan_holdsが全フレームでdiagnostic=="invalid_route"、rerouted==false、target_lanes==[]、on_target_lane==falseを報告する。
- expectations.yaml記載の実測（560 frames, dt=0.05）: 全フレームでdiagnostic="invalid_route"、reason="no_plan"、target_lanes=[]、rerouted=false、deviation_count=0。VD警告ログは1回のみ（latched、"invalid_route — target-lane guidance unavailable, continuing with position-based lane keeping"）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| route_lane_plan_holds | expect_diagnostic="invalid_route", expect_rerouted=false, expect_target_lanes=[], expect_on_target_lane=false | 経路が無効と判定され、車線帯の診断が一切行われないことを確認 |

## 関連

- バッチ: `route_lane_batch.yaml`（report: route_lane）
- 期待値: `merge_required_for_exit_ramp.expectations.yaml`
- 関連ID: `vd-func:FUNC-050`
