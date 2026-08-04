# 警報単独エピソード(FCWのみ・AEB非介入、手動運転中ADAS)

同一レーンから減速中の先行車へ緩やかに接近する幾何(カットイン無し)に、警報後にほどほどの強さで反応する
合成ドライバ("brake_after_warning"プロファイル)を対にしたシナリオ。

## 検証の狙い

正例(REQ-AD-025 段e、slug `md-fcw-warning-only-episode`)。
運転者が警報(FCW)に反応して自分で減速した場合、AEBの介入ゲートは一度もトリップせず、
「警報だけで済んだ」事象としてHVDに残ることを見る。同じ段eのもう一方の主張(`md-fcw-leads-intervention`、
警報が介入に先行しリードが閾値以上)は `md_aeb_stationary_lead.xosc` が担う — そちらは両方の事象が実際に起きる。

## 2026-08-05 第1パス: 全面再設計(カットイン幾何→同一レーン接近)

mdadas_run1(2026-08-04)の初回実行(カットインベースの旧幾何)で `adas_state_matches`(gt.fcw, active)が **FAIL**
(`no reported frame had gt.fcw.state_name == 'active' ... (observed: ['standby'])`)。
実測telemetryを見ると、候補は同じくt=1.75で確定していたが(`md_aeb_unresponsive`と同じカットインタイミング)、
その時点のttc=18.771、a_req=0.135と、警報ゲート(TTC<3.5 or a_req>2.0)から遥かに遠かった。
その後もgap/v_closeは緩やかにしか動かず、**観測された最小ttcは9.914、最大a_reqは0.391**(いずれも閾値に遠く届かず)。

原因は旧幾何(EgoSpeed=20, LeadSpeed=14, LeadStartS=140, 先行車が緩くブレーキ)が「AEBに介入させない」ことだけを
過剰に優先し、v_close/gapがそもそも警報を発火させるだけの接近率を作れていなかったこと。加えて、実測で判明した
もう一つの事実 — **無反応(ペダル入力ゼロ)のManualDrive車両は等速コースティングではない**。同じtelemetryから
自車速度を直接読むと、t=0.25〜4.0の間(スロットル・ブレーキとも0)で 19.863→17.875 m/s と減速しており、
Δv/Δt ≈ **0.53 m/s²** の自然減速(空気抵抗・転がり抵抗等)が働いている。旧設計はこれを未考慮で「無反応=等速」と
仮定しており、この点でも余裕を失っていた。

### 算術による再設計(推測ではなく導出)

`md_aeb_stationary_lead.md` と同じ導出を流用する(詳細はそちらを参照。しきい値の出典も同じ)。

```
警報発火(未対応の場合) t_warn   = gap0/v_close - min(3.5, v_close/4)
介入発火(未対応の場合) t_interv = gap0/v_close - min(2.5, v_close/6)
リード(未対応)         = min(3.5, v_close/4) - min(2.5, v_close/6)
```

v_close=16.0 m/sを選ぶとリード(未対応の場合)は**ちょうど1.0s**(v_close≥15の平坦域、`md_aeb_stationary_lead.md`の
表を参照)。gap0=104.0mとすると t_warn=6.5-3.5=**3.0s**、t_interv=6.5-2.5=**4.0s**。
これは`md_aeb_stationary_lead.xosc`と**全く同じv_close・gap0**であり、意図的に揃えている
(「未対応なら同じ結果になるはずの幾何に、運転者の反応だけを足す」という設計)。

第1パスは「未対応なら4.0sで介入するところを、3.0sの警報から0.1s後(t=3.1s)に運転者が反応する」設計だった。

## 2026-08-05 第2パス: mdadas_run2で判明した「導出値と実測値のズレ」による再FAILと修正

mdadas_run2で本シナリオを実行した結果、`adas_state_matches`(gt.fcw, active)が再び **FAIL**
(`no reported frame had gt.fcw.state_name == 'active'`)。
一方、**同一のv_close・gap0を共有する `md_aeb_stationary_lead.xosc` は実測で PASS しており、そちらの実測値が
本シナリオの前提を裏切っていたことが判明した**:

| | 算術導出(第1パス想定) | 実測(mdadas_run2, md_aeb_stationary_lead) |
| :-- | --: | --: |
| 警報発火 t_warn | 3.00 s | **3.30 s**(gt.fcw ACTIVE, frame 65) |
| 介入発火 t_interv | 4.00 s | **4.75 s**(gt.aeb ACTIVE, frame 94) |
| リード | 1.00 s | **1.45 s** |

導出は両方とも「安全側」(実測の方が遅い=余裕がある)にズレていた — 誤差そのものは危険ではない。
しかし第1パスの`brake_after_warning`プロファイルは**導出値t_warn=3.0を基準に0.1s後(t=3.1)**で反応していたため、
**実測の警報発火(t=3.30)より前に運転者がブレーキを踏み始めていた**。これにより閉じ速度が警報ゲートに
届く前から落ち始め、**警報そのものが一度も発火しなかった**(FAIL)。

### これは数値のズレだけの問題ではない(意味論的な誤り)

