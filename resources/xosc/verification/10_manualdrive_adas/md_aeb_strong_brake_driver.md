# 強ブレーキ中のAEB非重畳(手動運転中ADAS)

07_aeb/cutin_hard_brake.xosc の幾何(カットイン+急制動)を流用し、自車のコントローラを ManualDriveController に差し替え、
AEB要求以上のブレーキを既に踏んでいる合成ドライバ("strong_brake_at"プロファイル)を対にしたシナリオ。

## 検証の狙い

正例(REQ-AD-025 段c、slug `md-aeb-brake-not-stacked`)。
人間が既にAEBの要求量以上のブレーキを踏んでいる場合、AEBはその上に追加のブレーキを重畳してはならない
(実効ブレーキは人間値とAEB要求値のmax合成であるべきで、和ではない)。
本シナリオの狙いは衝突回避/緩和そのものではなく、ブレーキ指令の**合成方式**である
(幾何はcutin_hard_brake.xoscと同一で、完全回避不能な域である事実は変わらない — cutin_hard_brake.md参照)。

## 2026-08-05 リテューン: 初回実行でSKIPした原因と対処

mdadas_run1(2026-08-04)の初回実行で `brake_not_stacked` が **SKIP**(`'gt.aeb' never ACTIVE over 335
reported frame(s) -- nothing to check for stacking`)。原因は単純なタイミングの見誤り: 当初のプロファイル
(ブレーキ開始t=3.0)は、この幾何の実測AEB作動区間(t=1.75〜2.85、その後~t=2.9で接触)の**完全に後**に
位置していた(md_aeb_unresponsive.xosc / md_aeb_unresponsive.md の実測参照)。窓が空振りしていただけであり、
matcherが「何も無かったので何も確認できない」と正しく報告していた。

対処: `profiles/strong_brake_at.json` のブレーキ立ち上がりを t=1.55〜1.60(実測admission t=1.75の直前)へ前倒しし、
判定窓もt=1.65〜3.0(実測作動区間に収まる範囲)へ retune した。tolerance・min_lead_sなどmatcherのパラメータ自体は
一切変更していない(座標=幾何・タイミングのみを動かした)。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、Init速度30.0 m/s(108 km/h)、コントローラ=ManualDriveController |
| 他エンティティ | 先行車、road1 lane-2、s=78(バンパー間ギャップ約48 m)、速度12.0 m/s(43 km/h) |
| 入力プロファイル | `profiles/strong_brake_at.json`(retune後) — t=1.55まで全0、t=1.60で全ブレーキ(1.0)へランプし以後保持 |
| 走行時間 | StopTrigger: シミュレーション時間 20 s |

## 進行(retune後)

1. t=0-1.55: 自車Init速度30.0 m/s。運転者はペダル入力ゼロ(unresponsiveと同じ立ち上がり)。カットイン(t>1.6で開始)が進行中。
2. t=1.55-1.60: 運転者のブレーキが0から1.0へ急速にランプする。brake=1.0は物理max_dec=10.0 m/s²に相当し、
   adas_brake_full_decel_mps2の既定値8.0 m/s²を上回る。
3. t≈1.75: AEBの候補がこの幾何で実測どおり確定し(md_aeb_unresponsive.xosc/mdでの実測: ttc=0.738, a_req=11.46)、
   AEB自身のブレーキ要求(gt.aeb.brake_request)も実測ではほぼ即座に1.0近くへ飽和する(a_req=11.46が
   adas_brake_full_decel_mps2=8.0を大きく超えているため)。運転者は既にt=1.60から全ブレーキを保持しているため、
   実効ブレーキは人間値(既に最大)から変化しないはずである。
4. t≈2.85-2.9: 接触(md_aeb_unresponsiveの実測と同じ幾何)。AEBの候補は失われ標準に戻る。
5. t>20s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 運転者のブレーキ入力がAEB要求以上である区間で、実効ブレーキコマンドが人間値と一致し(max合成)、上乗せされないこと(`brake_not_stacked`)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| brake_not_stacked | function gt.aeb, tolerance 0.05(**GUESS**、変更なし), after 1.65s(retune), before 3.0s(retune), min_frames 1 | 実効ブレーキが人間値のmax合成であり和でないこと |

## この幾何固有の限界(誠実に記録する未解決点)

mdadas_run1の実測により判明した重要な事実: **この(cutin_hard_brakeベースの)幾何ではAEBのブレーキ要求
(gt.aeb.brake_request)が候補確定の瞬間からほぼ1.0に飽和している**(a_req=11.46が
adas_brake_full_decel_mps2=8.0を大きく超えるため)。つまり運転者側も1.0(最大)まで踏まないと「AEB要求以上」を
満たせず、両者とも1.0という**ペダル上限で天井打ちした状態**でしか比較できない。

これは、実効ブレーキが「正しくmax(人間, AEB)=1.0でクランプされた」のか「本当は1.0を超えて和算されたが
出力側で1.0にクランプされた」のかを、**この資産だけでは区別できない**ことを意味する(どちらも観測上
1.0にしか見えない)。天井打ちしない(a_reqが8.0未満で飽和しない)ゲンな幾何、例えば
`md_aeb_stationary_lead.xosc`スタイルの緩やかな同一レーン接近を使えば、AEB要求が0-1のどこかで
非飽和のまま推移する時間帯を作れる可能性があるが、今回のretuneは「既存幾何のタイミングを直す」依頼の
範囲内で行っており、幾何そのものの再設計はスコープ外と判断した。

これは検証計画§4-2が元々このmatcherに割り当てていた設計(E2EはGREEN担保のみ、sum-vs-max の
厳密な弁別はC++単体テストの責務)を**弱めるものではなく、むしろその設計判断が正しかったことを実測が裏付けた**
形になる — 「なぜE2Eで赤実証しにくいか」の具体的理由が、この幾何については「AEB要求が即座に飽和するため
non-trivialな比較区間を作れない」という形で判明した。

## 数値の出典・要校正の明示

- toleranceの0.05(ペダル0-1スケール)は依然として**推測**。ビルド前は実測ノイズフロアが取れなかったため
  未校正のまま据え置いている(matcherパラメータは今回のretune対象外)。
- t=1.55/1.60のブレーキオンセット・ランプ時間は、md_aeb_unresponsiveの実測(候補確定t=1.75)から**逆算した値**
  (推測ではなく導出)。判定窓(after 1.65, before 3.0)も同じ実測(作動区間1.75-2.85、接触~2.9)から導出。

## 関連

- バッチ: `manualdrive_adas_batch.yaml`
- 期待値: `md_aeb_strong_brake_driver.expectations.yaml`
- 入力プロファイル: `profiles/strong_brake_at.json`
- 関連ID: `req-vd-ad:REQ-AD-025` 段c / `vd-func:FUNC-075`
- 参考(幾何の出典): `resources/xosc/verification/07_aeb/cutin_hard_brake.xosc`
- 実測データ(retuneの根拠): `test_results/mdadas_run1/md_aeb_unresponsive/telemetry.jsonl`(2026-08-04、同一幾何の無反応ドライバ実行)
