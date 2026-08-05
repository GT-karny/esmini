# 手動運転中 ADAS の設計 — AEB / ACC / LKA / MSL 並行稼働

> ステータス: **フェーズA（AEB列、REQ-AD-025）実装済み**（2026-08-04〜05）。
> C++ ユニット69件緑（`GT_esmini/test/CMakeLists.txt` の manualdrive/ 配下、661/661の内数）、
> ManualDrive 検証バッチ（`manualdrive_adas_batch.yaml`）の6シナリオがいずれも frames=0 死活問題
> 無く440フレームずつ生成、AEB 実発火（gt.aeb ACTIVE 実測）と衝突速度低減（8.26 m/s）を確認、
> 既存回帰ベースライン3本（car_following_traffic_control / aeb_safety / anticipation_driving、
> 計25シナリオ）は deviations=0 で不動、全新設機能は既定 OFF。
> **§2-1（sim_time の由来）・§2-3（ドメイン所有と評価タイミング）・§3-2（FCWリードの成因）の
> 3点は実装・実測で当初の想定と食い違いが判明し、本改訂で訂正した**（各節参照）。
> **フェーズB（観測列・DriverOverride、2026-08-05）実装済み**: §8-3 の populate 機構
> （`AdasFunctionState` の `driver_override`/`custom_state` → `AddADASFunctionEx` の既定値付き
> 第6引数 → `GT_HostVehicleReporter` → OSI）、アクセル起因 producer（キックダウン →
> `custom_state=DRIVER_OVERRIDE_ACCEL`）、matcher `driver_override_reported`、および §9 に
> 記録していた `warning_min_a_req_mps2` の config 化。**ブレーキ/ステア理由の producer は
> ACC（フェーズC）/LKA（フェーズD）にしか無く、本フェーズで実証できたのは機構＋アクセル
> 経路のみ**（req-vd-ad:REQ-AD-028 段b は `met: false` のまま）。
> フェーズB実測: C++ ユニット **673/673 緑**（ManualDrive ADAS 関連 81 ケース、うちフェーズB
> 新規 12）、`manualdrive_adas_batch` は 7 シナリオ・16 matcher が pass=16/fail=0/skip=0、
> 既存回帰ベースライン5本（car_following_traffic_control / aeb_safety / anticipation_driving /
> scenario_handoff / stop_line_pairing、計31シナリオ）は deviations=0 で不動。
> **フェーズC（ACC/Stop&Go/MSL、2026-08-05）実装済み**: AccLonController（§4、状態機械・
> 一点評価の policy 天井・加速度領域の速度ループ・Stop&Go 保持）、SpeedLimiter（§6、
> スロットル片側クランプ）、ACC/MSL 排他（§6）、DriverOverride の新 producer 2つ
> （§8-3 の brake 理由と ACC の accel 一時上書き）、gt.acc / gt.msl の HVD 行（§8-2）。
> フェーズC実測: C++ ユニット **731/731 緑**（フェーズC新規58）、`manualdrive_adas_batch`
> は **20 シナリオ・51 matcher が pass=51/fail=0/skip=0**、matcher 単体（Python）77 緑、
> 既存回帰ベースライン5本（計31シナリオ）は deviations=0 で不動、ODR 適合 quick 緑。
> **§4-3/§12 の残確認事項（TrafficLightAware / StopYieldSignAware のコントローラ非依存性）は
> 実走で確認済み**（§12 参照）。**§12 が要求していたクリープ×停止保持の実測も完了し、
> 閾値2つを実測から確定した**（同）。
> **フェーズD（LKA/LDW）〜E（常設化）は未実装（設計のみ）**。
> 検証資産は別文書（manualdrive_adas_verification_plan.md）が扱う。
> 知識グラフ: `req-vd-ad:REQ-AD-025`（手動AEB）/ `REQ-AD-026`（手動ACC）/ `REQ-AD-027`（手動LKA）/
> `REQ-AD-028`（面3観測性）/ `REQ-AD-029`（HMI提示）/ `REQ-AD-030`（速度リミッター）/ `REQ-AD-031`（Stop&Go）、
> `vd-func:FUNC-075`（AEB並行監視と横断基盤）/ `FUNC-079`（手動ACC）/ `FUNC-080`（手動LKA）/ `FUNC-081`（MSL）。
> `feature:F7`（運転主体切替）とは complements（切替そのもの vs 並行稼働）。
> 要求の受入基準は requirements_vd_ad.yaml の acceptance_ladder が真実源であり、本文書は方式を定める。

## 1. 目的とスコープ

ManualDrive（人間運転、G29 ハンコンまたはキーボード）中は、現状 ADAS が一切稼働しない。
AEB は実装済みで常設ゲートまで持っているのに、`ITrafficPolicy` 群が VirtualDriver コントローラ専属に生成されるため、人間が運転している間は安全層が存在しない。
本設計はこのギャップを塞ぎ、手動運転に AEB（FCW 警報フェーズ込み）、ACC（Stop&Go 段a/b 込み）、LKA（LDW 込み）、MSL（速度リミッター）を重ねる。

スコープ外を先に固定する。

- 運転主体の切替そのもの（`feature:F7` の領分。本設計は「人間が主運転者のまま」の重ね合わせだけを扱う）
- 自動再発進（REQ-AD-031 段c/d。段d は縦ドメイン判断の全部＝ADAS→AD の境界を越える）
- ELK、BSD、TJA、レーンセンタリング常時アシスト（台帳に後続候補として残置済み）
- FFB 反力の実機特性そのもの（G29 実機限定観点として検証計画で区分する）

## 2. アーキテクチャ

### 2-1. 全体構成 — 案A（Coordinator 内フック）

新設の中心は **AdasCoexistenceStack**（`GT_esmini/src/control/manualdrive/` 配下）である。
`ControllerManualDrive` がメンバとして所有し、`ManualDriveCoordinator::RunFrame` から毎フレーム呼ばれる。
内部に `TrafficPolicyManager`（既存）と、後述の調停器（PedalArbitrator）、機能別の状態機械を持つ。

