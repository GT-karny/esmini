# 停止線ペアリング（信号ヘッド近接側）

停止線ペアリング機能（design/stop_line_stop_target.md）のTrafficLightAware側の判別シナリオである。
生成道路`signalized_short_block__b12_hs4_sl8`（head-setback=4, stop-line-setback-a=8）を使い、road 0がジャンクションAのヘッド（id=0, s=96）と、対応する停止線信号（type 294, country OpenDRIVE, id=2, s=92）を持つ。
信号id=0は走行時間全体を通じて赤である。

## 検証の狙い

TrafficLightAware側の停止線ペアリングが、信号ヘッドのマージン基準ではなく、対応する停止線の位置基準に自車の停止目標を切り替えることを判別する。
停止線（s=92）はヘッド（s=96）より手前にあり、min(交差点入口距離, ヘッド距離)アンカーはヘッド側を選ぶ（near-side配置）。
ヘッド基準ではroad 0 s≈91付近、停止線基準ではroad 0 s≈87付近に停止し、両者は約4 m離れる（expectations.yamlのnotes記載の実測値）。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `signalized_short_block__b12_hs4_sl8.xodr`（road 0: ジャンクションAヘッドid=0 s=96、停止線信号id=2 s=92） |
| 自車 | road 0 / lane -1 / s=10 でテレポート、ルート road0→road100→road2、巡航速度13.889 m/s |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 25 s で停止 |

## 進行

1. t=0 s: road 0 / lane -1 / s=10 にテレポートし、ルートを割り当て、`VirtualDriverController`を起動する。
2. t=0 s: Cruise Eventが開始し、`SpeedAction`（linear, 3.0 s）で目標速度13.889 m/sへ加速する。
3. 信号id=0は`Phase name="red" duration="60"`により走行時間全体を通じて赤。信号id=1（road 2, ジャンクションB）も同時に赤だが、自車はそこまで到達しない。
4. t=25 s: `StopTrigger`でシミュレーションが終了する。

## 期待する挙動

- road 0 s=[84, 89]の区間で、赤信号下（require_red=true）、最低2.0 s以上、速度0.3 m/s以下で完全停止する（`stopped_at_signal`）。停止線ペアリングにより、ヘッド基準ではなく停止線基準の位置に停止する。
- sim_time [18.0, 25.0]の区間で`speed_below` 閾値0.5 m/s（赤が続く間は停止を維持）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| stopped_at_signal | signal_id 0, road_id 0, s_range [84,89], min_duration 2.0, stop_speed 0.3, require_red true | 停止線ペアリングにより停止線基準の位置に完全停止 |
| speed_below | threshold 0.5, sim_time [18.0, 25.0] | 赤が続く間は停止を維持 |

## 関連

- バッチ: `stop_line_pairing_batch.yaml`（policies: [traffic_light]）。`car_following_traffic_control_batch.yaml`には未所属。
- 期待値: `red_hold_stop_line_paired.expectations.yaml`
- 関連: `GT_esmini/test/regression_baseline/stop_line_pairing_expected.yaml`に本シナリオを含む3件のベースラインが登録されている（全件pass）。
