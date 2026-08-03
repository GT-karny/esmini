# VD 追い越しマヌーバ (OvertakeManeuver) — 抜けるかではなく、抜けたあと経路に戻れるかで決める

`vd-func:FUNC-056`（追い越しの発起・完遂）の設計。
`vd-component:lane-change-initiation`（`vd-func:FUNC-055`、`lane_change_initiation.md`）が作った
**1ホップの車線変更機構の上に、動機をもう1つ足す**形をとる。前提となる所有権・優先順位・軌道生成・
指示器の作法はすべて `lane_change_initiation.md` §1-§12 を継承し、本書では**差分だけ**を決める。

本書が扱う要求は2本ある（`requirements_vd_ad.yaml`、2026-08-04 に凍結例外として発行）。

- `req-vd-ad:REQ-AD-023` — 遅い先行車を追い越して巡航速度へ戻る
- `req-vd-ad:REQ-AD-024` — 追い越しによって経路上の分岐・右左折を逃さない

**2本に割った理由**は要素技術が別だからである。前者は先行車認識とギャップ受容、後者は経路計画との
突き合わせで、片方だけ完成しうる。`REQ-AD-016/017` および `REQ-AD-019/020` と同じ立て方。

---

## 1. 設計の主題 — 追い越しには締切が無い

`lane_change_initiation.md` の全体は**締切に追われる車線変更**の設計である。接続点までに目標レーンへ
移らなければ経路を逃すので、`dist_to_connection` を数え下げ、`required_m` を割り込んだら発起する。
決断距離（§3）も先行合図の前進予測形（§11-11）も、この「締切が自車より速く迫ってくる」構図から出ている。

**追い越しにはその締切が無い。** 遅い先行車の後ろを走り続けることは、遅いだけで違法でも危険でもない。
したがって追い越し側の設計は、FUNC-055 の骨格を借りながら、次の2点で**逆向き**になる。

| | FUNC-055（経路要求 LC） | 本設計（追い越し） |
| :--- | :--- | :--- |
| 発起の駆動力 | 締切（`dist_to_connection` の減少） | 動機（先行車が目標速度より遅い） |
| 待つことの代償 | 経路を逃す | 無い |
| 合図の3秒をどう作るか | 締切を T 秒前進予測（§11-11） | **待てばよい。合図してから T 秒待って発起する** |

3番目が本設計で最も効く差である。FUNC-055 は「3秒前に合図する」を**時間で遡れないので距離で解いた**
（§11-1）。追い越しには締切が無いので**遡る必要が無い** — 合図を先に出し、`indicator_lead_time_s` だけ
実際に待ってから発起すればよい。結果として法定リードは近似ではなく**設計上ちょうど** `T` になる
（下振れはフレーム量子化ぶんだけ）。前進予測形も、その `v_cap` の罠も、チャタリング用ラッチも要らない。

> **決定: 追い越しの先行合図は「合図 → ドウェル T 秒 → 発起」のタイマ形とする。**
> `ShouldSignalLaneChangeHop`（前進予測形）は**流用しない**。あれは `dist_to_connection` と
> `n_remaining` を引数に取る締切専用の述語であり、追い越しには対応する量が存在しない。
> 合成した `dist_to_connection` を食わせて動かすことは機械的には可能だが、数の意味が変わる。

`indicator_lead_time_s`（既定 3.0、道路交通法 第53条第1項／同施行令 第21条第1項）は
**新設せず `lane_change_indicator_lead_time_s` をそのまま使う**。法定値は進路変更の種類で変わらない。

## 2. 本件の肝 — 「経路を逃す追い越しはしない」

追い越しは目標レーン帯から一時的に離れる行為である。許可条件は「離れて戻ってくるのに要る距離」と
「戻ったあと経路上のホップを完了するのに要る距離」の和が、接続点までの距離に収まること。

```
d_out   = v_pass * lc_merge_cfg_.duration_max_s          # 往路ホップの地上距離
d_pass  = v_pass * t_pass                                 # 追い抜きに要る地上距離（§3）
d_back  = v_pass * lc_merge_cfg_.duration_max_s          # 復路ホップの地上距離
d_route = RequiredLaneChangeDistance(n_back, v_pass, lc_init_cfg_)   # 既存の量。新規に作らない

許可 ⟺ d_out + d_pass + d_back + d_route + lane_change_reserve_distance_m <= dist_to_connection
```

`d_route` は `lane_change_initiation.md` §3 の式そのままで、**追い越しのために新しい距離の概念を
持ち込まない**。安全余裕も既存の `lane_change_reserve_distance_m`（20.0）を再利用する。

