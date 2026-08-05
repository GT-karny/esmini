# 手動運転中 ADAS 検証計画

> ステータス: **フェーズA（AEB列、§2-1・§4-2の該当5 matcher）は実装・実行済み**（2026-08-04〜05:
> §7 のハーネス改修一式、`manualdrive_adas_batch.yaml` の5シナリオが実行可能・440フレームずつ
> 生成、赤実証は当初計画のE2E設定極性反転ではなく Python 両極性ユニットテストへ差し替え済み
> — 詳細は §4-2 の訂正注記）。
> **フェーズB（観測列、2026-08-05）実行済み**: §2-2 の6 slug のうち
> `md-driver-override-accel-custom-state` と `md-state-three-value-discipline` を
> `manualdrive_adas_batch.yaml` に行として追加し、`driver_override_reported` を新設して
> 正負とも緑（実測は §4-2 の該当行）。同バッチは 7 シナリオ・16 matcher が
> pass=16/fail=0/skip=0。**残る4 slug のうち `md-driver-override-brake-reason` /
> `md-driver-override-steering-reason` は producer が ACC（フェーズC）/LKA（フェーズD）に
> しか無いため本フェーズでは実証不能**、`md-adas-native-name-enum` / `md-harness-hvd-readback`
> はフェーズAで達成済み（REQ-AD-028 段a/段c）。
> **フェーズC（ACC・Stop&Go・MSL列、2026-08-05）実行済み**: §2-3 の 21 slug のうち 17 を
> `manualdrive_adas_batch.yaml` の 13 行（7 → 20 シナリオ）として実装し、新 matcher 6 件
> （`adas_state_sequence` / `setting_reflected` / `speed_capped_at` / `no_brake_output` /
> `stop_hold_stationary` / `restart_after_trigger`）を新設して **20/20 シナリオ・51 matcher が
> pass=51/fail=0/skip=0**。§3-1 の MDA-XODR-01（制限速度が途中で変わる直線）は
> `straight_500m_speed_limit_step.xodr` として作成し ODR 適合 quick 緑。
> **MDA-XODR-02（下り勾配）は作らなかった**——理由は §3-1 の追記を参照。
> §2-2 の `md-driver-override-brake-reason` はフェーズCで**実証可能になり、実証した**
> （ACC のブレーキ解除が REASON_BRAKE_PEDAL の初の producer）。
> ~~未実装のまま残る §2-3 の 4 slug~~ → **★2026-08-05（フェーズD着手時のウォームアップ）で
> 2 slug を実装・実行した**: `md-sng-traffic-light-stop`（`md_sng_traffic_light`、赤固定の
> fabriksgatan 交差点。実測: `gt.acc.stop_requested` 574/600 フレーム、停止保持 299 フレーム
> 連続・変位 0.024 m。**この1本が設計§12の残射程＝TrafficLightAware 本体と分岐路での
> CopyRoute 起点歩行を同時に閉じた**）と `md-msl-speed-limit-linked`
> （`md_msl_speed_limit_linked` を `variant` で2構成。実測: `gt.msl.cap_mps` が 13.889
> 対 7.431、到達速度 13.77 対 7.31）。どちらも実装はフェーズCで入っており、欠けていたのは
> 刺激だけだった。残るのは `md-acc-cruise-no-lead` と `md-kickdown-shared-consistency`
> （後者は `md_msl_kickdown` で gt.aeb / gt.msl 両行を同一窓で見る形で**実質的に実証済み**
> だが、専用 slug としては起こしていない）。
> **フェーズD（LKA・LDW列、2026-08-05）実行済み**: §2-4 の 10 slug のうち 6 を
> `manualdrive_adas_batch.yaml` の 7 行（23 → 30 シナリオ）として実装し、新 matcher 2 件
> （`lane_kept_within` / `steer_output_absent`）を新設して **30/30 シナリオ・82 matcher が
> pass=82/fail=0/skip=0**。§2-2 の `md-driver-override-steering-reason` もフェーズDで
> **実証可能になり、実証した**（LKA の中断が REASON_STEERING_INPUT の初の producer）——
> これで REQ-AD-028 段b の3経路が全部そろい met へ昇格した。
> 残る §2-4 の 4 slug は `md-lka-ffb-feedback`（**G29 実機限定**、§6 の区分どおり自動ゲート
> 対象外）と HMI の3件（`md-hmi-state-display` / `md-hmi-warning-presentation` /
> `md-hmi-no-new-channel`）で、後者はいずれもフロント側の領分＝フェーズE の目視確認と
> 同じ作業なのでそちらへ寄せた（emit 側 = gt.lka / gt.ldw の HVD 行は本フェーズで揃っている）。
> **§3-4の常設化（フェーズE）は未実装（計画のみ）**。方式は
> [manualdrive_adas_design.md](manualdrive_adas_design.md) が真実源であり、本文書はその §10 の
> フェーズ完了条件（対応する負 matcher が緑）を満たすために何を作るかを定める。
> 知識グラフ: `req-vd-ad:REQ-AD-025`〜`REQ-AD-031`、`vd-func:FUNC-075`/`FUNC-079`/`FUNC-080`/`FUNC-081`。
> 受入基準の数値は requirements_vd_ad.yaml の acceptance_ladder が真実源。

