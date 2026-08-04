# 緩やかな同一レーン接近によるFCW先行(手動運転中ADAS)

停止中の先行車へ同一レーンから緩やかに接近する幾何(カットイン無し)を新設し、自車のコントローラを
ManualDriveController に差し替え、全入力ゼロの合成ドライバ("unresponsive"プロファイル、md_aeb_unresponsiveと共用)を
対にしたシナリオ。REQ-AD-025 段eのうち、警報(FCW)が介入(AEB)に先行することを実測するための資産。

## 検証の狙いと、なぜ新設したか

正例(REQ-AD-025 段e、slug `md-fcw-leads-intervention`)。**2026-08-05新設**。

初回バッチ実行(mdadas_run1, 2026-08-04)で `md_aeb_unresponsive.xosc`(cutin_hard_brake幾何の流用)にこのslugを
持たせていたが、実測リードが**0.000s**(FAIL)だった。原因は幾何の選び方であり実装のバグではない
— 詳細は `md_aeb_unresponsive.md` の「実測結果と切り出した知見」節。要約すると:

FCWはAEBと**同じ候補選定パラメータ(横方向侵入の3フレームデバウンス)を共有**した「しきい値だけ緩いAebSafety」
(`GT_esmini/src/control/manualdrive/AdasCoexistenceStack.cpp` の `DeriveFcwGateConfig`)であるため、
**候補として認識される前は、しきい値の差が何のリードも買わない**。急激なカットインでは、候補が
「まだ横侵入中で未確定」から「同一レーン内候補として確定」への遷移が1フレームで起き、確定した瞬間には既に
両ゲート(FCWのTTC<3.5もAEBのTTC<2.5も)を跨いでいた(実測: t=1.75でttc=0.738、a_req=11.460)。

対応は、**横方向侵入デバウンスが関与しない幾何**(カットイン無し・同一レーンからの接近、07_aeb/normal_following.xosc
のheaderが言う「dLaneId==0、frame 1から通常追従として扱われる」パターン)へ切り替えること。これなら候補は
ほぼt=0から追跡され、TTC/必要減速度は滑らかに変化するので、2つのゲート(FCWの緩い方・AEBの厳しい方)が
**異なる時刻**で交差する — その差が測れるリードになる。

## 算術による幾何設計(推測ではなく導出)

先行車が停止していて自車が無反応(AEBが実際に介入するまで減速しない)なら、閉じ速度 v_close はほぼ一定であり、
gap(t) = gap0 - v_close·t となる。これは`md_aeb_unresponsive`の実測(t=1.75でttc=gap/v_close=12.499/16.925=0.738、
a_req=v_close²/(2·gap)=16.925²/(2·12.499)=11.46)と厳密に一致する式であり、**式そのものは実測で検算済み**。

しきい値(いずれもリポジトリの現行値、実装コードから直接確認・推測ではない):

| ゲート | TTCしきい値 | 必要減速度しきい値 | 出典 |
| :-- | --: | --: | :-- |
| AEB介入 | aeb_ttc_threshold = **2.5 s** | aeb_min_a_req = **3.0 m/s²** | `GT_esmini/config/virtual_driver.json` |
| FCW警報 | warning_ttc_threshold_s = **3.5 s**(既定) | warning_min_a_req_mps2 = **2.0 m/s²**(コンパイル済み既定) | `manual_drive_headless_stub.json` の `adas_aeb_warning_ttc_threshold_s` / `GT_esmini/include/gt_esmini/control/manualdrive/AdasCoexistenceStack.hpp` |

`AdasCoexistenceStack.cpp` の `DeriveFcwGateConfig`:
```
fcw.ttc_threshold = max(warning_ttc_threshold_s, aeb.ttc_threshold)  = max(3.5, 2.5) = 3.5
fcw.min_a_req     = min(warning_min_a_req_mps2, aeb.min_a_req)       = min(2.0, 3.0) = 2.0
```

各ゲートは「TTC<しきい値 **かつ** a_req>しきい値」がAND条件で、遅い方(=時刻が大きい方)の条件がボトルネックになる。
gap(t)=gap0-v_close·tのもとで:

```
ttc(t)   = gap(t)/v_close = gap0/v_close - t
a_req(t) = v_close²/(2·gap(t))
```

TTC条件がしきい値Tを跨ぐ時刻: t_ttc(T) = gap0/v_close - T
a_req条件がしきい値Aを跨ぐ時刻: gap0-v_close·t = v_close²/(2A) → t_areq(A) = gap0/v_close - v_close/(2A)

警報発火 = max(t_ttc(3.5), t_areq(2.0)) = gap0/v_close - min(3.5, v_close/4)
介入発火 = max(t_ttc(2.5), t_areq(3.0)) = gap0/v_close - min(2.5, v_close/6)

リード = 介入発火 - 警報発火 = **min(3.5, v_close/4) - min(2.5, v_close/6)** — gap0に依存せず、v_closeのみの関数。

| v_close | min(3.5, v_close/4) | min(2.5, v_close/6) | リード |
| --: | --: | --: | --: |
| 8 m/s | 2.0 | 1.333 | 0.667 s(**0.8未達**) |
| 12 m/s | 3.0 | 2.0 | 1.0 s |
| 14 m/s | 3.5 | 2.333 | 1.167 s |
| **16 m/s** | **3.5** | **2.667→2.5** | **1.0 s** |
| 20 m/s | 3.5 | 2.5 | 1.0 s |

v_close≥15 m/sで両方の min() が上側の定数(3.5と2.5)で飽和し、リードは **1.0 s の平坦域**になる
(v_close=15ちょうどで両方の枝が連続的に一致することも確認済み)。この平坦域を選んだ理由は、
実際のv_closeが本シナリオの近似(無反応ドライバの閉じ速度は完全に一定、という仮定)から多少ズレても
結果が変わりにくいから — 実測では無反応ドライバでも約0.4-0.5 m/s²の自然減速(コースティング抵抗、
`md_fcw_warning_only.md`参照)があり、3-4秒の接近でv_closeが16から14程度まで下がりうるが、
14でもリードは1.167sとむしろ増える方向であり、0.8sの床を割らない。

v_close=16(EgoSpeed=16.0、Lead停止=0.0)を採用し、gap0=104.0mとして
t_warn = gap0/16 - 3.5 = 6.5-3.5 = **3.0s**、t_interv = 6.5-2.5 = **4.0s**(Init/コントローラ活性化の
落ち着き時間0.5sの後に余裕を持って発火する)。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、Init速度16.0 m/s(57.6 km/h)、コントローラ=ManualDriveController |
| 他エンティティ | 先行車(停止)、road1 lane-1(**同一レーン**、カットイン無し)、s=134(gap0≈104m) |
| 入力プロファイル | `profiles/unresponsive.json` — md_aeb_unresponsiveと共用、全チャンネル0.0を全区間保持 |
| 走行時間 | StopTrigger: シミュレーション時間 12 s |

## 進行

1. t=0: 自車はInit SpeedAction(step)で16.0 m/sに初期化。先行車は同一レーンs=134で完全停止。
2. t=0〜: 運転者(合成)は全区間ペダル0.0。候補はほぼ最初のフレームから同一レーンの先行車として追跡される
   (横方向侵入デバウンスが関与しない)。
3. t≈3.0s付近: TTC/必要減速度がFCWのゲート(TTC<3.5 or a_req>2.0)を跨ぎ、gt.fcwがACTIVEになると見込む。
4. t≈4.0s付近: TTC/必要減速度がAEBのゲート(TTC<2.5 and a_req>3.0)を跨ぎ、gt.aebがACTIVEになり介入すると見込む。
5. t>12s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 警報(FCW)が介入(AEB)に先行し、リードが0.8s以上(算術導出は約1.0s)であること(`fcw_leads_intervention`)。
- AEBが実際に発火していること(`manual_aeb_fires`、上記の前提を補強)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| fcw_leads_intervention | min_lead_s 0.8(md_aeb_unresponsiveから変更なし)、after 0.0s, before 12.0s | 警報が介入に先行しリードが閾値以上 |
| manual_aeb_fires | function gt.aeb, after 3.5s, before 12.0s | AEBが実際に発火したこと |

