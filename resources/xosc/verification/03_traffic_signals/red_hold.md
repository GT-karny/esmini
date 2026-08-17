# 赤信号での停止維持

fabriksgatan_traffic_lights road 3の信号（id=1, s=109）が走行時間全体を通じて赤のままである。
自車はこれに向かって巡航し、停止線手前で停止し、そのまま停止を維持する。

## 検証の狙い

スクリプトによる停止指定は一切なく、TrafficLightAwareポリシー（Phase 3b, A3）が赤信号だけを根拠に自律的に停止・維持できることを判別する。
ポリシー導入前は赤信号を無視して走行を継続し、`stopped_at_signal`判定は失敗する。

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
3. 信号id=1は`Phase name="red" duration="60"`により走行時間全体を通じて赤。
4. t=25 s: `StopTrigger`でシミュレーションが終了する。

## 期待する挙動

- road 3 s=[96, 110]の区間で、赤信号下（require_red=true）、最低2.0 s以上、速度0.3 m/s以下で完全停止する（`stopped_at_signal`）。
- sim_time [18.0, 25.0]の区間で`speed_below` 閾値0.5 m/s（緑が来ないので発進しない）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| stopped_at_signal | signal_id 1, road_id 3, s_range [96,110], min_duration 2.0, stop_speed 0.3, require_red true | 終始赤で停止線手前に完全停止 |
| speed_below | threshold 0.5, sim_time [18.0, 25.0] | 緑が来ないので停止を維持 |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`（policies: [traffic_light]）
- 期待値: `red_hold.expectations.yaml`