> **`reserve_distance_m` が2回入ることについて（実装時に指摘され、意図的に残した）**:
> `RequiredLaneChangeDistance` は `n_back >= 1` のとき内部で `reserve_distance_m` を1回足す。
> 上式はその外側でもう1回足すので、`n_back >= 1` では合計2回入る。**これは意図である。**
> 2つは別の見積りに対する余裕だからである — 内側は**経路ホップ**の見積り、外側は
> **追い越しマニューバ**（`t_pass` の推定と `duration_max_s` による所要時間）の見積りに対する余裕。
> 外側を落とすと `n_back == 0`（追い越しレーンも目標レーン帯に入っている）のときに
> マニューバ側の余裕が**丸ごと消える**。
> 二重計上はガードを保守側（追い越しにくい側）へ倒すので、法定・安全のどちらの観点でも
> 危険側には効かない。値を1本の新キーに切り出すことはしない（キーを増やさない §8 の方針）。

### 2-1. `n_back` は「今の残ホップ数」ではない — ここが取りこぼしどころ

プロンプト段階の素案は「戻ったあと経路上のホップを完了するのに要る距離＝`RequiredLaneChangeDistance(n_remaining, ...)`」
としていた。**これは誤りである。** `n_remaining` は**現在レーンから**目標レーン帯までのホップ数なので、
既に目標レーンにいる（`on_target_lane == true`）ときは 0 になり、式が自明に通ってしまう。
だが追い越しはまさにその「既に目標レーンにいる」状態から始まる。

正しい量は**追い越しレーンから目標レーン帯までのホップ数**である。

```
n_back = ComputeLaneHopPlan(passing_lane_id, route_lane_status.target_lanes).n_remaining
```

- 追い越しレーンが目標レーン帯の**外**なら `n_back = 1` — 戻る1回ぶんの距離が要る
- 追い越しレーンも目標レーン帯に**入っている**なら `n_back = 0` — 経路は脅かされない

> **プロンプトが挙げた3つ目の論点「追い越しレーンが目標レーンから遠ざかる側か近づく側かで扱いを
> 変えるか」への答えは「変えない」。** `n_back` が差を丸ごと吸収する。場合分けを書くと、
> 目標レーン帯が2車線ある道路（`target_lanes` が複数）で誤る。

### 2-2. 「この先の分岐で別レーンが要る」を取りこぼさないか

取りこぼさない。`BuildRouteLanePlan` は**最終ウェイポイントのレーンから後ろ向きに伝播**させて、
各バンドに「その先へ実際に接続するレーンだけ」を記録する（`RouteLanePlan.hpp:81-86`）。
つまり現在バンドの `target_lanes` には**下流の要求が既に畳み込まれている**。
現在バンドの `target_lanes` と `dist_to_connection` だけを見れば足り、次のバンドを先読みする必要は無い。

### 2-3. 符号の罠 — `dist_to_connection < 0` の極性は FUNC-055 と**また違う**

`dist_to_connection == -1.0` は「このバンドから先へ繋がる接続点が無い」の意味である
（`RouteLanePlan.cpp:412-419`、契約は `RouteLanePlan.hpp:77`）。同じ値の扱いが3か所で違う。

| 述語 | `dist < 0` のとき | 理由 |
| :--- | :--- | :--- |
| `ShouldAttemptLaneChangeHop` | **true**（発起する） | もう待つ先が無いので即断（FUNC-055 §11-3） |
| `ShouldSignalLaneChangeHop` | **false**（合図しない） | 負のセンチネルに `<=` は常に真＝出っぱなしになる |
| **本設計の経路ガード** | **true（許可）** | **逃す分岐がそもそも無い** |

`route_lane_status.valid == false`（経路が無い／経路外の道路にいる）のときも同様に**許可**する。
守るべき経路が存在しないので、守れないことも無い。

### 2-4. 残る穴 — ガードは事前条件であって保証ではない

ガードは発起の**時点で**「戻れるだけの距離がある」ことしか確かめない。発起後に隣が詰まって
復路のギャップが空かなければ、距離があっても戻れない（§5）。**この残余リスクは設計として受け入れ、
消したことにしない。** 緩和として §5 に「経路ホップが締切に入ったら追い越しを打ち切って戻りにかかる」
安全弁を1つ置く。

## 3. 「遅い」の定義 — 定数を置かず、追い抜きに要る時間から導く

根拠なく「相対速度 3 m/s 以上を遅いとする」のような定数は置かない。追い抜きに要る時間から導く。

追い越しは、自車が先行車に対して**相対的に** `L_clear` だけ前へ出る運動である。

```
L_clear = g0 + g1 + L_ego + L_lead
```

