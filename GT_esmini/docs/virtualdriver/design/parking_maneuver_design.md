# VD 駐車機能 — 枠探索とマヌーバ実行の設計

> ステータス: **未実装（設計のみ）**。
> 知識グラフ: `req-vd-ad:REQ-AD-019`（駐車枠の探索・選定）/ `req-vd-ad:REQ-AD-020`（駐車マヌーバの実行）、
> `vd-func:FUNC-076`（枠探索・選定）/ `vd-func:FUNC-077`（マヌーバ実行）。受け皿シーンは `scene:SCN-014`。
> 正文は `GT_esmini/docs/knowledge/requirements_vd_ad.yaml` の該当 ID を見ること。

## 1. 目的とスコープ

本設計は `REQ-AD-019`「駐車枠の探索・選定」と `REQ-AD-020`「駐車マヌーバの実行（前進・後退）」を実装へ落とすためのものである。
両要求は `REQ-AD-016/017` と同じ理由で1本にまとめていない。
枠を選ぶ（認識・判断）と枠へ入る（軌道生成・制御）は要素技術が別で、片方だけが先に完成しうるからである。

対象は**直角駐車**の**前進進入**と**後退進入（バック駐車）**で、後退側は**切り返し3回以内**を許容する。
枠寸法は日本の普通車標準である**幅2.5m×奥行5.0m**を検証基準とする。
**縦列駐車**と**非構造面（レーン無し広場）への駐車**はスコープ外とし、後続の要求・設計に送る。

精度基準は `REQ-AD-020` の acceptance_ladder 段cに従い、必須と望ましいの2水準を持つ。

- **必須**: 車体外形が四輪とも枠内に収まる、ヨー偏差が±5°以内、前後端のはみ出しがない、切り返しが3回以内
- **望ましい**: ヨー偏差が±3°以内（ISO 16787 の実務水準を示す二次資料の値。原文は未確認）、枠中心からの横偏差が0.3m以内、切り返しが1回以内

この2水準を持つのは、**切り返しが精度を高めるための手段**として位置づけられているためである。
1回で必須水準に達する場合でも、切り返しを重ねて望ましい水準へ寄せてよい。
この寄せる動作を担うのが §7 の修正ループである。

## 2. アーキテクチャ

### 2-1. モード遷移 — レーン追従から駐車専用コントローラへ

現行の `ControllerVirtualDriver` は `TrajectoryShortPlanner`（route 前方を `MoveAlongS` で歩く `(x,y,v,t)` プレビュー生成）
→ `ManeuverAwareSpeedPlanner`（`v_target(s)` 天井）→ `PIDPurePursuitDriver`（`(x,y,v,t)` から steer/throttle/brake を逆算）という
パイプラインで走る。
このパイプラインは**任意の2D経路を追従する入力点を持たない**。
`TrajectoryShortPlanner` は常に前進のみを仮定し、横方向は「レーン中心 + t 軸オフセット」に限定されている。
駐車の軌道（後退円弧、切り返し点での方向反転、通路を跨ぐ斜め移動）はこの入力形に収まらない。

したがって駐車モードでは既存パイプラインを丸ごと止め、専用コントローラが `PedalSteerCommand`（steer/throttle/brake/gear）を
直接組み立てて `ControllerVirtualDriver::Step()` へ返す構成を採る。
分岐は `Step()` の冒頭、既存プランナー群の呼び出しより前に置く。

```
Step()
  if (parking_mode_active_)
      cmd = parking_controller_.Compute(object_, timeStep, &parking_snapshot_);
  else
      // 既存パイプライン: TrajectoryShortPlanner -> ManeuverAwareSpeedPlanner -> PIDPurePursuitDriver
      ...
  // 以降（AdSteeringEnvelope、manual override merge、bus 発行、ApplyLights）は両モード共通
```

`AdSteeringEnvelope`（§5 で述べる操舵速度制限とは別の、雨天速度域向けの横安全包絡線）、手動オーバーライドのマージ、
ドメインバスへの発行、`ApplyLights` は駐車モードでもそのまま通す。
駐車モードだけを特別扱いする理由がないからである。

### 2-2. モード遷移の条件

**開始条件**は「自車が駐車を計画している」（`REQ-AD-019` 本文の前提）を操作可能な条件へ落とす必要がある。
本設計では次の代理条件を提案する。

- route の終端（目的地 waypoint）が parkingSpace を持つ road、または parkingSpace が複数存在する road の近傍にある
- かつ、車速がしきい値以下で、当該 road の通路（driving レーン）上にいる

これは `REQ-AD-016`（起点と目的地だけからの経路生成）の完成を前提にしない代替であり、目的地ルーティングが実装されるまでの
**暫定条件**として扱う。
`REQ-AD-016` が完成した時点で、目的地が駐車枠であることをルーティング層が直接教える形に置き換わる見込みである。
この置き換えは本設計のスコープ外とし、§12 のオープン課題に送る。

**終了条件**は3通りある。

- マヌーバ完了（終端姿勢が §7 の必須水準を満たす）
- 修正ループが3回の切り返しに達しても必須水準を満たせない（そのまま停止し、テレメトリで未達を報告する。§7）
- シナリオまたは手動運転者による制御の奪取（`IsScenarioControlled` 相当のガード、または手動オーバーライドの検出）

