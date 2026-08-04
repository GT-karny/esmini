# 実機G29での順構成分割（横=MD/縦=VD、フィールドテスト専用）

**このシナリオは実機のG29ステアリング/ペダルを接続したフィールドテスト専用であり、自動ゲートでは走らない。**
GUIから何気なく実行しても、意図した検証（横=人・縦=AIの分担成立の目視確認）にはならない点に注意すること。
ヘッドレス版`scenario_split_domain_md_vd.xosc`と中身は同一で、ManualDriveのConfigFileだけ物理G29入力（`manual_drive_realwheel_split.json`、input_type=sdl2_wheel）に差し替えてある。
xosc内コメントに「CIには載せない」と明記された目視デモ用シナリオである。

## 検証の狙い

CI用アセットと同じ構造のまま入力源だけ実機に差し替えてあるため、ここで見えるものはヘッドレスで毎回検査しているものと同じ現象になる、とxosc内コメントは述べる。
road 4は半径約49mの定曲率アークで、ハンドルを切り続ければ車がカーブに追従し（横=人）、ハンドルから手を離せば直進して車線を外れる（横がAIでないことの証拠）。
その間ずっと速度が約30km/hに保たれれば縦はAIが握っている。
3つが同時に観測できて初めて「横=人/縦=AD」の成立と言える。
1つでも欠けたら成立していない。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `f7_curve_onset.xodr`（road 4、半径約49mのアーク） |
| 自車 | road 4 / lane -1 / s=10→s=175、目標速度8.333 m/s（step、即時） |
| ドメイン所有 | 横=ManualDriveController（人）／縦=VirtualDriverController（AI） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 120 s 超過 |

## 進行

1. Init：Ego位置teleport、速度8.333 m/sへstep。ManualDriveController（ConfigFile `manual_drive_realwheel_split.json`）を横のみActivate、VirtualDriverController（既定ConfigFile=stub）を縦のみActivate。
2. ハンドルを切り続けるかどうかで横方向の帰属を目視確認する。速度は約30km/h付近を維持し続ける（縦=AI）。
3. t>120s：StopTriggerでシミュレーション終了。

## 期待する挙動

自動matcherは無く、xosc内コメントに記載された目視判定手順が判定手段になる。

- ハンドルを切り続ける→車はカーブに追従する（横は人が握っている）。
- ハンドルから手を離す→車は直進して車線を外れる（横はADではない）。特に「手を離しても車線に戻る」場合は横がADに握られており分割が壊れている。
- その間ずっと速度は約30km/h（8.333 m/s）付近に保たれる（縦はADが握っている）。
- 速度を30km/hに落としてあるのは、13.889 m/sだとroad 4を12秒で走り切ってしまい観察時間が無いため。8.333 m/sなら約20秒観察できる。
- **このシナリオではハンドルに力は出ない（意図的）。** VirtualDriverはinput_type=stubで動きデバイスに触れる経路を持たず、ManualDriveはこの分割では非積分側になるため初回フレームでSDLFFBSink::Update()が早期リターンする。デバイスを開くのはManualDriveの1プロセス内ハンドルのみで、力出力は初回フレームで落ちる。
- ただしヘッドレスで確認できるのは「そこまで制御フローが到達したこと」までで、実機のトルクが実際にゼロになったことはSDLFFBSink自体の性質上未確認。実機で最初に見る項目はここになる、とxosc内コメントは述べている。

## 関連

- バッチ: 常設バッチには未所属（ヘッドレス版`scenario_split_domain_md_vd.xosc`は`scenario_handoff_batch.yaml`に所属するが、本シナリオ自体はフィールドテスト専用の個別実行資産。配布パッケージの手順書REALWHEEL_TEST.md参照、起動オプション`window 60 60 1280 720`が必須）。
- 関連ID: `feature:F7`
