# AEB c2c CCRs 自車70km/h vs 停止先行車

直線路で自車が70km/hから、同一車線上に停止した先行車へ接近する。
AEB（自動緊急ブレーキ）の刺激探索セルの1つで、CCRs（Car-to-Car Rear stationary）系統の最高速度側。

## 検証の狙い

Euro NCAP AEB car-to-car試験の古典的な3系統（CCRs/CCRm/CCRb）のうち、
先行車が完全停止しているケースを自車速度別に並べたグリッドの1セル。
自車速度が上がるほど衝突回避に要する減速要求が厳しくなる方向を、
CCRs系統の高速端（70km/h）で確認する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`（road id=1, lane -1の直線路） |
| 自車 | road 1 / lane -1 / s=30.0、初期速度70km/h（19.4444m/s）、VirtualDriverController（lateral/longitudinal）有効、ルートはs=30.0→s=480.0 |
| 他エンティティ | 先行車（Lead）: road 1 / lane -1 / s=151.667、速度0km/h（停止）、コントローラなし |
| 走行時間 | StopTrigger: シミュレーション時間 13.9 s 超過 |

初期車間（バンパー間）は `gap = max(40.0, closing_speed × 6.0)` で決まる。
自車速度70km/hでの closing_speed（=自車速度、先行車は停止のため）は19.4444 m/s、
`19.4444 × 6.0 = 116.667` m が下限の40.0 mを上回るため、gap=116.667 mとなる（TTC0=6.0sちょうど）。
車両原点間距離はこれに車長5.0 mを加えた121.667 mで、自車s=30.0に対し先行車s=151.667（30.0+116.667+5.0）となる。

## 進行

1. t=0: 自車は70km/hで走行開始、VirtualDriverControllerがルート追従を開始する。
2. 先行車は速度0km/hのまま静止し続ける。
3. 自車が先行車に接近するにつれ、AEBポリシーの介入が期待される領域に入る。
4. t=13.9s超過でStopTriggerが成立し、シナリオが終了する。

## 期待する挙動

このバッチは合否判定用ではない。
`aeb_c2c_grid_batch.yaml` は expectations もbaselineも持たない探索スイープ層であり、
CIの回帰ゲートには配線されていない。
合否判定（AEB介入の有無・タイミングの当落線）は別バッチ `aeb_safety_batch.yaml` が担う。
本シナリオで得られるのは、自車速度・先行車状態を振ったときのAEB挙動（介入タイミング、減速プロファイルなど）の刺激探索データである。

## 関連

- バッチ: `aeb_c2c_grid_batch.yaml`（policies: lead, aeb。回帰ゲート/CI非配線の探索スイープ層）
- 関連ID: `req-vd-ad:REQ-AD-010`, `req-vd-ad:REQ-AD-011`