### 2-3. vd-component の分割

既存の `ITrafficPolicy` 実装7種は「走行中の交通ルール遵守で縦制約を出す」型であり、駐車の主軸（枠選定・軌道生成・
マヌーバ実行）はこの型に馴染まない。
`route-lane-plan` や `lane-change-initiation` が `ITrafficPolicy` を立てず独立層として `component_catalog_vd.yaml` に
登録されているのと同じ理由で、駐車機能も新設の vd-component として分割する。

実装が存在しない現時点では `component_catalog_vd.yaml` への登録は行わない（同ファイルの `path` フィールドは実在パスを
指す前提で、未実装ユニットを登録すると台帳の意味が壊れる）。
フェーズA着手時に、実装と同じコミットで登録する。

想定する分割は次の5ユニットである。

| ユニット（仮 slug） | 想定 layer | 責務 |
| :--- | :--- | :--- |
| `parking-space-finder` | 独立層（mid-long-planner に近いが既存 `IMidLongPlanner` ではない） | §3。parkingSpace 列挙・占有判定・access 判定・RS 検算による足切り・ランキング |
| `parking-rs-checker` | cross-cutting | §6。Reeds-Shepp 検算。枠選定・マヌーバ開始前検算・修正ループの3箇所から呼ばれる |
| `parking-trajectory-planner` | 独立層（short-planner に近いが既存 `IShortPlanner` とは互換性がない） | §4。三画/五画運動の軌道生成、Ma&Qian 評価関数による半径選定 |
| `parking-maneuver-fsm` | driver-model 相当（cmd を直接出す） | §2-1, §7。マヌーバの状態機械、修正ループの制御 |
| `parking-terminal-servo` | driver-model 内のステージ | §7。終端姿勢への低速位置サーボ |

既存の3インターフェース（`IShortPlanner`/`IMidLongPlanner`/`IDriverModel`）を流用しない理由は `route_lane_plan_design.md` §3-2
と同じである。
これらのインターフェースは「差し替え可能な実装が複数ありうる」から抽象化されている。
駐車の各ユニットは1実装しかなく、既存インターフェースの契約（`v_target(s)` を返す、`(x,y,v,t)` プレビューを消費する等）とも
形が合わない。

## 3. 枠探索・選定ロジック（REQ-AD-019）

### 3-1. 列挙

`roadmanager::Road::GetNumberOfObjects()` / `GetRoadObject(idx)` で route 近傍の road を走査し、
`RMObject::GetParkingSpace()`（`RoadManager.hpp:2309`）が既定値でない（＝`<parkingSpace>` 子要素を持つ）オブジェクトを
候補として拾う。
この走査パターンは `GT_esmini/src/road/odr_side/OdrObjectExtras.cpp`（`GetNumberOfObjects`/`GetRoadObject` を road ごとに
回す）に前例がある。
parkingSpace のパースは既に面1（`GT_RoadManager.cpp` の `parking_space_node` 処理、`Set/GetParkingSpace()` 経由の保持）で
完了しており、`GT_OSIReporter.cpp` が OSI `StationaryObject`（`TYPE_OTHER`、`source_reference` に `restrictions:` 識別子）
として出力もしている。
本ユニットが新設するのは**この既存出力を VD 側が消費する経路**であり、パース自体は再実装しない。

### 3-2. 占有判定

枠のポリゴン（`parkingSpace` の輪郭。無ければ枠を囲む `object` の `outline`/`repeat` から矩形近似する）と、
他エンティティの OBB（有向境界ボックス）の交差判定で空き枠を絞り込む。
OBB 交差判定は `ConflictPointResolver` が交差点で使っている方式を流用できる見込みだが、詳細設計はフェーズA着手時に行う。

### 3-3. access 属性

`ParkingSpace::Access`（`ACCESS_ALL`/`ACCESS_BUS`/`ACCESS_CAR`/`ACCESS_ELECTRIC`/`ACCESS_HANDICAPPED`/`ACCESS_RESIDENTS`/
`ACCESS_TRUCK`/`ACCESS_WOMEN` の8種）を見て、自車属性（乗用車=`ACCESS_CAR`/`ACCESS_ALL` のみ許容、といった単純規則）に
合わない枠を除外する。
自車属性を持たないエンティティ設定が大半であるため、既定は「`ACCESS_ALL`/`ACCESS_CAR` のみ選定対象、それ以外は除外」と
する。

### 3-4. RS 検算によるランキング

空き枠かつ access 適合の候補が複数あるとき、§6 の Reeds-Shepp 検算を全候補に対して回し、`feasible=false` の枠を除外、
残った候補は `num_cusps`（切り返し相当数）が少ない順に優先する。
これは §6 で述べる「片側保証の棄却フィルタ」としての用途であり、RS 曲線そのものを走行経路として使うわけではない。

### 3-5. 再選定

接近中に選定枠が新たに占有された場合（他車の割り込み等）、§3-2 の占有判定を再度回し、次点の候補へ切り替える。
再選定は駐車モード開始前（まだ通路レーンを走行中）にのみ行い、マヌーバ実行中（§7 の状態機械が `MANEUVERING` 以降）には
行わない。
マヌーバ中の枠変更は軌道の再生成を要し、REQ-AD-019 の受入段dが「将来スコープ」と明記する高度化（複数回切り返しの高度化）
と地続きの複雑さを持つためである。