ポリシー本体はゼロ改修で流用する。
`AebSafety` と `LeadVehicleAware` の `Evaluate()` は `TrafficPolicyContext{ego, entities, sim_time}` だけを消費する。
`ego`（`object_`）と `entities`（`entities_`）は upstream `Controller` 基底クラスのメンバで、ManualDrive も継承済みである。

**★2026-08-05 訂正（実装で判明）**: `sim_time` は upstream `Controller` 基底クラスのメンバでは**ない**（`Controller.hpp` に該当メンバが存在しない）。
`ControllerVirtualDriver` はタイムステップ加算で維持する自前の `sim_time_` を持ち（`ControllerVirtualDriver.hpp:283`）、`ControllerManualDrive` も同型の自前 `sim_time_` を新設した（`ControllerManualDrive.hpp:120`、`ManualDriveCoordinator::RunFrame` の加算箇所で毎フレーム進める）。
結論（3値ともゼロ改修で `TrafficPolicyContext` に詰められる）自体は変わらないが、根拠は「継承で無償に得られる」ではなく「ManualDrive 側が自分で維持する責務」である。
**帰結**: `ctx.sim_time` を読む将来のポリシー・新設ロジック（ヒステリシス・デバウンス・タイマを持つもの全般）は、この per-controller の `sim_time_` 加算が毎フレーム正しく実行され続けることに暗黙に依存する。加算箇所を消す・迂回するリファクタは `sim_time` を凍結させ、時刻依存の判定を沈黙して壊す。

VD 専属だったのは「VD コンストラクタが new する」という所有関係と、constraint を経路追従へ折り込む消費側だけである。

消費側は持ち込まない。
VD の 3 層（ManeuverAwareSpeedPlanner、TrajectoryShortPlanner、PIDPurePursuitDriver）は経路追従が前提で、人間が操舵する ManualDrive には過剰である。
代わりに、`AdSteeringEnvelope` が確立した「純ロジック、esmini 非依存、単体テスト可能」の様式で、constraint をペダル指令へ変換する小さな部品を新設する（§3）。

### 2-2. 挿入点 — cmd 確定直後、バス publish の前

`RunFrame` のデータフローは、入力ポーリング → オーバーライド判定 → cmd 組み立て → コマンドバス publish/consume → 出力ゲート → 物理ステップ、の順である（ManualDriveCoordinator.cpp:15-407）。
ADAS の調停は **cmd 組み立て（step 3）の直後、バス publish（step 3-bus、同 :97 付近）の前**に挿入する。

publish の前である理由は split 構成にある。
バスは「所有ドメインの指令を publish し、integrator が非所有ドメインを consume する」契約なので、publish 後に cmd を書き換えると、split 相手が consume する値に ADAS の介入が乗らない。
publish 前に調停すれば、所有チャネルに流れる値が常に ADAS 調停済みになる。

### 2-3. ドメイン所有による二重装備の回避

ADAS 各機能は縦横いずれかのドメインに属する（AEB、ACC、MSL は縦、LKA と LDW は横）。
設計当初は「AdasCoexistenceStack は自分（ManualDrive）がそのドメインの所有者であるときに限り当該ドメインの機能を**評価**する」としていた。

**★2026-08-05 訂正（実装で意図的に変更）**: 実装ではこれを外し、`AdasCoexistenceStack::Step` は所有関係にかかわらず policy（`AebSafety`/`LeadVehicleAware` の `Evaluate()`）を**毎フレーム走らせる**（`AdasCoexistenceStack.cpp:218`）。
`DomainOwnershipLedger::OwnerOf` が効くのは**介入（ペダルへの反映）だけ**で、非所有時は `ComputeManualAdasFrame` が `PassThrough(driver_cmd)` を返して人間の入力をそのまま通す（同 `.cpp:141-145`）。

**理由**: `AebSafety` は3フレームの横方向侵入デバウンス（`dt_history_`）を持つ。評価そのものを所有権で止めると、所有が ManualDrive に戻った直後の数フレームはこの履歴が冷え切っており、AEB が検知すべき遭遇を数フレーム遅れて拾う——所有権受け渡し直後に AEB が効かない窓ができてしまう。これは§12 が既に挙げているリスクと同じ種類のもので、評価を毎フレーム走らせ続けることで履歴を温存し、この窓を消す。

**二重装備が起きないことの確認**: 評価は毎フレーム走るが、非所有時は §8 の HVD 報告行が UNAVAILABLE のまま（介入が無いため detail が空）で、ペダルは人間入力のパススルーに留まる。二重装備（両コントローラが同時に介入する）は起きない——起きているのは「非所有側も内部状態だけ温めておく」ことであり、外部への効果（ペダル・HVD の ACTIVE 遷移）は所有側だけに限定される。

この規則が split 構成の二重装備を消す。
横=手動、縦=VD の構成では、縦は VD がフルスタック（AebSafety 込み）で持っているから、ManualDrive 側の AEB と ACC は（内部評価はしても）介入を出さない。
逆構成（横=VD、縦=手動）では LKA 側が眠る。
機能の availability は所有と連動して STANDBY / UNAVAILABLE に反映する（§8）。

### 2-4. 触るファイルの範囲

コア改修は次に閉じる（概算は要求 note に記載の 400〜700 行＋ハーネス側）。

- 新規: `AdasCoexistenceStack`、`PedalArbitrator`（縦の調停）、`AccLonController`（ACC 速度制御）、`LaneKeepAssist`（レーン中心偏差と補正）、`KickdownDetector`（共有部品）、`AdasWarningChannel`（警報の集約）
- 変更: `ControllerManualDrive`（メンバ追加と `GetADASFunctions` 新設）、`ManualDriveCoordinator`（フック挿入）、`ManualDriveConfig`（`adas` セクション、§9）、`GT_esminiLib.cpp`（報告ディスパッチ、§8）、`AdasFunctionReport`（Name ミラー拡張、§8）、`GT_HostVehicleReporter`（DriverOverride、§8）
- 無改修: `AebSafety`、`LeadVehicleAware`、`TrafficLightAware`、`StopYieldSignAware`、`TrafficPolicyManager`、VD 側の全経路

`GT_esminiLib.cpp` のディスパッチは全コントローラの OSI 出力の集約点なので、ここだけは変更を最小にし、単体テストを厚く当てる（§12）。

