# 交差点の右左折における方向指示器 — 距離で規定されたものを距離で解く

対象要求: `req-vd-ad:REQ-AD-021`
実装機能: `vd-func:FUNC-061`（指示器の自発操作）／マヌーバは `vd-func:FUNC-057`（交差点発起）

車線変更側（`req-vd-ad:REQ-AD-018`）は
[`lane_change_initiation.md` §11](lane_change_initiation.md) が持つ。
**本書と §11 は別文書である。** 法的根拠が「3秒前」と「30 m 手前」で次元から違い、
実装経路も `DetectManeuverDir` の別々の段に分かれている。
§11-10 が config キーを1本にまとめないと決めた理由がそのまま、文書を分ける理由でもある。

---

## 1. 何が壊れていたか — 実測3件

2026-08-03、実 Release ビルド・`gt_sim_test run`・dt=0.02 での実測。

| # | 症状 | 実測 |
| :-- | :--- | :--- |
| 1 | 接続路が長い交差点で**一度も点かない** | `t_junction__a90.xodr`（接続路 33.2 m）で左折する走行6本すべて点灯フレーム 0 |
| 2 | 短い接続路でも**7.18 m 手前**でしか点かない | `fabriksgatan_traffic_lights.xodr`（接続路 14.87 m）で点灯距離 7.18 m（法定の約 1/4） |
| 3 | **曲り終わる前に消える** | 交差点進入 11.76 s → 消灯 12.06 s（進入から `indicator_min_on_time` 0.30 s ちょうど）。脱出は 14.82 s |

3件はすべて**同じ1つの関数**に由来する。
`RouteLookaheadJunctionTurnDirection`（`JunctionTurn.hpp:61-103`）は、
junction 所属 road を素通りして `passed_junction = true` を立て、
**その先の非 junction road（出口側の腕）に到達して初めて**方向を返す（同 :93-100）。

ここから2つが機械的に出る。

- 点灯には `交差点入口までの距離 + 接続路長 < lookahead` が要る。
  接続路長が単独で lookahead を超える交差点は**構造的に点灯できない**（欠陥1）。
  超えない交差点でも、lookahead から接続路長が丸ごと差し引かれる（欠陥2）。
- ego が接続路に乗ると `current_track` 自身が junction road になり、
  `if (track == current_track) continue`（同 :88）で接続路が判定対象から外れる。
  `passed_junction` が立たないまま出口の腕に出るので `:98` で `return 0`。
  **旋回中は必ず 0 を返す**（欠陥3）。

### 速度を上げても解決しない

初速を 5.0〜16.7 m/s に振っても、交差点進入時の速度は全て ~4.9 m/s に収束する
（`ManeuverAwareSpeedPlanner` が `scan_distance: 300.0` で曲率減速をかけるため）。
lookahead は速度比例なので、**曲がるために減速するほど先読みが縮む**。

`indicator_lead_time` を大きくするだけの対症療法を採らないのはこのためである。
定数を膨らませても、接続路長に食われる構造と旋回中 0 を返す構造は残る。

---

## 2. 根治の骨子 — 1つの `lookahead` が2つの役目を兼ねていた

現行の `lookahead` は**別の2つの距離**を1つの変数で表している。

1. **方向を特定するために走査すべき距離** — 現行実装では「入口まで＋接続路長」
2. **合図を開始すべき距離** — 法定は「交差点の手前の側端から 30 m 手前」

この2つが同一だったことが欠陥1と欠陥2の共通の根である。分離する。

### 2-1. 方向は接続路自身の幾何から決める

出口側の腕まで走査を届かせる要件をやめる。
接続路に到達した時点で、**その接続路の進入ヘディングと退出ヘディングの差**から方向を出す。

これで(1)は「入口までの距離」だけになり、**接続路長への依存が構造的に消える**。

手法は既にコードベースにある。`IsSharpJunctionConnector`（`JunctionTurn.hpp:14-29`）が
`SetTrackPos(road_id, 0.0, 0.0)` と `SetTrackPos(road_id, length, 0.0)` の `GetH()` 差で
「鋭さ」を計算している。方向版は**その `fabs` を外し、走行方向で符号を掛ける**だけでよい。