## 4. 軌道生成（REQ-AD-020）

軌道生成の主構造は Han (2022)[^han2022] の単位運動連鎖に置く。
車両の単位運動は前進直進 `S+`・後退直進 `S-`・左転前進 `L+`・左転後退 `L-`・右転前進 `R+`・右転後退 `R-` の6種で、
これらを文字列として連結した「画」でマヌーバを表す。

### 4-1. 旋回半径と車両の代表点

後軸中心を基準に、ホイールベース `l` と舵角 `δ` から旋回半径を得る。

```
R  = l / tan(δ)                                  # 旋回中心 O から後軸中心までの距離
R_A = sqrt((R + w0/2)^2 + p_r^2)                  # O から後方外側コーナーまでの距離
R_B = sqrt((R + w0/2)^2 + (l + p_f)^2)             # O から前方外側コーナーまでの距離
R_c = R - w0/2                                    # O から後方内側車輪までの距離
```

`w0` は車両全幅、`p_f`/`p_r` は前後オーバーハングである。

### 4-2. 三画運動（バック直角駐車、切り返しなし）

`S+ R- S-`（前進で枠の脇を通過 → 後退円弧で向きを合わせる → 枠内へ直進後退）が最短形である。
`R-` の開始点は、後軸中心が枠中心線から `R` だけオフセットした点にとる。

切り返しなしで入庫できる枠幅の下限 `Wmin` は、進入開始時の横方向オフセット `ΔY`（自車と枠中心線の距離）に応じて
3レジームに分かれる。

```
ΔY >= R_c + p_r          -> Wmin = w0
R_c <= ΔY < R_c + p_r     -> Wmin = sqrt(R_A^2 - e^2) - R_c        # e = ΔY - R_c
ΔY < R_c                  -> Wmin = R_A - sqrt(R_c^2 - e^2)         # e = R_c - ΔY
```

Han (2022) は模型車での実験で、この式が予測する合否境界が実測と一致することを確認している。

通路側に必要な範囲（駐車操作のために枠の手前でどれだけ通路を使うか）は次の式で見積もる。

```
Xmin = R + l + p_f     # 進行方向の必要長
Ymin = R_B - e          # 枠をどれだけ追い越して停止する必要があるか
```

### 4-3. 五画運動（枠幅が Wmin に満たない場合、切り返し1回）

枠幅が §4-2 の `Wmin` を満たさないとき、前進側にオフセット角 `θ` を作ることで枠幅要求を緩められる。
`θ` の大小で経路の形（S の位置に前進が入るか後退が入るか）が変わる。

```
0°  < θ <= 45°   -> S+ L+ S+ R- S-        # 中間の直進は前進
                      Δx = R * tan(θ/2)
                      Δs = R * (tan(90° - θ/2) - tan(θ/2))
45° < θ <= 90°   -> S+ L+ S- R- S-        # 中間の直進が後退に反転（Δs の符号も反転）
```

`θ = 45°` で中間の直進区間が消滅し、四画運動になる。
`θ` は「枠境界をクリアできる最小値」を選ぶ。
Han (2022) の実験例では `θ_min ≈ 20°` が使われている。

`θ` のレジームによって `Ymin` の式に現れる `R_B`/`β`（`β = arctan[(l + p_f) / (R ± w0/2)]`）の定義が変わるため、
実装時は原文の該当式を再確認すること。
本設計書では構造（θ による分岐と5つの単位運動への分解）のみを確定事項として扱う。

### 4-4. 前進駐車（自前導出）

Han (2022) と Ma & Qian (2025) のいずれも**前進駐車の閉形式を持たない**。
両論文はバック駐車（reverse parking）のみを扱っている。

アッカーマン幾何（後軸中心を基準にした円弧幾何）自体は進行方向に依存しないため、旋回半径・代表点の式（§4-1）は
前進側にもそのまま使える。
ただし**衝突チェックの当たり点が反転する**。
バック駐車では後角（枠の奥）が最初にクリアすべき境界だが、前進駐車では前角（枠の入口側の対向縁）が最初にクリアすべき
境界になる。
この当たり点の反転を含む前進駐車の衝突チェック式は、両論文に裏付けを持たない**自前導出**であることを明記する。
導出の妥当性検証は §12 のオープン課題とする。

### 4-5. Ma & Qian (2025) — 半径候補の採点層

三画/五画運動で `R`（＝舵角 `δ`）が一意に決まらない場合（許容範囲に複数の半径候補が残る場合）、
Ma & Qian (2025)[^maqian2025] の多目的評価関数を候補選定に使う。

実行可能半径帯 `[R_min, R_max]` は、枠角・通路境界・左境界との当たり判定（原論文 eq.7-10）から求める。
帯内の候補を次の評価関数で採点し、最大のものを採用する。

```
E = a1 * k_hat + a2 * alpha_hat + a3 * s_hat
```

`k_hat`/`alpha_hat`/`s_hat` はそれぞれ曲率・舵角余裕・経路長を正規化した値で、重みの一例は `a1=0.5, a2=0.3, a3=0.2`
（快適性を最優先、次に効率、最後に余裕という優先順位に対応）である。

