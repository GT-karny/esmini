# 先行車の急制動追従

自車と先行車がstraight_500m_plain.xodrのlane -1を13.889 m/sで巡航する。
約50mのギャップを保った状態で、t=8sに先行車が2.0 m/sまで急減速する。
LeadVehicleAwareポリシー（Phase 3a, A3）が自車を減速させ、追突せず安全車間を保てるかを検証する。

## 検証の狙い

LeadVehicleAwareポリシーが無い状態（Phase 3a導入前）では、自車は指令速度を維持したまま先行車に接近し続け、THW（車間時間）が崩壊してmaintained_following_distanceがFAILする。
本シナリオはその崩壊を検出しつつ、Phase 3a導入後は先行車の急制動に追従して減速し、安全車間を維持できることを確認する。
THWはOSIで取り込んだシーンから測定するため、バッチ実行時はOSIキャプチャが必須（car_following_traffic_control_batch.yamlのdefaults.osi: true）。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_plain.xodr`（直線・平坦） |
| 自車 | road1/lane-1/s=30出発。Initでの速度指定なし（推定: 既定0 m/s）。目標速度13.889 m/s（Cruiseイベント、t=0開始、3.0秒のlinearランプ） |
| 他エンティティ | 先行車（car_blue）。road1/lane-1/s=80出発、初期速度13.889 m/s（Init、step）。t=8sに2.0 m/sを目標として6.0 m/s²のレートで急減速（LeadBrakeイベント） |
| 走行時間 | StopTrigger: シミュレーション時間25 s |

## 進行

1. t=0: 自車がCruiseイベントで13.889 m/sへ3.0秒のlinearランプで加速を開始する。先行車は既にInitで13.889 m/sに設定済み。
2. t=8s: LeadBrakeイベントが発火し、先行車が2.0 m/sを目標に6.0 m/s²のレートで急減速する。
3. t=25s: StopTrigger成立。

## 期待する挙動

- p10（worst-caseテール）のTHWが、sim_time 8.0s以降を通じて安全下限0.6 sを下回らない。
- sim_time 17.0〜24.0sの間に自車速度が5.0 m/s未満まで下がる（先行車の急減速に追従して大きく減速したことの確認）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| maintained_following_distance | percentile=10, after sim_time=8.0, min_thw=0.6 | 急制動する先行車に対しても安全車間を維持（追突しない） |
| speed_below | threshold=5.0, after sim_time=17.0, before sim_time=24.0 | 先行車の急減速に追従して減速（巡航から大きく減速） |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`（policies: [lead]）
- 期待値: `follow_hard_brake.expectations.yaml`
- 関連ID: `vd-phase:Phase3`