## 1. 目的とスコープ

本文書は次を定める。

- 検証観点のカタログ（§2）
- 観点を発火させる刺激資産（xodr / xosc / 入力プロファイル / バッチ）のマトリクス（§3）
- 各 matcher と、その matcher が実際に赤くなることを示す資産の対応（§4）
- しきい値の根拠を資産側に残す運用（§5）
- 自動化できる範囲、G29 実機が要る範囲、目視で確認する範囲の区分（§6）
- 検証ハーネスの改修仕様（§7。設計書 §11 が本文書へ委譲した 5 点の受け皿）
- 現時点で未設計のまま残す接続点（§8）

手動運転中 ADAS の検証には、既存の VD 検証に無い刺激軸が 1 つ増える。
**人間入力プロファイル**（無反応、定常操舵、強ブレーキ、アクセルパルス、全踏み）である。
シナリオ（xosc）が世界を決め、入力プロファイルが運転者を決め、両方の組で 1 観点が発火する。
この軸の注入手段は §7-4 で定める。

## 2. 検証観点カタログ

観点には slug を付け、matcher、資産、knowledge graph の記述から一貫して参照する。

### 2-1. AEB 列（REQ-AD-025、フェーズA）

| slug | 内容 |
| :--- | :--- |
| `md-aeb-intervention-unresponsive-driver` | 025a: 無反応ドライバ＋衝突コースで介入し回避/緩和する |
| `md-aeb-no-false-intervention` | 025b: 衝突コース不在で人間のペダルに介入しない（負） |
| `md-aeb-brake-not-stacked` | 025c: 人間が AEB 要求以上のブレーキを踏んでいるとき上乗せしない |
| `md-aeb-kickdown-suppression` | 025d: 全踏みで介入が抑制され、抑制事象が観測できる |
| `md-fcw-leads-intervention` | 025e: 警報が介入に先行し、リードが閾値以上 |
| `md-fcw-warning-only-episode` | 025e: 人間が警報に反応して回避した場合、警報単独事象として残る（介入なし） |

### 2-2. 観測列（REQ-AD-028、フェーズB）

| slug | 内容 |
| :--- | :--- |
| `md-adas-native-name-enum` | 028a: 全機能が正規 Name 列挙（NAME_OTHER なし）で HVD に出る |
| `md-driver-override-brake-reason` | 028b: ブレーキ起因の上書きが REASON_BRAKE_PEDAL で出る。**フェーズB では実証不能**——機構（`expect_reason` を含む matcher と populate 経路）は完成しているが、ブレーキ起因の上書きを生む producer は ACC の解除（フェーズC）にしか無い。「無い Reason を要求したら赤になる」ことだけ単体で固定済み |
| `md-driver-override-steering-reason` | 028b: 操舵起因の上書きが REASON_STEERING_INPUT で出る。**フェーズB では実証不能**——producer は LKA の中断（フェーズD）。同上 |
| `md-driver-override-accel-custom-state` | 028b: アクセル起因が custom_state（DRIVER_OVERRIDE_ACCEL）で出る（Reason 列挙に該当値が無い規格制約の補完）。**フェーズB達成**（正=`md_aeb_kickdown_suppress` の 5.4-20.0 s 窓 293 フレーム全てで active＋トークン一致、負=`md_aeb_unresponsive` 391 フレームで非作動、同一実行内の対照=同 kickdown 行の `gt.fcw` 293 フレーム非作動） |
| `md-harness-hvd-readback` | 028c: ハーネスが in-process API 経由で HVD を読み matcher 入力にできる |
| `md-state-three-value-discipline` | 028a: 「切ってあった」（UNAVAILABLE）と「見張っていて撃たなかった」（STANDBY）が区別されて出る。**フェーズB達成**（`md_aeb_no_conflict.xosc` を `adas_aeb_enabled` の true/false 2構成で回す対。既定行=STANDBY 251 フレーム、`variant: adas_off` 行=UNAVAILABLE 251 フレーム。片方だけでは主張が成立しないので、どちらか一方を消さないこと） |

### 2-3. ACC / Stop&Go / MSL 列（REQ-AD-026 / 031 / 030、フェーズC）

