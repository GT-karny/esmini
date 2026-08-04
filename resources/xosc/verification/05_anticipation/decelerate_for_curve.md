# R60カーブ手前の予見的減速

自車がR60の左カーブに向けて直線路を巡航し、カーブ進入前にどれだけ滑らかに減速できるかを見るシナリオ。
コマンドされた減速アクションは無く、減速はすべてVirtualDriverの中長期プランナー（Phase 2 / A2）による予見に委ねられる。

## 検証の狙い

ゼロ慣性のDefault車ならR60を巡航速度のまま通過できるが、物理挙動を持つ車では横加速度が約3.2 m/s^2を超え、カーブ前に約sqrt(2.0*60)=10.95 m/sまで減速する必要がある。
xosc内コメントによれば、この予見的減速はPhase 2の中長期プランナーが無ければ発生せず、Phase 2導入前はここでのanticipation系matcherがFAILすることが期待されていた。
DefaultとのXY/速度RMSE比較は意図的に行わない（ゼロ慣性のDefault車はR60を13.889 m/sのまま通過してしまい比較が無意味なため）。VirtualDriverのテレメトリ単体から「物理的に妥当な減速」を判定する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `decelerate_curve_r60.xodr`（road0 s=500からR60の円弧区間） |
| 自車 | 初期位置 road0 lane-1 s=300。Story Eventで目標速度13.889 m/s(50 km/h)へ3.0 s linearで加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間28 s超過 |

## 進行

1. t=0: Story Event「Cruise」開始、目標速度13.889 m/sへ3.0 sかけてlinearに加速。
2. route上をroad0 s=300からs=690まで(fastest)走行し、s=500のカーブ進入点に接近する。
3. t=28 s到達でStopTrigger成立、シナリオ終了。

## 期待する挙動

- カーブ入口(road0 s=500)より手前で、目標速度11.0 m/s(許容差1.0)まで減速している。
- カーブへの減速アプローチ区間(s=500進入、t=4.0 s以降)で、減速度が3.5 m/s^2以下、jerkが9.0 m/s^3以下(smooth_window=9)に収まる、スラム停止ではない滑らかな減速。
- t=4.0 s以降、road0 lane-1のレーンを維持し続ける（ステアリング飽和によるオフレーン逸脱がない）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `speed_reduction_before_landmark` | landmark_s=500, road_id=0, target_speed=11.0, tolerance=1.0 | カーブ進入前に快適速度へ減速 |
| `deceleration_profile_smooth` | max_decel=3.5, max_jerk=9.0, smooth_window=9, landmark_s=500, road_id=0, after sim_time 4.0 | 減速がスラム停止ではなく制御された範囲に収まる |
| `lane_keep` | road_id=0, lane_id=-1, after sim_time 4.0 | カーブ中もレーンを維持する |

## 関連

- バッチ: `anticipation_driving_batch.yaml`（所属。dt=0.05, max_time=40.0, osi=true, report=anticipation）
- 期待値: `decelerate_for_curve.expectations.yaml`
