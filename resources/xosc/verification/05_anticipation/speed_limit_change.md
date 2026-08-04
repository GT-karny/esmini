# 制限速度低下区間手前の滑らかな減速

直線路を巡航中の自車に、s=250の「制限速度」ランドマーク手前でコマンドされた減速アクションが発行されるシナリオ。
カーブ/右折系シナリオと異なり、この減速はStory Eventによる明示的なSpeedActionであり、Phase 1の時点でも滑らかに追従できることを確認する陽性対照。

## 検証の狙い

xosc内コメントによれば、これはSpeedActionダイナミクスの再構成経路を検証する：目標速度への追従がdeceleration_profile_smooth / speed_reduction_before_landmark matcherに対して滑らかであることを、Phase 2の中長期プランナーを介さずに確認する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m.xodr`（直線路） |
| 自車 | 初期位置 road1 lane-1 s=20。Story Eventで目標速度13.889 m/sへ加速後、走行距離150 mでLimitSpeed=8.33 m/sへ減速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間30 s超過 |

## 進行

1. t=0: Story Event「Cruise」開始、目標速度13.889 m/sへ3.0 sかけてlinearに加速。
2. 走行距離が150.0 mに達した時点でStory Event「Slow」開始、目標速度8.33 m/s(30 km/h)へ5.0 sかけてsinusoidalに減速。
3. route上をroad1 s=20からs=450まで(fastest)走行する。
4. t=30 s到達でStopTrigger成立、シナリオ終了。

## 期待する挙動

- t=4.0〜11.0 sの間、速度が13.0 m/sを上回っている（13.889 m/sのクルーズに到達済み）。
- 制限速度ランドマーク(road1 s=250)より手前で、目標速度8.33 m/s(許容差0.8)まで減速している。
- 減速フェーズ(s=250進入、t=10.0 s以降)で、減速度が3.5 m/s^2以下、jerkが3.0 m/s^3以下(smooth_window=5)に収まる。カーブ/右折系シナリオより厳しめのcomfort閾値。
- 走行を通して(横方向アクションなし)road1 lane-1のレーンを維持する。

expectations.yamlの注記によれば、dt=0.05での概算タイミングは、クルーズ到達が約t=3.5 s、Slowトリガーが走行距離150 m時点(約t=12 s, s約170)、8.33 m/sへの減速完了がs約214で、いずれもs=250のランドマークより十分手前になる。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `speed_above` | threshold=13.0, after sim_time 4.0, before sim_time 11.0 | 減速前にクルーズ速度へ到達している |
| `speed_reduction_before_landmark` | landmark_s=250, road_id=1, target_speed=8.33, tolerance=0.8 | ランドマークまでに新しい制限速度へ減速済み |
| `deceleration_profile_smooth` | max_decel=3.5, max_jerk=3.0, smooth_window=5, landmark_s=250, road_id=1, after sim_time 10.0 | コマンド減速を滑らかに追従する |
| `lane_keep` | road_id=1, lane_id=-1 | 横方向アクションが無く、開始レーンを維持し続ける |

## 関連

- バッチ: `anticipation_driving_batch.yaml`（所属。dt=0.05, max_time=40.0, osi=true, report=anticipation）
- 期待値: `speed_limit_change.expectations.yaml`
