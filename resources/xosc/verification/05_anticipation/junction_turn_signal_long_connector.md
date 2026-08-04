# 長い接続路での右折方向指示灯点灯（欠陥1回帰ガード）

T字交差点の長い曲線接続路（約33.2 m）を右折するシナリオ。接続路自体の長さが旧ルックアヘッド予算を超えても、方向指示灯が法定30 m手前で点灯することを確認する。

## 検証の狙い

req-vd-ad:REQ-AD-021の欠陥1回帰ガード（docs/virtualdriver/design/junction_turn_signal.md #1、行1）。
xosc内コメントによれば、修正前のRouteLookaheadJunctionTurnDirectionは「交差点進入までの距離＋接続路長 < ルックアヘッド」を満たさないと旋回方向を検出できず、接続路自体の長さがルックアヘッドを超えるだけで、どれだけ手前から接近しても指示灯が点灯しなかった。
修正後は接続路自体の進入/退出headingから方向を導出するため、接続路長に依存しない。
自車はroad0 s=20からスタートし、交差点進入点(road0終端 s=100)まで80 mの直線区間を持つ。これはdesign docが要求する約60 mの最小助走距離を上回り、指示灯の30 mトリガー地点に達する時点で自車は巡航状態にある（加速中ではない）。

expectations.yamlの注記によれば、この旋回は幾何学的にRIGHT turnである。接続路101はplanView上3つのクロソイド区間(s=[0, 10.4714, 22.7339, 33.2053])で構成され、進入hdg 0 rad、曲率が0→-0.069095→-0.069095→0と推移する。各区間のheading delta（0.5*(k0+k1)*L）を積算すると合計-1.5708 rad(-90度、東→南)になる。接続路の先行道路road0はcontactPoint="end"（順方向リンク）で、自車はs増加方向(trav_dir=+1)に接続路101を走行するため符号反転は不要。dir = sign(heading_delta)*trav_dir = -1 で、design docの規約(+1=左、-1=右)によりRIGHT turnとなる。

decelerate_for_right_turn(接続路14.87 m、expect_dir: left)、cross_straight_junction(expect_dir: none)とあわせて3シナリオで、junction_turn_signal.mdセクション1が測定した欠陥（長すぎて検出不能／短すぎるリード距離／直進での誤検知）をそれぞれ担当する。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `t_junction__a90.xodr`（T字交差点id=100。road0(東行)/road1(西行)が接続路100(直線、長さ40)で結ばれ、幹road2(北)へは接続路101(road0側、長さ約33.205 m)または接続路102(road1側)で到達） |
| 自車 | 初期位置 road0 lane-1 s=20。Story Eventで目標速度13.889 m/sへ3.0 s linearで加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間30 s超過 |

## 進行

1. t=0: Story Event「Cruise」開始、目標速度13.889 m/sへ3.0 sかけてlinearに加速。
2. route上をroad0(lane-1)から接続路101(lane-1)、road2(lane1) s=50まで走行する。
3. t=30 s到達でStopTrigger成立、シナリオ終了。

## 期待する挙動

- 接続路101への進入(交差点進入点)の29.5 m以上手前で、右方向の指示灯が点灯する(expect_dir: right)。
- 接続路自体が約33.2 mと長く、旧ルックアヘッド予算(距離＋接続路長方式)では点灯できなかった条件だが、修正後は接続路自体の幾何(進入/退出heading)から方向が決まるため、接続路長に依存せず点灯する。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `indicator_leads_junction_turn` | min_distance_m=29.5, expect_dir=right | 法定30 m手前で指示灯が点灯する。接続路長が旧ルックアヘッドを超える条件での欠陥1回帰ガード |

## 関連

- バッチ: `anticipation_driving_batch.yaml`（所属。dt=0.05, max_time=40.0, osi=true, report=anticipation）
- 期待値: `junction_turn_signal_long_connector.expectations.yaml`
- 関連ID: req-vd-ad:REQ-AD-021