- `g0` = 現在のバンパ間ギャップ（実測値。`ScanLeadInLane` が返す）
- `g1` = 復帰後に確保するバンパ間ギャップ。**`lane_change_gap_min_m`（8.0）を再利用**する
  — 「これ以上詰めない最小ギャップ」の意味がそのまま当てはまる。新設しない
- `L_ego`, `L_lead` = 各車のバウンディングボックス長（実測値）

相対速度 `Δv = v_pass - v_lead` で割れば追い抜きに要る時間になる。

```
t_pass  = L_clear / Δv           （Δv <= 0 なら追い抜けない＝追い越し不可）
v_pass  = min(v_desired, v_ceiling)
```

`v_desired` は `last_action_target_`（`ControllerVirtualDriver.hpp:246`、直近 SpeedAction の**終端値**）。
`ResolveTargetSpeed()` が返す `target_speed` は遷移の補間途中の参照値なので**使わない**
（`lane_change_initiation.md` §11-11 が `v_cap` について書いた注意と同じ罠）。

> **決定: 「遅い」は独立した閾値として持たない。`t_pass <= overtake_max_pass_time_s` に畳む。**

これで速度差の下限は導出量になる。

```
Δv_min = L_clear / overtake_max_pass_time_s
```

`g0 = 15 m`、`g1 = 8 m`、車長 5 m 同士なら `L_clear = 33 m`、`t_max = 10 s` で `Δv_min = 3.3 m/s`
（約 12 km/h 遅い）。**数値は道路と車両寸法から毎フレーム決まり、config には現れない。**

### 3-1. `overtake_max_pass_time_s` の根拠

既定 **10.0 s**。根拠は AASHTO の追い越し視距（Passing Sight Distance）モデルの成分 `d2`
＝**追い越し車が対向／追い越し車線を占有する時間** `t2`（設計速度により 9.3–11.3 s）である。
本値はその範囲の中央付近を取った。

> **これは出典にもとづく設計値であって、本プロジェクトの実測値ではない。**
> 実測で校正したい量ではなく「どれだけ長く隣の車線に居座ってよいと考えるか」という方針値なので、
> config で可変にしてある。実測に置き換わる性質の数ではない。

### 3-2. 「先行車に実際に拘束されている」ことも要る

速度差だけでは足りない。200 m 先の遅い車のために今すぐ追い越し車線へ出るのは誤りである。
拘束されているかの定義は**リポジトリに既にある**ものを使う。`LeadVehicleAware` は
`gap > follow_margin * s*` を自由流（速度キャップ無し）と定義している（`LeadVehicleAware.hpp:41`、
`follow_margin = 1.5`）。

> **決定: 追い越しを検討するのは `gap_lead_m <= follow_margin * s*` のとき**
> ＝ LeadVehicleAware が「自由流ではない」と判定する領域に入ったとき。**新しい閾値を作らない。**

`s*` は IDM の希望車間で、`lead_idm::DesiredGap()` が計算している。同関数が外から呼べなければ
同じ式をローカルに書き下してよい（値が一致することを単体テストで固定する）。

## 4. 追い越しレーンの選択 — LHT/RHT を場合分けしない

> **決定: 追い越しレーンは「同一走行方向のまま、レーン id が 0 に近づく側の隣接レーン」。**

この1つの規則で RHT と LHT の両方が正しくなる。

- RHT（右側通行）: 走行レーンは負 id。`-2 → -1` が 0 に近づく側＝**左**＝追い越し車線 ✔
- LHT（左側通行）: 走行レーンは正 id。`+2 → +1` が 0 に近づく側＝**右**＝追い越し車線 ✔

どちらも「センターラインに近い側」であり、これは追い越し側の定義そのものである。
`direction_step` は RHT で `+1`、LHT で `-1` になる。指示器の左右は既存の
`LaneChangeIndicatorDir(current, target, along_s)` がそのまま解決する（新規に方位を判定しない）。

追い越しレーンが存在しない／走行レーンでない場合は、対向車線を使う追い越し（§7）へ落ちる。

## 5. 状態機械 — 1ホップ機構を2回まわす

`LaneChangeInitiationState` は「今どのホップを打っているか」しか持たず、フェーズを持たない
（`LaneChangeInitiation.hpp:193-204`）。追い越しは往路と復路の2ホップなので、**その上に薄い
フェーズ状態を1つ置く**。既存の POD には触らない（`lane_change_initiation.md` §11-4 が
「`LaneChangeInitiationState` の POD には触らない」と決めた理由をそのまま継承）。