**走行方向の補正は必須である。**
`GetH()` の差は常に「s=0 → s=length」の順で取った road フレームの生の差である。
経路が接続路を s 減少方向に通るなら、走行順で見た差は符号が逆になる。
`Position::GetDrivingDirection()` の ±180° 補正は始点・終点に同じ定数を足すだけなので
**差分では相殺され、この符号反転を自動的には直さない**。
`Position::GetDrivingDirectionRelativeRoad()`（`RoadManager.hpp:4606`）で明示的に掛ける。

**通行区分（LHT/RHT）には依存しない。**
ヘディング差の符号は純粋な幾何であり、どちら側を走るかとは無関係である。
`LaneChangeIndicatorDir` のコメント（`JunctionTurn.hpp:40-45`）が車線変更について述べている
原理がそのまま当てはまる。LHT/RHT が効くのは「s 増加方向に走るか」の判定だけで、
それは `GetDrivingDirectionRelativeRoad()` の内側に既に畳まれている。

### 2-2. トリガは距離を第一級にする

法定が距離規定である以上、`v × lead + 定数` という時間ベースの形は噛み合っていない。

```
trigger_m = max(indicator_min_distance_m, speed * indicator_lead_time)
```

`indicator_min_distance_m` の既定は **30.0**（法定値そのもの）。

**トリガは1フレーム先を見る。** 実際に点灯を判定する距離は

```
reach_m = trigger_m + speed * dt
```

である。サンプリングが離散なので、`dist <= 30.0` を最初に満たすフレームは
**すでに法定地点を最大1フレームぶん通り過ぎている**。この項を入れる前の実測が
接続路 33.2 m で **29.74 m** で、法定をわずかに割っていた。
施行令21条1項は「合図を開始していなければならない地点」を定めるものなので、
**早い側に外すのは適合、遅い側に外すのは不適合**である。

車線変更側が §11-11 で採ったのと同じ動き（しきい値を追うのをやめ、次のフレームが
どこに来るかを予測する）である。走査上限も `reach_m + step` にする
（`trigger + step` のままだと、点灯すべきフレームより後に接続路を検知しうる）。
`indicator_lead_time`（既定 2.0）は残す。高速側では時間の意味が生きるためである
（20 m/s で 40 m > 30 m）。低速側では 30 m の床が効く。

現行式の `+ 10.0` の定数は**削除する**。
あれは接続路長に食われるぶんを埋める代償であって、法的にも運動学的にも根拠がない。

### 2-3. 旋回中は方向を返し続ける

`start` 自身が junction 所属 road のときは、走査に入らずその接続路の幾何から方向を返す。
ラッチではなく**状態を持たない判定**にする。`DetectJunctionTurn` が `const` であることを保てるうえ、
交差点内で信号待ち・譲り待ちで停止しても（実測で v=1.26 m/s まで落ちる走行がある）
速度に依存せず合図が保たれる。

消灯は ego が接続路を抜けた時点で方向が 0 に戻り、`indicator_min_on_time`（0.30 s）後に落ちる。
道交法53条2項「当該行為が終わるまで継続」を満たす。

### 2-4. 入口までの距離を返す — 検証がこれを要求する

走査ループは既に `traveled` を持っている（`JunctionTurn.hpp:74`）が、
これは 2.0 m 刻みの累積であり、接続路に踏み込んだぶんを含む（最大 `step` の過大評価）。
検知した時点の `pos.GetS()` で補正すれば、追加の走査なしに正確な値が出る。

```
into_connector = (trav_dir >= 0) ? pos.GetS() : (road->GetLength() - pos.GetS())
dist_to_entry  = max(0.0, traveled - into_connector)
```

走査上限は `trigger_m + step` とする。
`step` を足さないと、しきい値をまたぐフレームの検知が最大 2.0 m 遅れ、
法定 30 m を量子化のぶんだけ割る。

---

## 3. インターフェース（実装者はこの名前を使うこと）

### 3-1. `JunctionTurn.hpp`

