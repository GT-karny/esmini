# 黄信号での停止判断

fabriksgatan_traffic_lights road 3の信号（id=1, s=109）が青4 s→黄8 s→赤（以降）と切り替わる。
黄への切り替わり時点で、自車は停止線からおよそ68 m手前におり、制動距離を超えるためTrafficLightAwareポリシーは停止を選ぶ。

## 検証の狙い

スクリプトによる停止指定は一切なく、TrafficLightAwareポリシー（Phase 3b, A3）が黄信号を見て「安全に停止できる余裕がある場合のみ止まる」判断を下すことを判別する。
ポリシー導入前は赤同様に信号を無視して走行を続け、`stopped_at_signal`判定が失敗する。
停止開始が黄の間に始まってよいため、require_redは緩和（false）されている。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `fabriksgatan_traffic_lights.xodr`（信号id=1, road 3 s=109） |
| 自車 | road 3 / lane -1 / s=11 でテレポート、ルート road3→road12→road1、巡航速度13.889 m/s |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 25 s で停止 |

## 進行

1. t=0 s: road 3 / lane -1 / s=11 にテレポートし、ルートを割り当て、`VirtualDriverController`を起動する。
2. t=0 s: Cruise Eventが開始し、`SpeedAction`（linear, 3.0 s）で目標速度13.889 m/sへ加速する。
3. 信号id=1はt=0〜4 sが青（`Phase name="green" duration="4"`）、t=4〜12 sが黄（`Phase name="yellow" duration="8"`）、t=12 s以降が赤（`Phase name="red" duration="40"`）。
4. t=25 s: `StopTrigger`でシミュレーションが終了する。

## 期待する挙動

- road 3 s=[96, 110]の区間で、最低1.0 s以上、速度0.3 m/s以下で完全停止する（`stopped_at_signal`）。require_redはfalseで、停止開始が黄の間でもよい。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| stopped_at_signal | signal_id 1, road_id 3, s_range [96,110], min_duration 1.0, stop_speed 0.3, require_red false | 黄信号で停止判断し停止線手前に停止 |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`（policies: [traffic_light]）
- 期待値: `yellow_decision_stop.expectations.yaml`
