# 出口ランプへ向けた自発的車線変更の発起（隣接車あり・ギャップ受容）

`lane_change_to_exit_ramp.xosc`の姉妹シナリオ。
自車をroad0/lane-2から出発させ、lane-3に隣接車（AdjacentTraffic）を1台配置することで、-2→-3→-4の2ホップの車線変更にギャップ受容判定を絡めて検証する。

## 検証の狙い

vd-func:FUNC-055 / req-vd-ad:REQ-AD-017段cの検証を、無条件受容ではなく実際のギャップ待ちを伴う形で行う。
無印（隣接車なし）シナリオが発起の判断そのものを切り出すのに対し、本シナリオはギャップ受容を刺激する。
自車をlane-1から出発させる3ホップ構成は実測でdeviation_count=1のFAILとなった。
バグではなく、1ホップ目のギャップ待ちだけで190m予算のうち約78m消費し、残り2ホップ（必要約94m）に対して残り65mしか残らないという距離設計上の問題だったため、出発レーンをlane-2に変更し2ホップ構成へ再構成した。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `highway_example_with_merge_and_split.xodr`（junction 1。無印シナリオと同一の道路・接続構造） |
| 自車 | road0/lane-2/s=10出発、目標速度13.889 m/s（Cruiseイベント、t=0開始、3.0秒のlinearランプ）。Route: road0/-2/s=10 -> road4/-1/s=50 -> road2/-1/s=40 |
| 他エンティティ | AdjacentTraffic（car_yellow）。road0/lane-3/s=10（自車と同一sの隣レーン）、速度8.333 m/s（Init、step、ObjectControllerなし=既定のレーン追従） |
| 走行時間 | StopTrigger: シミュレーション時間28 s |

## 進行

1. t=0: 自車はlane-2、AdjacentTrafficはlane-3、両者とも同一sから出発する。自車13.889 m/s、AdjacentTraffic 8.333 m/sで、自車の方が速い。
2. t=7.55s付近まで: -2→-3への1回目の車線変更のギャップ判定がAdjacentTrafficにより保留される（実測: 1回目のarmまでにlead_gapで32フレーム、rear_gapで46フレーム拒否）。
3. t=7.55s: ギャップが受容され1回目の車線変更がarmされる。
4. t=7.55〜14.30s: -2→-3→-4の2回の車線変更が単一の連続armセグメントとして実行される（完了したホップがdisarmし次のホップが同一フレームで再armするため、armed状態は途切れない）。
5. t=28s: StopTrigger成立。

## 期待する挙動

- ギャップ待ちを挟みながらも2回の車線変更が接続点までに完了し、deviation_countは0に留まる。
- 目標レーン帯は{-4}で無印シナリオと一致する（road0滞在中を捉えるEXISTS判定）。road2へ遷移した最終フレームではtarget_lanesは[-1]に変わる（road2の帯）。
- ウインカーは車線変更のarm判定より最低3.0秒前に点灯する。この値は法定フロア（lane_change_indicator_lead_time_s）そのものであり、ギャップ待ちの長さに依存しない下限として設定されている。expectations.yaml記載の実測（2026-08-03）ではt_sig=0.55, t_arm=7.55, lead=7.00s。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| route_lane_plan_holds | max_deviations=0 | ギャップ待ちを挟んでも2回の車線変更が接続点までに完了することを確認 |
| route_lane_plan_holds | expect_target_lanes=[-4] | 目標レーン帯が無印シナリオと同じ{-4}に解決されることを確認 |
| indicator_leads_lane_change | min_lead_s=3.0 | ウインカーが法定フロア3.0秒以上前に点灯することを確認（ギャップ待ちの長さに依存しない下限） |

## 関連

- バッチ: `route_lane_batch.yaml`（policies: [lane_change_initiation], report: route_lane）
- 期待値: `lane_change_to_exit_ramp_with_traffic.expectations.yaml`
- 関連ID: `vd-func:FUNC-055` / `req-vd-ad:REQ-AD-017`
