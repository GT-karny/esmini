# 交差点を直進通過するときの過減速ガード

自車がfabriksgatanの交差点をSTRAIGHTに直進通過するシナリオ。
接続路の旋回率が小さいにもかかわらずManeuverAwareSpeedPlannerが交差点減速キャップをかけていないかを確認する。

## 検証の狙い

decelerate_for_left_turn.xosc（同じ交差点・同じ進入路/レーン）の逆パターンにあたる。
接続路12はheading delta約0.046 radを15.5 mで通過する、ほぼ直線の接続路で、turnRateは約0.003 rad/mとSHARP_TURN_RATE(0.04)を大きく下回る。
このためManeuverAwareSpeedPlannerはturn_speedキャップを課してはならず、junction制約も立ててはならない。
xosc内コメントによれば、これは「直進交差点での過減速」バグ（P2 issue 1）の回帰ガードで、修正前は両チェックがFAILし（一律5 m/sの交差点キャップ）、修正後はPASSする。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `fabriksgatan_traffic_lights.xodr`（road3→接続路12→road1、ほぼ直線の接続路） |
| 自車 | 初期位置 road3 lane-1 s=11。Story Eventで目標速度13.889 m/sへ3.0 s linearで加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間25 s超過 |

## 進行

1. t=0: Story Event「Cruise」開始、目標速度13.889 m/sへ3.0 sかけてlinearに加速。
2. 自車はroad3(lane-1)からroute経由で接続路12(lane-1)、road1(lane-1)へ直進通過する。
3. t=25 s到達でStopTrigger成立、シナリオ終了。

## 期待する挙動

- t=1.0 s以降、junction種別の速度制約が一度も立たない。
- t=1.0 s以降、接続路12上での速度が10.0 m/s（cruiseの13.889 m/sに対し）を常に上回る。turn_speedキャップのバグ挙動は5.0 m/s。
- 接続路12の通過中、右左折方向指示灯が一度も点灯しない（expect_dir: none）。req-vd-ad:REQ-AD-021の新matcherに対する否定側の確認資材で、指示灯を点灯しやすくする修正が誤って直進にも点灯してしまわないかを見る。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `no_constraint_kind` | kind=junction, after sim_time 1.0 | 直進通過でjunction制約を立てない |
| `min_speed_above` | threshold=10.0, road_id=12, after sim_time 1.0 | 接続路12上で13.889 m/sクルーズを維持し過減速しない |
| `indicator_leads_junction_turn` | expect_dir=none | 直進通過を旋回と誤判定して指示灯を点灯させない |

## 関連

- バッチ: `anticipation_driving_batch.yaml`（所属。dt=0.05, max_time=40.0, osi=true, report=anticipation）
- 期待値: `cross_straight_junction.expectations.yaml`
- 関連ID: req-vd-ad:REQ-AD-021
