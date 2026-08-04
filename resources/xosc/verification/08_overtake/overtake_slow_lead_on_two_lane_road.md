# 片側2車線での遅い先行車の追い越し(成立・復帰)

自車が遅い先行車を同方向の追い越しレーンで追い越し、出口ランプへ接続するレーンへ戻るシナリオ。
経路バジェットに大きな余裕を与えた、追い越しの成立と復帰だけを見るクリーンな成功例。

## 検証の狙い

正例(REQ-AD-023段a-c、追い越しマヌーバの成立)。
同方向のパッシングレーン経路(隣接レーンのうちidが0に近い側)を、経路バジェットガードから切り離して単独で検証する。
経路バジェットは大きな余裕(margin 273.33 m)を与えているため、このシナリオは追い越しが成立し出口ランプへ接続するレーンへ復帰するかだけを測る。
ガードが正しく拒否するかはovertake_declined_before_route_branch、安全弁が正しく打ち切るかはovertake_aborted_for_route_branchの役目。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_restricted_exit__l800_lanes2_exitm2.xodr`(直線800 m、片側2車線、末端junctionがThrough継続路とExitランプ(road0のレーン-2からのみ接続)に分岐) |
| 自車 | road0 lane-2、s=30、目標速度20.0 m/s。Route: road0(-2,s30)→road101(-1,s30.1)→road2(-1,s40) |
| 他エンティティ | 先行車(SlowLead)、road0 lane-2、s=75(バンパー間ギャップ40 m)、速度8.0 m/s、ObjectControllerなし(esmini既定のレーン追従) |
| 走行時間 | StopTrigger: シミュレーション時間 50 s |

## 進行

以下は2026-08-04の実測(Releaseビルド)によるトレース。

1. t=0: 自車は20.0 m/s、先行車は8.0 m/sでスタート。自車はこの時点で既に先行車に速度拘束されている。
2. t=0.05: signal_out(方向指示灯点灯、lamp=L、armed=False)。
3. t=3.00: out(armed=True、dir=+1、往路の指示器先行時間2.95s)。
4. t=5.00: レーン-1(追い越しレーン)への移動完了。
5. t=6.40: pass(先行車の追い越し完了位置に到達)。
6. t=10.25: signal_back(lamp=R点灯)。
7. t=13.25: back(armed=True、dir=-1、復路の指示器先行時間3.00s)。
8. t=15.15: レーン-2(出口ランプ接続レーン)への復帰完了。
9. t=16.65: idle(状態機械が待機状態へ戻る)。
10. t>50s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 経路バジェットは十分な余裕を持つ(required=496.667 m、dist_to_connection=770.0 m、マージン273.33 m)ため、追い越しは一度もブロックされない。
- matcher `overtake_decision_holds`(expect_considered=true): 追い越しトリガ(delta_v=12.0 m/s、t_pass=4.8333s<=overtake_max_pass_time_s=10.0)が少なくとも一度は成立すること。
- matcher `overtake_decision_holds`(expect_phases=[signal_out, out, pass, signal_back, back, idle]): 状態機械が最初から最後まで通ること。idleで始まらないのは自車がフレーム1から既に先行車に拘束されているためであり、しきい値の緩和ではない。
- matcher `overtake_decision_holds`(expect_cleared_lead=true): 復路へ移る前に先行車を実際にクリアしたこと(g1+L_ego/2+L_lead/2=13.0 m以上前方)。
- matcher `indicator_leads_lane_change`(往路、window [0.0, 8.0]、min_lead_s=2.9): 実測2.95s。2.9は法定3.0s(lane_change_indicator_lead_time_s)から1フレーム分(dt=0.05)を差し引いた値。
- matcher `indicator_leads_lane_change`(復路、window [9.0, 20.0]、min_lead_s=2.9): 実測3.00s。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| overtake_decision_holds | expect_considered: true | 追い越しトリガが実際に発火すること |
| overtake_decision_holds | expect_phases: [signal_out,out,pass,signal_back,back,idle] | 状態機械が最後まで完走すること |
| overtake_decision_holds | expect_cleared_lead: true | 復路前に先行車をクリアしたこと |
| indicator_leads_lane_change | window [0.0,8.0], min_lead_s 2.9 | 往路の指示器先行時間が法定水準を満たすこと |
| indicator_leads_lane_change | window [9.0,20.0], min_lead_s 2.9 | 復路の指示器先行時間が法定水準を満たすこと |

## 関連

- バッチ: `overtake_batch.yaml`
- 期待値: `overtake_slow_lead_on_two_lane_road.expectations.yaml`
- 関連ID: `vd-func:FUNC-056` / `req-vd-ad:REQ-AD-023`
