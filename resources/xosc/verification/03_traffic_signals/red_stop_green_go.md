# 赤で停止・青で発進

fabriksgatan_traffic_lights road 3の信号（id=1, s=109）に向けて、自車が13.889 m/sで巡航し、road 12経由でroad 1へ直進する。
信号は接近中は赤（18 s間）、t=18 sに青（40 s間）へ切り替わる。

## 検証の狙い

スクリプトによる停止指定は一切なく、TrafficLightAwareポリシー（Phase 3b, A3）が赤信号を読んで自律的に停止し、青転後に発進することを判別する。
ポリシー導入前は赤を無視して走行を続け、`stopped_at_signal`判定が失敗する。
赤の継続時間は、13.9 m/sからの快適減速が青転前（t≈14 s）に0.3 m/s以下へ収束できるだけの長さを確保している。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `fabriksgatan_traffic_lights.xodr`（信号id=1, road 3 s=109） |
| 自車 | road 3 / lane -1 / s=11 でテレポート、ルート road3→road12→road1、巡航速度13.889 m/s |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 34 s で停止 |

## 進行

1. t=0 s: road 3 / lane -1 / s=11 にテレポートし、ルートを割り当て、`VirtualDriverController`を起動する。
2. t=0 s: Cruise Eventが開始し、`SpeedAction`（linear, 3.0 s）で目標速度13.889 m/sへ加速する。
3. 信号id=1はt=0〜18 sが赤（`Phase name="red" duration="18"`）、t=18 s以降が青（`Phase name="green" duration="40"`）。
4. t=34 s: `StopTrigger`でシミュレーションが終了する。

## 期待する挙動

- road 3 s=[96, 110]の区間で、赤信号下（require_red=true）、最低1.0 s以上、速度0.3 m/s以下で完全停止する（`stopped_at_signal`）。赤はt=13 s頃まで続く。
- sim_time 20.0 s以降、`speed_above` 閾値5.0 m/s（青（t=18 s以降）に変わって発進）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| stopped_at_signal | signal_id 1, road_id 3, s_range [96,110], min_duration 1.0, stop_speed 0.3, require_red true | 赤信号で停止線手前に完全停止 |
| speed_above | threshold 5.0, after sim_time 20.0 | 青に変わって発進 |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`（policies: [traffic_light]。osi:trueでrequire_red判定に必要な信号相をキャプチャする）
- 期待値: `red_stop_green_go.expectations.yaml`
