# 隣接レーンの追い越し車両(AEB候補にも採用されない)

隣接レーンの車両が一度もカットインせずに自車を追い越して先行し続けるシナリオ。
AEBの候補採用の入口段階で正しく弾かれることを見る。

## 検証の狙い

負例(REQ-AD-013、SOTIF誤作動抑止)。
追い越し車は終始隣接レーン(-2)にとどまり、dLaneIdは常に-1(in_path=false)のまま自車を追い越す。
直線路上の横方向オフセットdt(車線幅相当、約3.07 m)はジッターの無いほぼ一定値であるため、デバウンスされた侵入キュー(AebDtHistory::kDepthの3フレームにわたる累積|dt|収縮>kEncroachMove=0.03 m)も一度も発火しない。
AebSafety::Evaluateの候補ループは「|dLaneId|<=1 かつ(in_path==true または encroaching==true)」を満たす候補が無いため毎フレーム`!best`でreturnし、衝突コースゲート(TTC/a_req)にすら到達しない。
これはbenign_cutin(検知は正当に発火しゲートだけが弾く)とは対照的な、候補採用の入口で弾かれるケースである。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、目標速度22.0 m/s(79.2 km/h) |
| 他エンティティ | 追い越し車、road1 lane-2、s=10(自車の20 m後方)、速度30.0 m/s(108 km/h) |
| 走行時間 | StopTrigger: シミュレーション時間 13 s |

## 進行

1. t=0: 自車が22.0 m/sへ加速開始(3.0s linearランプ)、追い越し車は隣接レーン(-2)を30.0 m/sの定速で走行開始。
2. 約t=(30-10)/(30-22)=2.5s: 追い越し車が自車を追い越す(レーン変更は行わない)。
3. 追い越し車はその後もレーン-2を先行し続け、レーンを変更しない。
4. t>13s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 追い越し車は一度もレーン変更しないため、AebSafetyの候補にすら採用されない。
- matcher `no_emergency_without_conflict` はPASSすること(sim_time 0.5s以降で緊急制動source:"aeb"のSTOP_AT_S制約が一度も現れない)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| no_emergency_without_conflict | after sim_time 0.5s | 一度もカットインしない隣接レーンの追い越し車両がAEB候補に採用されず緊急制動も出ないこと |

## 関連

- バッチ: `aeb_safety_batch.yaml`(gate:aeb-safety-regression)
- 期待値: `parallel_overtake.expectations.yaml`
- 関連ID: `req-vd-ad:REQ-AD-013` / `vd-func:FUNC-001`
