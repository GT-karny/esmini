# crossPath合成横断歩道での譲り停止（P5 crossPath版 p005）

p005系の横断譲り停止と同じシナリオ構成で、道路側だけが異なる。
道路にauthoredな横断歩道オブジェクトを一切持たず、`<crossPath>`から合成された横断歩道に対しても自車（VirtualDriverController）が歩行者へ譲って停止することを確認する。

## 検証の狙い

道路`straight_crosswalk_crosspath__mid.xodr`のroad 0には`<object type="crosswalk">`のauthored定義が無い。
代わりに歩行者横断用の道路と、`<crossPath>`を持つ仮想ジャンクションがあり、GT側の合成処理（`OdrJunctionExtras.cpp`）がs=250（footprint s∈[248,252]、元のauthoredオブジェクトと同じ範囲）に4隅を持つCROSSWALK RMObjectを再構成する。
本シナリオは、CrosswalkPedestrianAwareポリシーがこの合成横断歩道を、authoredなオブジェクトとまったく同じようにポリシーコード無変更で検知することのゼロ差分証明である。
自車が停止しない場合は、ポリシーではなく合成処理（footprint/pose）を疑うべき、とバッチyamlのコメントは明記している。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_crosswalk_crosspath__mid.xodr`（road 0、crossPath合成の横断歩道、authoredオブジェクトなし） |
| 自車 | road 0 / lane -1 / s=100→s=450、目標速度13.9 m/s（step、即時） |
| 他エンティティ | 歩行者（`pedestrian_adult`）：road 0 / lane 2 / s=250に静止配置、t>6.327sから速度1.4 m/sでlane 2→lane -2（s=250固定）を横断 |
| 走行時間 | シミュレーション時間 35 s 超過 |

## 進行

1. Init：自車はroad 0 s=100→s=450のルートで速度13.9 m/sへ即座に到達しVirtualDriverControllerが横・縦をActivate。歩行者はroad 0 lane 2 s=250に静止配置。
2. t>6.327s：歩行者がFollowTrajectoryActionで速度1.4 m/s、lane 2→lane -2（road 0 s=250のまま）を横断し始める。
3. VDのCrosswalkPedestrianAwareポリシーが合成横断歩道と歩行者を検知し、譲って停止する。
4. 歩行者の横断後、自車は巡航へ復帰する。
5. t>35s：StopTriggerでシミュレーション終了。

## 期待する挙動

- sim_time 14.0〜16.0 sの間、速度が1.5 m/s未満まで低下する（歩行者への譲り停止。OFFであればこの窓でも巡航速度13.9のまま）。
- sim_time 6.0〜20.0 sの間、自車と歩行者のOBB（バウンディングボックス）間の最小分離が0.3 mを上回る（衝突回避ゲート。歩行者footprintは0.6×0.5m実寸）。
- sim_time 17.5 s以降、速度が12.0 m/sを上回る（歩行者通過後の巡航復帰、停止しっぱなしでない）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `speed_below` | threshold=1.5、sim_time 14.0〜16.0 s | 横断者への譲り停止。窓は停止・深クロール相の内側 |
| `min_obb_separation_above` | threshold=0.3、sim_time 6.0〜20.0 s | 衝突回避ゲート（OBB）。歩行者footprintとの最小分離が重なり(≤0)とクリーン(1.54)を弁別する閾値 |
| `speed_above` | threshold=12.0、sim_time > 17.5 s | 歩行者通過後の巡航復帰 |

## 関連

- バッチ: `crosswalk_crosspath_batch.yaml`（`crosswalk_pedestrian_batch.yaml`＝object-encoded版とは別バッチとして意図的に分離されている）
- 期待値: `09_crosswalk_crosspath__p005.expectations.yaml`
- 関連ID: `vd-phase:Phase3d`