```
IDLE ──trigger&guard成立──> SIGNAL_OUT ──T秒経過&gap受容──> OUT ──ホップ完了──> PASS
                                │                                                │
                                └──trigger/guard崩れ──> IDLE                     │
                                                                                  │
IDLE <──ホップ完了── BACK <──T秒経過&gap受容── SIGNAL_BACK <──先行車をクリア──┘
```

| フェーズ | 何をしているか | 指示器 |
| :--- | :--- | :--- |
| `IDLE` | 追い越していない | 0 |
| `SIGNAL_OUT` | 往路の合図中。まだ横に動いていない | 追い越し側 |
| `OUT` | 往路ホップ armed（`lc_merge_state_` が走っている） | 追い越し側（arm 時にラッチ） |
| `PASS` | 追い越しレーンを走行中。先行車を抜きにかかる | 0 |
| `SIGNAL_BACK` | 復路の合図中。まだ横に動いていない | 復帰側（往路の逆） |
| `BACK` | 復路ホップ armed | 復帰側 |

### 5-1. 各遷移の条件

- **`IDLE → SIGNAL_OUT`**: §3 の trigger（拘束されている・`Δv > 0`・`t_pass <= t_max`）
  かつ §2 の経路ガード成立。**ギャップ受容は条件に入れない**
  （`lane_change_initiation.md` §11-3 の「合図は『入りたい』の表明であって『入れる』の表明ではない」）
- **`SIGNAL_OUT → OUT`**: 合図開始から `lane_change_indicator_lead_time_s` 経過**かつ**
  `EvaluateGapAcceptance` が受容。ドウェルが満ちる前にギャップが空いても**発起しない**（法定リードを守る）
- **`SIGNAL_OUT → IDLE`**: trigger または経路ガードが崩れた（先行車が加速した／分岐が近づいた）
- **`OUT → PASS`**: `lc_merge_state_.active` が false（既存の完了判定と同一）
- **`PASS → SIGNAL_BACK`**: 先行車を**クリアした**＝相対縦位置が `g1 + L_ego/2 + L_lead/2` 以上前に出た。
  クリア判定は追い越し開始時に記録した先行車のポインタに対して行う（毎フレーム再探索しない
  — 追い抜いた瞬間に「同一レーン前方の最近接車」が別車に変わって判定が飛ぶため）
- **`SIGNAL_BACK → BACK`**: 合図開始から `lane_change_indicator_lead_time_s` 経過かつギャップ受容
- **`BACK → IDLE`**: `lc_merge_state_.active` が false

### 5-2. 戻れなくなったときの扱い

> **決定: 中断も強制復帰もしない。追い越しレーンに留まり、復路のギャップ受容を毎フレーム再試行する。**

`lane_change_initiation.md` §5 の「入れなかったときはそのまま通過し、逸脱を記録する」をそのまま
引き継ぐ。減速も待機もしない（同 §9 がスコープ外と決めている）。接続点に達したときに
追い越しレーンにいれば、既存の `deviation_count` 機構が逸脱を1つ記録する
（`ControllerVirtualDriver.cpp:747-770`）。**新しい中断機構を作らない。**

ただし安全弁を1つだけ置く。

> **`PASS` の間に経路ホップが締切に入ったら、先行車をクリアしていなくても直ちに `SIGNAL_BACK` へ移る。**

追い抜きを途中で諦めて経路を優先する。これが REQ-AD-024 の段 c に対応する。

#### 弁は「合図しきい値」で開く（2026-08-04、実測にもとづく訂正）

当初この判定を `ShouldAttemptLaneChangeHop`（＝**発起**しきい値）で書いた。**実測で破綻した。**

弁が発起しきい値で開くと、開いた時点で残距離はちょうど1ホップぶんしかない。そこから
`SIGNAL_BACK` が法定の `indicator_lead_time_s` を使うと、使ったぶんだけ残距離が足りなくなる。
初回実装はこの矛盾を「締切時はドウェルを飛ばして即発起する」ことで解いており、
その結果**復路の指示器リードが実測 0.05 s**（クリア経路では 3.00 s）となって
`req-vd-ad:REQ-AD-023` 段 d を破っていた。

> **決定: 弁は `ShouldSignalLaneChangeHop`（＝合図しきい値、前進予測形）で開く。**
> 合図しきい値は発起しきい値より `v × T` だけ手前にあり、**その `v × T` こそドウェルが使う距離**である。
> 弁が開いた 3 秒後に発起しきい値へちょうど到達する。`lane_change_initiation.md` §11-3 が
> `ShouldSignal*` と `ShouldAttempt*` の間に置いた二段しきい値の関係が、そのままここでも効く。

**締切はギャップ受容も飛ばさない。** 安全でないと判定した隙間へ、分岐に間に合わせるために
突っ込むという取引をこの層はしない。ギャップが空かなければ追い越しレーンに留まり、
既存の `deviation_count` が逸脱を記録する（本節冒頭の決定どおり）。