## 実測結果(mdadas_run2, 2026-08-05)

両matcherともPASS。

| | 算術導出 | 実測(mdadas_run2) |
| :-- | --: | --: |
| 警報発火 t_warn(gt.fcw ACTIVE) | 3.00 s | **3.30 s**(frame 65) |
| 介入発火 t_interv(gt.aeb ACTIVE) | 4.00 s | **4.75 s**(frame 94) |
| リード | 1.00 s | **1.45 s** |

導出(1.0s)は実測(1.45s)より**保守的(安全側)**だった — 「無反応ドライバの閉じ速度は完全に一定」という近似が、
実際には無視していたコースティング減速(~0.53 m/s²、`md_fcw_warning_only.md`参照)や初期フレームの過渡応答の分だけ
ズレを生んだためと考えられる。ズレの向き自体は安全側(閾値0.8sに対して実測1.45sは1.8倍のマージン)であり、
REQ-AD-025 段eは**これで実測により確認された**(以前は「仮定」だったものが「実測値」になった)。

**恒久的な教訓(md_fcw_warning_only.mdへ引き継いだ)**: この算術導出は**幾何の設計**(v_close・gap0の選定、
どちらのゲートが先に発火するかの定性予測)には十分な精度を持つが、**反応プロファイルのタイミングをそこから
直接決めるには精度が不足する**。実際、`md_fcw_warning_only.xosc`の第1パスはこの導出値(t_warn=3.0)を基準に
運転者の反応時刻(t=3.1)を決めたが、実測の警報発火(3.30)より前に反応してしまい、警報そのものを消してしまう
結果になった(詳細は `md_fcw_warning_only.md` の「第2パス」節)。反応プロファイルは**実測された発火時刻**に
アンカーする必要がある。

## 数値の出典・要校正の明示

- しきい値(aeb_ttc_threshold=2.5 / aeb_min_a_req=3.0 / warning_ttc_threshold_s=3.5 / warning_min_a_req_mps2=2.0)は
  いずれも実装コード/configから直接確認した値であり推測ではない(出典は本文の表)。
- リード=1.0sという算術導出は、mdadas_run2の実測(1.45s)により**保守的な下限として裏付けられた**
  (推測ではなく実測確認済み。上記「実測結果」節参照)。
- gap0=104.0mは「t_warnをInit落ち着き後の3.0sに置く」という設計上の選択であり、リードの値そのもの
  (導出1.0s/実測1.45s)には数学的に影響しない(上記導出参照)。
- 無反応ドライバの閉じ速度が「完全に一定」という仮定は近似だった。実測されたコースティング減速
  (~0.53 m/s²、`md_fcw_warning_only.md`)により、t_warn/t_interv自体は導出値より実測で0.3〜0.75s程度後ろへ
  ずれたが、リードの値(v_close=16の平坦域)への影響は小さく、むしろ安全側に出た(上記の表参照)。

## 関連

- バッチ: `manualdrive_adas_batch.yaml`
- 期待値: `md_aeb_stationary_lead.expectations.yaml`
- 入力プロファイル: `profiles/unresponsive.json`(md_aeb_unresponsiveと共用)
- 関連ID: `req-vd-ad:REQ-AD-025` 段e / `vd-func:FUNC-075`
- 移設元: `md_aeb_unresponsive.md`(2026-08-04実測でリード0.000sだった経緯)
- しきい値の出典コード: `GT_esmini/src/control/manualdrive/AdasCoexistenceStack.cpp`(DeriveFcwGateConfig)、
  `GT_esmini/include/gt_esmini/control/manualdrive/AdasCoexistenceStack.hpp`、
  `GT_esmini/config/virtual_driver.json`、`GT_esmini/config/manual_drive_headless_stub.json`
- 実測データ(PASS・導出値との比較根拠): mdadas_run2(2026-08-05、coordinator実行)
- 教訓の引き継ぎ先(導出値でなく実測値へ反応をアンカーする必要性): `md_fcw_warning_only.md` の「第2パス」節
