# 通常の同一レーン追従(AEB誤作動なし)

先行車がカットインも急制動も行わず、自車と同一レーンを緩やかな速度差で走り続けるシナリオ。
衝突コースが存在しない通常追従でAEBが誤発火しないことを見る。

## 検証の狙い

負例(REQ-AD-013、SOTIF誤作動抑止)。
正例(cutin_hard_brake系、REQ-AD-001)のSOTIFミラーにあたる。
先行車は最初から自車と同一レーン(dLaneId=0)にいるため、LeadVehicleAware(lead policy)は通常のコンフォート追従としてフレーム1から処理する。
AebSafetyも同じ候補を即座に採用する(in_path==true、横侵入デバウンス不要)が、衝突コースゲート(TTC<aeb_ttc_threshold=2.5s かつ a_req>aeb_min_a_req=3.0 m/s²)は非発火のままでなければならない。
AEBが弾かれる段階は候補採用ではなくゲートであり、これはbenign_cutin(検知は発火するがゲートで弾く)と共通し、parallel_overtake(候補採用の入口で弾く)とは異なる。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、目標速度25.0 m/s(90 km/h) |
| 他エンティティ | 先行車、road1 lane-1(同一レーン)、s=85(バンパー間ギャップ約50 m)、速度23.0 m/s(82.8 km/h) |
| 走行時間 | StopTrigger: シミュレーション時間 13 s |

## 進行

1. t=0: 自車が25.0 m/sへ加速開始(3.0s linearランプ)、先行車は同一レーンを23.0 m/sの定速で先行。
2. その後、追加のマヌーバは無く両車は速度差2.0 m/sのまま接近を続ける。
3. t>13s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 無制御(constant speed、最悪ケース想定)でもt=13s(StopTrigger)時点でTTC=12.0s(閾値2.5sの4.8倍)、a_req=0.083 m/s²(閾値3.0 m/s²の1/36)であり、発火域から大きく安全側。
- matcher `no_emergency_without_conflict` はPASSすること(sim_time 0.5s以降で緊急制動source:"aeb"のSTOP_AT_S制約が一度も現れない)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| no_emergency_without_conflict | after sim_time 0.5s | 衝突コースの無い通常追従でAEBの緊急制動が誤発火しないこと |

## 関連

- バッチ: `aeb_safety_batch.yaml`(gate:aeb-safety-regression)
- 期待値: `normal_following.expectations.yaml`
- 関連ID: `req-vd-ad:REQ-AD-013` / `vd-func:FUNC-001`
