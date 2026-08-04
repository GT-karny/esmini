# 直進定速走行

自車1台が直線500m道路を20 m/sの定速で走行する。
Defaultコントローラのベースラインに対して、VirtualDriverの縦方向制御を検証するための決定論的な基準シナリオである。

## 検証の狙い

交通・カタログ・目標物のいずれも存在しない、経路にも依存しない(path-robust)最小構成で、速度の到達・維持だけを切り分ける。
横方向アクションが一切ないため、レーン維持が崩れれば縦方向制御以外の要因(横方向制御・ルーティング)が疑われる。
Step 3ではこの実行をDefaultコントローラのベースライン軌跡とXY/速度RMSEで比較する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m.xodr`（直線500m） |
| 自車 | road 1 / lane -1 / s=50 でテレポート、目標速度20 m/s（`EgoSpeed`、stepダイナミクスで即時反映） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 30 s で停止 |

## 進行

1. t=0 s: road 1 / lane -1 / s=50 にテレポートし、`SpeedAction`（`dynamicsShape="step"`, `value="0.0"`）で目標速度20 m/sを即時設定する。Initのみで速度指定が完結し、Story側に追加のManeuver/Eventはない。
2. t=30 s: `StopTrigger`（`SimulationTimeCondition` > 30）でシミュレーションが終了する。

## 期待する挙動

- sim_time 2.0 s以降、`speed_above` 閾値19.0 m/s（コマンドされた20 m/sに到達・維持する）。
- sim_time 2.0 s以降、`speed_below` 閾値21.0 m/s（オーバーシュートしない）。
- `lane_keep`: road 1 / lane -1 を維持する（横方向アクションがないため）。
- baseline: controller `Default`、source `results/baselines/straight_constant_speed/groundtruth.osi`。Step 3でVirtualDriver実行との比較に使う。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| speed_above | threshold 19.0, after sim_time 2.0 | コマンド速度20 m/sに到達・維持 |
| speed_below | threshold 21.0, after sim_time 2.0 | オーバーシュートなし |
| lane_keep | lane_id -1, road_id 1 | 開始レーンを維持 |

## 関連

- バッチ: 常設バッチ（`car_following_traffic_control_batch.yaml` / `stop_line_pairing_batch.yaml`）には未所属。個別実行用。
- 期待値: `straight_constant_speed.expectations.yaml`
