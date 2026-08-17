# 交差点内停止の禁止（正例）

「don't block the box」交差点ガード（`JunctionStopGuard.hpp`）の判別シナリオである。
自車は、12 m街区（road 2）を挟んで12 m離れた2つの信号付きT字交差点（交差点100・交差点200）を横断する。

## 検証の狙い

交差点100の出口から3 m先にあるroad 2の赤信号（ヘッドid=1）を、交差点100の入口にあるヘッドid=0の青信号が覆い隠す配置になっている。
nearest-headルールだけでは、自車の起点がs=92を過ぎるまでid=1の赤が見えず、その時点で交差点100まで8 mしか残っていない。
ガードなしでは自車が交差点100に進入してから制動し、接続路（road 100）上に停止して交差点を塞いでしまう。
ガードありでは交差点100の手前（約5 m前）で停止し、両交差点をid=1が青になってから通過する。
本シナリオはガードが機能することを確認する正例である。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `signalized_short_block__b12.xodr`（road0-[交差点100]-road2(12m)-[交差点200]-road4。ヘッド2基とも type 1000001 車両用, orientation "+"） |
| 自車 | road 0 / lane -1 / s=10 でテレポート、ルート road0→road100→road2→road200→road4、巡航速度10.0 m/s |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 34 s で停止 |

## 進行

1. t=0 s: road 0 / lane -1 / s=10 にテレポートし、ルートを割り当て、`VirtualDriverController`を起動する。
2. t=0 s: Cruise Eventが開始し、`SpeedAction`（linear, 3.0 s）で目標速度10.0 m/sへ加速する。
3. ヘッドid=0（交差点100入口, road0 s=92）は走行時間全体を通じて青。ヘッドid=1（交差点200入口, road2 s=3）はt=0〜16sが赤（`Phase name="junction_b_red" duration="16"`）、t=16s以降が青（`Phase name="junction_b_green"`）。
4. t=34 s: `StopTrigger`でシミュレーションが終了する。

## 期待する挙動

- road 2区間で`min_speed_above` 閾値1.0 m/s（交差点100出口直後の12 m街区に停止してはならない）。expectations.yamlの実測では、ガードあり10.01 m/s、ガードなし0.00 m/s。
- road 100区間（交差点100の接続路自体）で`min_speed_above` 閾値1.0 m/s。実測ではガードあり6.23 m/s、ガードなし6.39 m/s。
- sim_time [15.4, 15.9]の範囲で`speed_below` 閾値0.3 m/s（赤の間は実際に停止していること）。
- sim_time 25.0 s以降で`speed_above` 閾値8.0 m/s（青になった後に走り出し、両交差点を抜けている）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| min_speed_above | threshold 1.0, road_id 2 | 交差点100出口直後（12m街区）に停まらない |
| min_speed_above | threshold 1.0, road_id 100 | 交差点100の接続路そのもので停止しない |
| speed_below | threshold 0.3, sim_time [15.4, 15.9] | 赤の間は実際に停止している |
| speed_above | threshold 8.0, after sim_time 25.0 | 青転後に再発進し両交差点を通過 |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`（policies: [traffic_light]。デフォルトの40 s StopTriggerを34 sで上書き）
- 期待値: `junction_not_blocked.expectations.yaml`
