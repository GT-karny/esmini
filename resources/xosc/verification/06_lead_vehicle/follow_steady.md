# 定速先行車への追従

自車がstraight_500m_plain.xodrのlane -1で13.889 m/sを目標に加速する一方、先行車は同レーンを10.0 m/sで定速巡航する。
自車が先行車に接近しても、快適な車間（THWバンド）に収束して落ち着くかを検証する。

## 検証の狙い

LeadVehicleAwareポリシー（Phase 3a, A3）が無い状態では、自車は指令速度13.889 m/sを維持したまま先行車に接近し続け、p50のTHWがバンドを下回ってmaintained_following_distanceがFAILする。
本シナリオはPhase 3a導入後、自車が快適な追従車間（時間車間バンド）に収束することを確認する。
先行車はDefaultコントローラ（ObjectControllerなし、レーン追従の既定挙動）で、Initで設定した定速をそのまま保つ。
自車のCruiseはStory Eventであり、Initの速度アクションはVirtualDriverから見えないため、Story Eventとして与えられている。
THWはOSIで取り込んだシーンから測定するため、バッチ実行時はOSIキャプチャが必須（batch osi: true）。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_plain.xodr`（直線・平坦） |
| 自車 | road1/lane-1/s=30出発。Initでの速度指定なし（推定: 既定0 m/s）。目標速度13.889 m/s（Cruiseイベント、t=0開始、3.0秒のlinearランプ） |
| 他エンティティ | 先行車（car_blue）。road1/lane-1/s=80出発、速度10.0 m/s（Init、step、以後一定） |
| 走行時間 | StopTrigger: シミュレーション時間30 s |

## 進行

1. t=0: 自車がCruiseイベントで13.889 m/sへ3.0秒のlinearランプで加速を開始する。先行車は既にInitで10.0 m/sに設定済み。
2. t=0〜8s付近: 自車が約50mのギャップを詰めながら先行車に接近する。
3. t=30s: StopTrigger成立。

## 期待する挙動

- p50のTHWが、sim_time 8.0s以降を通じて1.0〜3.5 sのバンド内に収まる（ギャップが詰まりすぎず、開きすぎない快適な追従）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| maintained_following_distance | percentile=50, after sim_time=8.0, min_thw=1.0, max_thw=3.5 | 自レーン先行車に対し快適な車間（THW）を維持 |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`（policies: [lead]）
- 期待値: `follow_steady.expectations.yaml`
- 関連ID: `vd-phase:Phase3`
