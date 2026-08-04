# 実機G29での逆構成分割（横=VD/縦=MD、フィールドテスト専用）

**このシナリオは実機のG29ステアリング/ペダルを接続したフィールドテスト専用であり、自動ゲートでは走らない。**
GUIから何気なく実行しても、意図した検証（AI操舵の実機体感と停止時の舵保持）にはならない点に注意すること。
`scenario_realwheel_split_md_vd.xosc`の横縦を入れ替えた構成で、横=VirtualDriverController（AI操舵）、縦=ManualDriveController（人のペダル）を固定する。

## 検証の狙い

xosc内コメントによれば、台帳の帰結（宣言順に依存しない）として横=VD/縦=MDでは積分器がManualDriveControllerになる（縦の所有者が積分器になる規則）。
`scenario_realwheel_split_md_vd.xosc`（横=MD/縦=VD、積分器=VD）とは積分器が逆になり、コマンドバスの両方向を通す構成になる。
同時に2点を確認する。
(1) 逆構成そのものが成立するか——AIがカーブに追従して車線内を走り、人のペダルで加減速でき、ハンドルを回しても進路が変わらないこと。
(2) カーブ途中で停止したときに舵が保持されるか——xosc内コメントによれば修正前は停止時に舵が中立を通過して反対側の全舵角まで振れる不具合があった（実測 -0.1059 rad → +0.6058 rad）。修正後は保持される（→ -0.1038 rad、commit e72e6394）。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `f7_curve_onset.xodr`（road 4、半径約49mのアーク） |
| 自車 | road 4 / lane -1 / s=10→s=175、開始速度8.333 m/s（以後は完全に人のペダルで決まる） |
| ドメイン所有 | 横=VirtualDriverController（AI）／縦=ManualDriveController（人）、積分器=ManualDriveController |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 180 s 超過 |

## 進行

1. Init：Ego位置teleport、開始速度8.333 m/sへstep（以後の速度はこの値を初期値としてのみ使う）。ManualDriveController（ConfigFile `manual_drive_realwheel_reverse.json`）を縦のみActivate、VirtualDriverControllerを横のみActivate。
2. 起動直後からホイールがAIの操舵に合わせて動く（サーボONが正常）。
3. カーブ途中でブレーキを踏んで完全停止し、前輪の向きを見る（舵が保持されるかを確認）。
4. t>180s：StopTriggerでシミュレーション終了。

## 期待する挙動

自動matcherは無く、xosc内コメントに記載された実機での観測手順が判定手段になる。

- ハンドルを切っても進路が変わらない（横をVDが握っている証拠）。
- AIがR≈49mのアークに追従して車線内を走る。
- ペダルで加減速でき、VDは縦を持たないためVDの速度指令はバスに publish されず、AIの速度維持と人のブレーキが競合しない。
- カーブ途中で完全停止したとき、前輪の向きが保持される（振れて反対側の全舵角まで動かない）。
- 安全ウォッチドッグS1が明示有効。target_track有効時のreachable capはmin(ffb.max_force=1.0, target_track_max_force=0.6)=0.6のため|force| >= 0.570が2.0秒継続すると走行を強制終了し力を落とす。
- 起動時にログで`manual_drive_realwheel_reverse.json`が読まれていること、`target_track enabled=true`、`max_saturation=2.0s`が出ていることを確認する。xosc内コメントは、順構成用の`_split.json`はサーボOFFのため取り違えるとサーボが効かない旨を注意している（2026-07-30に一度取り違えた実例あり）。

## 関連

- バッチ: 常設バッチには未所属。フィールドテスト専用の個別実行資産（配布パッケージの手順書REALWHEEL_REVERSE_TEST.md参照、起動オプション`window 60 60 1280 720`が必須）。
- 関連ID: `feature:F7`
