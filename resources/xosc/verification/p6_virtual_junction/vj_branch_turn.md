# 仮想ジャンクション分岐ルート走行（P6 VJシーンA）

VirtualDriverControllerが仮想ジャンクション888をまたぐルート（road 1→road 3）を走行し、アンカーで主道路を離れて分岐road 2へ進入できることを確認する。

## 検証の狙い

自車はroad 1（未分割の主道路）上に始点を置き、目的地はroad 2を経由した先のroad 3にある。
ルートは仮想ジャンクション（junction 888、road 1のspan [95,105]）を横断して解決される必要があり、VDはアンカー（s=100）で主道路を離れ-45度の分岐road 2へ入り、road 3まで走り切らなければならない。
設計文書（`odr_p6_virtual_junction_design.md` §6 S7b）のHARD RULEにより、road 1のspan上ではjunction id/membershipのアサーションを一切置けない（v1ではこの区間で`GetJunctionId=-1`/`IsInJunction=false`を返す、interpretiveな挙動で上流issue #592に問題提起済み）。
このため「分岐が実行された」ことは junction id ではなく、到達先が road 3であること（OpenDRIVEのroad id）で証明する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `virtual_junction_23.xodr`（road 1: +x方向直線、s=0@(0,0)、length 200、アンカーs=100@(100,0) / road 2: (100,0)からhdg=-45度、length 30、終点~(121.2,-21.2) / road 3: そこからhdg=-45度、length 50、終点~(156.6,-56.6)） |
| 自車 | road 1 / lane -1 / s=10、目標速度9.0 m/s（Story Eventでlinear 3.0s掛けて到達） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 30 s 超過 |

## 進行

1. Init：road 1 s=10へteleport、road 1 s=10→road 3 s=40のルートを割り当て、VirtualDriverControllerを横・縦ともActivate。
2. t=0：Cruiseイベントが発火し目標速度9.0 m/sへ3.0秒かけて加速する。
3. アンカー（s=100）で主道路road 1を離れ、-45度の分岐road 2へ進入する（分岐の証拠：ego x/yがroad 1のy~0の線を離れ、road 2の-45度形状に沿ってyが負方向に動く）。
4. road 2からroad 3へ継続して走行する。

## 期待する挙動

- 走行後半でroad 3に到達している（分岐が実行された証拠）。
- 旋回中も失速しない。
- 指令速度9.0 m/sを大きく超えない。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `lane_keep` | road_id=3、sim_time > 20.0 s | 仮想ジャンクションを分岐しroad 3に到達している（road idはOpenDRIVEの道路id、junction idではない） |
| `min_speed_above` | threshold=1.0、sim_time > 3.0 s | 分岐旋回中も失速しない |
| `speed_below` | threshold=12.0 | 指令速度を大きく超えない（制御が安定） |

## 関連

- バッチ: `p6_vj_batch.yaml`
- 期待値: `vj_branch_turn.expectations.yaml`
