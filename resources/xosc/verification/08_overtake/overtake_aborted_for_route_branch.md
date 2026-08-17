# 経路ホップ締切による追い越しの打ち切り(安全弁)

自車が追い越し中盤に入った後、先行車がEgo自身の巡航速度を超えて加速し、二度と追い越しきれなくなるシナリオ。
経路ホップの締切が到来した時点で追い越しを打ち切り復帰する安全弁を見る。

## 検証の狙い

REQ-AD-024段c(セクション5-2の安全弁: 追い越し中に経路ホップの締切がSlowLeadクリア前に到来したら、追い越しを打ち切って元のレーンへ戻る)を検証する。
自車は姉妹シナリオ(overtake_slow_lead_on_two_lane_road)と同一の開始位置・経路バジェット(margin 273.33 m、余裕あり)を持つため、経路バジェットガード自体はブロックしない。
t=8.0sで先行車が8.0 m/sから25.0 m/s(自車の巡航速度20.0 m/sより速い)へ加速し、自車は以後二度と先行車をクリアできなくなる。
PASSフェーズを終える唯一の経路が経路ホップ締切による強制復帰だけになるよう設計されている。

overtake_batch.yaml側のコメントは本ファイルを指してCONFIDENCE: LOW(ビルド前作成、実測なし)と述べているが、これは本ファイルの旧版(v2、t=9.2sで先行車のdelta_vを12.0→0.1へ縮める設計)についての記述である。
本ファイル自身のヘッダ(REVISION HISTORY)によれば、現行版(v3、2026-08-04)はC++実装側の2件の修正(安全弁がSIGNALしきい値で開くこと、SIGNAL_BACKが締切到来時もdwellとgap受理を省略しないこと)を反映した上で実機ビルドに対して実測されており、7件すべてのmustがPASSしている。
overtake_batch.yaml側のコメントはこの書き直しに追随しておらず、古い設計を指したままである可能性が高い。
以上の経緯を踏まえ、本ファイルの現行版については実測結果に基づいて記述する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_restricted_exit__l800_lanes2_exitm2.xodr`(姉妹シナリオと同一) |
| 自車 | road0 lane-2、s=30、目標速度20.0 m/s。Route: road0(-2,s30)→road101(-1,s30.1)→road2(-1,s40) |
| 他エンティティ | 先行車(SlowLead)、road0 lane-2、s=75(バンパー間ギャップ40 m)、初速8.0 m/s、t>8.0sでstepにより25.0 m/sへ加速 |
| 走行時間 | StopTrigger: シミュレーション時間 60 s |

## 進行

以下は2026-08-04の実測(Releaseビルド、GIT REV v3.4.1-863-9488c193-dirty)によるトレース。

1. t=0: 自車20.0 m/s、先行車8.0 m/sでスタート。
2. t=0.05: signal_out(considered=true、delta_v=12.0、lead_id=1)。
3. t=3.00: out(lc_armed=true、dir=+1、往路の指示器先行時間2.95s)。
4. t=6.40: pass。
5. t=8.0: 先行車が25.0 m/sへ加速(自車の巡航速度20.0 m/sを上回る)。以後自車は先行車をクリアできなくなる。
6. t=31.75: signal_back(route_budget_m=199.14、SIGNALしきい値=単ホップARMしきい値140 m+指示器dwell分60 mの約200 mに到達、blocked_reason=route_budget)。
7. t=34.75: back(lc_armed=true、dir=-1、復路の指示器先行時間3.00s、route_budget_m=139.1=ARMしきい値ちょうど)。
8. t=38.15: idle。
9. t>60s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 追い越しトリガは姉妹シナリオと同一条件で発火する(expect_considered=true)。
- 状態機械はsignal_out→out→pass→signal_back→back→idleの全フェーズを通るが、signal_backへの遷移は自然なクリアではなく経路ホップ締切による強制復帰である。
- 自車は先行車を一度もクリアしない。cleared_leadは全フレームでfalseのまま(expect_cleared_lead=false)。これは姉妹シナリオ(cleared_lead=trueで完走)と正反対の極性であり、両シナリオが同じフィールドの両極性を実証して初めて、このフィールドが定数化していないことの証拠になる。
- 復帰の理由が経路バジェット(安全弁)であることをblocked_reasonが示す(expect_blocked_reason=route_budget)。
- 往路・復路とも指示器先行時間は締切による強制復帰でも省略されない(往路2.95s、復路3.00s、いずれも法定3.0sの1フレーム分許容内)。
- 復帰後もroute_lane_plan_holdsが逸脱ゼロを保つ(max_deviations=0)。安全弁が経路を保護できていることの確認。
- cleared_leadフィールドについては、過去のリビジョンでコントローラ側の欠陥(復帰理由フラグとクリア判定フラグが同一のflagから導出されていた)が見つかり、2026-08-04にControllerVirtualDriver.cppで修正済みである。現行のexpect_cleared_lead=falseはこの修正後の実測(全1240フレームでfalse)に基づく。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| overtake_decision_holds | expect_considered: true | 追い越しトリガが実際に発火すること |
| overtake_decision_holds | expect_phases: [signal_out,out,pass,signal_back,back,idle] | 状態機械が最後まで完走すること |
| overtake_decision_holds | expect_cleared_lead: false(全run) | 先行車を一度もクリアしていないこと |
| overtake_decision_holds | expect_blocked_reason: route_budget | 復帰が経路バジェット安全弁によるものであること |
| indicator_leads_lane_change | window [0.0,8.0], min_lead_s 2.9 | 往路の指示器先行時間が法定水準を満たすこと |
| indicator_leads_lane_change | window [25.0,45.0], min_lead_s 2.9 | 復路の指示器先行時間が締切下でも法定水準を満たすこと |
| route_lane_plan_holds | max_deviations: 0 | 安全弁作動後も経路計画からの逸脱が無いこと |

## 関連

- バッチ: `overtake_batch.yaml`
- 期待値: `overtake_aborted_for_route_branch.expectations.yaml`
- 関連ID: `vd-func:FUNC-056` / `req-vd-ad:REQ-AD-024`
