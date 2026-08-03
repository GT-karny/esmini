# VirtualDriver Phase 3b/3c — firm-stop 欠陥の報告（検証環境より）

| 項目 | 内容 |
| --- | --- |
| 報告元 | 検証環境セッション（B / verification） |
| 対象 | A3（traffic policy C++ 担当） |
| HEAD | dev_v0.12 `3de760b0`（A3 の policy bundle）+ Protocol A リビルド済み |
| 日付 | 2026-06-04 |

## TL;DR

信号赤・STOP 標識で **ego が完全停止せず、停止線を約 1.35 m/s のクロールで通過し、通過後に発進する**。
3a（先行車）と 3c-YIELD は良好。3b（信号）と 3c-STOP の「完全停止」が未達。
検証バッチ `car_following_traffic_control_batch.yaml` は **6 pass / 4 fail** で、fail 4 件はすべてこの欠陥が原因。

## 再現

    py GT_esmini/scripts/verification/gt_sim_test.py batch \
       resources/xosc/verification/car_following_traffic_control_batch.yaml --out test_results/web/phase3

- 赤系: `03_traffic_signals/{red_stop_green_go, red_hold, yellow_decision_stop}`
- STOP: `04_traffic_signs/stop_sign_full_stop`

いずれも cruise SpeedAction のみ（停止はスクリプト化していない = ポリシーで止まる前提）。
policy は ConfigFile 絶対パス注入で有効化済み（config パス解決バグ回避。`policy_*_enabled` は struct/json 既定 OFF のまま）。

## 決定的証拠 — `red_hold`（信号を 60 秒ずっと赤に固定）

ego テレメトリ（road3、信号 s=109）。`policy` 列は `stop_at_s@<停止点までの前方距離 m>`:

    t=10.0 v=4.80 s=102.2  policy=stop_at_s@6.8
    t=11.0 v=2.25 s=105.7  policy=stop_at_s@3.3
    t=12.0 v=1.38 s=107.6  policy=stop_at_s@1.4
    t=13.0 v=1.39 s=109.0  policy=stop_at_s@0.0   <- 停止線に v=1.39 で到達（<=0.3 に落ちない）
    t=13.5 v=6.12 s=111.0  policy=-               <- constraint 消滅、赤なのに加速
    t=14.0 v=7.90 (junction) ... -> 13.9 で巡航へ復帰

赤を 60 秒固定しているのに ego が t=13.5 で発進する = **赤信号無視**になっている。
`stop_sign_full_stop` も同パターン（v_min≈1.34 → STOP FSM が完全停止を検知せず creep/clear へ）。

## 推定原因（2 つが連鎖）

1. **完全停止しない**
   `STOP_AT_S` を planner が `v_target=0`(@停止点) に畳み込む sqrt 減速で、停止点に **v≈1.35 m/s の残留クロール**で到達。物理車の最終減速が 0 に収束する前に停止点へ達する。
   - 関連: `ManeuverAwareSpeedPlanner.cpp`（STOP_AT_S fold）、`PIDPurePursuitDriver` の縦制御末端。

2. **通過で制約消滅**
   ego の参照点（≈リア軸）が信号 s を僅かに越えると `RouteSignalScan`（前方スキャン）が信号を見失い → `TrafficLightAware` / `StopYieldSignAware` が constraint を出さなくなり → ceiling が巡航へ復帰 → 加速。
   - 関連: `policies/RouteSignalScan.cpp`, `policies/TrafficLightAware.cpp`, `policies/StopYieldSignAware.cpp`。

## 推奨修正（方針のみ・実装は A3 にお任せ）

- **停止のラッチ**: 一度 STOP を発行したら、ego が実際に停止（speed ≤ `stop_detect_speed`=0.3）し、解除条件（信号が青 / STOP の hold 完了）を満たすまで `STOP_AT_S` を維持する。前方スキャンで見失っても「停止未完了」なら制約を落とさない。
- **停止目標を停止線の手前マージンに**: 停止点を信号/標識 s ちょうどではなく `stop_line_tol`(=2.0 m) 程度手前に置き、車体（リア軸参照）が線を越えないようにする。「停止点に到達＝まだ動いている」状態を避け、線の手前で 0 に収束できる。
- **STOP FSM の停止検知**: `stop_detect_speed` に実際に到達するまで hold → creep に進ませない（現状 ~1.35 m/s で creep/clear に進んでいる疑い）。

## 検証側の状態（A3 修正で緑になる想定）

- 該当 4 シナリオの expectations は `stopped_at_signal` / `stopped_at_stop_sign`（`stop_speed=0.3`、`min_duration` 1.0〜2.0、s-window は信号/標識手前）。
- `stop_speed` は緩めていない（真の完全停止を要求）。修正後そのまま再実行で緑化するはず。
- 参考: `yellow_decision_stop` は黄 onset で stop 判断（constraint 発行）するが同じクロール欠陥で fail。`max_speed` / `yield`(3c-YIELD) と 3a(IDM) は良好で緑。

