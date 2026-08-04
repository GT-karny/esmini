# 青信号での通過（負例）

自車がfabriksgatan_traffic_lights道路の信号（road 3, s=109）を、走行時間全体を通じて青のまま通過する。
TrafficLightAwareポリシーが青信号で不要に減速・停止しないことを確認する負例シナリオである。

## 検証の狙い

TrafficLightAwareポリシー（Phase 3b, A3）が過検知し、青信号でブレーキをかけてしまう不具合を防ぐ判別シナリオである。
road 3区間の`min_speed_above`で減速の有無を判定する。
ポリシー導入前（no policy）でも合格するため、この1本が緑であることはポリシーが動いている証拠にはならない。
ポリシーの存在を要求する側の対は`red_stop_green_go`であり、2本を合わせて読む必要がある。
expectations.yamlの記述によれば、閾値は当初の8.0から12.0へ引き上げられている。
これは、TrafficLightAwareが非車両ヘッド（歩行者用信号）をフィルタする前は、road 3 s=114の赤い歩行者ヘッドがブレーキを誘発し最小速度が9.17 m/sまで落ちていたためである。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `fabriksgatan_traffic_lights.xodr`（信号は road 3 s=109、road 12経由で road 1へ） |
| 自車 | road 3 / lane -1 / s=11 でテレポート、VirtualDriverController起動、ルート road3→road12→road1、巡航速度13.889 m/s |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 20 s で停止 |

## 進行

1. t=0 s: road 3 / lane -1 / s=11 にテレポートし、ルート（road3 s=11→road12 s=7.75→road1 s=12）を割り当て、`VirtualDriverController`を起動する。
2. t=0 s: Cruise Eventが開始し、`SpeedAction`（linear, 3.0 s）で目標速度13.889 m/sへ加速する。
3. 信号（id=1, road3 s=109）は`Phase name="green" duration="60"`により走行時間全体を通じて青である。
4. t=20 s: `StopTrigger`でシミュレーションが終了する。

## 期待する挙動

- sim_time 5.0 s以降、road 3区間で`min_speed_above` 閾値12.0 m/s（青信号では減速・停止しない）。フィルタ適用後の実測最小速度は13.98 m/s。
- sim_time 5.0 s以降、`speed_above` 閾値12.0 m/s（巡航速度に到達している）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| min_speed_above | threshold 12.0, road_id 3, after sim_time 5.0 | 青信号区間で巡航維持（減速・停止しない） |
| speed_above | threshold 12.0, after sim_time 5.0 | 巡航速度に到達 |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`（policies: [traffic_light]）
- 期待値: `green_no_stop.expectations.yaml`
- 関連: `pedestrian_signal_ignored`が、本シナリオの巡航速度では捉えきれない歩行者ヘッド誤反応（ブレーキの一時的な落ち込み）を、低速走行下で完全停止として顕在化させる専用の判別シナリオである（expectations.yamlのnotesに記載）。
