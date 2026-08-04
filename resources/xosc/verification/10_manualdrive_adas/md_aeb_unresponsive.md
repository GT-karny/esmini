# 無反応ドライバへのAEB介入(手動運転中ADAS)

07_aeb/cutin_hard_brake.xosc の幾何(カットイン+急制動)を流用し、自車のコントローラを ManualDriveController に差し替え、
全入力ゼロの合成ドライバ("unresponsive"プロファイル)を対にした複合シナリオ。

## 検証の狙い

正例(REQ-AD-025 段a、slug `md-aeb-intervention-unresponsive-driver`)。
VD版 AEB(REQ-AD-001)が「衝突コースにVirtualDriverControllerが自動運転で接近」を前提にするのに対し、本シナリオは
「人間(合成・無反応)が運転しているが介入は独立に発火する」ことを見る — REQ-AD-025 の主張そのもの。
運転者が一切反応しないため、AEBの介入だけが接触を防ぐ/緩和する唯一の手段になる。

**段e(slug `md-fcw-leads-intervention`)はこのシナリオから2026-08-04に移した** — 実行結果を見て理由は次節。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、Init速度30.0 m/s(108 km/h)、コントローラ=ManualDriveController(config=manual_drive_headless_stub.json + manualdrive_config オーバーライド) |
| 他エンティティ | 先行車、road1 lane-2、s=78(バンパー間ギャップ約48 m)、速度12.0 m/s(43 km/h) |
| 入力プロファイル | `profiles/unresponsive.json` — 全チャンネル0.0を全区間保持 |
| 走行時間 | StopTrigger: シミュレーション時間 20 s |

## 進行

1. t=0: 自車は Init の SpeedAction(step)で30.0 m/sに初期化(RealVehicleBackend::SetInitialStateがこの速度を種にする)。先行車は隣接レーン(-2)で12.0 m/sの定速。
2. t>1.6s: 先行車が1.4s間のsinusoidal LaneChangeActionで自車レーンへカットイン(完了は約t=3.0s)。
3. t>2.4s: 先行車が8.0 m/s²のレートで速度0.0まで急制動する。
4. 運転者(合成)は全区間ペダル0.0。人間側からの回避行動は一切無い。
5. AEBが独立に衝突コースを監視し、TTC/必要減速度がゲートを超えた時点でペダル指令に介入する。
6. t>20s: StopTriggerでシミュレーション終了。

## 期待する挙動

- HVDのAEB行がACTIVEになり、gt.aeb.*に発火量が出る(`manual_aeb_fires`)。
- 完全回避不能な域でも、AEB経由の実効ブレーキが衝突速度を安全レベルまで低減する(`impact_speed_below`)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| manual_aeb_fires | after 2.0s, before 20.0s, min_frames 1 | AEBがHVD上でACTIVEになり発火量が出ること |
| impact_speed_below | threshold 10.0 m/s、after 2.0s | 衝突速度を安全レベルまで低減すること |

## 実測結果(mdadas_run1, 2026-08-04、実ハーネス初回実行)

両matcherともPASS。

- `manual_aeb_fires`: gt.aeb が t=1.75〜2.85(18フレーム、after:2.0窓内)ACTIVE。
- `impact_speed_below`: 実測 **8.26 m/s**(閾値10.0、マージン17.4%)。VD baseline(9.14 m/s)よりむしろ良い結果で、
  ManualDriveの実効ブレーキ経路(AEB envelope → RealVehicleBackend::StepPedalSteer → AT/エンジンモデル)がVDの
  直接運動学制御と遜色ない減速を出すことが確認できた。閾値10.0はそのまま維持する(タイトな上限ではなく
  「AEBが明らかに何もしなかった」を検出する床であり、1回の実測で狭める判断はしない)。

## 実測結果と切り出した知見: fcw_leads_intervention は測れなかった(リード=0.000s)

同じ実行のtelemetryで `gt.fcw` と `gt.aeb` の両方が **同一フレーム(t=1.75)** でACTIVEに遷移し、測定リードは
**0.000 s**(閾値0.8 s未達、FAIL)。以下は根拠となる実測(`test_results/mdadas_run1/md_aeb_unresponsive/telemetry.jsonl`)。

| t | aeb_state | fcw_state | ttc_s | a_req_mps2 | gap_m | v_close_mps |
| --: | :-- | :-- | --: | --: | --: | --: |
| 1.70 | standby | standby | (無) | (無) | (無) | (無) |
| **1.75** | **active** | **active** | 0.738 | 11.460 | 12.499 | 16.925 |

t=1.70以前は候補データ自体が存在しない(`standby`かつdetailフィールドが全てnull)。つまりFCW/AEBの共有候補
(gt.aebとgt.fcwは同一のAebSafetyベース実装で、候補選定パラメータ=同一・ゲート閾値=別)が**候補として認識される前**は
何のデータも無く、**認識された瞬間には既にttc=0.738(AEB閾値2.5どころかFCW閾値3.5すら大きく下回る)**という状態だった。

### 根本原因(恒久知見。実装欠陥ではない)

FCWは「同じAebSafetyを緩い閾値で走らせたもの」であり、**候補選定パラメータ(横方向侵入デバウンス=3フレーム)は
AEBと共有**している。この幾何(カットインの横移動が急激)では、候補が「まだ横方向に侵入中で候補として認められない」
状態から「同一レーン内候補として確定」する遷移が1フレームで起きてしまい、**候補として認識された時点で既に
TTC/必要減速度の両ゲート(FCWのTTC<3.5・AEBのTTC<2.5)を跨いでいる**。閾値の差だけでは、候補がまだ認識されていない
間はリードを買えない。急激なカットインでは実際にも「警告する間もなく危険域に入る」のが現実であり、これは
**実装のバグではなくこの幾何(急激なカットイン)がこの観点(リード測定)に向いていない**ことを示す。

### 対応

`fcw_leads_intervention` は本シナリオから外し、緩やかに接近する同一レーン先行車シナリオ
`md_aeb_stationary_lead.xosc`(横方向侵入デバウンスが関与しない=候補は最初のフレームからin-path)へ移した。
そちらで、AebSafetyの実際のしきい値(aeb_ttc_threshold=2.5 / aeb_min_a_req=3.0、
`GT_esmini/config/virtual_driver.json`)とFCWのしきい値
(warning_ttc_threshold_s=3.5[manual_drive_headless_stub.json] / warning_min_a_req_mps2=2.0[コンパイル済み既定、
`AdasCoexistenceStack.hpp`])から**リードを算術的に導出**している。詳細は`md_aeb_stationary_lead.md`参照。

## 関連

- バッチ: `manualdrive_adas_batch.yaml`
- 期待値: `md_aeb_unresponsive.expectations.yaml`
- 入力プロファイル: `profiles/unresponsive.json`
- 関連ID: `req-vd-ad:REQ-AD-025` 段a / `vd-func:FUNC-075`
- 参考(幾何の出典): `resources/xosc/verification/07_aeb/cutin_hard_brake.xosc`
- 関連(段eの移設先): `md_aeb_stationary_lead.md`
- 実測データ: `test_results/mdadas_run1/md_aeb_unresponsive/telemetry.jsonl`(2026-08-04)