```cpp
struct JunctionTurnLookahead
{
    int    dir           = 0;      // +1 = left, -1 = right, 0 = none
    double dist_to_entry = -1.0;   // m。接続路上にいるときは 0.0、未検出は -1.0
    bool   on_connector  = false;  // ego 自身が junction 所属 road 上にいる
};

// 接続路単体の旋回方向。trav_dir は +1 (s増加方向に走行) / -1 (s減少方向)。
int ConnectorTurnDirection(const roadmanager::Road* road, int trav_dir);

JunctionTurnLookahead RouteLookaheadJunctionTurn(const roadmanager::Position& start,
                                                 roadmanager::OpenDrive* odr,
                                                 double lookahead,
                                                 double step = 2.0);
```

`kJunctionTurnHeadingThresholdRad`（0.10 rad）は据え置く。
`cross_straight_junction`（heading delta ≈ 0.046 rad）が 0 判定に落ちる余裕がある。

**`kJunctionSharpTurnRateRadPerMeter`（0.04 rad/m）と一本化しない。**
あちらは `ManeuverAwareSpeedPlanner` と `ControllerRouteDrive` が使う
「鋭さ」の rate 判定で、方向を使わない別系統である。しきい値を揃えると
指示器を直したつもりで曲率減速の挙動が動く。

### 3-2. `ControllerVirtualDriver::DetectJunctionTurn`

```
trigger   = max(vd_config_.indicator_min_distance_m, speed * vd_config_.indicator_lead_time)
result    = RouteLookaheadJunctionTurn(object_->pos_, odr, trigger + step)
点灯条件  = result.on_connector || (result.dir != 0 && result.dist_to_entry <= trigger)
```

### 3-3. config キー

| キー | 型 | 既定 | 備考 |
| :--- | :--- | :--- | :--- |
| `indicator_min_distance_m` | number | **30.0** | 新設。法定値そのもの |
| `indicator_lead_time` | number | 2.0 | 既存。高速側でのみ支配的 |
| `indicator_min_on_time` | number | 0.3 | 既存。変更なし |

`lane_change_indicator_lead_time_s`（3.0）は**車線変更側専用**であり本経路から参照しない
（§11-10）。1本にまとめないこと。

### 3-4. テレメトリ

新ブロック `junction_turn` を出す。

```json
"junction_turn": { "dir": 1, "dist_to_entry_m": 24.6, "on_connector": false }
```

**このブロックは検証のために必須である。**
現在のテレメトリには「その road が junction 所属か」を示す情報が一切無く
（`VirtualDriverTelemetryJson.cpp` に junction の語が1つも無い）、
Python 側の検証ハーネスは xodr へのアクセス手段を持たない。
`on_connector` が無いと「交差点に進入したフレーム」を機械判定できず、
距離を測る matcher が原理的に書けない。

---

## 4. 検証 — 距離を機械判定する

既存の `matcher:indicator_leads_lane_change` は**秒しか測らない**ので流用できない。

### 4-1. 新設する signal と matcher

- signal `junction_turn_signal_distance` — 点灯と交差点進入の**距離**関係
- matcher `indicator_leads_junction_turn` — 下記を判定

| パラメータ | 意味 |
| :--- | :--- |
| `min_distance_m` | 点灯開始から接続路進入までの走行距離の下限 |
| `expect_dir` | `left` / `right` / `none`（`none` は直進の負検証） |

判定内容:

1. `junction_turn.dir != 0` を伴う指示器点灯の立ち上がりフレーム `t_sig` を取る
2. `junction_turn.on_connector` が最初に true になるフレーム `t_junc` を取る
3. `t_sig`〜`t_junc` の**走行距離**を積算し `min_distance_m` と比較
4. 区間中に消灯が無いこと
5. `on_connector` が false に戻るまで点灯が続くこと（欠陥3の再発検知）
6. 点灯側が `expect_dir` と一致すること

距離の積算は `ego.x/y` のフレーム間ユークリッド距離で行う。
`vd_metrics.py` の `domain_split_holds` に既存の前例がある（`math.hypot` の畳み込み）。
`speed × dt` の積分より頑健で、フレーム重複（バッチ末尾で同一 `sim_time` が複製されうる）にも耐える。

### 4-2. しきい値は 29.5 m にする

