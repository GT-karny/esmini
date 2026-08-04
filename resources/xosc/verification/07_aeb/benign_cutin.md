# 大ギャップ・無制動のカットイン(AEBゲート拒否)

先行車がEgoと同一のカットイン幾何(cutin_hard_brake.xoscと同一)でEgoレーンへ進入するが、急制動を伴わず縦方向ギャップも大きいシナリオ。
AEBの衝突コースゲートが正しく非発火のままであることを見る。

## 検証の狙い

負例(REQ-AD-013、SOTIF誤作動抑止)。
このバッチで最も強いSOTIF誤作動テストと位置づけられている。
cutin_hard_brake.xoscと全く同じカットイン幾何(CutInTime=1.6s、CutInDur=1.4s、sinusoidal、RelativeTargetLane value=0)を使うため、AebSafetyの横侵入キュー(累積|dt|収縮の検知)は正当に発火し、候補として採用される。
つまり検知は正しく動く。
しかし急制動が無く縦方向ギャップが巨大(LeadStartS=140、v_close=EgoSpeed-LeadSpeed=2.0 m/s)なため、衝突コースゲート(TTC<aeb_ttc_threshold=2.5s かつ a_req>aeb_min_a_req=3.0 m/s²)は非発火のままでなければならない。
このテストがREDになった場合、侵入キューが緊急経路を過剰発火させている、すなわちゲートが検知から分離されていないことを意味する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、目標速度25.0 m/s(90 km/h) |
| 他エンティティ | 先行車、road1 lane-2、s=140(バンパー間ギャップ約105 m)、速度23.0 m/s(82.8 km/h) |
| 走行時間 | StopTrigger: シミュレーション時間 14 s |

## 進行

1. t=0: 自車が25.0 m/sへ、先行車が隣接レーン(-2)で23.0 m/sへそれぞれ加速/定速開始。
2. t>1.6s: 先行車が1.4s間のsinusoidal LaneChangeActionで自車レーンへカットインを開始(完了は約t=3.0s)。
3. 先行車はその後も減速せず23.0 m/sで走行を続ける。
4. t>14s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 無制御(constant speed、最悪ケース想定)でもt=14s(StopTrigger)時点でTTC=38.5s(閾値2.5sの15.4倍)、a_req=0.026 m/s²(閾値3.0 m/s²の1/115)であり、ゲートは発火域から大きく安全側。
- matcher `no_emergency_without_conflict` はPASSすること(sim_time 0.5s以降で緊急制動source:"aeb"のSTOP_AT_S制約が一度も現れない)。
- matcher `min_obb_separation_above` は接触に近づかないことの補助サニティチェック(閾値20.0 m、計算上の最小分離~77 mに対する安全マージン)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| no_emergency_without_conflict | after sim_time 0.5s | AEBの緊急制動(source:"aeb")が一度も出ないこと |
| min_obb_separation_above | threshold 20.0 m, after sim_time 0.5s | 検知が正当でも実際には接触へ近づかないことの確認 |

## 関連

- バッチ: `aeb_safety_batch.yaml`(gate:aeb-safety-regression)
- 期待値: `benign_cutin.expectations.yaml`
- 関連ID: `req-vd-ad:REQ-AD-013` / `vd-func:FUNC-001`