## 3. 縦の調停 — PedalArbitrator

### 3-1. 調停の順序

人間のペダル指令に対する縦方向の合成は、生成、制限、安全の 3 段を固定順で通す。

1. **ACC（生成）**: ACC が ACTIVE のとき、`AccLonController` がスロットルとブレーキを生成する（§4-2）。人間のアクセルが閾値を超えていれば人間値を採用し（一時上書き）、ACC のブレーキ生成は抑止する。
2. **MSL（制限）**: スロットルをキャップ値に対応する上限へクランプする（§6）。キックダウン中は素通し。
3. **AEB（安全）**: 発火中はブレーキを `max(現在値, AEB 要求)` に引き上げ、スロットルを 0 にする。キックダウンによる抑制（§3-2）が立っている間はこの段を飛ばす。

安全段を最後に置くのは、生成と制限の結果がどうであれ安全の主張が最終値に残るようにするためである。
ブレーキの合成が max なのは REQ-AD-025 段c の主張そのもの（人間が AEB 要求以上を踏んでいれば上乗せしない）で、人間の強いブレーキを弱める経路は存在しない。

### 3-2. AEB の実車型抑制

方式決定（2026-08-04）により、運転者の明確な加速意思で AEB 介入を抑制する（UN R152 の運転者上書き規定に相当。二次資料経由であり、準拠は主張しない）。
抑制条件はキックダウン検出（§3-3）で、抑制の成立と解除にはヒステリシスを置く（閾値は要校正。検証計画の赤実証資産で両極性を示す）。
抑制事象は DriverOverride と custom_detail に出す（§8）。

FCW（REQ-AD-025 段e）は AEB の前段として同じ policy 出力から作る。
`AebSafety` は作動しなかったフレームでも gap / ttc / a_req を PolicyDetail に出す（W3 実装済み）ので、警報判定は「TTC が警報閾値を下回った」を介入閾値より緩い側に置くだけでよい。
警報リード（介入の 0.8 s 以上前が目安）は閾値差から生まれ、時系列は検証で実測する。

**★2026-08-05 訂正（実測）**: 「閾値差からリードが生まれる」という上記の想定は、cut-in（急な割込み）では成立しないことを実測で確認した。
`md_aeb_unresponsive`（`07_aeb/cutin_hard_brake.xosc` の運転主体差し替え）で `gt.fcw` と `gt.aeb` が**同一フレーム**（t=1.75、frame 34）で ACTIVE になり、リードは **0.000 s** だった（`fcw_leads_intervention` 実測、要求段eの `min_lead_s=0.8` に対し fail。`test_results/mdadas_run1/md_aeb_unresponsive/verdict.json`）。

**原因**: FCW ゲートは `DeriveFcwGateConfig` で `ttc_threshold` / `min_a_req` だけを緩め、候補**選定**パラメータ（lookahead・lateral_tol・stop_margin）は AEB 介入ゲートと verbatim で共有している（`AdasCoexistenceStack.cpp:112-123` のコメント）。
両ゲートとも、`AebSafety` 自身が持つ3フレームの横方向侵入デバウンス（`dt_history_`）が cut-in の候補を「侵入」と認めるまでは、警報側も介入側もそもそも候補自体が見えない。
閾値差が動かせるのは「見えている候補にどちらが先に反応するか」だけであり、**候補自体が見えていない間は閾値差が何も買わない**。cut-in はデバウンスが解けた瞬間に両ゲートの閾値をほぼ同時に跨ぐため、リードが潰れる。

**帰結**: REQ-AD-025 段e の ≥0.8 s リードは、候補が数フレームかけて連続的に閾値へ近づく遭遇（同一車線上の先行車・停止車への接近など）でのみ成立し、cut-in のような突発的な候補出現では成立しない。この制約は§12 のリスク一覧にも追記した。

### 3-3. KickdownDetector（共有部品）

アクセル全踏み相当の検出は、AEB 抑制（REQ-AD-025 段d）と MSL 一時解除（REQ-AD-030 段b）が同じ意味論を使う。
別々に実装すると閾値が食い違い、「AEB は抑制されたのに MSL は解除されない」という説明不能な状態を作るため、閾値とデバウンスを持つ単一の部品にする。
純ロジックで単体テスト可能にする（AdSteeringEnvelope と同じ様式）。

### 3-4. 要求減速度からブレーキ量への変換

STOP_AT_S 系 constraint から得た要求減速度をブレーキ踏量へ写す変換は、開ループの静的マップにしない。
物理バックエンドは差し替え可能（RealVehicleBackend / NetworkPhysicsBridge）で、ブレーキ特性への静的マップはバックエンドを替えた瞬間に狂うからである。
実測減速度と要求減速度の差を閉ループ（PI）で詰め、初期値だけフィードフォワードで与える。
飽和は全ブレーキ。

## 4. ACC

### 4-1. 状態機械と操作系

状態は OSI の 3 値規律に写す前提で設計する（OFF は UNAVAILABLE、待機は STANDBY、制御中は ACTIVE。§8-2）。

- **OFF → STANDBY**: on/off 操作（ボタンまたは config）。
- **STANDBY → ACTIVE**: set 操作。set 時の自車速度を設定速度の初期値にする（実車標準）。
- **ACTIVE → STANDBY**: ブレーキ踏下（解除）、または利用可能速度域からの逸脱（REQ-AD-026 段f）。resume 操作で直前の設定速度へ復帰する。
- **一時上書き**: ACTIVE のままアクセル踏下で人間値を採用し、離すと追従へ戻る（状態遷移ではない。§3-1 の生成段の分岐）。

操作の割当は `manual_drive.json` の既存ボタンマッピング流儀（`auto_resume_button` などと同じ、-1 = 未割当）に乗せる。
新設キーは on/off、set/resume、設定速度の増減、車間段階の切替の 5 つ（§9）。
キーボード入力でも同じ経路を通るため、操作系の検証に実機は要らない。

設定速度の増減はステップ幅（既定 5 km/h 相当、config）で行い、車間は段階式 THW（既定 3 段階、config 配列）を循環切替する。
設定値と実効値は毎フレーム custom_detail に出す（「設定した」と「効いた」の分離。REQ-AD-026 acceptance）。

