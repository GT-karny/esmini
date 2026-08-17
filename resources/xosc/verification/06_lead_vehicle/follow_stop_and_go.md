# 停止・再発進する先行車への追従

自車がstraight_500m_plain.xodrのlane -1で13.889 m/sを目標に加速し、先行車は10.0 m/sで巡航した後、t=6sに停止、t=15sに再発進する（ストップ・アンド・ゴー交通を模擬）。
LeadVehicleAwareポリシー（Phase 3a, A3）が自車を先行車の後方で停止させ、再発進に追従して再加速させられるかを検証する。

## 検証の狙い

LeadVehicleAwareポリシーが無い状態では、自車は指令速度を維持したまま停止した先行車に追突する形で接近し、THWが崩壊してmaintained_following_distanceがFAILする。
本シナリオはPhase 3a導入後、自車が先行車の停止に合わせてほぼ停止し、先行車の再発進に合わせて再加速できることを確認する。
THWはOSIで取り込んだシーンから測定するため、バッチ実行時はOSIキャプチャが必須（batch osi: true）。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_plain.xodr`（直線・平坦） |
| 自車 | road1/lane-1/s=30出発。Initでの速度指定なし（推定: 既定0 m/s）。目標速度13.889 m/s（Cruiseイベント、t=0開始、3.0秒のlinearランプ） |
| 他エンティティ | 先行車（car_blue）。road1/lane-1/s=80出発、速度10.0 m/s（Init、step）。t=6sに4.0 m/s²のレートで0.0 m/sまで減速・停止（LeadStopイベント）。t=15sに4.0秒のlinearランプで10.0 m/sへ再加速（LeadGoイベント） |
| 走行時間 | StopTrigger: シミュレーション時間28 s |

## 進行

1. t=0: 自車がCruiseイベントで13.889 m/sへ3.0秒のlinearランプで加速を開始する。先行車は既にInitで10.0 m/sに設定済み。
2. t=6s: LeadStopイベントが発火し、先行車が4.0 m/s²のレートで0.0 m/sまで減速・停止する。
3. t=15s: LeadGoイベントが発火し、先行車が4.0秒のlinearランプで10.0 m/sへ再加速する。
4. t=28s: StopTrigger成立。

## 期待する挙動

- p10のTHWが、sim_time 6.0s以降を通じて安全下限0.5 sを下回らない（追突しない）。
- sim_time 14.0〜16.0sの間に自車速度が2.0 m/s未満まで下がる（先行車の停止に合わせてほぼ停止する、クロール）。
- sim_time 22.0s以降に自車速度が5.0 m/sを超える（先行車の再発進に合わせて再加速する）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| maintained_following_distance | percentile=10, after sim_time=6.0, min_thw=0.5 | 停止・再発進する先行車に対し安全車間を維持（追突しない） |
| speed_below | threshold=2.0, after sim_time=14.0, before sim_time=16.0 | 先行車の停止に合わせてほぼ停止する（クロール） |
| speed_above | threshold=5.0, after sim_time=22.0 | 先行車の再発進に合わせて再加速する |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`（policies: [lead]）
- 期待値: `follow_stop_and_go.expectations.yaml`
- 関連ID: `vd-phase:Phase3`