**記号衝突への注意**: 原論文は記号 `L` を枠の長さとホイールベースの二重の意味で使っており、また重みの対応が本文§3.2.4と
§4.1で食い違う箇所がある。
実装時はこの重みを写経せず、快適性・効率・余裕の優先順位という**理由**から再導出すること。
連続単一曲線（4次多項式平滑化）方式は本設計の離散セグメント構造（§4-2/4-3の単位運動列）と両立しないため不採用とする。

### 4-6. クロソイド平滑化（フェーズ2オプション）

Vorobieva et al. (2014)[^vorobieva2014] のクロソイド平滑化は、旋回中心を角度 `µ` だけ回転させ、半径を `R1 >= R_min` に
置き換えることで、円弧と直線の接続点でのステップ的な曲率変化を連続化する（原論文の Case A/B/C 分岐に対応）。
この手法は**後付けできない**。
経路生成の段階から旋回中心の回転を織り込む必要があるため、単純な後処理としては実装できない。

esmini のシミュレーション上の車両には、据え切り（停車中の転舵）による物理的な摩耗が存在しない。
したがって**フェーズ1は円弧＋直線＋接続点での停車据え切りで実装**し、クロソイド化は**忠実度向上が要る場合のフェーズ2
オプション**として位置づける。
§5-4 で、操舵速度制限（フェーズ1で導入）とクロソイド平滑化（フェーズ2）が同じ物理量を異なる形で扱っているだけであることを
述べる。

## 5. 操舵指令生成と操舵速度制限

### 5-1. なぜ制限が要るか

このリポジトリには、G29 実ハンコンを FFB PID サーボ（`FfbTargetServo`、CONSTANT 力 @250Hz）で物理的に駆動する構成
（MD横/VD縦のドメイン分割、`domain_split_ownership.md`）がある。
駐車コントローラがセグメント境界（§4-2の`S+`と`R-`の切り替わり等）で目標舵角をステップ状にジャンプさせると、実ホイールは
物理的にその速度で追従できない。

さらに `ControllerVirtualDriver` には既存の手介入検出（`feature:F7` の `OverrideManager`）があり、これは AD 目標舵角と
実ホイール角の残差を見て手動介入を判定する。
過去に「残差が運転者の抵抗ではなく AD 目標角の**移動速度**そのものを測ってしまう」誤ラッチが報告されたことがあり
（AUTO_RESUME 再武装直後、目標角が1フレームで大きく動く事例）、resume-merge の文脈ではこの経路が是正されている
（commit `fa4b049b`）。
ただしこの是正の確認範囲は力2水準・速度1点・timing1パターンに限られる。
駐車の「停車中に転舵 → レート制限下でランプ」という timing パターンはこの確認範囲に含まれないため、同じ機序が別文脈で
再発する余地が残る。
これは §12 のオープン課題として扱う。

### 5-2. スルーレート制限の定義

駐車コントローラが出す舵角指令に、設定可能な**最大舵角速度 `v_δ`** によるスルーレート制限を掛ける。

単位系は既存の `AdSteeringEnvelope`（`feature:F7` の安全包絡線、`steer_rate_max`）に揃え、**前輪の等価舵角 `δ` の
レート [rad/s]** を基準とする。
`AdSteeringEnvelope` の `steer_rate_max` は既定 1.5 rad/s で、これは通常走行時のピーク（実測プール最大 0.769 rad/s、
別の27シナリオ実測で最大 0.964 rad/s）に対して定めた値である。
駐車の据え切りはこれとは別の運動レジーム（速度ゼロ、フルロック級の大振幅転舵）であるため、`v_δ` は
`steer_rate_max` を流用せず**駐車専用のパラメータ**として置く。

既定値は本設計書では未確定とし、フェーズC（§11）着手時に次の2点を根拠として校正する。

- 実車の据え切り速度の代表値（一般的な EPS の据え切り角速度は概ね数十 deg/s のオーダーで、正確な値は実車仕様に依存する）
- G29 の FFB PID サーボの追従能力。`ffb_g29_sdl2_traps` の実測では、CONSTANT 力サーボの追従性はデッドバンド
  （摩擦の breakaway をゲイン `Kp` で割った値、G29 実測で概ね 0.0475 frac）と振幅の大小に強く依存する。
  駐車の大振幅転舵は、既存 F7b の実車追従率測定（旋回系シナリオで 92-109%）に近い体制になる見込みだが、駐車専用の
  実測はまだ無い。

### 5-3. フェーズ1幾何への影響 — 境界での停車据え切り

フェーズ1（円弧＋直線＋接続点での停車据え切り、§4-6）では、単位運動の境界（例えば `S+` から `R-` への切り替わり）で
車両を停止させ、`v_δ` の制限下で目標舵角まで転舵してから次の単位運動を開始する。

この構成では、**§4 の幾何式は無傷のまま**である。
`Wmin`/`Xmin`/`Ymin` はいずれも旋回半径 `R` と車両寸法だけで決まる式で、転舵にかかる時間を含んでいない。
`v_δ` 制限が追加するのは、境界ごとに `Δt = Δδ / v_δ` の待ち時間だけであり、**軌道の形は変わらず、マヌーバ全体の所要時間
だけが伸びる**。