### 4-2. 速度制御（生成側）

`AccLonController` は次の合成で目標速度を決め、速度 PID でペダルに落とす。

```
v_target = min( set_speed,
                policy ceiling（後述）,
                speed_limit           # respect_speed_limit 有効時のみ
              )
```

policy ceiling は、`TrafficPolicyManager` が返した constraint 列を現在位置で評価した速度上限である。
MAX_SPEED はそのまま、STOP_AT_S と MAX_SPEED_TO_S は停止点までの残距離から運動学的に許容速度を引く（`sqrt` の 1 行）。
VD の 3 層プランナのようなプロファイル生成はせず、現在位置の 1 点評価に留める。
これで足りるのは、ACC が経路を追わず「今の上限」だけを守ればよいからである。

車間の維持は `LeadVehicleAware`（IDM 系）の constraint がそのまま担う。
THW 段階の切替は `LeadVehicleAwareConfig` の該当パラメータを段階値で差し替えることで実現する（policy 本体は無改修）。

速度 PID は PIDPurePursuitDriver の縦ブロック（v_target と現在速度だけを読む約 30 行）を参考に、`AccLonController` 内へ独立実装する。
加減速の上限は ACC 自身の config に持つ（`accel_max`、`decel_max`）。
VD の `comfort_decel` は流用しない。
あれは「VD 自身の減速の滑らかさ」の値であり、人間が主運転者の場面へ適用する意味的根拠がないからである（要求 note の再定義方針）。

### 4-3. Stop&Go（REQ-AD-031 段a/b）

`LeadVehicleAware` の constraint を v=0 まで許し、停止後は**停止保持**に入る。
停止保持はブレーキ保持指令で実現する。
RealVehicle は AT のクリープを持つため、保持ブレーキを抜くと勝手に前進する。クリープ挙動との整合（保持量、解除時の飛び出し）はフェーズC 実装時の要確認事項である（§12）。

再発進は人間のアクセル操作を唯一のトリガとする（再発進判断はしない。REQ-AD-031 段a の定義）。
アクセルが閾値を超えたら保持を解除し、追従制御へ戻る。

段b（信号と一時停止を停止対象に含める）は、`TrafficLightAware` と `StopYieldSignAware` を config（`stop_targets`）に応じて手動スタックへ Add するだけである。
停止線基準の停止目標は VD 側（REQ-AD-003/004）と同じ実装が働く。
ただし、両 policy がコントローラ非依存であることは `ITrafficPolicy` 契約と AebSafety / LeadVehicleAware の署名確認から推定した段階であり、ManualDrive 文脈での実走はフェーズC で最初に確認する。

### 4-4. 自動再発進との境界

REQ-AD-031 段c（時限）と段d（発進判断つき）は本設計のスコープ外だが、境界だけ固定しておく。
段d は縦ドメインの判断を全部持つことになり、既存の lat/lon split（縦=VD）との違いは「ブレーキで解除する ACC 意味論か、AUTO_RESUME で戻る F7 意味論か」に縮退する。
将来段d を実装する場合は、新しい制御則を書くのではなく、split 構成に ACC 型の解除・復帰意味論を被せる方向を第一候補とする。

## 5. LKA

### 5-1. レーン中心偏差モジュール（新設の本体）

横方向には流用できる既存部品がない。
PIDPurePursuitDriver はトラジェクトリ preview 点列への追従であり、レーン中心そのものを追わない（2026-08-04 調査確定）。
新設する `LaneKeepAssist` は、`Object::pos_` のレーン相対量（レーン中心からのオフセットと横速度）だけを入力にする。

介入判定は **TLC（time to line crossing）** を主とする。

```
margin = レーン半幅 − |offset| − 車半幅
TLC    = margin / |横速度|          # 逸脱方向に動いているときのみ
```

TLC が閾値を下回るか、オフセットが直接閾値を超えたら逸脱傾向と判定する。

### 5-2. 補正の出力

補正は `cmd.steering` への加算で行い、振幅上限とレート制限を持つ envelope でクランプする（AdSteeringEnvelope と同じ様式の横版）。
振幅上限は「人間が常に上回れる」大きさに置く。
LKA は逸脱をなくす保証ではなく、逸脱傾向を車線内へ押し戻す補助であり、上限を超える補正が要る場面は人間か AEB の領分である。

### 5-3. 人間操舵優先

次のいずれかが立っている間、補正を出さない（出していれば即時中断する）。

- 方向指示器が作動している（意図的な車線変更）
- 人間の操舵入力レートが閾値を超えている（明確な操舵意思）

検出は入力側の単純な閾値で行う。
`OverrideManager` の FFB 残差ラッチは「AD が握るハンドルを人間が奪う」向きの検出器であり、意味論が逆なので流用しない（要求 note の決定）。
実ハンコンのトルク感に基づく検出精度の改善は G29 実機限定の後続観点とする。

### 5-4. LDW（警報のみモード）

`warning_only` 構成では、逸脱判定（§5-1）はそのまま動かし、補正出力（§5-2）だけを止めて警報チャネル（§7）へ流す。
判定と出力を分離しておくのは REQ-AD-027 段f の判定要件（警報は出るが cmd.steering は不変、の両極性）を実装構造で保証するためである。

## 6. MSL（速度リミッター）

`PedalArbitrator` の制限段（§3-1）に置く。
現在速度が設定速度に近づいたらスロットル上限を絞り、超過時は 0 にする（ブレーキは出さない。下り坂で設定速度を超えるのはリミッターの定義どおりであり、REQ-AD-030 段a の負系がこれを固定する）。
キックダウンで一時解除し、離すと復帰する（§3-3 の共有部品）。

`speed_limit_linked` 構成では、キャップ値に設定速度でなく `GetSpeedLimit()` を使う（REQ-AD-026 段g と同じ参照経路）。
ACC と MSL の同時 ON は排他とする（実車の慣例に合わせ、後から ON にした側が先の側を STANDBY に落とす）。

## 7. 警報チャネル — AdasWarningChannel