#### 帰結 — 「弁が開く」と「抜き切って戻る」は排他ではない、が「弁が開いて戻れる」条件は限られる

復路のギャップ受容が要求する車間（`max(gap_min_m, v × headway_lead_s)`、既定 v=20 で 24 m）は、
`HasClearedLead` のしきい値（`gap_min_m + (L_ego+L_lead)/2`、既定で 13 m）より**大きい**。
したがって「先行車の前へ出て戻る」経路では、戻れる時点で必ず既にクリアしている。

**弁が開いたうえで復帰できるのは、先行車が自車より速くなって前方へ抜けていった場合**である。
このとき ego はクリアしていない（`cleared_lead == false`）まま、元レーンの前方が空くので戻れる。
＝「追い越しを断念して、速くなった相手の後ろへ戻る」。**検証資産はこの形で作る**（§9-4）。

### 5-3. 優先順位

`lane_change_initiation.md` §2 の順序に**最下位として1段足す**。

```
storyboard LaneChangeAction  >  resume-merge  >  経路要求 AD LC  >  追い越し AD LC
```

実装は既存の「armed でないとき」分岐（`ControllerVirtualDriver.cpp:999-1059`）の
**経路ブランチのあとに `else if` を1本足す**形にする。経路要求 LC が先に取る。
`suppressed`（storyboard 横アクション／resume-merge 進行中／手介入）は既存の同じ変数を使い、
追い越しも同じ3トリガーで中止する（`OvertakePhase` を `IDLE` へ落とす）。

## 6. 指示器 — `DetectManeuverDir` への変更は1行だけ

`DetectManeuverDir()` はコントローラのメンバ `lc_signal_dir_` を読むだけで、
なぜホップが armed になったかを知らない（`ControllerVirtualDriver.cpp:1878-1882`）。
したがって**追い越しは `lc_signal_dir_` に同じ契約で書き込むだけでよく、`DetectManeuverDir` の
段構造は変えない**。

必要な変更は 1878 行の **enable ゲート1行だけ**である。

```cpp
// 変更前
if (lc_init_cfg_.enabled && lc_signal_dir_ != 0)
// 変更後
if ((lc_init_cfg_.enabled || overtake_cfg_.enabled) && lc_signal_dir_ != 0)
```

> **並行セッション（`req-vd-ad:REQ-AD-021` 交差点ウィンカー）との衝突回避**:
> あちらは `DetectJunctionTurn`（`:1897-1905`）側を触る。本件が `DetectManeuverDir` に触るのは
> **上記1行のみ**とし、それ以外の追い越しロジックは `Step()` の LC ブロック内に閉じる。

外側ゲート `if (lc_init_cfg_.enabled)`（`:926`）も
`if (lc_init_cfg_.enabled || overtake_cfg_.enabled)` に広げる。ブロック内の**経路要求ぶんの分岐は
`lc_init_cfg_.enabled` で内側から改めて閉じる** — 追い越しだけを有効化した利用者に経路要求 LC が
出てはならない。

`ShouldAttemptLaneChangeHop` / `ShouldSignalLaneChangeHop` は `cfg.enabled` を**内部で見る**
（`LaneChangeInitiation.cpp:59, 77`）。§5-2 の安全弁がこれを呼ぶときは
`lane_change_initiation_enabled=false` でも判定が要るので、**`enabled=true` に上書きしたコピーを
渡す**（この一点だけ。他の値は触らない）。この迂回は実装コメントで明示すること。

## 7. 対向車線を使う追い越し（片側1車線）

**スコープに含める**（2026-08-04 ユーザー判断）。ただし**独立キー
`overtake_use_opposing_lane_enabled` で二重に閉じ、既定 OFF**とする。理由は
`vd-func:FUNC-030`（追い越し禁止区間・実線遵守）が未実装であり、追い越し禁止の標示を無視して
対向車線に出る挙動になるためである（§10）。

### 7-1. 対向レーンの特定

同一走行方向に追い越しレーンが無いときだけ発動する。対向レーンは**レーン id の符号が反転した側**の
最初の走行レーンで、RHT の `-1` からは `+1`。

