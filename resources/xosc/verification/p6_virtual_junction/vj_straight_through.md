# 仮想ジャンクション直進通過（P6 VJシーンB、T2不変条件）

VirtualDriverControllerが仮想ジャンクション888のスパンを、分岐road 2へ逸れることなく主道路road 1のまま直進通過することを確認する。

## 検証の狙い

自車はroad 1上に始点を置き、目的地も同じroad 1のさらに先（s=180）にある。
この目的地は仮想ジャンクションのspan [95,105]を越えているが、あくまでroad 1上であり分岐road 2へは向かわない。
本シーンはVDレベルでのT2直進通過不変条件——アンカー（s=100）を素通りして分岐へ逸れない——を検証する。
`vj_branch_turn.xosc`と同じ設計文書のHARD RULE（§6 S7b）が適用され、road 1のspan上ではjunction id/membershipのアサーションを置けない。
road id自体のアサーションは許容されるため、「road 1に留まり続けること」はroad idで証明する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `virtual_junction_23.xodr`（road 1: +x方向直線。仮想ジャンクション888のspanは[95,105]） |
| 自車 | road 1 / lane -1 / s=10→s=180（単一区間、分岐を経由しない）、目標速度9.0 m/s（Story Eventでlinear 3.0s掛けて到達） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 30 s 超過 |

## 進行

1. Init：road 1 s=10へteleport、road 1 s=10→road 1 s=180のルートを割り当て、VirtualDriverControllerを横・縦ともActivate。
2. t=0：Cruiseイベントが発火し目標速度9.0 m/sへ3.0秒かけて加速する。
3. アンカー（s=100）を素通りし、road 2の-45度形状には追従しない（road 1はy~0の直線を維持）。
4. スパンを越えてroad 1上の目的地まで走行する。

## 期待する挙動

- 走行全体を通じてroad 1 / lane -1に留まり、分岐road 2へは一切逸れない。
- スパン通過中も失速しない。
- 指令速度9.0 m/sを大きく超えない。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `lane_keep` | road_id=1、lane_id=-1（走行全体） | 仮想ジャンクションを直進通過し常にroad 1に留まる（T2不変条件） |
| `min_speed_above` | threshold=1.0、sim_time > 3.0 s | スパン通過中も失速しない |
| `speed_below` | threshold=12.0 | 指令速度を大きく超えない（制御が安定） |

## 関連

- バッチ: `p6_vj_batch.yaml`
- 期待値: `vj_straight_through.expectations.yaml`