FCW（§3-2）と LDW（§5-4）は同じ提示経路を通す。
`AdasWarningChannel` は機能別の警報フラグを集約し、次の 3 面に配る。

1. **HVD**: 各機能の custom_detail（`gt.aeb.warning` など）。面3 の判定はこれを読む（一次証拠）。
2. **UI**: HVD を既に購読している Web フロント（osi_bridge 経由）が描画する。REQ-AD-029 段b の「UI 必須」はこの経路で満たし、表示専用の新経路は作らない（同 段c）。
3. **FFB パルスと音**: 拡張オプション。フェーズ実装では口だけ切り、既定 OFF。

## 8. OSI / HVD 報告と DriverOverride

### 8-1. 報告経路 — AddADASFunctionEx 直結

24 枠固定の `GetADASStates` 経路は使わない。
custom_detail が乗らず（GT_esminiLib.cpp:1565 で常に空）、State 導出の規律も呼び出し元任せになるからである。
代わりに VD と同型の経路を通す: `ControllerManualDrive::GetADASFunctions()` を新設し、`GT_esminiLib.cpp` の ManualDrive 分岐（:1580 付近）から `AddADASFunctionEx` へ流す。

`AdasFunctionReport.hpp` には手動スタック用の純関数 `BuildManualAdasFunctionReport(flags, snapshot, 状態機械の出力)` を追加する。
VD 用の集計行（`gt.virtual_driver` / NAME_URBAN_DRIVING）は手動文脈では意味的に誤りなので、手動レポートには集計行を置かない。

### 8-2. Name / State の割当

必要な機能はすべて OSI 3.7.0 の正規 Name 列挙に実在する（NAME_OTHER は使わない）。

| 機能 | Name（値） |
| :--- | :--- |
| FCW | FORWARD_COLLISION_WARNING (3) |
| LDW | LANE_DEPARTURE_WARNING (4) |
| AEB | AUTOMATIC_EMERGENCY_BRAKING (7) |
| ACC | ADAPTIVE_CRUISE_CONTROL (10) |
| LKA | LANE_KEEPING_ASSIST (11) |
| MSL | SPEED_LIMIT_CONTROL (25) |

`osi_adas::Name` ミラー（AdasFunctionReport.hpp）に 4 値を追加し、GT_esminiLib.cpp の static_assert ピン留めも同数増やす。
State は既存 3 値規律を踏襲する: config OFF またはドメイン非所有 = UNAVAILABLE、有効だが介入なし = STANDBY、介入または制御中 = ACTIVE。
「切ってあった」と「見張っていて撃たなかった」の区別は手動文脈でも保つ。

### 8-3. DriverOverride の populate

`GT_HostVehicleReporter` に機能行単位のセッターを新設し、`AddADASFunctionEx` の拡張引数で `driver_override{active, reasons}` を受ける。
理由の写像は次で固定する。

- ブレーキ起因（ACC 解除）→ `REASON_BRAKE_PEDAL`
- 操舵起因（LKA 中断）→ `REASON_STEERING_INPUT`
- アクセル起因（AEB 抑制、MSL 解除、ACC 一時上書き）→ Reason 列挙に該当値が**ない**（規格制約、2 値のみ）。`custom_state` に `DRIVER_OVERRIDE_ACCEL` を置いて補完する

**★2026-08-05 フェーズB 実装（上記の方式は維持、以下は確定した細部）**

「機能行単位のセッター」と「`AddADASFunctionEx` の拡張引数」は**同じ 1 つの機構**として実装した。
`AddADASFunctionEx` は行を `custom_name` で識別して state/detail を書き換える既存の口であり、上書き欄も同じ行の同じ呼び出しで書ける。
別に `SetADASFunctionDriverOverride(custom_name, ...)` を生やすと、同じスロットへの書き口が 2 つになり、しかも呼び出し元は片方しか持たない。
拡張引数は**既定値付き**にしてある——これが「他コントローラの HVD 出力が不変」を型で保証する:
既存の呼び出し（RealDriver 24 スロット、VirtualDriver 行）は引数を渡さず、既定値は「言うことが無い」を意味し、直列化時に submessage も `custom_state` も**書かれない**ので、直列化バイトが変わらない。

**`reported` フラグ（3 値ではなく 2 値 + 有無）**。
`AdasDriverOverride` は `active` の他に `reported` を持つ。
`reported=false` なら submessage を書かない、`reported=true, active=false` なら**明示的に** `active=false` を書く（OSI の `optional bool` なので明示 false はワイヤ上に存在する）。
これは「評価して上書き無しと測った」と「誰も見ていない」を面3が区別するためで、区別しないと **populate 機構ごと消した実行で負の matcher が緑になる**。
書くのはゲートが開いている行（config 有効かつドメイン所有＝State が UNAVAILABLE でない行）だけ——動いていなかった機能は上書きされようがない。
これは §8-2 の STANDBY / UNAVAILABLE 規律を上書きチャネルへそのまま持ち込んだもので、`detail` を bypass 時に空のままにする §2-3 の扱いとも同じ形である。

**アクセル起因の producer 条件は `kickdown_effective`**（キックダウンがラッチしており、かつ抑制が config で有効）であって、「この フレームで実際に AEB 要求を握り潰した」（`PedalArbitrationSnapshot::aeb_suppressed`）**ではない**。
OSI の DriverOverride が問うのは「その**機能**が運転者に上書きされたか」であり、AEB は前方に対象が居るかどうかにかかわらず、キックダウンが続く限り現に介入できない＝上書きされている。
狭い方（このフレームで実際に握り潰した）を採ると、上書き欄が交通状況に合わせて点滅し、運転者の入力を追わなくなる。
狭い事実は `gt.aeb.suppressed` の custom_detail として引き続き観測でき、どちらも失われない。

**FCW 行には上書きを立てない**。
キックダウンが抑制するのは介入であって警報ではない（危険へ向けて加速している最中にこそ警報は残る必要がある）。
副次的な利点として、同一フレーム・同一実行の中に「上書き有りの行（gt.aeb）」と「上書き無しの行（gt.fcw）」が同時に存在するので、負の対照を別シナリオを増やさずに取れる。

### 8-4. custom_detail キー