| slug | 内容 |
| :--- | :--- |
| `md-acc-follow-thw-band` | 026a: 先行車追従で THW が設定段階のバンド内 |
| `md-acc-cruise-no-lead` | 026a: 先行車不在で設定速度巡航 |
| `md-acc-brake-cancel` | 026b: ブレーキ踏下で解除し、resume まで再介入しない |
| `md-acc-accel-temporary-override` | 026b: アクセル踏下で一時上書き、離すと追従へ復帰 |
| `md-acc-state-transition-sequence` | 026c: 操作列に対応する State 遷移列が HVD に出る |
| `md-acc-aeb-independence` | 026d: ACC 作動中に AEB が独立に発火し safety が勝つ |
| `md-acc-set-speed-runtime-change` | 026e: 走行中の設定速度増減が追従目標に反映される |
| `md-acc-speed-range-gate` | 026f: 利用可能速度域の域外進入で ACTIVE→STANDBY、介入出力が止む（両極性） |
| `md-acc-speed-limit-cap` | 026g: respect_speed_limit の有効/無効で実効上限が変わる（両極性） |
| `md-acc-thw-stage-runtime-change` | 026h: 車間段階の切替で収束後の実測 THW が変わる（1 本の刺激で 2 段階以上を跨ぐ） |
| `md-sng-stop-hold-no-creep` | 031a: v=0 到達後、停止保持中に前進しない（クリープ抑止） |
| `md-sng-restart-on-accel` | 031a: アクセルパルスで保持解除、追従再開 |
| `md-sng-traffic-light-stop` | 031b: 停止対象に信号を含めた構成で停止線手前に停止する |
| `md-sng-stop-sign-stop` | 031b: 同、一時停止標識 |
| `md-sng-target-config-polarity` | 031b: 停止対象を先行車のみに構成すると信号・標識で介入しない（構成の両極性） |
| `md-msl-throttle-cap` | 030a: 全開スロットルでも設定速度を超えない |
| `md-msl-no-brake-downhill` | 030a: 下り坂で設定速度を超えてもブレーキを出さない（リミッターの定義、負） |
| `md-msl-kickdown-release` | 030b: 全踏みで一時解除、離すと復帰 |
| `md-msl-speed-limit-linked` | 030c: 連動モードの有無で実効キャップが変わる（両極性） |
| `md-acc-msl-mutual-exclusion` | 設計 §6: 後から ON にした側が先の側を STANDBY に落とす |
| `md-kickdown-shared-consistency` | 設計 §3-3: AEB 抑制と MSL 解除が同一の検出で同時に立つ |

### 2-4. LKA / LDW / HMI 列（REQ-AD-027 / 029、フェーズD）

| slug | 内容 |
| :--- | :--- |
| `md-lka-drift-correction` | 027a: 弱ドリフトで補正が入り車線内に留まる |
| `md-lka-human-steer-priority` | 027b: 明確な操舵入力中は介入しない/即時中断（負） |
| `md-lka-indicator-suppression` | 027b: 指示器作動中は介入しない（負） |
| `md-lka-state-hvd` | 027c: on/off と状態遷移が HVD に出る |
| `md-lka-ffb-feedback` | 027d: 補正が FFB 反力として実ハンドルに伝わる（**G29 実機限定**） |
| `md-lka-speed-range-gate` | 027e: 域外速度では同一の逸脱刺激でも介入しない（両極性） |
| `md-ldw-warning-without-steer` | 027f: warning_only で警報は出るが cmd.steering は不変（両極性） |
| `md-hmi-state-display` | 029a: 状態と設定値が運転席 UI に表示される（**目視＋フロントテスト**） |
| `md-hmi-warning-presentation` | 029b: FCW/LDW が UI に提示される（同上） |
| `md-hmi-no-new-channel` | 029c: 提示が osi_bridge 既存配線由来である（**実装レビュー基準**、実行時判定なし） |

### 2-5. 横断・相互作用

| slug | 内容 |
| :--- | :--- |
| `md-default-off-baseline-parity` | 全機能既定 OFF の状態で、既存回帰バッチ（car_following / aeb_safety / anticipation）のベースラインが不動 |
| `md-split-no-double-equipment` | 横=手動、縦=VD の split で ManualDrive 側 AEB/ACC が評価されない（UNAVAILABLE で出る） |
| `md-self-determinism-control` | 同一構成 2 回実行の決定フィールド厳密一致（判定手法の前提。§7-5） |

## 3. 刺激資産マトリクス

### 3-1. xodr

道路は既存資産の再利用を第一とし、新規は 2 種に絞る。

| ID | 内容 |
| :--- | :--- |
| （既存流用） | 直線 2 車線（07_aeb の straight_500m_2lane）: AEB / ACC / LKA / MSL の大半 |
| （既存流用） | 信号交差点（03_traffic_signals 系）と一時停止交差点（04_traffic_signs 系）: Stop&Go 段b |
| `MDA-XODR-01` | 制限速度が途中で変わる直線 → **作成済み: `straight_500m_speed_limit_step.xodr`**（90→50 km/h @ s=250、ODR 適合 quick 緑） |
| ~~`MDA-XODR-02`~~ | ~~下り勾配区間つき直線（`md-msl-no-brake-downhill` 用）~~ → **★2026-08-05 作らないと決定** |

**★2026-08-05: MDA-XODR-02（下り勾配）を作らない決定と、その理由**。

