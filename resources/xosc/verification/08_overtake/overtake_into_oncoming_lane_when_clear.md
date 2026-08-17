# 対向車線を使った追い越し(対向車なし)

片側1車線の道路で、対向車が存在しない状況で自車が対向車線を使って遅い先行車を追い越すシナリオ。
同方向のパッシングレーンが無い道路でのレーン選択のフォールバックを見る。

## 検証の狙い

正例(REQ-AD-023段e、対向車線を使った追い越し)。
片側1車線の道路には同方向のパッシングレーンが存在しないため、追い越しは対向車線(+1)を使うしかない。
このシナリオには対向車が一台も配置されておらず、lookahead内に対向車がいないという条件は空虚に真となる。
したがって本ファイルが検証するのは、同方向レーンが無いときに正しく対向車線へフォールバックしてレーンを選択できるかという点と、姉妹シナリオ群と同じ追い越しトリガ・経路バジェット算術であり、対向車ギャップ安全算術(セクション7-2の(v_ego+v_opp)*t_total*safety_factor)そのものはこのファイルの対象外である。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_restricted_exit__l800_lanes1_exitm1.xodr`(直線800 m、片側1車線、末端junctionがThrough継続路とExitランプ(road0のレーン-1からのみ接続)に分岐) |
| 自車 | road0 lane-1、s=30、目標速度20.0 m/s。Route: road0(-1,s30)→road101(-1,s29.77)→road2(-1,s40) |
| 他エンティティ | 先行車(SlowLead)、road0 lane-1、s=75(バンパー間ギャップ40 m)、速度8.0 m/s。対向車は配置なし |
| 走行時間 | StopTrigger: シミュレーション時間 50 s |

## 進行

以下は2026-08-04の実測(Releaseビルド)によるトレース。

1. t=0: 自車20.0 m/s、先行車8.0 m/sでスタート。自車はこの時点で既に先行車に速度拘束されている。
2. 往路: レーン-1から対向車線(+1)へ、指示器先行時間2.95s(両車線構成の姉妹シナリオと同じlane_change_indicator_lead_time_s=3.0設定による)。
3. 対向車線上で先行車を追い越す(対向車が存在しないため対向車ギャップ判定は空虚に真)。
4. 復路: レーン-1(出口ランプ接続レーン、道路上唯一のレーン)へ復帰、指示器先行時間3.00s。
5. t>50s: StopTriggerでシミュレーション終了。

## 期待する挙動

- matcher `overtake_decision_holds`(expect_considered=true): 追い越しトリガ(delta_v=12.0 m/s、t_pass=4.83s、先行車の拘束ギャップ)は両車線構成の姉妹シナリオと同一であり、実際に発火すること。
- matcher `overtake_decision_holds`(expect_phases=[signal_out,out,pass,signal_back,back,idle]): 対向車線(+1)を使って往復し、junctionの手前でレーン-1(道路上唯一の出口接続レーン)へ復帰すること。
- matcher `overtake_decision_holds`(expect_cleared_lead=true): 復路前に先行車を実際にクリアしたこと。
- matcher `indicator_leads_lane_change`(往路、window [0.0, 8.0]、min_lead_s=2.9): 実測2.95s。
- matcher `indicator_leads_lane_change`(復路、window [9.0, 20.0]、min_lead_s=2.9): 実測3.00s。往路・復路で指示器の点灯側が反転すること(dirの符号確認)も併せて検証される。
- 対向車ギャップ安全算術(セクション7-2)自体は対向車が存在しないため本ファイルでは検証されない。対向車ありの構成は別シナリオの役目であり、このバッチの対象外。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| overtake_decision_holds | expect_considered: true | 追い越しトリガが実際に発火すること |
| overtake_decision_holds | expect_phases: [signal_out,out,pass,signal_back,back,idle] | 対向車線を使って往復完走すること |
| overtake_decision_holds | expect_cleared_lead: true | 復路前に先行車をクリアしたこと |
| indicator_leads_lane_change | window [0.0,8.0], min_lead_s 2.9 | 往路の指示器先行時間が法定水準を満たすこと |
| indicator_leads_lane_change | window [9.0,20.0], min_lead_s 2.9 | 復路の指示器先行時間が法定水準を満たすこと |

## 関連

- バッチ: `overtake_batch.yaml`
- 期待値: `overtake_into_oncoming_lane_when_clear.expectations.yaml`
- 関連ID: `vd-func:FUNC-056` / `req-vd-ad:REQ-AD-023`
