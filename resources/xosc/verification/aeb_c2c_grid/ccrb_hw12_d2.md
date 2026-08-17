# AEB c2c CCRb 車間12m・先行車減速2m/s^2

直線路で自車・先行車ともに50km/hで発進し、同一車線を同速度で走行する。
t=2.0sから先行車が減速度2m/s^2で停止まで減速する。
AEB（自動緊急ブレーキ）の刺激探索セルの1つで、CCRb（Car-to-Car Rear braking）系統。

## 検証の狙い

Euro NCAP AEB car-to-car試験の古典的な3系統（CCRs/CCRm/CCRb）のうち、
先行車が走行中に制動するケースを、初期車間と先行車の減速度の2軸で振ったグリッドの1セル。
発進時点では自車・先行車が同速度で閉じ込め速度が0のため、
車間と先行車の減速度そのものがAEB挙動を左右する条件を、
車間の狭い側（12m）と減速の弱い側（2m/s^2）の組み合わせで確認する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`（road id=1, lane -1の直線路） |
| 自車 | road 1 / lane -1 / s=30.0、初期速度50km/h（13.8889m/s）、VirtualDriverController（lateral/longitudinal）有効、ルートはs=30.0→s=480.0 |
| 他エンティティ | 先行車（Lead）: road 1 / lane -1 / s=47.0、初期速度50km/h（13.8889m/s）、コントローラなし。t=2.0s以降、減速度2.0m/s^2（linear rate）で速度0まで減速するManeuver（優先度overwrite）を実行 |
| 走行時間 | StopTrigger: シミュレーション時間 11.9 s 超過 |

初期車間（バンパー間）はhw軸の値をそのまま与える。closing speed=0（自車・先行車とも同速度で発進）のため、
TTC0ベースの `gap = max(40.0, closing_speed × 6.0)` は定義できず、このセルでは適用しない。
hw=12.0 mに車長5.0 mを加えた17.0 mが車両原点間距離で、自車s=30.0に対し先行車s=47.0（30.0+12.0+5.0）となる。

## 進行

1. t=0: 自車・先行車ともに50km/hで発進する。自車はVirtualDriverControllerがルート追従を開始する。
2. t=2.0s: 先行車が減速度2.0m/s^2の一定減速（linear rate）で速度0まで減速するManeuverを開始する。
3. 先行車が減速するにつれ、自車と先行車の相対速度が生じ、AEBポリシーの介入が期待される領域に入る可能性がある。
4. t=11.9s超過でStopTriggerが成立し、シナリオが終了する。

## 期待する挙動

このバッチは合否判定用ではない。
`aeb_c2c_grid_batch.yaml` は expectations もbaselineも持たない探索スイープ層であり、
CIの回帰ゲートには配線されていない。
合否判定（AEB介入の有無・タイミングの当落線）は別バッチ `aeb_safety_batch.yaml` が担う。
本シナリオで得られるのは、初期車間・先行車減速度を振ったときのAEB挙動（介入タイミング、減速プロファイルなど）の刺激探索データである。

## 関連

- バッチ: `aeb_c2c_grid_batch.yaml`（policies: lead, aeb。回帰ゲート/CI非配線の探索スイープ層）
- 関連ID: `req-vd-ad:REQ-AD-010`, `req-vd-ad:REQ-AD-011`