この資産の狙いは REQ-AD-030 段a の**負系**——リミッターはスロットルを絞るだけでブレーキを
出さない——を、重力で設定速度を超えさせて撃つことだった。
ところが `RealVehicle` は道路ピッチを**姿勢にしか使っていない**: `terrain_pitch_` は
`GetCombinedAttitude` に入るだけで、縦方向の加速度には一切寄与しない。
つまり勾配路は「下り坂に**見えて**平地のように**振る舞う**」。

作れば、実行でき、フレームも出て、matcher も緑になる。そして何も測っていない。
**起こせない現象の名前を持つ資産は、無いより悪い**——名前が主張になってしまうので、読んだ人は
測られたと思う。

同じ主張は別の刺激で取れる。リミッターのキャップが現在速度に対して**低すぎる**構成
（`md_msl_throttle_cap`: 20 m/s で ON にしてから 0.85 スロットルを踏み続ける）でも、車両は
キャップ以上を要求され、スロットルは 0 近くまで絞られ、そこでブレーキが出るか出ないかが問われる。
勾配は要らない。実測: `gt.msl.brake_out` は全フレーム 0.000。

全 xodr は ODR 適合 quick を通し、全 xosc は waypoint 規律（`check_route_waypoints.py`）に準拠する。

### 3-2. xosc（コア約 20 本）

エゴのコントローラを ManualDriveController に差し替える点以外は、既存 VD 検証資産の幾何を流用できるものが多い。

- `md_aeb_cutin_unresponsive`（07_aeb cutin_hard_brake の運転主体差し替え）
- `md_aeb_stationary_lead` / `md_aeb_no_conflict_cruise`（負）
- `md_aeb_strong_brake_driver` / `md_aeb_kickdown_suppress`
- `md_fcw_warning_only`（接近が緩く、警報後に入力プロファイル側が減速して介入に至らない）
- `md_acc_follow_lead_speed_change` / `md_acc_cruise_empty_road`
- `md_acc_cancel_resume_override`（操作列: set → ブレーキ解除 → resume → アクセル上書き → 復帰）
- `md_acc_setting_changes`（設定速度増減と THW 段階切替を 1 本で跨ぐ）
- `md_acc_speed_range_exit` / `md_acc_speed_limit_cap`（MDA-XODR-01）
- `md_sng_lead_stop_restart` / `md_sng_traffic_light` / `md_sng_stop_sign` / `md_sng_target_config_lead_only`（負）
- `md_lka_drift_left` + `md_lka_drift_right`（左右対で判定の対称性も兼ねる）
- `md_lka_lane_change_with_indicator`（負）/ `md_lka_human_steer`（負）/ `md_ldw_warning_only`
- `md_msl_full_throttle_cap` / `md_msl_downhill`（MDA-XODR-02）/ `md_msl_kickdown`
- `md_split_lat_manual_lon_vd`（`md-split-no-double-equipment` 用）

`md-default-off-baseline-parity` は新規資産を作らない。
既存 3 バッチをそのまま回し、committed baseline との per-scenario 照合が不動であることを見る（回帰ゲートの既存機構で足りる）。

### 3-3. 入力プロファイル

各 xosc に入力プロファイル（§7-4 の ScriptedInputSource が読む時系列）を対で持たせる。
プロファイルは資産であり、xosc と同じディレクトリに置いて同じレビューを受ける。

| プロファイル | 内容 | 主な使用先 |
| :--- | :--- | :--- |
| `unresponsive` | 全入力ゼロ（無反応ドライバ） | AEB 正例、ACC 追従 |
| `steady_throttle` | 一定スロットル | AEB 負例、MSL |
| `strong_brake_at(t)` | 指定時刻から AEB 要求超のブレーキ | 025c |
| `kickdown_at(t)` | 指定時刻から全踏み | 025d、030b |
| `accel_pulse_at(t)` | 短いアクセルパルス | Stop&Go 再発進 |
| `weak_drift` | 微小な定常操舵オフセット | LKA 正例 |
| `deliberate_steer_at(t)` | 高レートの操舵入力 | 027b |
| `ops(t→操作)` | ボタン操作列（set / resume / 増減 / THW 切替） | 026b/e/h |

### 3-4. バッチ設計

**`manualdrive_adas_batch.yaml`（常設候補）**は正例と負例を同一マニフェスト・同一ベースラインに置く。
aeb_safety_batch.yaml が確立した前例と同じ理由（片方だけ守ると、閾値を緩めて正例を通し負例を壊す取引がゲートを素通りする）による。
フェーズ A の時点では AEB 列のみを載せ、フェーズ C / D で行を増やす（バッチは 1 本を育てる。機能別に分けない）。

**★2026-08-05 フェーズB で追加した行**（6 → 7 シナリオ）。