> **訂正（2026-08-04、実装時に発覚）**: 本節は当初「id 差 `+2`（id 0 は幅ゼロのセンターレーン）」と
> 書いていたが**誤り**である。`Position::Delta` は基準レーンを跨ぐとき
> **`dLaneId` の絶対値を 1 減らしてセンターレーンを無視する**。
> ```cpp
> diff.dLaneId = laneIdB - adjustedLaneIdA;
> if (SIGN(laneIdB) != SIGN(adjustedLaneIdA))
> {   // reduce delta by one to disregard the reference lane
>     diff.dLaneId  = (abs(diff.dLaneId) - 1) * SIGN(diff.dLaneId);
>     diff.dOppLane = true;
> }
> ```
> （`EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp:12279-12287`）
> したがって `-1 → +1` は生の差 `+2` が **`+1` に潰れる**。**`direction_step` は `±1`** である。
>
> **その帰結として、同方向の隣接レーンと対向レーンが同じ `dLaneId` を返しうる。**
> 判別子は `dOppLane`（`true` = 基準レーンの反対側）と `dDirection`（`false` = 逆向き）で、
> 対向車の走査は**この2つで必ず絞る**こと。`dLaneId` だけでは足りない。

**`ScanAdjacentLaneGap` は対向車に流用しない。** 同関数は見つけた前方車について
「その車の**後端**」までの距離をバンパ間ギャップとして計算する（`LaneChangeInitiation.cpp:210-214`）。
対向車で自車に向いているのは**前端**なので、そのまま使うと非対称なバウンディングボックスで
黙って測り違える。対向用のスキャンは別に書き、両側とも前端オフセットで測る。

### 7-2. 対向車の判定は前走車の式では**書けない**

`EvaluateGapAcceptance` の前方条件は `gap >= max(gap_min_m, v_ego * headway_lead_s)` で、
**同方向を前提に相対速度を使っていない**（`LaneChangeInitiation.cpp:109-118`）。対向車は
`v_ego + v_opp` で接近するので、この式をそのまま当てると**危険側に大きく外す**。専用の条件を書く。

```
t_total = lc_merge_cfg_.duration_max_s + t_pass + lc_merge_cfg_.duration_max_s   # 対向車線の占有時間
必要な対向ギャップ = (v_ego + v_opp) * t_total * overtake_oncoming_safety_factor
```

対向車が居なければ `overtake_oncoming_lookahead_m` の範囲内に居ないことを条件とする。

> **実装前に単体テストで固定すべき事実（推測で書かないこと）**:
> `Position::Delta` が対向車について返す `ds` の符号と、`dOppLane` / `dDirection`
> （`RoadManager.hpp:3864-3873`）の値。対向車は「自車の s 増加方向の前方」に居るので `ds > 0` に
> なるはずだが、`dDirection` の真偽を含めて**実物で確かめてから**式を書くこと。

### 7-3. 見通し距離のモデルは無い

`overtake_oncoming_lookahead_m`（既定 **400.0 m**）は**スキャン距離**であって見通し距離ではない。
遮蔽（縦断勾配・カーブ・建物）のモデルはこのシミュレータの本層に存在しない。
既定 400 m は AASHTO の追い越し視距（設計速度 60 km/h でおよそ 410 m）に合わせた**出典由来の値**で、
「その範囲に対向車が居ないこと」しか主張しない。**見通しが確保されていることの主張はしない。**
これは §10 に残タスクとして記帳する。

`overtake_oncoming_safety_factor`（既定 **1.5**）は、対向車が加速する余地と `t_pass` の見積り誤差を
吸収する係数である。根拠は「見積り誤差に対する 50% 余裕」という工学的な安全率であり、実測値ではない。

## 8. config キー（確定。実装者はこの名前を使うこと）

新設は **5個だけ**。距離・余裕・合図秒数は既存キーを再利用し、増やさない（§2, §3）。

| キー | 型 | 既定 | 根拠 |
| :--- | :--- | :--- | :--- |
| `overtake_enabled` | bool | **false** | 独立キー。`lane_change_initiation_enabled` と同居させない |
| `overtake_use_opposing_lane_enabled` | bool | **false** | FUNC-030 未実装ゆえの二重ゲート（§7） |
| `overtake_max_pass_time_s` | number | 10.0 | AASHTO PSD の左車線占有時間 `t2`（9.3–11.3 s）中央付近（§3-1） |
| `overtake_oncoming_lookahead_m` | number | 400.0 | AASHTO PSD（設計速度 60 km/h ≒ 410 m）。**スキャン距離であって見通し距離ではない**（§7-3） |
| `overtake_oncoming_safety_factor` | number | 1.5 | 見積り誤差に対する安全率（§7-3） |

**再利用するキー（新設しない）**: `lane_change_gap_min_m`（復帰後ギャップ `g1`）、
`lane_change_gap_headway_lead_s` / `lane_change_gap_headway_rear_s` / `lane_change_gap_ttc_min_s`
（ギャップ受容）、`lane_change_reserve_distance_m`（経路ガードの余裕）、
`lane_change_indicator_lead_time_s`（合図ドウェル）、`lane_change_lead_time_s` /
`lane_change_min_lead_distance_m`（`RequiredLaneChangeDistance` 経由）、
`resume_merge_duration_max_s`（ホップ所要時間）、`idm_lookahead`（同方向スキャン範囲）。

