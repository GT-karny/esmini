# 出口ランプへ向けた自発的車線変更の発起（隣接車なし）

自車のみのEntities構成で、highway_example_with_merge_and_split.xodrのjunction 1を、road0/lane-1からroad4経由でroad2へ向かう。
lane_change_initiation_enabledを有効化した状態で、-1→-2→-3→-4の3ホップの自発的車線変更が接続点までに完了するかを検証する。

## 検証の狙い

vd-func:FUNC-055（AD発起の自発的車線変更）/ req-vd-ad:REQ-AD-017受入ラダー段c（「接続点までに目標レーンへ自発的に車線変更する」）を検証する。
Entitiesに自車のみを置くことで隣接レーンは常に無条件に空車と扱われ、あらゆるギャップ受容判定は自明に成立する。
これにより「車線変更を発起する判断そのもの」を、ギャップ受容ロジックから切り離して検証する。
隣接車を加えたギャップ受容側の検証は、姉妹シナリオ`lane_change_to_exit_ramp_with_traffic.xosc`が担う。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `highway_example_with_merge_and_split.xodr`（junction 1。road0のlane-4のみがconnection id=3経由でroad2へ接続。lane-4はs=50でゼロ幅から始まりs=175で3.0m幅に達するテーパー） |
| 自車 | road0/lane-1/s=10出発、目標速度13.889 m/s（Cruiseイベント、t=0開始、3.0秒のlinearランプ）。Route: road0/-1/s=10 -> road4/-1/s=50 -> road2/-1/s=40（road4を明示した3 Waypoint、Route::IsValid()==trueに必須） |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間28 s |

## 進行

1. t=0: road4明示の3 WaypointのRouteが割り当てられる。lane_change_initiation_enabled=trueはバッチのpolicies: [lane_change_initiation]により注入される。
2. t=0: dist_to_connectionは190m（road0は200m長、自車初期s=10）。n_remaining=3の必要距離required_m = 3*max(13.889*6.0, 40.0)+20 = 270m > 190mであるため、1回目の車線変更はフレーム1で即座に決定される（待機なし）。
3. t=0〜: -1→-2→-3→-4と3回連続で車線変更を実行しながらroad0を横断し、road4、続いてroad2へ遷移する。
4. t=28s: StopTrigger成立。

## 期待する挙動

- 隣接車が存在しないため、3回の車線変更は接続点までにすべて完了し、deviation_countは全run通じて0に留まる。
- RouteLanePlanが解決する目標レーン帯は{-4}である。
- ウインカーは車線変更のarm判定より最低2.0秒前に点灯する。expectations.yaml記載の実測（2026-08-03、forward-projection則section 11-11導入後）ではt_sig=0.40, t_arm=2.70, lead=2.30s。
- expectations.yaml notes記載の実測（2026-08-02、PASS）: -1→-2→-3→-4の3回の車線変更を190m予算内で完了し、road4、road2へ遷移、deviation_countは全run通じて0。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| route_lane_plan_holds | max_deviations=0 | 隣接車なしで3回の車線変更を接続点までに完了し、逸脱が発生しないことを確認 |
| route_lane_plan_holds | expect_target_lanes=[-4] | 目標レーン帯が{-4}に解決されることを確認 |
| indicator_leads_lane_change | min_lead_s=2.0 | ウインカーがarm判定より最低2.0秒前に点灯することを確認 |

## 関連

- バッチ: `route_lane_batch.yaml`（policies: [lane_change_initiation], report: route_lane）
- 期待値: `lane_change_to_exit_ramp.expectations.yaml`
- 関連ID: `vd-func:FUNC-055` / `req-vd-ad:REQ-AD-017`
