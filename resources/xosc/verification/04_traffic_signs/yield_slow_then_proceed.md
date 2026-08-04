# 対向なしのYIELDでの徐行と通過

自車がYIELD標識のある直線道路を巡航速度で走行し、標識手前で速度を落としつつも完全停止はせずに通過する挙動を検証する。
道路・標識はyield_proceed_clearと同一。

## 検証の狙い

YIELDでは対向・交差交通がなくても、いつでも道を譲れるよう速度を落とす「注意(caution)」が期待される挙動になる。
speed_belowで接近区間の減速を、min_speed_aboveで完全停止していないことを、それぞれ確認して切り分ける。
Phase 3c導入前は自車が標識前でも巡航速度を維持したままになり、`speed_below`が不成立(FAIL)になる(この不一致が判別点)。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_yield_sign.xodr`(500 m直線2車線。YIELD標識 id=10, type=205, country=de, s=200, t=-3.57, lane -1のみ有効) |
| 自車 | road 1 / lane -1 / s=30 にテレポート。Cruiseイベントで目標速度13.889 m/sへ3.0 s(linear)で加速 |
| 他エンティティ | なし(対向車・交差車なし) |
| 走行時間 | StopTrigger: シミュレーション時間 35 s 超過 |

## 進行

1. sim_time > 0でCruiseイベントが起動し、目標速度13.889 m/sへ3.0 s(linear)で加速する。
2. StopYieldSignAwareがYIELD標識(id=10, s=200)への接近を検知し、注意のため速度を落とす。
3. sim_time 11.0〜18.0 sの区間で速度10.0 m/s未満まで減速しつつ、0.5 m/s以上は維持して完全停止しない。
4. 標識通過後、巡航速度へ復帰する。
5. sim_time > 35 sでStopTriggerが成立してシナリオが終了する。

## 期待する挙動

- sim_time 11.0〜18.0 sの区間(road 1)で速度が10.0 m/s未満まで低下する(注意の徐行)。
- 同区間で速度0.5 m/s以上を維持し、完全停止はしない。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| speed_below | threshold=10.0, after sim_time=11.0, before sim_time=18.0 | YIELD手前で減速(注意) |
| min_speed_above | threshold=0.5, road_id=1, after sim_time=11.0, before sim_time=18.0 | 対向無しなので完全停止はしない |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`(policies: [stop_yield])
- 期待値: `yield_slow_then_proceed.expectations.yaml`
