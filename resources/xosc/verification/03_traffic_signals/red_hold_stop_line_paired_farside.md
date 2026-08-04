# 停止線ペアリング（信号ヘッド遠方側）

停止線ペアリング機能のfar-side判別シナリオである。
生成道路`signalized_short_block__b12_sl8_fsa1`（stop-line-setback-a=8, head-farside-offset-a=1.0）を使う。
road 0は対応する停止線信号（type 294, country OpenDRIVE, id=2, s=92）のみを持ち、ジャンクションAを制御するヘッド（id=0, "light_junction_a_farside"）はroad 2（ジャンクションA出口側の短い街区）のs=1.0に、OpenDRIVEコントローラ経由で配置される。
信号id=0は走行時間全体を通じて赤である。

## 検証の狙い

ヘッドがジャンクションの向こう側（far side）に設置される配置では、ヘッド基準アンカーだけでは停止線を正しくペアリングできない。
min(交差点入口距離, ヘッド距離)アンカーが交差点入口を選ぶことで、停止線（road 0 s=92）へのペアリングが成立することを判別する。
本シナリオは、ジャンクションAを経由してしか到達できない停止線信号を正しい位置で捉えられることを確認する正例である。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `signalized_short_block__b12_sl8_fsa1.xodr`（road 0: 停止線信号id=2 s=92のみ。ヘッドid=0はroad 2 s=1.0からコントローラ経由でジャンクションAを制御） |
| 自車 | road 0 / lane -1 / s=10 でテレポート、ルート road0→road100→road2、巡航速度10.0 m/s |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 35 s で停止 |

## 進行

1. t=0 s: road 0 / lane -1 / s=10 にテレポートし、ルートを割り当て、`VirtualDriverController`を起動する。
2. t=0 s: Cruise Eventが開始し、`SpeedAction`（linear, 3.0 s）で目標速度10.0 m/sへ加速する。
3. 信号id=0は`Phase name="red" duration="60"`により走行時間全体を通じて赤。信号id=1（road 2, light_junction_b）は走行時間全体を通じて青。
4. t=35 s: `StopTrigger`でシミュレーションが終了する。

## 期待する挙動

- road 0 s=[84, 89]の区間で、赤信号下（require_red=true）、最低2.0 s以上、速度0.3 m/s以下で完全停止する（`stopped_at_signal`）。min(交差点入口, ヘッド)アンカーが入口を選び、停止線基準の位置に停止する。
- sim_time [20.0, 35.0]の区間で`speed_below` 閾値0.5 m/s（赤が続く間は停止を維持）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| stopped_at_signal | signal_id 0, road_id 0, s_range [84,89], min_duration 2.0, stop_speed 0.3, require_red true | far-sideヘッドでも交差点入口アンカーにより停止線基準の位置に完全停止 |
| speed_below | threshold 0.5, sim_time [20.0, 35.0] | 赤が続く間は停止を維持 |

## 関連

- バッチ: `stop_line_pairing_batch.yaml`（policies: [traffic_light]）。`car_following_traffic_control_batch.yaml`には未所属。
- 期待値: `red_hold_stop_line_paired_farside.expectations.yaml`
- 関連: `GT_esmini/test/regression_baseline/stop_line_pairing_expected.yaml`に本シナリオを含む3件のベースラインが登録されている（全件pass）。この判別のために、アンカー方式がヘッド基準からジャンクション入口基準を含むmin()方式へ再設計された経緯がxosc内コメントに記載されている。