この待ち時間はマヌーバ実行 FSM の明示的な状態として表す。

```
IDLE -> APPROACH -> STEER_AT_STANDSTILL -> TRACK_SEGMENT -> STEER_AT_STANDSTILL -> TRACK_SEGMENT -> ... -> TERMINAL_SERVO -> DONE
                     ^                                       ^
                     単位運動の境界で毎回経由する             次の単位運動区間を走行
```

`STEER_AT_STANDSTILL` では車速0を維持しつつ `v_δ` でランプし、目標舵角に達したら `TRACK_SEGMENT` へ遷移する。

### 5-4. フェーズ2クロソイドとの接続

走行しながら転舵する場合に同じ `v_δ` 制限を掛けると、軌跡は自然にクロソイド（曲率が弧長に比例して変化する曲線）になる。
Vorobieva et al. (2014) のクロソイド最小パラメータの式

```
A_min = sqrt(R_min * L_min)
L_min = v_longi * (δ_max / v_δ)
```

は、`v_δ` を明示的なパラメータとして持つ式そのものである。
つまり**フェーズ2のクロソイド化は、操舵速度制限を「停車中」から「走行中」へ拡張したものとして位置づけられる**。
フェーズ1は `v_longi = 0` の特殊ケース（`L_min = 0`、クロソイド区間が生じず、その場の待ち時間 `Δt` に縮退する）に
相当し、フェーズ2はこれを一般化して `v_longi > 0` でも滑らかに転舵しながら曲線区間へ入る。
両フェーズが同じ `v_δ` という1つの物理量の異なる使い方であることが、§4-6 で「後付け不可・生成段階から要る」と
述べたクロソイド化の性質と整合する。

### 5-5. 修正ループ・終端サーボへの適用

§7 の修正ループが再計画する小修正マヌーバ、および終端位置サーボ（`parking-terminal-servo`）が出す低速時の微調整
指令にも、同じ `v_δ` 制限を適用する。
低速だからといって指令のジャンプを許すと、§5-1 の懸念（実ホイールの追従不能、F7 誤ラッチ機序の再現条件）が終端付近
でも成立してしまうためである。

## 6. Reeds-Shepp 検算コンポーネント

### 6-1. 責務

Reeds-Shepp 曲線[^rs-pythonrobotics]は、後退を含む最短経路の存在と長さを閉形式で求める古典的な結果である。
本設計では**経路そのものを走行経路として使わない**。
使うのは「ある姿勢からある姿勢へ、最小旋回半径 `r_min` の制約下で到達可能か」という**片側保証**だけである。

> 障害物を考慮しない Reeds-Shepp 検算が infeasible を返したなら、障害物を無視しても到達不可能である。

この片側保証（infeasible ⇒ 実際にも不可能。feasible ⇒ 障害物次第で実際には不可能なこともある）が、責務分離の根拠になる。
障害物込みの実行可能性は §3-2 の占有判定と §4 の軌道生成が担い、本コンポーネントは**否定側の判定だけ**を受け持つ。

### 6-2. API

```cpp
struct ReedsSheppSegment { char type; double signed_length; };

struct ReedsSheppResult
{
    bool                              feasible;
    double                            total_length;
    int                               num_cusps;      // 方向反転の回数（切り返し相当）
    std::vector<ReedsSheppSegment>    segments;
};

ReedsSheppResult CheckReedsSheppFeasibility(const Pose2D& current, const Pose2D& target, double r_min);
```

**点列（実座標での経路点）は生成しない**。
弧長と種別だけを返すことで、点列生成につきまとう浮動小数点の累積誤差（後述）を最初から避ける。

### 6-3. 呼び出し箇所

1. **枠選定時の足切り・ランキング**（§3-4）。`feasible=false` の枠を除外し、`num_cusps` が少ない候補を優先する。
2. **マヌーバ開始前の事前検算**。`num_cusps` が3を超える場合、当該枠を「非対応枠」として却下する（`REQ-AD-020` の
   切り返し3回以内という受入基準と対応する上限）。
3. **修正ループの再到達可能性ゲート**（§7）。小修正マヌーバを再計画する前に、現在姿勢から終端姿勢へ到達可能かを確認する。

### 6-4. C++ 移植難所

参照実装は PythonRobotics の `reeds_shepp_path_planning.py`（MIT ライセンス）で、移植時に次の難所がある。

- `mod2pi` の符号付き剰余の厳密な一致（言語間で剰余演算の符号規約が異なる）
- `acos`/`asin` の定義域クランプ（浮動小数点誤差で引数が `[-1, 1]` をわずかに超えることがある）
- `u1` 境界付近の数値誤差
- `step_size × maxc` の正規化における単位系の取り違え
- `LRLR` 系列は `u1 <= 2` のケースのみを扱う（劣最適解を意図的に切り捨てている実装であり、全網羅ではない）

移植元は **MIT ライセンスの著作権表示を保持する義務**がある。
移植先ファイルのヘッダに `reeds_shepp_path_planning.py` の著作権表示を引き継ぐこと。

## 7. 修正ループ

### 7-1. 終端姿勢誤差の判定

