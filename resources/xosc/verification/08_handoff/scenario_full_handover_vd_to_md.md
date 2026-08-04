# VDからManualDriveへの全ドメイン一括引き渡し（成立パターンの最小形）

VirtualDriverControllerが横・縦とも保持する状態から、シナリオイベントでManualDriveControllerへ両ドメインを一括で引き渡す。
「VDを明示的に解放してから、MDをActivateする」という2アクション構成が、宣言順に依存せず正しく切り替わることを固定する。

## 検証の狙い

1アクション（MDを横・縦でActivateするだけ）でも書けるが、OpenSCENARIO v1.3以降ではそれだと現職のVDが降りない。
xosc内コメントによれば、`OSCPrivateAction.cpp`の per-domain 解放は現職コントローラではなく新参コントローラに対して`DeactivateDomains`を呼ぶため、1アクションでは両者がactiveのまま残り、毎フレーム後にStepした方が姿勢を総取りする。
xosc内コメントに記載された実測（road 4、8秒、t=4で引き渡し）では、1アクション+MD先宣言は引き渡しが失敗（車輪角がVDの値のまま変化せずエラーも出ない）、1アクション+MD後宣言は挙動は切り替わるがVDが解放されずFFBサーボが持ち主不在のまま回り続ける、としている。
本シナリオの2アクション版（先にVDを解放、次にMDをActivate）は宣言順に依存せず切り替わり、VDの制御出力解放がスクリプトどおりの時刻に起きる。
同ディレクトリの`scenario_split_domain_md_vd.xosc`は横=MD・縦=VDの片ドメインずつの分担を検証するのに対し、本シナリオは両ドメインを同時にVDからMDへ丸ごと渡すパターンを固定する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `f7_curve_onset.xodr`（road 4、半径約49mのアーク） |
| 自車 | road 4 / lane -1 / s=50→s=170、速度13.889 m/sへstep到達 |
| ドメイン所有 | t<4.0s: 横=VD・縦=VD／t>4.0s以降: 横=MD・縦=MD（一括移管） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 8 s 超過 |

## 進行

1. Init：Ego位置teleport、速度13.889 m/sへstep。VirtualDriverController（VD）を横・縦ともActivate。ManualDriveController（MD）はConfigFile `manual_drive_headless_stub.json`（入力ゼロ）で宣言のみ（先に宣言される）。
2. t>4.0s（パラメータ`HandoverAt`）：ReleaseVDイベントが先に発火し、VDを横・縦ともfalseでActivate（解放）。
3. 同じくt>4.0s：TakeMDイベントが発火し、MDを横・縦ともActivate。
4. t>8s：StopTriggerでシミュレーション終了。

## 期待する挙動

自動matcherは無く、xosc内コメントに記載された観測方法に基づく目視/CSV確認が判定手段になる。

- ManualDriveは入力ゼロのstubを使うため、引き渡し後は操舵ゼロ・スロットルゼロになる。
- road 4は半径約49mのアークなので、引き渡しが起きていればt=4以降まっすぐ進んで車線を外れ、速度も惰行で落ちる。
- 起きていなければVDのままレーン追従を続ける。
- lane_offsetを見ればどちらが起きたか一目でわかる、とxosc内コメントは述べている。

## 関連

- バッチ: 常設バッチには未所属。個別実行用（`GT_Sim.exe`にosc/headless/fixed_timestep 0.05/csv_loggerオプションを渡すコマンド例がxoscコメントに記載）。
- 関連ID: `feature:F7`
