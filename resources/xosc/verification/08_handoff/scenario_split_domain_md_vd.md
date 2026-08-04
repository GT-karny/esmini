# ドメイン分割所有：横=ManualDrive/縦=VirtualDriver（ヘッドレス）

自車にManualDriveControllerとVirtualDriverControllerを同時に割り当て、横方向をMD、縦方向をVDへ分担させる。
両者が同時にactiveのまま、単一の物理積分器の下でそれぞれの担当ドメインだけを制御できることを確認する。

## 検証の狙い

xosc内コメントによれば、実装前（S1-S3導入前）はこの分担が成立しなかった。
両コントローラともStep()で`active_domains_`を参照せず、それぞれ自前のRealVehicleBackendで姿勢・速度を丸ごと書き戻すため、同一フレーム内で後にStepした方が全ドメインを総取りしていた。
現在の実装（`docs/virtualdriver/design/domain_split_ownership.md`）はS1所有台帳・S2出力ゲート・S3コマンドバスの3段で、宣言順に依存せず分担が成立する。
期待値yamlのmatcherは、縦=VDでしか説明できない再加速、横=MDでしか説明できない車線逸脱、そして走行速度と報告速度の比率という3方向から「片方が全ドメインを総取りしていないこと」を切り分ける設計になっている。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `f7_curve_onset.xodr`（road 4、半径約49mのアーク） |
| 自車 | road 4 / lane -1 / s=50→s=170、目標速度13.889 m/s（step、即時） |
| ドメイン所有 | 横=ManualDriveController／縦=VirtualDriverController |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 8 s 超過 |

## 進行

1. Init：Ego位置teleport、速度13.889 m/sへstep。ManualDriveController（ConfigFile `manual_drive_headless_stub.json`、入力ゼロ）を横のみActivate、VirtualDriverControllerを縦のみActivate（`objectControllerRef`指定、宣言順を入れ替えてもcsv 163行がbyte-identicalとxosc内コメントに記載）。
2. VDがroad 4のR≈49mアークに沿って操舵し、MDの入力ゼロなスロットルに代わって速度13.889 m/sを維持・再加速する。
3. t>8s：StopTriggerでシミュレーション終了。

## 期待する挙動

- 縦はVDのもの：MDのスロットルはゼロなのに速度が再加速する（ゼロスロットルでは不可能）。
- 横はVDのものではない：車がroad 4のアークに追従せず車線を逸脱する（MDのstub入力は定常ゼロ操舵のため）。
- 単一積分器：走行速度（World_Position変化率）と報告速度（Current_Speed）の比が0.98〜1.02に収まる。乖離があれば、状態段でMD由来の物理とVD由来の速度フィールドが不整合にマージされている兆候。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `domain_split_holds` | sim_time 1.0〜8.0 s、min_speed_gain=2.0、min_lane_departure=1.0、ratio_band=[0.98, 1.02] | 横=ManualDrive/縦=VirtualDriverの分担が成立し、単一積分器であること。実測: 速度はR~49mの旋回で9.76 m/sまで落ちた後14.1へ再加速（利得約4.3 m/s）、車線内|lane_offset|は最大約5.9m（車線幅約3.5m） |

## 関連

- バッチ: `scenario_handoff_batch.yaml`
- 期待値: `scenario_split_domain_md_vd.expectations.yaml`
- 関連ID: `feature:F7`