マヌーバ完了直後、終端姿勢を §1 の必須/望ましい2水準と照合する。

- **必須水準**を満たさない場合、小修正マヌーバを再計画する（§7-2）。
- **必須水準は満たすが望ましい水準を満たさない**場合も、切り返し回数が上限（3回）に達していなければ小修正マヌーバを
  試みてよい（§1 で述べた「切り返しは精度を高める手段」という位置づけに対応する）。
- **望ましい水準を満たす**場合、そこで停止しマヌーバを完了とする。

### 7-2. 小修正マヌーバの再計画

現在姿勢を起点、選定枠の中心姿勢を終点として、§6 の Reeds-Shepp 検算で到達可能性を確認したうえで、§4 の軌道生成
（三画/五画運動）を再度回す。
再計画された軌道にも §5 の操舵速度制限をそのまま適用する。

### 7-3. 打ち切り

切り返しが3回に達しても必須水準を満たせない場合、**それ以上の再計画は行わずそのまま停止**する。
テレメトリへ未達を報告し（§10 のテレメトリ設計に含める）、ログには1回だけ警告を出す（`route_lane_plan_design.md` §4-1
と同じ「1回だけ警告」の作法に揃える）。
安全側に倒す唯一の選択として「動き続けて水準を追い求める」のではなく「止まって報告する」を選ぶ。

## 8. 後退指令経路

### 8-1. 現状のギャップ

`PedalSteerCommand::gear` は既定 0 で、`ControllerVirtualDriver::Step()` 内では `frame.pedal_steer` が存在する場合に
`m.gear`（手動入力側の値）でのみ上書きされる（`VehicleCommand.hpp:34`、`ControllerVirtualDriver.cpp:1254-1255`）。
この上書きは `lat_manual`/`lon_manual` のどちらであるかに関わらず起きるが、**AD側（`auto_cmd`）が gear を設定する経路は
そもそも存在しない**。
物理層（`RealVehicle::UpdatePhysicsAT`/`AutoTransmission`）は P/R/N/D の状態機械と `REVERSING_LIGHTS` 連動を既に持つが、
これは手動パドル入力を起点にした経路でしか駆動されない。

### 8-2. 新設する経路

駐車コントローラが後退を要する単位運動（`S-`/`R-`/`L-`）を実行するフレームでは、`cmd.gear = -1` を**AD発**で設定する。
既存の `if (frame.pedal_steer) { ... cmd.gear = m.gear; ... }` はそのまま残し、駐車モード中に手動オーバーライドが入った
場合は従来どおり手動側の gear が優先される（§2-1 で述べた「両モード共通」の扱いに従う）。

### 8-3. バックランプとの整合

`AutoLightController` は速度ベース（`speed < -0.01`）で `REVERSING_LIGHTS` を判定するのに対し、VD/ManualDrive/
RealDriver はギアベース（`cmd.gear == -1`）で判定しており、判定根拠が2系統に分かれている。
本設計が新設する AD発 `cmd.gear = -1` はギアベース経路に載るため、既存の `ControllerVirtualDriver::ApplyLights()`
の `set_light(VehicleLightType::REVERSING_LIGHTS, cmd.gear == -1)`（`:1937`）がそのまま機能する。
2系統の不整合自体（速度ベース側との食い違い）を解消することは本設計のスコープ外とする。

## 9. 自動ハザード点灯

### 9-1. 要件

駐車マヌーバの開始でハザード（`WARNING_LIGHTS`）を点灯し、完了または離脱で消灯する。

### 9-2. 実装フック

`ControllerVirtualDriver::ApplyLights()`（`:1907-1940`）は現状 `BRAKE_LIGHTS`/`REVERSING_LIGHTS`/`INDICATOR_LEFT`/
`INDICATOR_RIGHT` のみを書き、`WARNING_LIGHTS` を一度も書かない。
ここに駐車モード用のフックを追加する。

```cpp
set_light(VehicleLightType::WARNING_LIGHTS, parking_mode_active_);
```

### 9-3. Mode::FLASHING と duration

`LightState` は `Mode::OFF`/`Mode::ON`/`Mode::FLASHING` と点滅 duration を持つ。
`ManualDriveCoordinator.cpp:396` の既存ハザード実装は `Mode::ON`（`:359` の `ls.mode = on ? Mode::ON : Mode::OFF`）を
使っており、**点滅しない**。
これはバグであり、本設計では**踏襲しない**。

正例は `AutoLightController.cpp` の指示器実装（`:850, :854`）で、`Mode::FLASHING` を明示的に設定している。
本設計のハザードフックも `Mode::FLASHING` を duration 明示（既定 0.5s/0.5s、`LightBlinkTicker` の既定値に揃える）で
設定する。

### 9-4. IsScenarioControlled ガード

`ApplyLights()` の既存 `set_light` ラムダは `ext->IsScenarioControlled(type)` を先に見て、シナリオが当該灯火を制御中
なら書き込みをスキップする（既存パターン）。
ハザードフックも同じラムダを通すため、この保証は自動的に引き継がれる。
xosc の `LightStateAction` が `WARNING_LIGHTS` を明示制御している場合、それが常に勝つ。

### 9-5. 指示器との排他

