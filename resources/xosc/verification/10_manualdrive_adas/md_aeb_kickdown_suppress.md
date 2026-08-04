# 全踏み(キックダウン)によるAEB抑制(手動運転中ADAS)

07_aeb/cutin_hard_brake.xosc の幾何(カットイン+急制動)を流用し、自車のコントローラを ManualDriveController に差し替え、
「前半は無反応、途中から全踏み」の合成ドライバ("kickdown_at"プロファイル)を対にしたシナリオ。

## 検証の狙い

正例(REQ-AD-025 段d、slug `md-aeb-kickdown-suppression`)。
運転者の明確な加速意思(アクセル全踏み相当)がAEBの介入を抑制し、かつその抑制事象が観測できることを見る。
単に「何も起きなかった」ではなく「介入していたものが抑制で止まった」ことを証明する構成にした
(検証計画§4-1の赤実証資産規律・kickdown_at.jsonの説明を参照)。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、Init速度30.0 m/s(108 km/h)、コントローラ=ManualDriveController |
| 他エンティティ | 先行車、road1 lane-2、s=78(バンパー間ギャップ約48 m)、速度12.0 m/s(43 km/h) |
| 入力プロファイル | `profiles/kickdown_at.json` — t=5.0まで全0(unresponsiveと同一)、t=5.3で全スロットル(1.0)へランプし以後保持 |
| 走行時間 | StopTrigger: シミュレーション時間 20 s |

## 進行

1. t=0-5.0: md_aeb_unresponsive.xoscと同一の入力(全ペダル0)。カットイン(t>1.6)と急制動(t>2.4)が進行し、
   AEBがこの間に既に介入を開始していることが期待される。
2. t=5.0-5.3: 運転者のスロットルが0から1.0へ急速にランプする(adas_kickdown_threshold=0.95以上)。
3. t>5.3: 運転者は全スロットルを保持し続ける。AEBはこの明確な加速意思を検知し、以後の介入を抑制するはずである。
4. t>20s: StopTriggerでシミュレーション終了。

## 期待する挙動

- キックダウン前(t=2.0-5.0)、AEBが既にHVD上でACTIVEになっていること(`manual_aeb_fires`、抑制の前提を証明)。
- キックダウン後(t=5.3-20.0)、AEBが介入しないこと(`no_intervention_in_window`)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| manual_aeb_fires | function gt.aeb, after 2.0s, before 5.0s, min_frames 1 | キックダウン前にAEBが既に介入していたこと(抑制の前提) |
| no_intervention_in_window | function gt.aeb, after 5.3s, before 20.0s, min_frames 1 | キックダウン後にAEBが介入しないこと(抑制そのもの) |

## 数値の出典・要校正の明示

- t=5.0という「AEBが既に介入しているはず」のカットオフは**推測**。cutin_hard_brake幾何でAEBが実際に介入を開始する時刻を
  ビルド前のため測定できておらず、手計算による見込みにすぎない。
- ビルド後の実測でAEBの介入開始がt=5.0より後だった場合、`manual_aeb_fires`側の窓([2.0, 5.0))を後ろへずらすか、
  kickdown_at.jsonのオンセット時刻を遅らせて再設計すること(閾値を緩めて帳尻を合わせない)。

## 関連

- バッチ: `manualdrive_adas_batch.yaml`
- 期待値: `md_aeb_kickdown_suppress.expectations.yaml`
- 入力プロファイル: `profiles/kickdown_at.json`
- 関連ID: `req-vd-ad:REQ-AD-025` 段d / `vd-func:FUNC-075`
- 参考(幾何の出典): `resources/xosc/verification/07_aeb/cutin_hard_brake.xosc`
