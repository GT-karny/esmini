# STOP標識での完全停止

自車がSTOP標識のある直線道路を巡航速度まで加速し、標識の手前で完全停止してから再発進する挙動を検証する。
停止はストーリー側にスクリプトされておらず、VirtualDriverが自律的に判断する。

## 検証の狙い

StopYieldSignAwareポリシー(Phase 3c, A3)がSTOP標識を認識し、停止線手前で確実に停止させることを確認する。
STOP標識はxodr上の静的な標識であり、OSIのトラッキングは不要(ego-kinematicのみで判定できる)。
Phase 3c導入前はVirtualDriverに交通標識ポリシーが存在せず、自車は標識を素通りしてしまい`stopped_at_stop_sign`が不成立(FAIL)になる。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_stop_sign.xodr`(500 m直線2車線。STOP標識 id=10, type=206, country=de, s=200, t=-3.57, lane -1のみ有効) |
| 自車 | road 1 / lane -1 / s=30 にテレポート。Cruiseイベントで目標速度13.889 m/sへ3.0 s(linear)で加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間 40 s 超過 |

## 進行

1. sim_time > 0でCruiseイベントが起動し、SpeedActionにより目標速度13.889 m/sへ3.0 s(linear)で加速する。
2. StopYieldSignAwareが自車ルート上のSTOP標識(id=10, s=200)を検知し、停止線手前で減速する。
3. road 1 の s=190〜201の範囲で1.0 s以上、速度0.3 m/s以下を維持して完全停止する。
4. sim_time > 22.0 sの時点で速度3.0 m/sを超えて再加速している。
5. ルートは s=480 まで続き、sim_time > 40 sでStopTriggerが成立してシナリオが終了する。

## 期待する挙動

- STOP標識の停止線(road 1, s=190〜201)で1.0 s以上、速度0.3 m/s以下の完全停止をする。
- 完全停止後、sim_time > 22.0 sでは速度3.0 m/sを超えて再加速している。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| stopped_at_stop_sign | sign_id=10, road_id=1, s_range=[190, 201], min_duration=1.0, stop_speed=0.3 | STOP標識の停止線で完全停止 |
| speed_above | threshold=3.0, after sim_time=22.0 | 安全確認後に発進(再加速) |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`(policies: [stop_yield])
- 期待値: `stop_sign_full_stop.expectations.yaml`
