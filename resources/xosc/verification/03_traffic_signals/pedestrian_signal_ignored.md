# 歩行者用信号ヘッドの無視（負例）

信号タイプフィルタの負例判別シナリオである。
fabriksgatan_traffic_lights road 3には車両用ヘッド（id=1, type 1000001, s=109）に加え、歩行者用ヘッドが2基（id=3 s=109、id=2 s=114、いずれもtype 1000002）存在する。
車両灯が青、両歩行者灯が赤のまま、自車は低速（5 m/s）で停止線を通過する。

## 検証の狙い

歩行者用ヘッドはいずれもorientation "+"と`<validity fromLane="-1" toLane="1"/>`を持ち、自車の走行レーン（-1）を含むため、orientationとレーン有効性のフィルタだけでは除外できない。
lamp種別（DONT_WALK/WALK）による信号タイプフィルタが機能して、自車が歩行者用信号を車両用の赤信号と誤認せず、停止しないことを確認する。
低速走行にすることで、巡航速度では短いブレーキの落ち込みにしかならない不具合を、完全停止として顕在化させている。
本シナリオは負例（ここで自車は止まってはならない）である。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `fabriksgatan_traffic_lights.xodr`（road 3。車両ヘッドid=1 s=109、歩行者ヘッドid=3 s=109／id=2 s=114） |
| 自車 | road 3 / lane -1 / s=85 でテレポート、ルート road3→road12→road1、巡航速度5.0 m/s |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 20 s で停止 |

## 進行

1. t=0 s: road 3 / lane -1 / s=85 にテレポートし、ルートを割り当て、`VirtualDriverController`を起動する。
2. t=0 s: Cruise Eventが開始し、`SpeedAction`（linear, 3.0 s）で目標速度5.0 m/sへ加速する。
3. 車両ヘッドid=1は走行時間全体を通じて青。歩行者ヘッドid=2・id=3は走行時間全体を通じて赤（`Phase name="vehicle_green_pedestrian_red" duration="60"`）。
4. t=20 s: `StopTrigger`でシミュレーションが終了する。

## 期待する挙動

- sim_time 1.0 s以降、`no_constraint_kind`（kind: stop）— 停止制約が一切発生しない。
- sim_time 4.0 s以降、road 3区間で`min_speed_above` 閾値4.5 m/s（赤の歩行者信号で減速・停止しない）。実測ではフィルタあり4.99 m/s、フィルタなし0.00 m/s。
- sim_time 4.0 s以降、`speed_above` 閾値4.5 m/s（巡航速度に到達している確認）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| no_constraint_kind | kind stop, after sim_time 1.0 | 停止制約が一切出ない |
| min_speed_above | threshold 4.5, road_id 3, after sim_time 4.0 | 赤の歩行者信号で減速・停止しない |
| speed_above | threshold 4.5, after sim_time 4.0 | 巡航速度に到達 |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`（policies: [traffic_light]）
- 期待値: `pedestrian_signal_ignored.expectations.yaml`
- 関連: expectations.yamlのnotesによれば、対をなす`red_stop_green_go`が同じフィルタの反対側（赤の車両灯には正しく停止すること）を検証する。
