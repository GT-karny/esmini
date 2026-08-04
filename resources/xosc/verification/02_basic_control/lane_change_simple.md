# 単純な車線変更

自車1台が直線道路上を走行中、t=3 sに`LaneChangeAction`（sinusoidal, 4.0 s）で1回だけ車線変更する。
Defaultコントローラのベースラインに対して、VirtualDriverの横方向追従を検証するシナリオである。

## 検証の狙い

車線変更の発火前は開始レーンを維持しているか、車線変更がちょうど1回だけ実行されるか、変更完了後は目標レーンに収束しているかの3点を切り分ける。
交通・カタログはなく、横方向制御の単体挙動に絞った最小構成である。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m.xodr`（直線500m） |
| 自車 | road 1 / lane -1 / s=20 でテレポート、目標速度15 m/s（`EgoSpeed`、stepダイナミクスで即時反映） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 15 s で停止 |

## 進行

1. t=0 s: road 1 / lane -1 / s=20 にテレポートし、`SpeedAction`（step, 0.0 s）で目標速度15 m/sを即時設定する。
2. t=3.0 s: `LaneChangeAction`（`dynamicsShape="sinusoidal"`, `value="4.0"`, 目標レーン絶対値1）が開始する。
3. t=15 s: `StopTrigger`（`SimulationTimeCondition` > 15）でシミュレーションが終了する。

## 期待する挙動

- sim_time 3.0 sより前は`lane_keep`: road 1 / lane -1 を維持する（変更発火前）。
- `lane_change_count` は1（車線変更はちょうど1回）。
- sim_time 9.0 sより後は`lane_keep`: road 1 / lane 1 に収束する（サイン波変更完了後）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| lane_keep | lane_id -1, road_id 1, before sim_time 3.0 | 変更発火前は開始レーンを維持 |
| lane_change_count | count 1 | 車線変更はちょうど1回 |
| lane_keep | lane_id 1, road_id 1, after sim_time 9.0 | 変更完了後、目標レーンに収束 |

## 関連

- バッチ: 常設バッチ（`car_following_traffic_control_batch.yaml` / `stop_line_pairing_batch.yaml`）には未所属。個別実行用。
- 期待値: `lane_change_simple.expectations.yaml`
- `GT_esmini/test/comparison_thresholds.yaml` に同名 `lane_change_simple` のオーバーライドエントリがある。PythonDriverController比較用の凍結ツール向け閾値と考えられ、本シナリオの `gt_sim_test` 判定（`expectations.yaml`）には使われない。
