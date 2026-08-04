# 定速走行時のウインカー先行点灯（法定3秒リード、段b）

自車をroad0/lane-3から出発させ、-3→-4の1ホップのみの車線変更を、Initのみで固定した定速9.0 m/sで実行させる。
req-vd-ad:REQ-AD-018の受入ラダー段b「定速で法定3秒のリードを満たす」を検証する。

## 検証の狙い

vd-func:FUNC-061（ウインカーの車線変更先行点灯）/ req-vd-ad:REQ-AD-018（道路交通法施行令21条1項相当の法定3秒リード要求）の段bを検証する。
姉妹シナリオ`lane_change_to_exit_ramp_during_gradual_acceleration.xosc`（段c、加速中の検証）と対をなす。
本シナリオは真に定速（Initでのstep SpeedActionのみ、Story側にランプなし）でのウインカー先行点灯が法定3.0秒のフロアに到達することを確認し、姉妹シナリオは緩やかな加速中でもそのリードが縮まないことを確認する。
両者はEgoSpeedの選び方も共通の設計に基づく。
既存の無印/隣接車ありシナリオはlane-1/-2発進（n_remaining=3）でrequired_m=270mが190m予算を超え、発起がt=0で即決してしまうためリードの測定ができない。
本シナリオと姉妹シナリオはlane-3発進（n_remaining=1、required_m=74m@9.0 m/s）にすることで発起をrun中盤に来させ、リードを実測可能にする。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `highway_example_with_merge_and_split.xodr`（junction 1。姉妹シナリオと同一の道路・接続構造。road0のlane-4のみがconnection id=3経由でroad2へ接続） |
| 自車 | road0/lane-3/s=10出発、速度9.0 m/s固定（Init、step SpeedAction、ControllerAction適用前に設定。Story側にランプ・イベントなし）。Route: road0/-3/s=10 -> road4/-1/s=50 -> road2/-1/s=40 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間28 s |

## 進行

1. t=0: Initで自車速度を9.0 m/sにstepで設定した後、ControllerActionでVirtualDriverControllerへ縦横制御を渡す。以後Story側は空のManeuverGroupで、速度は一切変更されない。
2. xosc内コメントの計算（dist_to_connection=190m, required_m=1*max(9.0*6.0,40)+20=74m）: signal閾値=required_m+v*3.0=101m→t_sig=(190-101)/9.0=9.9s、arm閾値=required_m=74m→t_arm=(190-74)/9.0=12.9s、予測leadは3.0秒ちょうど。
3. 実測（expectations.yaml、2026-08-03）: signal_active初回trueがt=9.80、armed初回trueがt=12.85、lead=3.05秒。
4. notes記載: t=21.45付近でtrackがroad4へ遷移する（road2到達前に28sのStopTriggerへ達する）。
5. t=28s: StopTrigger成立。

## 期待する挙動

- -3→-4の1回の車線変更が接続点までに完了し、deviation_countは全run通じて0に留まる。
- 目標レーン帯は{-4}に解決される。
- ウインカーは車線変更のarm判定より最低2.9秒前に点灯する。これは法定3.0秒フロアへの到達を主張するもので、閾値2.9はexpectations.yaml記載の実測値3.05秒よりわずかに低く設定されている（dt=0.05のフレーム量子化の余裕込み）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| route_lane_plan_holds | max_deviations=0 | -3→-4の1回の車線変更が完了し逸脱が発生しないことを確認 |
| route_lane_plan_holds | expect_target_lanes=[-4] | 目標レーン帯が姉妹シナリオ群と同じ{-4}に解決されることを確認 |
| indicator_leads_lane_change | min_lead_s=2.9 | 定速走行時、ウインカーのリードが法定3.0秒フロアに到達することを確認 |

## 関連

- バッチ: `route_lane_batch.yaml`（policies: [lane_change_initiation], report: route_lane）
- 期待値: `lane_change_to_exit_ramp_at_constant_speed.expectations.yaml`
- 関連ID: `vd-func:FUNC-061` / `req-vd-ad:REQ-AD-018`