`WARNING_LIGHTS` の書き込みは `VehicleLightBridge::ApplyLight()`（`GT_esmini/src/scenario/VehicleLightBridge.cpp:234-245`）
の集約展開で `INDICATOR_LEFT`/`INDICATOR_RIGHT` の**実スロットへ直接展開される**（`FOG_LIGHTS` が `FOG_LIGHTS_FRONT`/
`FOG_LIGHTS_REAR` へ展開されるのと同じ流儀）。
つまり `WARNING_LIGHTS` と `INDICATOR_LEFT`/`RIGHT` は**同一フレーム内で同じ実スロットを取り合う**。
`ApplyLights()` は現状、同一フレームで両方を書く順序に規律がないため、後勝ちで意図しない方が残りうる。

対処は次の2案を検討し、**(a) を推奨**する。

| 案 | 内容 | 判断 |
| :--- | :--- | :--- |
| (a) ハザード中は通常の indicator 書き込み（`:1938-1939`）をスキップ | `if (!parking_mode_active_) { set_light(INDICATOR_LEFT, ...); set_light(INDICATOR_RIGHT, ...); }` の1条件で足りる | **採用**。変更箇所が最小で、駐車中は方向指示器の意味自体が薄い（進路変更の予告ではなく作業中の存在灯示） |
| (b) ハザード書き込みを常に指示器より後段に置く | 同一フレーム内の書き込み順序だけで解決を図る | 不採用。両方が真であるフレームの実際の表示（点滅パターンの合成）が仕様として定義されないまま実装依存になる |

`IndicatorFSM`（`ManualDriveTypes.hpp:28-112`）と `AutoIndicatorPolicy`（AD側指示器）はいずれも `WARNING_LIGHTS` の存在を
知らない。
(a) 案であれば、両者を変更せずに駐車モード側の呼び出し順序だけで排他を成立させられる。

### 9-6. AutoLight 併用時の競合注意

`AutoLightController` が ego に対して有効な構成では、同コントローラ側の指示器 FSM（§9-3 で引用した実装)とも競合しうる。
`AutoLightController` は `IsScenarioControlled` に加えて GT が書いた `FLASHING` スロットを再スタンプしない配慮
（`VehicleLightBridge.cpp:111-121` のコメント）を持つが、これは「ネイティブ `LightStateAction` が握るスロットを尊重する」
仕組みであり、**VD が `MANUAL_DRIVE` ソースで書いた `WARNING_LIGHTS` を `AutoLightController` が上書きしない保証までは
含まない**。
両者が同一 ego に同時適用される構成が実運用でありうるかは §12 のオープン課題とする。

## 10. 観測・検証結線

### 10-1. 新設 signal 候補

`signal_catalog.yaml` の流儀（`exposure`: frame/OSI どちらの面から出すか、`emit`: 発生源から telemetry までの経路）に
従い、次を候補とする。

- 枠相対姿勢（ヨー偏差・横偏差・前後端余裕）
- 切り返し回数
- ハザード状態
- 後退ギア状態

### 10-2. テレメトリ拡張 vs OSI パーサ拡張

ハザード状態の観測には2案ある。

| 案 | 内容 |
| :--- | :--- |
| (a) VD テレメトリに hazard bool を追加 | `VirtualDriverTelemetryJson.cpp:157` の indicator ブロック（`vd-func:FUNC-061` の `lane_change_initiation.md` 前例と同型）に倣い、`hazard.active` を追加する。最小工数 |
| (b) `gt_sim_test.py` の OSI パーサを拡張し `light_state` を取る | signal の canonical 面は OSI（`GT_OSIReporter_Moving.cpp:547-600, 1030-1057` が `light_state.indicator_state=INDICATOR_STATE_WARNING` を毎フレーム出力済み）。非 VD 車（SUMO 等）のハザードにも同じ経路で効く |

**(b) を推奨**する。
`signal_catalog.yaml` の方針（OSI/HVD に対応概念があるものは canonical=OSI 側を優先し、無いものだけ frame 面の VD 独自
テレメトリにする）に整合し、駐車機能に限らない汎用の拡張になるためである。
ただし着手コストは (a) より大きく、`gt_sim_test.py` の OSI パーサ本体を触る変更になるため、フェーズD（§11）の中でも
別スコープとして切り出す。
`signal_catalog.yaml:572-587` の `lane_change_signal_timing`/`vehicle_lights` の二重 signal 化（意図と実態を別 signal で
持つ）は、駐車マヌーバ区間の「意図（ハザードを点けるべき区間）」と「実態（実際に点滅しているか）」を分けて観測する場合の
前例になる。

### 10-3. 新設 matcher 候補

- `parking_final_pose`（必須/望ましい2段判定、§1・§7 の水準に対応）
- `parking_cusp_count`（切り返し回数の上限判定）
- `parking_hazard_active`（マヌーバ区間がハザード点灯区間に包含されることを判定）

matcher の ID は `namespaces.yaml` の `matcher` エントリが enum 形式の `id_pattern` を持つため、新設時はこのパターンへ
追記が必要である（`GT_esmini/web/backend/services/vd_metrics.py` が source_of_truth）。

### 10-4. 刺激資産

