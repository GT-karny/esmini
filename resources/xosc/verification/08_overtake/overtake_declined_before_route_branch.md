# 経路分岐直前での追い越し見送り(経路バジェットガード)

自車が経路分岐(出口ランプ)に近い位置から遅い先行車に接近するシナリオ。
追い越しトリガ自体は成立するが、経路バジェットガードが追い越しを見送ることを見る。

## 検証の狙い

負例(REQ-AD-024段b、経路バジェットガード)。
姉妹シナリオ(overtake_slow_lead_on_two_lane_road)と同一の道路・同一のv_pass/v_lead/g0を使い、トリガ条件(delta_v、ギャップ拘束)は同一にしたまま、自車の開始位置だけを分岐に近づけている。
その結果dist_to_connectionがrequiredより小さくなり、ガードが正しく追い越しを拒否する。
このシナリオが検証すべき最重要点は、ガードが拒否したことと、トリガがそもそも発火していないことの区別である。
トリガが発火しないまま終わるシナリオはガードを何もテストしていない偽PASSになるため、expect_considered:trueとexpect_blocked_reason:route_budgetの両方を同時に主張する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_restricted_exit__l800_lanes2_exitm2.xodr`(姉妹シナリオと同一) |
| 自車 | road0 lane-2、s=400、目標速度20.0 m/s。Route: road0(-2,s400)→road101(-1,s30.1)→road2(-1,s40) |
| 他エンティティ | 先行車(SlowLead)、road0 lane-2、s=445(バンパー間ギャップ40 m、姉妹シナリオと同じ中心間隔45 m)、速度8.0 m/s |
| 走行時間 | StopTrigger: シミュレーション時間 30 s |

## 進行

1. t=0: 自車20.0 m/s、先行車8.0 m/sでスタート(トリガ条件は姉妹シナリオと同一)。
2. dist_to_connection=800-400=400.0 mに対しrequired=496.667 mであり、96.667 m不足するため経路バジェットガードが追い越しを拒否する。
3. 自車はlead policy(IDM追従)により先行車の後ろにキューして走行を続ける。
4. dist_to_connectionは自車の前進とともにさらに縮むため、requiredとの不足は走行時間中ずっと拡大し続ける。
5. t>30s: StopTriggerでシミュレーション終了。

## 期待する挙動

2026-08-04にReleaseビルド（esmini GIT REV v3.4.1-872-67b74a60）で実測済み。
`considered=true`・`blocked_reason=route_budget`・`phase=idle`のいずれも全1240フレームで成立し、4件のmustが全通過した。
phaseがidleのまま動かないことが「見送り」であることの証拠で、いったん追い越しに入ってから打ち切る`overtake_aborted_for_route_branch`とはここで区別される。
不足量(96.667 m)は僅差ではなく明確な不足として設計されているため、判定の感度は低いと見積もられている。

- matcher `overtake_decision_holds`(expect_considered=true): トリガ条件(delta_v=12.0 m/s、t_pass=4.83s、先行車の拘束ギャップ)は姉妹シナリオと同一であり、実際に発火しなければこのシナリオはガードを何もテストしていないことになる。
- matcher `overtake_decision_holds`(expect_blocked_reason=route_budget): 拒否の理由が経路バジェットガードであること(gap/oncoming/no_passing_lane/suppressed等の別理由ではないこと)。
- matcher `overtake_decision_holds`(forbid_phases=[signal_out,out,pass,signal_back,back]): 状態機械がidleから一度も出ないこと。
- matcher `overtake_decision_holds`(expect_cleared_lead=false): 追い越しを一度も試みていないため、先行車のクリアも一度も起きないこと。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| overtake_decision_holds | expect_considered: true | 追い越しトリガが実際に発火すること |
| overtake_decision_holds | expect_blocked_reason: route_budget | 拒否理由が経路バジェットガードであること |
| overtake_decision_holds | forbid_phases: [signal_out,out,pass,signal_back,back] | 状態機械がidleから出ないこと |
| overtake_decision_holds | expect_cleared_lead: false | 先行車のクリアが一度も起きないこと |

## 関連

- バッチ: `overtake_batch.yaml`
- 期待値: `overtake_declined_before_route_branch.expectations.yaml`
- 関連ID: `vd-func:FUNC-056` / `req-vd-ad:REQ-AD-024`
