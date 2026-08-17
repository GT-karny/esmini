# ControllerFollowRouteによる仮想ジャンクション横断スモークテスト

**このシーンはVirtualDriverControllerではなく、上流のControllerFollowRoute（カタログ参照`followRoute`）を使うスモークテストである。**
VirtualDriverController系の検証群（`vj_branch_turn` / `vj_straight_through`）とは異なる制御器を使う点に注意すること。

## 検証の狙い

S6で導入したアンカー対応の`LaneIndependentRouter`をFollowRouteControllerが消費し、仮想ジャンクションをまたぐルートを実際に計画・追従できることを確かめる。
xosc内コメントによれば、これはS6から先送りされていたスモークテストである。
自車はroad 1（s=10）からroad 3（s=40）へルートされ、FollowRouteはアンカーを通って分岐road 2上に合成されたウェイポイントを経由し、road 3へ到達しなければならない。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `virtual_junction_23.xodr`（road 1→road 2（アンカーs=100で分岐）→road 3） |
| 自車 | VehicleCatalog `car_red`、ControllerCatalog `followRoute`。road 1 / lane -1 / s=10、目標速度9.0 m/s（Init、step 1.0s） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 30 s 超過 |

## 進行

1. Init：road 1 s=10へteleport、速度9.0 m/sへ1.0秒かけてstep、ControllerFollowRouteを横・縦ともActivate。
2. t>0：Story Eventがroad 1 s=10→road 3 s=40のルートを割り当てる。
3. FollowRouteがアンカーを通る経路を計画し、分岐road 2を経てroad 3へ向かう。

## 期待する挙動

自動matcherは無く、xosc内コメントに記載された確認方法（記録した`.dat`から到達を確認）が判定手段になる。

- ヘッドレスesmini（`esmini.exe`、GT_Sim.exeではなく上流バイナリ）で記録した走行が、road 3に到達していること。
- 実行コマンド例（xosc内コメント記載）: `esmini.exe` に `headless`、`osc <このファイル>`、`fixed_timestep 0.05`、`record <dat>` を渡す。

## 関連

- バッチ: 常設バッチ（`p6_vj_batch.yaml`）には未所属。個別のヘッドレススモーク実行用。
