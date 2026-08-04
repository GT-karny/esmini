# AEB c2c CCRs 自車40km/h vs 停止先行車

直線路で自車が40km/hから、同一車線上に停止した先行車へ接近する。
AEB（自動緊急ブレーキ）の刺激探索セルの1つで、CCRs（Car-to-Car Rear stationary）系統。

## 検証の狙い

Euro NCAP AEB car-to-car試験の古典的な3系統（CCRs/CCRm/CCRb）のうち、
先行車が完全停止しているケースを自車速度別に並べたグリッドの1セル。
自車速度が上がるほど衝突回避に要する減速要求が厳しくなる方向を、
CCRs系統の速度軸上で確認する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`（road id=1, lane -1の直線路） |
| 自車 | road 1 / lane -1 / s=30.0、初期速度40km/h（11.1111m/s）、VirtualDriverController（lateral/longitudinal）有効、ルートはs=30.0→s=480.0 |
| 他エンティティ | 先行車（Lead）: road 1 / lane -1 / s=101.667、速度0km/h（停止）、コントローラなし |
| 走行時間 | StopTrigger: シミュレーション時間 11.8 s 超過 |

初期車間（バンパー間）は `gap = max(40.0, closing_speed × 6.0)` で決まる。
自車速度40km/hでの closing_speed（=自車速度、先行車は停止のため）は11.1111 m/s、
`11.1111 × 6.0 = 66.667` m が下限の40.0 mを上回るため、gap=66.667 mとなる（TTC0=6.0sちょうど）。
車両原点間距離はこれに車長5.0 mを加えた71.667 mで、自車s=30.0に対し先行車s=101.667（30.0+66.667+5.0）となる。

## 進行

1. t=0: 自車は40km/hで走行開始、VirtualDriverControllerがルート追従を開始する。
2. 先行車は速度0km/hのまま静止し続ける。
3. 自車が先行車に接近するにつれ、AEBポリシーの介入が期待される領域に入る。
4. t=11.8s超過でStopTriggerが成立し、シナリオが終了する。

## 期待する挙動

このバッチは合否判定用ではない。
`aeb_c2c_grid_batch.yaml` は expectations もbaselineも持たない探索スイープ層であり、
CIの回帰ゲートには配線されていない。
合否判定（AEB介入の有無・タイミングの当落線）は別バッチ `aeb_safety_batch.yaml` が担う。
本シナリオで得られるのは、自車速度・先行車状態を振ったときのAEB挙動（介入タイミング、減速プロファイルなど）の刺激探索データである。

## 関連

- バッチ: `aeb_c2c_grid_batch.yaml`（policies: lead, aeb。回帰ゲート/CI非配線の探索スイープ層）
- 関連ID: `req-vd-ad:REQ-AD-010`, `req-vd-ad:REQ-AD-011`
