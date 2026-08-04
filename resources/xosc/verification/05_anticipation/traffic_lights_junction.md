# 信号停止後の右折発進（交差点ドリフト回帰ガード）

自車がfabriksgatanの交差点手前で赤信号を模して停止し、待機後に発進して右折するstop-then-goシナリオ。
Phase 1の残存バグ（青信号発進時の交差点過速度進入からのドリフト）に対する回帰ガード。

## 検証の狙い

xosc内コメントによれば、Phase 1では青信号発進時に全速まで加速してから交差点に進入し、Pure-Pursuitステアリングが飽和して誤った接続路へドリフトしてしまう。
Phase 2の中長期プランナーは、青信号発進であっても交差点に対するv_targetをキャップするため、減速して正しく旋回できる。
expectations.yamlの注記によれば、Phase 2導入後の実測(commit 62a5beda)は、s=98.8(t=10.1)で完全停止、交差点進入時速度5.1 m/s(t=15.1)、最大|steer|0.849、接続路13経由でroad2にt=19.6で到達、というものだった。
deceleration_profile_smooth matcherは意図的に採用していない（完全停止が減速ウィンドウの解釈を混乱させるため。滑らかさの検証はdecelerate_for_right_turn（連続走行版）が担当する）。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `fabriksgatan_traffic_lights.xodr`（road3→接続路13(R約8 m)→road2） |
| 自車 | 初期位置 road3 lane-1 s=11。Story Eventで目標速度13.889 m/sへ加速→走行距離72 mで停止→2秒静止後に再加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間35 s超過 |

## 進行

1. t=0: Story Event「Cruise」開始、目標速度13.889 m/sへ3.0 sかけてlinearに加速。
2. 走行距離が72.0 mに達した時点でStory Event「StopAtLine」開始、目標速度0.0へ距離15.0 mかけてlinearに減速停止。
3. 静止(StandStillCondition)が2.0 s継続した時点でStory Event「GoOnGreen」開始、目標速度13.889 m/sへ5.0 sかけてlinearに再加速。
4. route上をroad3(lane-1)から接続路13(lane-1)、road2(lane1) s=200まで走行する。
5. t=35 s到達でStopTrigger成立、シナリオ終了。

## 期待する挙動

- t=10.0〜12.0 sの間、速度が0.5 m/s未満まで完全停止している（赤信号を模した停止）。
- 交差点進入点(road3 s=109)より手前で、目標速度5.0 m/s(許容差1.5、~sqrt(2*8)=4 m/sを見込んで最大6.5まで許容)まで減速している（青信号発進でタイトな旋回を予見し、巡航速度のまま進入しない）。
- t=12.0 s以降(青信号発進〜旋回通過)、ステアリングが飽和しない(閾値0.98)。
- t=24.0 s以降、正しい目的地road2に到達している（誤った接続路へドリフトしない）。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `speed_below` | threshold=0.5, after sim_time 10.0, before sim_time 12.0 | 交差点手前で完全停止する |
| `speed_reduction_before_landmark` | landmark_s=109, road_id=3, target_speed=5.0, tolerance=1.5 | 青信号発進でタイトな旋回を予見し過速度で進入しない |
| `steer_not_saturated` | threshold=0.98, after sim_time 12.0 | 発進〜旋回中にステアリングが飽和しない |
| `lane_keep` | road_id=2, after sim_time 24.0 | 正しい目的地road2に到達する（誤った接続路へドリフトしない） |

## 関連

- バッチ: `anticipation_driving_batch.yaml`（所属。dt=0.05, max_time=40.0, osi=true, report=anticipation）
- 期待値: `traffic_lights_junction.expectations.yaml`