法定値は 30.0 だが matcher の下限は **29.5** を使う。
これは**主張の緩和ではなくフレーム量子化の余裕**である。
コード側は `dist <= 30.0` の最初のフレームで点灯するので、実測値は 30.0 を僅かに下回る。

§11-9 が法定 3.0 秒に対し `min_lead_s: 2.9` を置いたのと同じ理由・同じ流儀である。

### 4-3. 資産と常設ゲート

`anticipation_driving_batch.yaml` に載せる。
このバッチは `run_regression_gate.ps1` の Step 2.7 と CI の
`vd-behavioral-regression` ジョブの**両方**で走る（＝常設ゲート）。
新しい gate を起こさずに ⑥ を満たせる。

| 資産 | 接続路長 | 役割 |
| :--- | ---: | :--- |
| `05_anticipation/decelerate_for_left_turn.xosc`（既存） | 14.87 m | 正: 短い接続路 |
| `05_anticipation/junction_turn_signal_long_connector.xosc`（**新設**） | 33.2 m | 正: 長い接続路（欠陥1の再発検知） |
| `05_anticipation/cross_straight_junction.xosc`（既存） | 15.5 m | **負: 直進では点かない** |

負の資産を必ず入れる。今回の修正は「点きやすくする」方向なので偽陽性が出やすい。

---

## 5. 判明した別件 — 旧 `decelerate_for_right_turn` は左折である

実測で「右折」の名を持っていた資産に **left** が点いた件を追跡した結果、
**指示器は正しく、資産名が誤り**であると確定した。

`fabriksgatan_traffic_lights.xodr` の接続路 road 13 は
`<arc curvature="+0.108108">`・`length=14.8696`・進入 `hdg=0.1457 rad`。
OpenDRIVE の正の曲率は反時計回りであり、
heading delta = `0.108108 × 14.8696 = +1.608 rad (+92°)`。
進入 8.3°（東向き）→ 退出 100.5°（北向き）＝ **左折**。

したがって `TurnDirectionFromHeadingDelta` が +1（left）を返したのは幾何どおりで、
LHT/RHT やレーン符号の取り違えではない。

**2026-08-04 に改名を実施済み。** `decelerate_for_right_turn` を
`decelerate_for_left_turn` へ改め、バッチ manifest・expectations・baseline・KG の
参照を同時に更新した（本節執筆時点では「改名は別サイクル」としていたが、
その後のサイクルで実施した）。expectations には `expect_dir: left` を、
理由つきで書いてある。

---

## 6. 既存ベースラインへの影響

**これは既定 ON の挙動変更である。** 車線変更側の
`lane_change_initiation_enabled` のようなフラグの内側に無い。

常設ゲート（Step 1〜2.9 と CI）の全シナリオを洗った結果は次のとおり。

- 幾何的に旋回条件（junction かつ heading delta > 0.10 rad）を満たすのは
  `decelerate_for_left_turn` と `traffic_lights_junction` の**2本のみ**。
  他は junction を持たないか、持っていても直進（delta < 閾値）である。
- その2本を含め、**既存 baseline の matcher に指示器を読むものは1つも無い**。
  全て速度・停止・追従・操舵・AEB・制御移譲のいずれかである。
- `ApplyLights` は灯火状態を書くだけで操舵・速度指令に一切フィードバックしない
  （`ControllerVirtualDriver.cpp:1907-1940`、`cmd` は読み取り専用）。

よって**点灯の変化それ自体で既存の pass/fail は動かない**。
ただし 4-3 で新 matcher を既存2シナリオの expectations に足すため、
`anticipation_driving_expected.yaml` の `matchers` 配列は増える。
これは `check_regression_baseline.py --update` で**実測から再生成**する。
`status: pass` を手で決め打ちしないこと。

---

## 7. スコープ外

- 車線変更側（`req-vd-ad:REQ-AD-018`）の式には触れない
- 国別のリードタイム／距離テーブルは作らない（§11-2 の決定を継承）
- ハザード・制動灯など他の灯火（`vd-func:FUNC-062`）
- 交差点マニューバそのもの（`vd-func:FUNC-057`）の改善
- ~~`decelerate_for_right_turn` の改名（§5）~~ 2026-08-04 に改名実施済み（§5）
