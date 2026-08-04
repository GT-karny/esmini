# タイトな左折交差点手前の予見的減速

自車がfabriksgatanの交差点に向けて巡航し、タイトな接続路（R約8 m）を曲がって隣接路へ抜けるシナリオ。
コマンドされた減速アクションは無く、減速は中長期プランナー（Phase 2）による予見に委ねられる。

## 検証の狙い

Default(ゼロ慣性)車は巡航速度のまま曲がれるが、物理挙動を持つ車はR約8 mの接続路を曲がるためにsqrt(2.0*8)=約4 m/sまで減速する必要がある。
xosc内コメントによれば、これはPhase 1の残存バグ再現シナリオでもある：中長期プランナーが無いと全速で交差点に進入し、Pure-Pursuitステアリングが飽和して誤った接続路へドリフトする。
ルートはresources/xosc/traffic_lights.xoscと同じだが、信号と歩行者を取り除いて旋回予見のみを検証する構成にしてある。

接続路13（curvature +0.108108、長さ14.8696、進入hdg 0.1457 rad）の幾何から導かれる実際の旋回はheading delta +1.608 rad(+92度)の**左折**である（OpenDRIVEの正の曲率は反時計回り）。指示灯はLEFTで点灯するのが正しい。以前は`decelerate_for_right_turn`という名前だったが、この幾何を踏まえて2026-08-04に改名した。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `fabriksgatan_traffic_lights.xodr`（road3→接続路13(R約8 m)→road2） |
| 自車 | 初期位置 road3 lane-1 s=11。Story Eventで目標速度13.889 m/sへ3.0 s linearで加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間25 s超過 |

## 進行

1. t=0: Story Event「Cruise」開始、目標速度13.889 m/sへ3.0 sかけてlinearに加速。
2. route上をroad3(lane-1)から接続路13(lane-1)、road2(lane1) s=200まで走行する。
3. t=25 s到達でStopTrigger成立、シナリオ終了。

## 期待する挙動

- 交差点進入点(road3 s=109)より手前で、目標速度5.0 m/s(許容差1.0、~sqrt(2.0*8)=4 m/sを見込んで最大5まで許容)まで減速している。
- 減速アプローチ区間(s=109進入、t=4.0 s以降)で、減速度が4.0 m/s^2以下、jerkが9.0 m/s^3以下(smooth_window=9)に収まる、firmだがスラム停止ではない減速。
- t=4.0 s以降、ステアリングが飽和しない(閾値0.98)。
- 接続路13進入の29.5 m以上手前で、左方向の指示灯が点灯する(expect_dir: left)。上記の通り、この旋回は幾何学的に左折である。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `speed_reduction_before_landmark` | landmark_s=109, road_id=3, target_speed=5.0, tolerance=1.0 | タイトな旋回を予見して交差点に巡航速度で進入しない |
| `deceleration_profile_smooth` | max_decel=4.0, max_jerk=9.0, smooth_window=9, landmark_s=109, road_id=3, after sim_time 4.0 | 減速がfirmだが制御された範囲に収まる |
| `steer_not_saturated` | threshold=0.98, after sim_time 4.0 | 旋回中にステアリングが飽和しない |
| `indicator_leads_junction_turn` | min_distance_m=29.5, expect_dir=left | 法定30 m手前での指示灯点灯（req-vd-ad:REQ-AD-021回帰ガード） |

## 関連

- バッチ: `anticipation_driving_batch.yaml`（所属。dt=0.05, max_time=40.0, osi=true, report=anticipation）
- 期待値: `decelerate_for_left_turn.expectations.yaml`
- 関連ID: req-vd-ad:REQ-AD-021
