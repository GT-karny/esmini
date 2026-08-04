# カーブ上でのカットイン+急制動への追突回避(AEB)

カットイン+急制動の複合シナリオを半径300 mの左カーブ上で走らせる変種。
カーブの操舵とAEBの制動が両立し接触が生じないことを見る。

## 検証の狙い

正例(REQ-AD-001、カーブ幾何変種)。
直線変種(cutin_hard_brake.xosc)と同一のカットイン+急制動パラメータを、定曲率R=300mの左カーブ(curve_2lane_r300.xodr)上で走らせる。
カットイン検知と操舵しながらの制動がカーブ曲率上でも成立することを確認する。
直線変種は物理的に完全回避不能な域でありMITIGATION判定だが、この変種は曲率による回避可能域を使うため、受入は分離維持(衝突回避そのもの)である。
AEB safety tierが未実装のときはこのテストもRED化する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `curve_2lane_r300.xodr`(半径300 mの定曲率カーブ、2車線) |
| 自車 | road1 lane-1、s=30、目標速度30.0 m/s。Route: s=30→240 |
| 他エンティティ | 先行車、road1 lane-2、s=78(バンパー間ギャップ約48 m)、速度12.0 m/s |
| 走行時間 | StopTrigger: シミュレーション時間 20 s |

## 進行

1. t=0: 自車が30.0 m/sへ加速開始、先行車は隣接レーン(-2)で12.0 m/sの定速。
2. t>1.6s: 先行車が1.4s間のsinusoidal LaneChangeActionで自車レーンへカットイン(完了は約t=3.0s)。
3. t>2.4s: 先行車が8.0 m/s²のレートで速度0.0まで急制動する。
4. 自車はカーブに沿ってレーンを維持しながらAEBの制動を行う必要がある。
5. t>20s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 直線変種とは異なり、この変種はMITIGATION(衝突速度低減)ではなく分離維持(衝突回避)を判定基準とする。
- matcher `min_obb_separation_above` はPASSすること(バンパー間分離が接触(0)まで縮まらない)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| min_obb_separation_above | threshold 0.5 m, after sim_time 2.0s | カーブ上でもカットイン+急制動する先行車に追突しない(OBB分離を安全床上に保つ) |

## 関連

- バッチ: `aeb_safety_batch.yaml`(gate:aeb-safety-regression)
- 期待値: `cutin_hard_brake_curve.expectations.yaml`
- 関連ID: `req-vd-ad:REQ-AD-001` / `vd-func:FUNC-001`
