# 停止線ペアリングによるSTOP標識の停止目標切り替え

stop_sign_full_stopの複製で、STOP標識に加えて停止線を表す信号(type=294)を追加した道路を使う。
停止目標が標識マージン基準から、ペアになった停止線基準へ切り替わることを検証する。

## 検証の狙い

StopYieldSignAwareの停止目標計算には、STOP標識のマージン(sign_stop_margin=3.0)を使う方式と、近傍のペア停止線信号(type=294)を使う方式がある。
道路`straight_stop_sign_stop_line.xodr`は標識(id=10, s=200)の5 m手前(s=195)に停止線(id=11, type=294, country=OpenDRIVE)を配置しており、5 mはsign_stop_margin(3.0)より2 m大きいため、両方式の停止目標位置が異なるs値になる。
標識マージン基準ならs≈197、停止線基準(ペアリング有効、既定でON)ならs≈192が目標になり、この差を`stopped_at_stop_sign`のs_rangeで判別する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_stop_sign_stop_line.xodr`(500 m直線2車線。STOP標識 id=10, type=206, country=de, s=200, t=-3.57, lane -1のみ有効。停止線信号 id=11, type=294, country=OpenDRIVE, s=195, t=0.0) |
| 自車 | road 1 / lane -1 / s=30 にテレポート。Cruiseイベントで目標速度13.889 m/sへ3.0 s(linear)で加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間 40 s 超過 |

## 進行

1. sim_time > 0でCruiseイベントが起動し、目標速度13.889 m/sへ3.0 s(linear)で加速する。
2. StopYieldSignAwareがSTOP標識(id=10, s=200)とペアの停止線(id=11, s=195)を検知し、停止線基準の停止目標(既定でペアリング有効)へ向けて減速する。
3. road 1 の s=187〜192の範囲で1.0 s以上、速度0.3 m/s以下を維持して完全停止する。
4. sim_time > 22.0 sの時点で速度3.0 m/sを超えて再加速している。
5. ルートは s=480 まで続き、sim_time > 40 sでStopTriggerが成立してシナリオが終了する。

## 期待する挙動

- 停止目標が標識マージン基準(標識s=200の3.0 m手前、s≈197)ではなく、ペアの停止線基準に切り替わり、road 1 の s=187〜192の範囲で1.0 s以上、速度0.3 m/s以下の完全停止をする。
- 完全停止後、sim_time > 22.0 sでは速度3.0 m/sを超えて再加速している。
- expectations.yamlのnotesによれば、ペアリングOFFの計測はこのバッチでは再現されず、standalone実行(ConfigFile上書き)で参考値として記録されている: road 1 s=194.995付近(停止線から3.90 m先)で停止したとされる。
- 同notesのペアリングON(この構成の既定)の実測は road 1 s=189.9948付近(停止線から1.11 m手前)で停止し、OFFとの差は約5.0002 mで設定された5 mのオフセットとほぼ一致するとされている。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| stopped_at_stop_sign | sign_id=10, road_id=1, s_range=[187, 192], min_duration=1.0, stop_speed=0.3 | 停止線ペアリングによりSTOP標識基準ではなく停止線基準の位置に完全停止 |
| speed_above | threshold=3.0, after sim_time=22.0 | 安全確認後に発進(再加速) -- stop_sign_full_stopと同型の恒常性チェック |

## 関連

- バッチ: `stop_line_pairing_batch.yaml`(policies: [stop_yield]。`car_following_traffic_control_batch.yaml`には未所属)
- 期待値: `stop_sign_stop_line_paired.expectations.yaml`