- 観測列の上書き（`md-driver-override-accel-custom-state`）は**新規シナリオを作らず**、既存の `md_aeb_kickdown_suppress`（正）と `md_aeb_unresponsive`（負）の expectations に `driver_override_reported` を足した。刺激は既にそこにある——キックダウン profile はまさに上書き入力そのもので、unresponsive はその対極。同じ実行から追加の観測量を読むだけの観点に、別の実行を作る理由がない。
- 3値規律（`md-state-three-value-discipline`）だけは 1 行増やした。「切ってあった」と「見張っていて撃たなかった」は**同じ振る舞い**を生むので、同一刺激を 2 構成で走らせる以外に示しようがない。ここで xosc を複製すると「絶対に食い違ってはいけない 2 ファイル」がレビュー対象に増えるため、`md_aeb_no_conflict.xosc` を**そのまま再利用**し、マニフェスト側の新キー `variant:` で出力ディレクトリを分けた（差分は `adas_aeb_enabled` だけ）。この機構は §7-2 が「両極性判定はバッチマニフェスト側からパラメータ化できることに依存する」と書いていたものの実体で、フェーズC の `md-acc-speed-limit-cap` / `md-msl-speed-limit-linked` / `md-sng-target-config-polarity` もこれに乗る。
- `variant` は同一 run ディレクトリへの衝突をマニフェスト読み込み時に拒否する（黙って上書きして両方 run 扱いになるのが最悪の失敗様式のため）。`check_regression_baseline.py` のキーも stem から run ディレクトリ名へ変えてあるので、フェーズE で常設化するときに 2 構成が 1 行へ潰れることはない。

拡張セット（`manualdrive_adas_extended_batch.yaml`、手動運用）には、境界校正用の資産（キックダウン閾値近傍、速度域境界の両側、THW 段階の全組合せ）を置く。

## 4. matcher 拡充

### 4-1. 赤実証資産の規律

駐車検証計画 §4-1 と同じ規律を適用する。
新設 matcher は「壊れた挙動で確実に赤くなる」ことを示す**赤実証資産**を 1 つ以上対応づけ、資産が存在しない matcher は常設ゲートに載せない。
E2E で赤実証を作りにくいものは C++ 単体テストで赤実証し、E2E は緑担保のみとする（駐車の `parking_reverse_gear_matches_segment` と同じ扱い）。

### 4-2. 拡充表