追従先は `lane_change_initiation.md` §8 の**5点セット**をそのまま守る（1つでも漏らすと GUI と実体がずれる）。

## 9. テレメトリと検証

### 9-1. 新しい観測量 — `overtake` ブロック

既存の `lane_change` ブロック（`VirtualDriverTelemetryJson.cpp:263-272`）は
`armed / target_track_id / target_lane_id / direction / n_remaining / required_m /
dist_to_connection / gap_accepted / gap_reason / signal_active` の10フィールドで、
**なぜホップが armed になったかを持たない**。追い越しの検証にはこれでは足りない。

> **完了の定義が要求する区別**: 「ガードが効いて追い越さなかった緑」と
> 「そもそも追い越しを検討していないから緑」は**別物**であり、後者は偽 PASS である。
> これを外から区別するには「検討した」と「なぜ止めたか」が観測量として要る。

新設する `overtake` ブロック（毎フレーム）:

| フィールド | 型 | 意味 |
| :--- | :--- | :--- |
| `phase` | string | `idle` / `signal_out` / `out` / `pass` / `signal_back` / `back` |
| `considered` | bool | **§3 の速度条件と拘束条件を満たす先行車がこのフレームに居た** |
| `lead_id` | int | 検討対象の先行車。無ければ -1 |
| `delta_v_mps` | number | `v_pass - v_lead`。`considered` が false のとき 0 |
| `t_pass_s` | number | 追い抜き所要時間の見積り |
| `required_m` | number | §2 の `d_out + d_pass + d_back + d_route + reserve` |
| `route_budget_m` | number | `dist_to_connection`（-1 は「接続点なし」） |
| `blocked_reason` | string | 固定語彙。`""` / `route_budget` / `gap` / `oncoming` / `no_passing_lane` / `suppressed` |
| `cleared_lead` | bool | 先行車を縦方向にクリアした |

`blocked_reason` の語彙を固定するのは `gap_reason`（`"" / lead_gap / rear_gap / rear_ttc`）と同じ流儀。
**`considered == true` かつ `blocked_reason == "route_budget"` かつ `phase` が終始 `idle`** が
「ガードが効いた」の一次証拠であり、`considered` が一度も true にならなければ**その緑は偽 PASS**である。

### 9-2. 新 matcher — `overtake_decision_holds`

新しい観測量が増えたので**新 matcher を作る**（`lane_change_initiation.md` §7 が
`route_lane_plan_holds` を拡張して済ませたのは観測量が増えなかったからで、今回は逆）。
`vd_metrics.py` に追加する。must キー:

| キー | 意味 |
| :--- | :--- |
| `expect_considered` | bool。`considered` が1フレームでも true になったか（**偽 PASS 検知の要**） |
| `expect_blocked_reason` | string。`blocked_reason` がこの値になるフレームが存在するか |
| `expect_phases` | list[string]。この順でフェーズが観測されたか（部分列一致） |
| `forbid_phases` | list[string]。これらのフェーズが一度も観測されないこと |
| `expect_cleared_lead` | bool。先行車をクリアしたか |
| `window` | `[t0, t1]`（任意） |

**キーを1つも指定しない must は `skip`**（`route_lane_plan_holds` / `indicator_leads_lane_change` と
同じ「何も評価しないものを pass にしない」規律）。`overtake` ブロックが欠けているフレームしか無ければ
`skip`（古い DLL の検出）。

### 9-3. 指示器は既存 matcher を **window 違いで2回**呼ぶ

`indicator_leads_lane_change` は gated 窓の中で **最初の** `signal_active` と **最初の** `armed` しか
拾わない（`vd_metrics.py:1757-1778` の `next(...)`）。追い越しは車線変更が2回なので、
**`window` を往路・復路で分けて2エントリ書く**。matcher 側は変更しない。
`direction` の一致検査（`:1816-1830`）が入っているので、復路で左右が反転していることも同時に検査される。

> `window` を分けて同 matcher を2回呼ぶ運用は**既存資産に前例が無い**（調査Cの確信度低項目）。
> 実測で両エントリが独立に pass することを示すまで、この方式が効くとは書かない。

### 9-4. 検証資産 — 道路が無いので作る