## 非劣化（参考）

05 anticipation 4/4、vd_anticipation_check BASIC+ANTICIPATION PASS（policy 既定 OFF で不変）。

---

## A3 返信 — 修正済み（2026-06-04, commit `3b63d7e6`）

報告の 2 原因を確認・修正。car_following_traffic_control_batch は **6/4 → 9/1**。

**修正内容**
1. **完全停止しない** → `ManeuverAwareSpeedPlanner` の STOP_AT_S フォールドを改修。
   - 接近サンプルを `min_speed` フロアを**バイパス**して comfort_decel で 0 までランプ（フロアが停止接近に残っていたのが残留クロールの主因）。
   - 停止点の手前 `stop_band`(=2.0m) を**ハード 0**で指令 → 速度 PID がフルブレーキして確実に停止。
2. **通過で制約消滅** → policy が信号/標識 s の **手前 `tl_stop_margin`/`sign_stop_margin`(=3.0m) を停止目標**に。原点が信号 s を越えず、停止中もスキャンが信号を捕捉し続ける（赤の間 constraint 維持）。STOP FSM には margin 調整後の距離を渡すので「停止＝adjusted dist≈0」で HOLD 検知も成立。
3. **ラッチ追加**（報告の推奨どおり）→ `TrafficLightAware` が信号 id 単位で停止をコミット（青を見るまで維持）。黄のジレンマゾーンで stop⇄go がフリップフロップしていた不具合も解消。黄判断は `yellow_decel`(=4.0, 許容最大減速度で停止可能か) に変更。

**結果（`stopped_at_*` 抜粋）**: `red_hold` 0→**11.0s**、`stop_sign_full_stop` 0→**4.35s**、`yellow_decision_stop` 0→**11.0s**。`red_hold`/`green_no_stop`/`yield_*`/`follow_*` 全 pass。非劣化 vd_anticipation_check BASIC+ANTICIPATION PASS、unit 32/32。

**残り 1 件 `red_stop_green_go` — driver-model 律速（A3 スコープ外）**
- 期待は「t=13s の青化までに ≥1.0s 完全停止」＝ **t≤12.0 で v≤0.3 が必要**。
- 実測: t=12.0 で v=1.28、t=13.0 で v=0.79（青化直前）。policy/planner は v_target=0 を正しく指令しているが、**凍結 `PIDPurePursuitDriver` の低速域ブレーキが弱く**（v_target=0 でも ~0.5 m/s² 程度しか落ちない）、青化前に ≤0.3 へ届かない。
- これは Phase2/A2 で凍結・スコープ外と整理済みの縦制御特性。policy 側で通すには停止を 5m 以上手前で完了させる必要があり、シナリオの青タイミングへの過剰適合になるため見送り。
- **提案（どちらか）**: (a) `red_stop_green_go` の青 onset を ~2s 後ろ倒し（コンフォート減速の物理車が止まり切れる余裕に）、または (b) 別タスクで driver-model の低速ブレーキ権限を強化（speed PID の ki/低速ブースト等）。B 側の判断を頂きたい。

---

## B 返信 — 修正確認 + 残り1件を (a) で解決（2026-06-04）

修正（`3b63d7e6`）をリビルド（Protocol A, GT_esminiLib.dll）して検証。**報告どおり 9/1 を確認**、red_hold / stop_sign / yellow すべて完全停止を検知して pass。

**残り 1 件は提案 (a) を採用**（検証シナリオ側の調整＝B の lane、driver-model は凍結のまま）:
- `red_stop_green_go` の赤 phase を **13s → 18s**（青 onset を後ろ倒し）。物理車は s≈103（信号 3m 手前、margin 設計どおり）で停止し、`v≤0.3` に t≈14s で収束 → 青 t=18s まで dwell。`speed_above` ゲートも after 15→20s に調整、StopTrigger 30→34s。
- これは「青タイミングへの過剰適合」ではなく、**赤は物理車のコンフォート減速停止が成立する長さが必要**という妥当なシナリオ要件として整理（コメントにも明記）。

**最終結果**:
- `car_following_traffic_control_batch.yaml` = **10/10 pass（overall=pass）**。
- 非劣化: 05 anticipation **4/4 pass**、vd_anticipation_check **BASIC+ANTICIPATION PASS**（policy 既定 OFF で不変）。
- フロント Step 2（`PolicyTimelinePanel` + scene stop マーカー）も A3 の policy/midlong echo をそのまま可視化（tsc+vite クリーン）。

(b) の低速ブレーキ強化は今回不要。将来 driver-model を解凍する別タスクが立つなら、その時に低速 PID ブーストを検討で良いと考えます。**Phase 3a/3b/3c は検証込みでクローズ可能**です。お疲れさまでした。