`create_parking_lot.py` 派生の xodr（枠2.5m×5.0m）と、前進/バック各シナリオの xosc、それらを束ねるバッチ yaml を新設
する。
回帰ゲートへの常設は既存の F系ゲート（`car_following_traffic_control_batch.yaml` 等）と同じ形（`gt_sim_test.py batch`
+ `check_regression_baseline.py`）に従う。

## 11. 実装フェーズ分割

| フェーズ | 内容 | 完了条件 |
| :--- | :--- | :--- |
| **A** | 枠探索（§3）。`parking-space-finder` の新設、列挙・占有判定・access判定・RS検算連携 | parkingSpace を含む合成 xodr で、占有枠の除外と access 判定がユニットテストで確認できる |
| **B** | 駐車モード遷移（§2）+ 前進駐車（§4-4）。`parking-trajectory-planner`/`parking-maneuver-fsm` の新設、既存パイプラインとの切替 | 前進駐車シナリオで枠内停止まで完走し、§1 の必須水準を満たす |
| **C** | バック駐車（§4-2/4-3）+ 自動ハザード（§9）+ 後退指令経路（§8）+ 操舵速度制限（§5）の統合 | バック駐車シナリオで切り返し3回以内に必須水準を満たし、マヌーバ区間でハザードが点灯し続け、`v_δ` 制限下でも実ホイール追従（G29構成を使う場合）が破綻しない |
| **D** | 検証結線（§10） | 新設 signal/matcher が回帰ゲートに常設され、バッチ実行でベースライン比較が通る |

各フェーズは独立にコミットし、`REQ-AD-019`/`REQ-AD-020` の acceptance_ladder 該当段を `met: true` へ更新する。

## 12. オープン課題

- **前進駐車の衝突チェック式**（§4-4）は自前導出であり、文献による裏付けがない。実装後、代表的な枠幅・オフセットの
  組み合わせで幾何的に検算し、当たり判定の反転が正しく効いているかを確認する必要がある。
- **OSI レーンの `SUBTYPE_PARKING` マッピング**が esmini 側で実施されているかは未確認。§3 の枠探索が parkingSpace
  オブジェクトを直接見るため実装上は必須ではないが、§10-2 の OSI パーサ拡張（案b）を採るならレーン側の扱いも合わせて
  確認する。
- **`gt_sim_test.py` の OSI パーサへの `light_state` 拡張**（§10-2 案b）は駐車機能に閉じない汎用変更であり、着手時期は
  フェーズD内で別途判断する。
- **F7 誤ラッチとの相互作用**（§5-1）。resume-merge 文脈での是正（commit `fa4b049b`）は力2水準・速度1点・timing1
  パターンでの確認に留まり、駐車モードの「停車中転舵→レート制限ランプ」という新しい timing パターンは未検証である。
  ハンコン接続構成で駐車モードを動かす場合、フェーズC統合時に誤ラッチの有無を確認するタスクとして残す。
- **駐車モードの開始条件**（§2-2）は `REQ-AD-016`（目的地ルーティング）が未実装であることを前提にした代理条件であり、
  `REQ-AD-016` 完成後に置き換わる見込みである。
- **AutoLight 併用時のハザード競合**（§9-6）は、両コントローラが同一 ego に同時適用される構成が実運用でありうるかを
  含めて未検証である。

## 13. 参考文献

- Han, I. (2022). Geometric Path Plans for Perpendicular/Parallel Reverse Parking in a Narrow Parking Spot with
  Surrounding Space. *Vehicles*, 4(4), 1195-1208. https://doi.org/10.3390/vehicles4040063 [^han2022]
- Ma, J.; Qian, Y. (2025). Research on Vertical Parking Path Planning Based on Circular Arcs, Straight Lines, and
  Multi-Objective Evaluation Function. *World Electric Vehicle Journal*, 16(3), 152.
  https://doi.org/10.3390/wevj16030152 [^maqian2025]
- Vorobieva, H. ほか. (2014). Geometric continuous-curvature path planning for automatic parallel parking.
  IEEE Intelligent Vehicles Symposium (IV), 2014. [^vorobieva2014]
- PythonRobotics, `reeds_shepp_path_planning.py`（MIT License）. https://github.com/AtsushiSakai/PythonRobotics
  [^rs-pythonrobotics]

[^han2022]: 作業用ローカルコピー: `thirdparty/parking_literature/vehicles-04-00063_fulltext.txt`（git 非追跡、非コミット）。
[^maqian2025]: 作業用ローカルコピー: `thirdparty/parking_literature/wevj16-03-152_fulltext.txt`（git 非追跡、非コミット）。
[^vorobieva2014]: 作業用ローカルコピー: `thirdparty/parking_literature/vorobieva_geometric_continuous_curvature_parallel_parking.pdf`
    （個人閲覧用、再配布不可、git 非追跡）。著者全員・巻頁・DOI はローカル PDF の表紙記載を正とし、本書では未確認のまま
    引用を避けた。
[^rs-pythonrobotics]: 作業用ローカルコピー: `thirdparty/parking_literature/reeds_shepp_path_planning.py` +
    `reeds_shepp_path_planning.LICENSE.txt`（git 非追跡、非コミット）。C++ 移植先には同梱の MIT ライセンス表示を
    引き継ぐこと（§6-4）。