| matcher | 状態 | 概要 | 赤実証資産 |
| :--- | :--- | :--- | :--- |
| `adas_state_matches` | 実装済み（フェーズA） | 窓内の機能別 State 列（ACTIVE/STANDBY/UNAVAILABLE）が期待と一致。REQ-AD-026 段c と 028a の主判定 | **実績（2026-08-05）**: Python 両極性ユニットテスト `test_manualdrive_matchers.py`（35テスト、§4-1のunit-level許容を適用）。E2E（`10_manualdrive_adas/`）は正例の緑担保のみで、当初計画の「同一資産を config OFF で回す」E2E 赤実証は未実施のまま置き換え |
| `adas_state_sequence` | 実装済み（フェーズC） | State 遷移の部分列一致（overtake の expect_phases と同型）。ラン長圧縮した列に対する**部分列**一致＝順序だけを主張し滞在時間は主張しない | **実績（2026-08-05）**: `test_manualdrive_matchers.py` の単体赤3件（遷移が起きない／順序が逆／`expect` が2要素未満・隣接重複で skip）。E2E は `md_acc_cancel_resume` / `md_acc_speed_range_gate` / `md_msl_kickdown` / `md_acc_msl_exclusion` で緑担保 |
| `min_obb_separation_above` | 既存流用 | 衝突分離。OSI scene 駆動で運転主体非依存 | （既存の赤実証を継承） |
| `impact_speed_below` | 既存流用 | 衝突速度低減（緩和域） | 同上 |
| `manual_aeb_fires` | 実装済み（フェーズA） | HVD の AEB 行が窓内で ACTIVE になり、gt.aeb.* に発火量が出る | **実績（2026-08-05）**: `test_manualdrive_matchers.py` の単体テスト。E2E（`md_aeb_unresponsive.xosc`）は正例の緑担保（実測: t=2.00 で ACTIVE）のみで、当初計画の「AEB を config OFF にした同一資産」でのE2E赤実証は未実施のまま置き換え |
| `no_intervention_in_window` | 実装済み（フェーズA） | 窓内で指定機能の ACTIVE が無く、ペダル実効値が入力プロファイルと一致 | **実績（2026-08-05）**: `test_manualdrive_matchers.py` の単体テスト。当初計画の「誤介入を誘発する閾値へ緩めた config」でのE2E赤実証は未実施のまま置き換え |
| `brake_not_stacked` | 実装済み（フェーズA） | 人間ブレーキ ≥ AEB 要求の窓で実効ブレーキ＝人間値 | E2E 赤実証は困難（計画時点の想定どおり）。**実績**: max 合成を `test_manualdrive_matchers.py` の単体テストで赤実証し、E2E（`md_aeb_strong_brake_driver.xosc`）は緑担保のみ——ただし当該 E2E 資産は閾値未校正で needs-review（実行はできるが判定が確定しない、`test_results/mdadas_run1/batch_summary.md`） |
| `fcw_leads_intervention` | 実装済み（フェーズA） | 警報立ち上がりが介入立ち上がりに先行し、リード ≥ min_lead_s | **実績（2026-08-05）**: `test_manualdrive_matchers.py` の単体テスト。当初計画の「警報閾値を介入閾値と同値にした config」でのE2E赤実証は未実施のまま置き換え。**副産物として E2E 側（`md_aeb_unresponsive.xosc`、cut-in 幾何）が実際に fail した**（リード 0.000s、design §3-2 訂正参照）——想定していた「config を壊して赤」ではなく「現状の想定どおりの入力で赤」という、計画時点で想定していなかった種類の赤である |
| `driver_override_reported` | 実装済み（フェーズB） | 上書き入力の窓と DriverOverride/custom_state の一致。`function` 必須、`expect_active`（既定 true）/ `expect_reason` / `expect_custom_state` / `mode`（all/any） | **実績（2026-08-05）**: 計画どおり2つの赤を `test_manualdrive_matchers.py` に置いた —— ① populate を止めた（行は出るが submessage が書かれない）→ fail、② 入力プロファイル時刻ずらし（窓が上書き区間から外れる）→ fail。加えて custom_state トークン違い・存在しない Reason 要求・負方向への迷い込みも赤で固定。E2E は正負とも緑担保（正=`md_aeb_kickdown_suppress` 293フレーム、負=`md_aeb_unresponsive` 391フレーム） |
| `setting_reflected` | 実装済み（フェーズC） | 設定値の変化が実効値に段差として現れる。**切替が 1 回も起きなければ赤**（定数フィールドの偽 PASS 防止）。等値でなく**同方向**を見る（実効値は min 合成なので新設定に届かないことが正当にありうる） | **実績**: 単体赤4件（保存したが適用しない／逆方向へ動く／変更が1回も無い＝fail／キー欠落で skip）。E2E は `md_acc_setting_changes`（set_speed）と `md_acc_follow_lead`（thw、mid→long→short の2回切替）で緑 |
| `speed_capped_at` | 実装済み（フェーズC） | 窓内の最大速度がキャップ値＋許容差以下。`cap_key` で**毎フレームのキャップを signal から読む**形も取れる（連動モードではキャップ自体が道路に沿って動くため） | **実績**: 単体赤2件（キャップ超過／`cap_key` 未記録で skip＝0 と読まない）。E2E は計画どおり「OFF にした同一資産」を `variant` で並走させて両極性（`md_acc_speed_limit_cap` ×2） |
| `no_brake_output` | 実装済み（フェーズC・負） | 窓内で**その機能自身の**ブレーキ寄与が無い（車両の実効ブレーキではない——人間のブレーキは主張と無関係に非零になりうる） | **実績**: 計画どおり単体で赤実証（`gt.msl.brake_out` が非零＝AEB のブレーキ変換を誤結線した形／チャネル未記録で skip）。E2E は下り坂ではなく平坦路の `md_msl_throttle_cap`（§3-1 の決定を参照） |
| `stop_hold_stationary` | 実装済み（フェーズC） | 保持中の変位が閾値以下。保持区間は**機能自身の `gt.acc.stop_hold`** でアンカーし、**連続区間ごとに**測る（1 run で停止→再発進→再停止が起きうるため） | **実績**: 単体赤2件（クリープ／保持が一度も起きない run は pass でなく skip）。E2E は `md_sng_lead_stop_restart` / `md_sng_stop_sign` で緑（実測変位 0.015-0.032 m、閾値 0.5 m） |
| `restart_after_trigger` | 実装済み（フェーズC） | アクセルパルス後、`within_s` 以内に `min_speed` へ達する。**パルス前の「動かなかった」は担当しない**（`stop_hold_stationary` の領分）——分けておくと、停止しなかった run が正しい方の matcher で赤くなる | **実績**: 単体赤1件（保持が解除されない）＋ skip 2件。E2E は `md_sng_lead_stop_restart` で緑（パルス t=21.0、到達 2.43 m/s） |
| `lane_kept_within` | 実装済み（フェーズD） | 窓内の \|offset\| が車線内閾値以下**かつレーンIDが不変**（LKA 正例）。`expect_kept: false` で逸脱側も判定できる | **実績（2026-08-05）**: `test_manualdrive_matchers.py` の単体赤3件（偏差超過／**レーン変化のみで偏差は小さいまま**／逸脱側の期待に対し車線を保ってしまった run）＋ skip 3件。E2E は `md_lka_drift` の左右対（正）と warning_only / below_band（`expect_kept: false`、同一刺激が \|offset\| 4.330m・レーン-1→-2 の逸脱になる）で両極性 |
| `steer_output_absent` | 実装済み（フェーズD・負） | 窓内で**機能自身の**補正操舵出力が無い（LDW モード、人間優先、域外） | **実績（2026-08-05）**: 単体赤3件（補正が漏れる／負方向の補正も赤／チャネル未記録で skip）。E2E は `md_lka_drift[warning_only]` / `[below_band]` / `md_lka_human_steer` / `md_lka_lane_change_with_indicator` の4行で緑 |
| `stopped_at_stop_line` 系 | 既存流用 | 停止線手前停止（REQ-AD-003/004 の判定を Stop&Go 段b に転用） | （既存の赤実証を継承） |

