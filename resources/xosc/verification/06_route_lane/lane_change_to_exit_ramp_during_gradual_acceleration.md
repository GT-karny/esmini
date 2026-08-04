# 緩やかな加速中のウインカー先行点灯（段c、加速中もリードが縮まないこと）

自車をroad0/lane-3から出発させ、-3→-4の1ホップの車線変更を、0から13.889 m/sへ20.0秒かけて緩やかに加速（a~=0.694 m/s²）しながら実行させる。
req-vd-ad:REQ-AD-018の受入ラダー段c「加速中もリードが縮まないこと」を検証する。

## 検証の狙い

vd-func:FUNC-061 / req-vd-ad:REQ-AD-018の段cを検証する。
姉妹シナリオ`lane_change_to_exit_ramp_at_constant_speed.xosc`（段b、定速時の検証）と対をなす。
姉妹シナリオが真に定速でのリードが法定3.0秒フロアに到達することを確認するのに対し、本シナリオは緩やかな加速が進行中でもそのリードが縮まないことを確認する。
既存の無印/隣接車ありシナリオの3.0秒ランプは短すぎて（a~3.9 m/s²）、n_remaining=3の発起がランプ完了前のフレーム1で即決してしまうため、加速中に発起するケースを示せない。
本シナリオはランプを20.0秒に伸ばし（a~=0.694 m/s²）、かつlane-3発進（n_remaining=1）にすることで、発起（~t=17s）がランプ進行中（0〜20s）に来るよう較正されている。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `highway_example_with_merge_and_split.xodr`（junction 1。姉妹シナリオと同一の道路・接続構造） |
| 自車 | road0/lane-3/s=10出発、目標速度13.889 m/s（Cruiseイベント、0から20.0秒のlinearランプ）。Route: road0/-3/s=10 -> road4/-1/s=50 -> road2/-1/s=40 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間40 s（姉妹シナリオ群の28sより長い。20.0秒のランプ、発起(~17s)、車線変更完了、接続点通過後の余裕を確保するため） |

## 進行

1. t=0: Cruiseイベントが発火し、自車は0から13.889 m/sへ20.0秒かけてlinearに加速を開始する。
2. xosc内コメントの計算: required_m = 1*max(v*6.0,40)+20はvの増加とともに増大するため、dist_to_connectionとの縮まり方は定速時のv*3.0より遅い（旧則の既知の限界）。発起は加速継続中のt~=16.9sに予測される。
3. 実測（expectations.yaml、2026-08-03）: signal_active初回trueがt=14.00、armed初回trueがt=17.05、lead=3.05秒。t_arm=17.05はランプ区間（0〜20s）の内側で発生し、狙い通り加速継続中に発起している。
4. notes記載の実測: trackはt=23.95でroad4、t=31.75でroad2へ遷移し、40sのStopTrigger内に収まる。deviation_countは全run通じて0。
5. t=40s: StopTrigger成立。

## 期待する挙動

- -3→-4の1回の車線変更が接続点までに完了し、deviation_countは全run通じて0に留まる。
- 目標レーン帯は{-4}に解決される。
- ウインカーのリードは加速中でも法定3.0秒フロア付近を維持し、縮まない。閾値2.9は姉妹シナリオ（定速版）と同じ余裕の取り方（実測値3.05秒よりわずかに低く、フレーム量子化の余裕込み）で設定されている。
- xosc内コメントには旧来の距離のみの二閾値則（section 11-3）ではlead~=2.03秒に圧縮される予測もあったが、forward-projection則（section 11-11）導入後の実測はforward-projection側の予測（~2.98秒）に一致し、旧則の圧縮は再現されなかった。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| route_lane_plan_holds | max_deviations=0 | -3→-4の1回の車線変更が完了し逸脱が発生しないことを確認 |
| route_lane_plan_holds | expect_target_lanes=[-4] | 目標レーン帯が姉妹シナリオ群と同じ{-4}に解決されることを確認 |
| indicator_leads_lane_change | min_lead_s=2.9 | 加速継続中でもウインカーのリードが法定3.0秒フロア付近を維持し縮まないことを確認 |

## 関連

- バッチ: `route_lane_batch.yaml`（policies: [lane_change_initiation], report: route_lane。defaults.max_time=42.0への引き上げが必要。個別scenarioのmax_time上書きはgt_sim_test.pyでは読まれない）
- 期待値: `lane_change_to_exit_ramp_during_gradual_acceleration.expectations.yaml`
- 関連ID: `vd-func:FUNC-061` / `req-vd-ad:REQ-AD-018`