このシナリオのslugは `md-fcw-warning-only-episode` — 「運転者が**警報に反応**して回避した」というエピソードを
作ることが目的であり、警報が実際に鳴る**前**にブレーキを踏み始めるプロファイルは、そもそもこのエピソードを
表現できていない(反応する対象がまだ存在しない)。したがって今回の修正は単なる数値合わせではなく、
**プロファイルの反応時刻は「導出値」ではなく「実測された警報発火時刻」に紐付けなければならない**という
恒久的な教訓を含む。導出(算術)は幾何(v_close・gap0の組み合わせ)を設計する段階では十分役に立つが
(実際、未対応ならAEBより先にFCWが発火する、という定性的な予測は的中し、しかも安全側の誤差だった)、
**反応プロファイルのタイミングをそれだけで決めるには精度が足りない**(未モデル化のコースティング減速や
初期フレームの過渡応答が数百ms単位でズレを生む)。

### 修正内容

`profiles/brake_after_warning.json` の反応開始時刻を、実測t_warn=3.30に対して0.20sのマージンを取った
**t=3.5**へ変更(ランプ完了はt=3.8)。実測t_interv=4.75までの猶予は約0.95s。ブレーキレベル(0.6)は変更していない
— 弱すぎた場合は次回実測後に「早める/強める」で調整する(matcherパラメータには触れない、という標準ルールに従う)。

`adas_state_matches`(gt.fcw)の`min_frames`を1→**3**へ引き上げた(coordinatorの依頼)。値の根拠は本プロジェクトの
既存の3フレームデバウンス慣例(候補確定の横方向侵入デバウンスと同じ幅、`GT_esmini/config/virtual_driver.json`の
`_policy_aeb`コメント参照)に揃えたものであり、恣意的な値ではない。

## この設計の未検証部分(誠実な明示)

**brake=0.6のランプ(t=3.5〜3.8)が、実際に実測t_interv=4.75までにv_close/ttc/a_reqを介入ゲートの外側まで
押し戻せるか**は、RealVehicleBackendのAT/エンジンモデルに対してブレーキランプを積分しないと分からず、
手計算では導出できない。これは第2パスでも唯一の**未検証な推測**である(反応の開始時刻は実測から導出したが、
反応の効果そのものは未検証)。次回のcoordinator実行で実測し、外れていれば反応をより早める/強めることで対応する
— matcherのパラメータ(閾値)を緩めることでは対応しない。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、Init速度22.0 m/s(79.2 km/h)、コントローラ=ManualDriveController |
| 他エンティティ | 先行車(走行中)、road1 lane-1(**同一レーン**、カットイン無し)、s=134(gap0≈104m)、速度6.0 m/s(21.6 km/h) |
| 入力プロファイル | `profiles/brake_after_warning.json`(第2パス) — t=3.5まで全0、t=3.8で中程度ブレーキ(0.6)へランプし以後保持 |
| 走行時間 | StopTrigger: シミュレーション時間 12 s |

## 進行(第2パス後)

1. t=0: 自車Init速度22.0 m/s。先行車は同一レーンs=134で6.0 m/sの定速。v_close=16.0 m/s。
2. t=0〜3.5: 運転者(合成)は無入力。候補はほぼ最初のフレームから同一レーンの先行車として追跡される。
3. t≈3.30(実測、md_aeb_stationary_leadの同一前半区間で確認済み): TTC/必要減速度がFCWのゲートを跨ぎ、
   gt.fcwがACTIVEになる。
4. t=3.5〜3.8: 運転者が(実測された)警報に反応し、中程度のブレーキ(0.6)へランプする。
5. t≈4.75(実測、未対応なら介入発火する時刻): 運転者の減速が効いていれば、AEBの介入ゲートはトリップしない
   はずである(この一点のみ未検証、上記参照)。
6. t>12s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 警報(FCW)がエピソード中に少なくとも一度(3フレーム以上)ACTIVEになること(`adas_state_matches`)。
- AEBの介入ゲートが一度もトリップしないこと(`no_intervention_in_window`)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| adas_state_matches | function gt.fcw, expect active, mode any, after 2.5s, before 12.0s, **min_frames 3**(第2パスで1→3) | 警報が3フレーム以上ACTIVEであること(単発フリッカーの偽PASS防止) |
| no_intervention_in_window | function gt.aeb, after 0.5s, before 12.0s | AEBの介入が一度も無いこと(警報単独) |

## 関連

- バッチ: `manualdrive_adas_batch.yaml`
- 期待値: `md_fcw_warning_only.expectations.yaml`
- 入力プロファイル: `profiles/brake_after_warning.json`
- 関連ID: `req-vd-ad:REQ-AD-025` 段e / `vd-func:FUNC-075`
- 導出の元(同一のしきい値・数式): `md_aeb_stationary_lead.md`(実測t_warn=3.30/t_interv=4.75/リード1.45sもそちらに記録)
- 実測データ(第1パスの失敗根拠・コースティング減速の実測): `test_results/mdadas_run1/md_fcw_warning_only/telemetry.jsonl`(2026-08-04)
- 実測データ(第2パスの失敗根拠・導出値とのズレの根拠): mdadas_run2(2026-08-05)の `md_aeb_stationary_lead` / `md_fcw_warning_only` 結果