**★2026-08-05 訂正**: フェーズA実装時点で、新設5 matcher（`adas_state_matches` / `manual_aeb_fires` /
`no_intervention_in_window` / `brake_not_stacked` / `fcw_leads_intervention`）の赤実証は、
計画段階で書いた E2E の config 極性反転ではなく、**Python 両極性ユニットテスト
`GT_esmini/scripts/verification/test_manualdrive_matchers.py`（35テスト、vd_metrics.py の
該当分岐を直接叩く）**に統一した。§4-1 が明示的に許す「E2E で赤実証を作りにくいものは
単体テストで赤実証し、E2E は緑担保のみとする」の適用範囲を、`brake_not_stacked` 1件だけでなく
新設5件全体へ広げた判断で、E2E側（`10_manualdrive_adas/`）は正例シナリオでの緑担保用途に
専念させている。各行の「赤実証資産」列は実績を反映済み。

既存 matcher の流用可否は、自車状態の取得元（telemetry か OSI scene か）に依存する。
OSI scene 駆動のもの（OBB 分離、衝突速度）は主体非依存で流用できるが、telemetry の `frame["ego"]` を併用するもの（THW 系）は §7-3 の HVD/scene 投影が入るまで確定しない。
流用可否の最終確定はハーネス改修後に行い、本表の「既存流用」行は現時点の見込みである。

### 4-3. 運用コスト

フェーズCで 6 件、フェーズDで 2 件を実装し、`namespaces.yaml` の matcher `id_pattern` は
27 → 33 → **35 件**になった。**計画が予定していた新規 matcher はこれで全て実装済み**である。

フェーズDの 2 件のうち `lane_kept_within` は、計画時点の記述（「窓内の |offset| が車線内閾値
以下」）のままでは**逸脱した run にこそ最もきれいな pass を返す** matcher になっていた。
読む偏差はレーン相対で、roadmanager の `Position::GetOffset()` はレーン境界で参照を張り替える
（本プロジェクト実測 -1.7482 → +1.9425 が1フレーム）——つまり車線を出た瞬間、偏差は新しい
レーンの中心基準で小さく戻る。レーンIDの不変を併せて要求することで塞いだ。
**判別のために作った観測量が短絡して定数に化ける**型の罠で、計画段階の1行の記述からは見えない。

新規 matcher は約 14 件で、追加のたびに `namespaces.yaml` の matcher `id_pattern`（enum 形式、source_of_truth: vd_metrics.py）への追記が要る。
駐車と同じく、フェーズごとに繰り返し発生する運用コストとして織り込む。

## 5. しきい値の根拠を資産メタデータに残す運用

駐車検証計画 §5 と同じ運用を適用する。
本計画で原典未確認のまま使う値は次のとおりで、資産側の notes に出典と確認状況を残す。

- FCW リード「介入の 0.8 s 以上前」: UN R152 由来だが**二次資料経由・原文未確認**（REQ-AD-001 response の記載を継承）
- AEB 抑制の「アクセル全踏み相当」: UN R152 の運転者上書き規定に相当、**同上**
- THW 段階の初期値（1.0 / 1.6 / 2.2 s）: 実車の車間 3 段階の慣例値、**要校正**
- LKA 作動下限の参考値（60 km/h 前後）: 実車の慣例、**要校正**

キックダウン閾値、速度域、TLC 閾値は本プロジェクトの校正値であり、拡張バッチ（§3-4）の境界資産で校正した記録を資産メタデータへ残す。

## 6. 自動化可能 / G29 実機限定 / 目視の区分

| 区分 | 対象 | 理由 |
| :--- | :--- | :--- |
| 自動化可能 | §2 のほぼ全観点 | ADAS の意思決定は入力ソースに依存しない。合成入力（ScriptedInputSource）と in-process 実行で完全に再現でき、`vd_ffb_notouch_parity.py` が確立した線引き（決定ロジックは合成入力で完全代替可能）を継承する |
| G29 実機限定 | `md-lka-ffb-feedback`（027d）、FFB パルス警報の体感、実トルクに基づく人間操舵優先の精度改善 | 実ハンコンの FFB サーボ特性・摩擦特性に依存し、headless の plant モデルでは個体差まで代替できない |
| 目視＋フロントテスト | `md-hmi-state-display` / `md-hmi-warning-presentation`（029a/b） | 表示はフロントエンドの領分。emit 側（HVD への出力）は自動判定し、描画はフロント側テスト流儀と目視に置く |
| 実装レビュー基準 | `md-hmi-no-new-channel`（029c） | 実行時に判定する量ではなく、差分レビューで守る構造制約 |