`gt.<機能>.<量>_<SI単位>` 規約（PolicyDetail.hpp）に従い、少なくとも次を出す。

| キー | 内容 |
| :--- | :--- |
| gt.aeb.warning | FCW 警報フラグ（既存 gt.aeb.ttc_s 等に追加） |
| gt.acc.set_speed_mps / gt.acc.effective_cap_mps | 設定速度と実効上限（制限速度キャップ後） |
| gt.acc.thw_setting_s / gt.acc.thw_actual_s | 車間の設定段階と実測値 |
| gt.lka.offset_m / gt.lka.tlc_s / gt.lka.warning / gt.lka.correction | 偏差、TLC、警報、補正量 |
| gt.msl.cap_mps / gt.msl.kickdown | キャップ値と一時解除フラグ |

状態遷移イベント（P17/P18 が切替基準点に使う）は、毎フレームの State 列と custom_detail のストリームから導出できる形にする。
イベント専用のチャネルは新設しない（ストリームがあれば遷移はフレーム差分で一意に取れるため）。

### 8-5. 検証用の in-process HVD アクセス

面3 の観測は HVD 直読が方式決定だが、検証ハーネスは in-process 実行（GT_esminiLib.dll 直結）であり、UDP 48199 の受信はハーネスの作りに合わない。
既存の OSI GroundTruth in-process 取得と同じ様式で、シリアライズ済み HostVehicleData を返す C API（`GT_GetOSIHostVehicleData` 相当）を GT_esminiLib に 1 本追加する。
これが REQ-AD-028 段c の C++ 側の実体である（ハーネス側の受けは検証計画が扱う）。

## 9. config

`manual_drive.json` に `adas` セクションを新設する。骨子:

```jsonc
"adas": {
  "aeb": {
    "enabled": false,                  // 既定 OFF（全機能共通の導入方針）
    "kickdown_suppress_enabled": true, // 実車型上書き（方式決定）
    // ttc_threshold 等は AebSafetyConfig を共有。警報閾値のみ追加
    // FCW ゲートは次の2値**両方**のクランプで決まる（AdasCoexistenceStack.cpp
    // DeriveFcwGateConfig）。片方だけ振っても、もう片方が支配的な遭遇では
    // 警報点が動かない — 校正時は必ずペアで扱う。
    "warning_ttc_threshold_s": 0.0,    // 要校正（AebSafetyConfig の 2.5 より緩い側＝大きい値）
    "warning_min_a_req_mps2": 2.0      // 要校正（同 3.0 より緩い側＝小さい値）。
                                       // ★2026-08-05 フェーズBで新設。それまで
                                       // compile-in default のみだった（本節末尾の note）
  },
  "acc": {
    "enabled": false,
    "set_speed_step_mps": 1.39,        // 5 km/h 相当
    "thw_stages_s": [1.0, 1.6, 2.2],   // 要校正
    "min_speed_mps": 0.0,              // Stop&Go 込みなので下限 0
    "max_speed_mps": 0.0,              // 0 = 無制限（要校正）
    "respect_speed_limit": false,
    "accel_max_mps2": 0.0,             // 要校正。VD comfort_decel は流用しない
    "decel_max_mps2": 0.0,
    "stop_and_go": {
      "enabled": true,
      "stop_targets": ["lead"],        // "traffic_light" / "stop_sign" を追加可（段b）
      "restart_accel_threshold": 0.0   // 要校正
    }
  },
  "lka": {
    "enabled": false,
    "warning_only": false,             // true = LDW のみ
    "min_speed_mps": 0.0,              // 要校正（実車は 60 km/h 前後が参考値）
    "max_speed_mps": 0.0,
    "tlc_threshold_s": 0.0,            // 要校正
    "correction_max": 0.0              // 要校正（人間が常に上回れる上限）
  },
  "msl": {
    "enabled": false,
    "speed_limit_linked": false
  },
  "kickdown_threshold": 0.95           // AEB 抑制と MSL 解除の共有（§3-3）
},
"buttons": {
  // 既存キーに追加。-1 = 未割当
  "acc_toggle_button": -1,
  "acc_set_resume_button": -1,
  "acc_speed_up_button": -1,
  "acc_speed_down_button": -1,
  "acc_thw_cycle_button": -1,
  "lka_toggle_button": -1,
  "msl_toggle_button": -1
}
```

全機能とも既定 OFF で入れる（F6 AutoLight、lane_change_initiation と同じ導入方針。既定挙動を変えず、回帰ベースラインを不動で通す）。
利用可能速度域のキー語彙（min/max_speed_mps）は ACC と LKA で共通にする（REQ-AD-026/027 の共通語彙決定）。

**★2026-08-05 追加 → 同日フェーズBで解消（記録は残す）: `warning_min_a_req_mps2` が config に露出していなかった件**。
フェーズBで on-disk キー `adas_aeb_warning_min_a_req_mps2` を新設し、`ManualDriveConfig` → `ManualAdasStackConfig` まで結線した（ユニットテスト `test_ManualDriveAdasConfig.cpp` の `BothFcwGateThresholdsAreIndependentlySettable` / `WarningMinAReqAloneDoesNotDisturbWarningTtc` で両方が独立に効くことを固定）。以下は当時の記録であり、なぜこれがただの「値の間違い」ではなく**校正の詰み**だったかの説明として残す。
FCW の発火点は `DeriveFcwGateConfig` が `warning_ttc_threshold_s` と `warning_min_a_req_mps2` の**両方**をクランプして決める（`AdasCoexistenceStack.cpp:119-120`）。
このうち `warning_ttc_threshold_s` は `config_.adas.aeb.warning_ttc_threshold_s`（`adas_aeb_warning_ttc_threshold_s`、`ManualDriveConfig.cpp:173`）として config から読めるが、対になる `warning_min_a_req_mps2`（既定 2.0 m/s²、`AdasCoexistenceStack.hpp:199`）は **`AdasCoexistenceStack.hpp` のコンパイル時デフォルトのみで、対応する config キー・パーサが無い**。
FCW の発火点は2値のペアで決まる以上、片方だけが config から効くのは校正時に噛み合わない——`warning_ttc_threshold_s` をどう振っても `warning_min_a_req_mps2` が支配的な側では FCW の発火点が動かない、という校正の詰み方をする。
本設計書は §9 に `warning_ttc_threshold_s` しか載せておらず、これも欠落を見落としていた。
~~**対応は次フェーズの実装課題として記録するのみに留める**~~（→ フェーズBで実施済み。上の追記を参照）。

