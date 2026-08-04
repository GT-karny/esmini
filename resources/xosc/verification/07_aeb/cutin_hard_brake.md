# カットイン後の急制動への追突抑止(AEB, 衝突速度低減)

先行車が隣接レーンから自車レーンへカットインした直後に急制動する複合シナリオ。
完全回避不能な域でAEBが衝突速度を低減できることを見る。

## 検証の狙い

正例(REQ-AD-001)。
Euro NCAP Cut-in幾何とCCRb相当の減速を組み合わせた複合テスト。
LeadVehicleAwareは同一レーンの先行車しか検知しないため、カットインの認識が遅れる。
このシナリオの閉じ速度・タイミングでは、最速の正当検知時刻(t≈1.75s、隣接レーンの並走を誤検知しないための下限)であっても完全回避には約13.5 m/s²(≈1.38g)を要し、モデル車(maxDeceleration=10 m/s²≈1.02g、乾燥路の実車ABS実測レンジ0.87-1.08gの上端に相当)の物理上限を超える。
したがってAEBの価値は衝突回避ではなく衝突速度の低減(MITIGATION、Euro NCAPのカラーバンド思想)で測る。
AEB safety tierが未実装のときはこのテストがRED化する回帰点(issue #34)。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、目標速度30.0 m/s(108 km/h) |
| 他エンティティ | 先行車、road1 lane-2、s=78(バンパー間ギャップ約48 m)、速度12.0 m/s(43 km/h) |
| 走行時間 | StopTrigger: シミュレーション時間 20 s |

## 進行

1. t=0: 自車が30.0 m/sへ加速開始、先行車は隣接レーン(-2)で12.0 m/sの定速。
2. t>1.6s: 先行車が1.4s間のsinusoidal LaneChangeActionで自車レーンへカットイン(完了は約t=3.0s)。
3. t>2.4s: 先行車が8.0 m/s²のレートで速度0.0まで急制動する。
4. LeadVehicleAwareの同一レーン限定検知のため、カットインの認識が遅れ、快適減速のみでは回避できない閉じ速度になる。
5. t>20s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 接触が起きる場合、初回接触フレームの閉じ速度が閾値以下であること(緩和)。
- 接触が全く起きない(完全回避)場合は無条件PASS。
- 無AEB時の閉じ速度は約18 m/s、AEB有効時は実測9.14 m/s(約49%低減、2026-07-21実測・2回再現)。閾値10.0 m/sに対するマージンは0.86 m/s(8.6%)。
- 完全回避側の検証はカーブ変種(cutin_hard_brake_curve)のmin_obb_separation_aboveが担う。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| impact_speed_below | threshold 10.0 m/s, after sim_time 2.0s(カットイン開始基準) | 完全回避不能域でも衝突速度を安全レベルまで低減すること |

## 関連

- バッチ: `aeb_safety_batch.yaml`(gate:aeb-safety-regression)
- 期待値: `cutin_hard_brake.expectations.yaml`
- 関連ID: `req-vd-ad:REQ-AD-001` / `vd-func:FUNC-001`
