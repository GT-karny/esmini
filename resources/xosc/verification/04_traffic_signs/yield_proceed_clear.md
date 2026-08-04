# 対向なしのYIELDでの通過

自車がYIELD標識のある直線道路を巡航速度で走行し、対向車・交差車が一切いない状態で標識を通過する挙動を検証する。
停止はスクリプトされておらず、VirtualDriverが自律的に判断する。

## 検証の狙い

YIELDはSTOPとは異なり、道を譲る対象がいなければ停止する必要がない。
StopYieldSignAwareが「対向無しのYIELDでは完全停止しない」ことを、徐行はするが停止しないyield_slow_then_proceedと対にして確認する。
本シナリオはPhase 3c導入前後どちらでも通過できる真の対照(true control)として位置付けられている。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_yield_sign.xodr`(500 m直線2車線。YIELD標識 id=10, type=205, country=de, s=200, t=-3.57, lane -1のみ有効) |
| 自車 | road 1 / lane -1 / s=30 にテレポート。Cruiseイベントで目標速度13.889 m/sへ3.0 s(linear)で加速 |
| 他エンティティ | なし(対向車・交差車なし) |
| 走行時間 | StopTrigger: シミュレーション時間 35 s 超過 |

## 進行

1. sim_time > 0でCruiseイベントが起動し、目標速度13.889 m/sへ3.0 s(linear)で加速する。
2. StopYieldSignAwareがYIELD標識(id=10, s=200)を検知するが、対向・交差する交通がないため停止判断はしない。
3. sim_time 11.0〜18.0 sの標識接近・通過区間で速度0.5 m/s以上を維持し、完全停止しない。
4. sim_time > 20.0 sの時点で速度8.0 m/sを超えて巡航へ復帰している。
5. sim_time > 35 sでStopTriggerが成立してシナリオが終了する。

## 期待する挙動

- sim_time 11.0〜18.0 sの区間(road 1)で速度0.5 m/s以上を維持し、完全停止しない。
- sim_time > 20.0 s以降は速度8.0 m/sを超え、巡航へ復帰している。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| min_speed_above | threshold=0.5, road_id=1, after sim_time=11.0, before sim_time=18.0 | 対向無しのYIELDでは完全停止しない(徐行でも止まり切らない) |
| speed_above | threshold=8.0, after sim_time=20.0 | 通過後は巡航へ復帰 |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`(policies: [stop_yield])
- 期待値: `yield_proceed_clear.expectations.yaml`
