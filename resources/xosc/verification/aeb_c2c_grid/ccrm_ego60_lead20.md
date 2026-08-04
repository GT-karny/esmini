# AEB c2c CCRm 自車60km/h vs 先行車20km/h（定速走行）

直線路で自車が60km/hから、同一車線上を20km/h一定で走行する先行車へ接近する。
AEB（自動緊急ブレーキ）の刺激探索セルの1つで、CCRm（Car-to-Car Rear moving）系統。

## 検証の狙い

Euro NCAP AEB car-to-car試験の古典的な3系統（CCRs/CCRm/CCRb）のうち、
先行車が一定速度（20km/h）で走行し続けるケースを自車速度別に並べたグリッドの1セル。
先行車が停止していないCCRs系統との対比で、閉じ込め速度（closing speed）が
自車速度と先行車速度の差で決まる状況でのAEB挙動を、CCRm系統の速度軸上で確認する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`（road id=1, lane -1の直線路） |
| 自車 | road 1 / lane -1 / s=30.0、初期速度60km/h（16.6667m/s）、VirtualDriverController（lateral/longitudinal）有効、ルートはs=30.0→s=480.0 |
| 他エンティティ | 先行車（Lead）: road 1 / lane -1 / s=101.667、速度20km/h（5.5556m/s、定速）、コントローラなし |
| 走行時間 | StopTrigger: シミュレーション時間 13.2 s 超過 |

初期車間（バンパー間）は `gap = max(40.0, closing_speed × 6.0)` で決まる。
closing_speed（=自車速度−先行車速度）は16.6667−5.5556=11.1111 m/s、
`11.1111 × 6.0 = 66.667` m が下限の40.0 mを上回るため、gap=66.667 mとなる（TTC0=6.0sちょうど）。
車両原点間距離はこれに車長5.0 mを加えた71.667 mで、自車s=30.0に対し先行車s=101.667（30.0+66.667+5.0）となる。

## 進行

1. t=0: 自車は60km/hで走行開始、VirtualDriverControllerがルート追従を開始する。
2. 先行車は20km/hの定速走行を続ける。
3. 自車と先行車の速度差（closing speed）に応じて自車が接近する。
4. t=13.2s超過でStopTriggerが成立し、シナリオが終了する。

## 期待する挙動

このバッチは合否判定用ではない。
`aeb_c2c_grid_batch.yaml` は expectations もbaselineも持たない探索スイープ層であり、
CIの回帰ゲートには配線されていない。
合否判定（AEB介入の有無・タイミングの当落線）は別バッチ `aeb_safety_batch.yaml` が担う。
本シナリオで得られるのは、自車速度・先行車速度差を振ったときのAEB挙動（介入タイミング、減速プロファイルなど）の刺激探索データである。

## 関連

- バッチ: `aeb_c2c_grid_batch.yaml`（policies: lead, aeb。回帰ゲート/CI非配線の探索スイープ層）
- 関連ID: `req-vd-ad:REQ-AD-010`, `req-vd-ad:REQ-AD-011`