調査の結論: **「直線・片側2車線以上・数百 m・その先に junction」を満たす既存 xodr は無い。**
最も近い `straight_500m_2lane.xodr`（500 m 直線・片側2車線）と `two_plus_one.xodr`（追い越し帯あり）は
**どちらも junction を持たない**ので「追い越すと分岐を逃す」配置が作れない。
`highway_example_with_merge_and_split.xodr` は接続レーン制限を持つが road0 が 200 m でカーブ主体。

> **決定: `resources/scenario_authoring/road_catalog/` に生成器を1本足す。**
> 既存4本（`gen_4way_priority` / `gen_signalized_short_block` / `gen_straight_crosswalk` /
> `gen_t_junction`）と同じ流儀で、長い直線本線＋末端の junction＋**接続レーンを1本に絞った分岐**を
> 出す。`--length` / `--lanes` / `--exit-lane` を引数に取り、**追い越しの距離バジェットを
> シナリオ側から算術で設計できる**ようにする。

道路2種:

| 道路 | 用途 |
| :--- | :--- |
| 片側2車線（`--lanes 2`） | 同方向追い越し（§4）。ego は外側 `-2`、追い越しレーンは `-1`、分岐は `-2` からのみ接続 |
| 片側1車線（`--lanes 1`） | 対向車線追い越し（§7） |

シナリオ4本（案。実装時に算術を確かめてから確定する）:

| シナリオ | 何を分離するか |
| :--- | :--- |
| `overtake_slow_lead_on_two_lane_road` | REQ-AD-023 段 a-c。追い越しが発起・完遂される |
| `overtake_declined_before_route_branch` | **REQ-AD-024 段 b。ガードが効いて追い越さない**（`considered=true` かつ `blocked_reason=route_budget`） |
| `overtake_aborted_for_route_branch` | REQ-AD-024 段 c。追い抜き途中で経路優先に切り替える（§5-2 の安全弁） |
| `overtake_into_oncoming_lane_when_clear` | REQ-AD-023 段 e（対向車線。§7） |

**シナリオを書く前に算術で成立を確かめること**（2026-08-03 の学び）。
`required_m` と道路長・ego 生成位置の予算が合わないと、ego が生成された瞬間に既にしきい値の内側にいて
何も測れない。`overtake_declined_*` は特に、**ガードが効く**ように
`d_out + d_pass + d_back + d_route + reserve > dist_to_connection` を満たしつつ、
**追い越しが検討される**ように `considered` が true になる配置でなければならない。
両立しない配置は「そもそも検討していないから緑」になる。

`deviation_count` は road が切り替わる瞬間にしか増えない。緑を見たら road/lane のトレースで
ego が実際に junction を渡ったことを確認する。Route の Waypoint は `scripts/check_route_waypoints.py` を通す。
xosc の header date は固定リテラル。

## 10. スコープ外（意図的に実装しないこと・残タスクとして記帳する）

| 項目 | 理由 | 記帳先 |
| :--- | :--- | :--- |
| `vd-func:FUNC-030` 追い越し禁止区間・実線遵守 | 未実装。**本設計は禁止区間を無視して追い越す。** 対向車線追い越しを既定 OFF にしているのはこのため（§7） | `vd-func:FUNC-030` の note・`REQ-AD-023` の残リスク |
| `vd-func:FUNC-044` 追い越し車への譲り | 譲る側は別機能。追い越される側の挙動は本設計では変えない | `vd-func:FUNC-044` の note |
| 見通し距離（遮蔽）のモデル | 縦断勾配・カーブ・遮蔽物による視距はこの層に無い。§7-3 はスキャン距離しか主張しない | `REQ-AD-023` の残リスク |
| 復路が塞がったときの減速・待機 | `lane_change_initiation.md` §9 の決定を継承（§5-2） | 本書 §5-2 |
| 2レーン以上ずらす追い越し | 1回に1レーンだけ動く（同 §3 の3つの理由がそのまま当てはまる） | 本書 |
| 追い越し後の巡航速度復帰の縦制御 | 既存の `ResolveTargetSpeed` / LeadVehicleAware がそのまま働く。本設計は横方向しか触らない | — |

## 11. 参考

- 前段の設計: `lane_change_initiation.md`（§1-§12 が本書の前提。特に §2 所有権・§3 決断距離・
  §4 ギャップ受容・§5 入れなかったときの振る舞い・§8 既定 OFF の5点セット・§11 指示器）
- 経路レーン帯: `route_lane_plan_design.md`
- 所有権の正典: `domain_split_ownership.md` / `control_ownership_pitfalls.md`
- 軌道の正典: `include/gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp` のヘッダコメント
- 記述ルール（Waypoint の静的検証）: `scenario_authoring_foundation.md` §10 と
  `scripts/check_route_waypoints.py`
- 生成器の流儀: `resources/scenario_authoring/README.md`