## 7. 検証ハーネス改修仕様

設計書 §11 が委譲した 5 点の受け皿である。
現状の gt_sim_test は VD テレメトリ必須（無いと frames=0 で即エラー）、policy 注入は VirtualDriverController 固定、HVD は未読であり、ManualDrive バッチはそもそも走らない。
以下は全てフェーズ A の完了条件に含まれる（これが無いと A の判定自体ができない）。

### 7-1. ManualDrive 実行モード

バッチマニフェストにコントローラ種別キーを追加し、ManualDrive 指定時は VD テレメトリ要求を外す。
フレーム取得の主観測源を OSI scene（既存の in-process 取込）と HVD（§7-3）に切り替え、実行打ち切り判定（GRACE）も scene のフレーム進行で行う。

### 7-2. config 注入の ManualDrive 版

`_prepare_policy_xosc` 相当を ManualDriveController の Properties（ConfigFile）に対応させる。
注入する config は `manual_drive.json` の `adas` セクション（設計書 §9）で、シナリオごとの構成差（stop_targets、warning_only、速度域、respect_speed_limit の有無）はここで与える。
両極性判定（同一刺激を 2 構成で回す）は、このパラメータ化がバッチマニフェスト側から書けることに依存する。

### 7-3. HVD 読み取り

設計書 §8-5 の in-process C API（シリアライズ済み HostVehicleData）を ctypes で受け、
`vehicle_automated_driving_function[]` を機能名 → {state, custom_detail, driver_override} の辞書へ投影して matcher 入力に足す。
自車の運動状態はこれまでどおり OSI scene の is_host から読む（面1化済みの経路を変えない）。

**★2026-08-05 フェーズB: 投影に `driver_override.present` と `custom_state` を追加**。
`present` は OSI の submessage 自体の有無で、`active` とは別の情報を運ぶ——「評価して上書き無しと測った」（present かつ active=false）と「誰もこのチャネルを書いていない」（present 無し）の区別である。
この区別が無いと、**populate 機構ごと消した実行でも負の matcher が緑になる**（上書きが観測されないという同じ観測値になるため）。
そこで `driver_override_reported` は方向によって前提を変えている: 正方向（`expect_active: true`）は present を要求しない——行そのものが出ている時点で計器は生きているので、submessage の欠如は真の負観測＝fail である。
負方向（`expect_active: false`）だけが present を要求し、書かれていなければ pass ではなく skip を返す。
これは既存の `no_intervention_in_window` が State について課している STANDBY / UNAVAILABLE の前提と同じ形で、「切ってあった機能は『正しく撃たなかった』ことの証拠にならない」を上書きチャネルへ移したものである。

### 7-4. ScriptedInputSource（C++ 側の小改修）

入力プロファイル（§3-3）を再生する決定論的な入力源を `IInputSource` 実装として新設する（`input_type: "scripted"`、プロファイルファイスパスを config で指定）。
ソケットを使わず、シミュレーション時刻に対する区分線形の時系列（steering / throttle / brake / buttons）をフレームごとに返すだけの部品である。
UDP 注入（NetworkInputBridge）を使わないのは、決定論性の担保（自己決定論性コントロールを全バッチの前提にする）と、テストプロセス側の送信ループを持たないためである。
既存の HeadlessFfbInput は FFB 結合の検証用にそのまま残り、本計画では操舵系プロファイルの一部（plant モード）で併用しうる。

### 7-5. 判定手法の踏襲

ベースライン照合は `vd_ffb_notouch_parity.py` の 2 段手順を踏襲する。
すなわち、(1) 同一構成の 2 回実行で決定フィールドが厳密一致すること（自己決定論性コントロール）を先に確認し、これがクリーンでない限り (2) の期待値照合を信用しない。
ManualDrive の決定フィールドは、ADAS 調停後の実効ペダル/操舵と、機能別 State 列である。

## 8. 未設計のまま残す接続点

- 既存 matcher（THW 系など telemetry 併用のもの）の流用可否の最終確定（§4-2。ハーネス改修後）
- TrafficLightAware / StopYieldSignAware の非依存性の実走確認（設計書 §12）。結果次第で §3-2 の Stop&Go 資産の構成が変わる
- キックダウン、TLC、速度域の各閾値の校正値（拡張バッチで校正してから常設バッチの期待値を固定する）
- HMI フロントテストの粒度（029a/b。フロント側のテスト流儀に合わせてフェーズ D で確定）
- 常設ゲートの Step 番号と CI 配線（フェーズ E。非ブロッキング開始 → 昇格は AEB 前例に従う）
- FFB パルス / 音の警報チャネル拡張が実装された場合の検証形（現計画では口だけ切って既定 OFF）