## 10. 実装フェーズと完了条件

各フェーズは検証スパインの 1 列を①〜⑥まで縫って閉じる。
完了条件の「対応する負 matcher が緑」は駐車設計と同じ規律で、matcher と赤実証資産の対応は検証計画が定める。

| フェーズ | 内容 | 完了条件 |
| :--- | :--- | :--- |
| **A** | AEB 列: AdasCoexistenceStack と PedalArbitrator の新設、AebSafety 配線、FCW 警報、HVD 報告経路（§8-1/8-2/8-5）、ハーネスの ManualDrive 対応 | AEB 正負バッチ緑（合成入力。無反応ドライバで介入、衝突コース不在で非介入、強ブレーキ非上乗せ、キックダウン抑制） |
| **B**（済 2026-08-05） | 観測列: DriverOverride populate（§8-3）、custom_state、状態機械の 3 値規律、REQ-AD-028 の matcher、＋フェーズA残債の `warning_min_a_req_mps2` config 化（§9） | 上書き検出の正負 matcher 緑 → **達成**（正=`md_aeb_kickdown_suppress` のキックダウン窓、負=`md_aeb_unresponsive`、対照=同 run の `gt.fcw` 行）。3値規律は同一 xosc を `adas_aeb_enabled` の true/false 2構成で回す対（バッチの `variant` キー）で示す。**ただし段b claim のうち brake/steer 経路は producer が C/D にしか無いため未実証** |
| **C**（済 2026-08-05） | ACC 列: AccLonController、操作系（ボタン、set/resume、速度と THW の走行中変更）、速度域ゲート、制限速度キャップ、Stop&Go 段a/b、MSL（§6。ACC と部品を共有するため同フェーズ） | 追従、解除と復帰、設定変更反映、停止と人間再発進、構成両極性のバッチ緑 → **達成**（20/20 シナリオ・51 matcher 緑、正負同居。両極性は同一 xosc の `variant` 2構成＝制限速度キャップ有無・停止対象構成。ACC作動中の AEB 独立発火も `md_acc_aeb_independence` で実証） |
| **D** | LKA 列: LaneKeepAssist 新設、TLC 判定、人間操舵優先、LDW、警報チャネルの UI 配線（REQ-AD-029） | 補正と非介入の正負バッチ緑 |
| **E** | 常設化: `manualdrive_adas_batch` の回帰ゲート組み込み（非ブロッキング開始 → 昇格は AEB 前例）、CI 相乗り、HvdGaugePanel の目視確認、docs | ゲート常設とベースライン commit |

フェーズ A に検証ハーネスの ManualDrive 対応を含めるのは、これが無いと A の完了条件自体が判定できないためである（gt_sim_test は現状 VD テレメトリ必須で、ManualDrive 走行は frames=0 で即エラーになる）。

## 11. 検証計画への接続点

検証計画（別文書）に委ねる項目を列挙しておく。

- 観点 slug のカタログと、matcher ごとの赤実証資産の対応
- 合成入力プロファイル（HeadlessFfbInput の frozen / follower モード割当、アクセルパルスの与え方）
- 自動と G29 実機限定の区分（LKA の FFB 反力、AEB 警報の体感は実機側）
- 判定手法: -NoTouchParity の「自己決定論性コントロール → 決定フィールド厳密一致」の踏襲箇所
- ハーネス改修の仕様（ManualDrive 実行モード、policy 注入の ManualDrive 版、§8-5 API の受け）

## 12. 既知のリスクと相互作用

**★2026-08-05 追加 → 同日フェーズBで解消: `warning_min_a_req_mps2` が config に露出していなかった**（§9 の追記を参照）。
FCW の発火点は `warning_ttc_threshold_s` と `warning_min_a_req_mps2` の両方のクランプで決まるが、
フェーズA時点では後者に config キーが無かった。ペアの片方しか校正できないため、
`warning_min_a_req_mps2` が支配的な条件では `warning_ttc_threshold_s` をいくら振っても FCW の
発火点（延いては段eのリード）が動かない、という校正の詰みが起こる。
フェーズBで `adas_aeb_warning_min_a_req_mps2` を新設して解消（ユニットテスト付き）。
**教訓として残す**: 「2値の合議で決まる判定点の、片方だけを config に出す」は、値が間違って
いるのではなく**校正という行為自体が不能になる**種類の欠陥で、閾値を振っても何も起きないという
症状でしか気づけない。判定点がペアで決まるなら露出もペアで行う。

**★2026-08-05 追加: FCW/AEB 候補選定の共有が cut-in でリードを潰す**（§3-2 訂正の要約）。
FCW ゲートと AEB 介入ゲートは候補**選定**パラメータ（lookahead・lateral_tol・stop_margin）を共有しており、両者とも `AebSafety` の3フレーム侵入デバウンスが候補を認識するまで発火できない。
cut-in のように候補が突発的に出現する遭遇では、デバウンスが解けた瞬間に両ゲートの閾値をほぼ同時に跨ぐためリードが潰れる（実測: リード0.000s、`md_aeb_unresponsive`）。
段eの≥0.8sリードは、候補が徐々に閾値へ近づく遭遇（同一車線上の先行車・停止車接近）でのみ成立する。この制約自体を仕様として認めるか、候補選定パラメータをFCW側だけ緩めて先出しできるようにする改修が必要かは、フェーズB以降の判断課題として残る。

**RealVehicleBackend の HVD ハンドル角欄**。
RealVehicleBackend.cpp:133-134 は内部 `current_hvd_` のハンドル角欄にタイヤ角を書いており、FFB / Coordinator / VD が一貫してタイヤ角として読むことで内部整合が取れている。
本設計の作業中に「OSI 意味論に合わせて」これを直すと、ManualDrive→Reporter 経路で 12.9 倍の二重変換が発生する（既知トラップ）。触らない。

**GT_esminiLib ディスパッチの集約点**。
:1570-1636 の else-if 連鎖は 4 コントローラの OSI 出力すべてが通る。
ManualDrive 分岐への追加は、他コントローラの HVD 出力に対する回帰テスト（既存 RealDriver 24 行の実 Name 列挙が不変であること等）とセットで行う。

**クリープと停止保持 → ★2026-08-05 フェーズC冒頭で実測済み**。
`hold_brake = 0.30` で 18.7 秒保持して変位 0.015 m（クリープ完全抑止）、解除時の飛び出しは
観測されず（パルス throttle 0.35 に対する素直な 2.43 m/s の加速のみ）。勾配での後退は測っていない
——RealVehicle は道路ピッチを姿勢にしか使わないため、勾配は縦運動に入らない（下の項を参照）。

**この実測が見つけた本当の問題は保持ブレーキ量ではなかった**。最初の実行では停止保持が
640 フレーム中 **0 フレーム**しか成立しなかった。原因は `stop_speed_eps_mps` の当初値 0.10 m/s が
**AT クリープの床（実測 0.16 m/s）より下**だったこと——ブレーキを当てない限り車両は 0 に収束せず、
判定の入口に**原理的に**到達しない。厄介なのは上流が全部正しく見えることで、ACC は ACTIVE、
実効上限は 0 まで減衰、車両は目視で止まっており、それでも `gt.acc.stop_hold` は永久に false のまま。
既定値を 0.5 m/s へ引き上げて解消した。
**恒久ルール**: この閾値を再校正するときは先にクリープ床を測ること。床より小さい値は Stop&Go を
丸ごと死んだコードにする。

**勾配は縦運動に入らない（設計の前提として明記）**。`RealVehicle` の `terrain_pitch_` は姿勢
（`GetCombinedAttitude`）にしか使われず、縦方向の加速度には一切寄与しない。したがって「下り坂で
設定速度を超える」現象はこの物理モデルでは起こせない。検証計画が MSL の負系（ブレーキを出さない）
に当てようとしていた下り勾配資産（MDA-XODR-02）は**作らなかった**——起こせない現象の名前を持つ
資産は、実行できて緑になり、何も測らない。同じ主張は平坦路でキャップが実際に効いている最中に
取っている（`md_msl_throttle_cap`）。

**★2026-08-05 追加: 「快適の上限」は加速度領域で掛けないと config が飾りになる**。
ACC の速度ループは当初、速度誤差から**直接ペダル**を作っていた。この形では誤差が大きい瞬間に
指令が飽和し、`decel_max_mps2` は何も制限しない——値を変えても挙動が変わらない config になる。
症状は「AEB が一度も撃たない」という形で出た: `md_acc_aeb_independence` で先行車が 8 m/s² で
急制動しても、2.0 m/s² の budget を持つはずの ACC がフルブレーキで吸収し切り、安全段に到達
しないままシナリオが緑になっていた（REQ-AD-026 段d を主張しながら段d の状況を一度も作れていない）。
ループを加速度領域へ移し、envelope で clamp してからペダル参照値（`full_brake_decel_mps2` /
`full_throttle_accel_mps2`）で割る形に変えて解消した。
**一般化**: 「上限」を指令の**単位が違う場所**で掛けると、飽和が上限を素通りする。上限は
それが定義されている次元で掛ける。

**comfort_decel の意味論**。
ACC の加減速上限は ACC 自身の config に持ち、VD の `comfort_decel` を参照しない（§4-2）。
参照すると「VD の減速の滑らかさ」を人間運転の場面に流用することになり、値の根拠が失われる。

**split 構成との相互作用**。
§2-3 の所有規則で二重装備は避けるが、「split 中に所有が動的に移る」ケース（F7 の切替と ADAS の availability 遷移が同フレームに重なる場合）の順序は、フェーズA の単体テストで固定する。

**ポリシーのコントローラ非依存性の残確認 → ★2026-08-05 フェーズCで実走確認済み（癒着なし）**。
`StopYieldSignAware` は ManualDrive の ego に対して STOP_AT_S を出す（実測 201 フレーム、
`md_sng_stop_sign`）。癒着は見つからず、snapshot 構築側の補完は不要だった。
**未確認だったのが何だったかを明記しておく**: 争点は「`ITrafficPolicy` の契約に適合するか」では
なかった。両 policy は `RouteSignalScan::ScanSignalsAhead` で **ego の Position から前方を歩いて**
標識を探すが、その歩行は `pos.CopyRoute(ego->pos_)` で ego の Route を複製するところから始まる。
ManualDrive の ego は Route を**持たない**——だから問いは「Route が無いとき、その歩行が何か
見つけるのか」だった。`MoveAlongS(..., HEADING_DIRECTION, true)` が Route 不在では straight-most で
進むため直線路では到達する、というのが答えである。
~~**射程は限定する**~~ → **★2026-08-05（フェーズD着手時）に射程を埋めた**。
フェーズC時点で確認できていたのは StopYieldSignAware × 直線路 1 本だけで、`TrafficLightAware`
本体と分岐路（Route があれば経路側を選ぶが、無ければ straight-most で「たまたま」選ばれた
接続路を見る）が残っていた。`md_sng_traffic_light`（`fabriksgatan_traffic_lights.xodr`、
赤固定、**RoutingAction を意図的に持たせない**）で両方を同時に実走した。
road 3 は junction 4 へ接続路 11 / 12 / 13 の**3本**で入るので、歩行は実際に選ぶ必要がある。
実測: `gt.acc.stop_requested` が 600 フレーム中 574、停止保持 299 フレーム連続で変位 0.024 m、
t=18–25 s の速度は 0.00。
**恒久事実**: 経路を持たない ego に対する `RouteSignalScan` の straight-most フォールバックは、
直線路だけでなく**分岐路でも**目的の標識・信号へ到達する。policy 本体（AebSafety /
StopYieldSignAware / TrafficLightAware の 3 つ）はいずれもコントローラ非依存であることが
実走で確定した。
一次記録: `GT_esmini/docs/virtualdriver/measurements/manualdrive_creep_stop_hold_2026-08-05.md`
（フェーズC分）、`test_results/mdadas_warmup`（本件）。
