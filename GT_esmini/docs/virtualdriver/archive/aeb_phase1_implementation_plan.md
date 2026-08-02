# AEB 実装計画（フェーズ1）— 完了・凍結

> **status: 完了・凍結**（`03e193b0` で `AebSafety` 実装、回帰ゲートに搭載済み）。
> 以下は着手時点の計画本文で、§8 のチェックボックスは当時のまま更新していない。
> AEB の現況は [`../design/adas_axis.md`](../design/adas_axis.md) と
> [`../measurements/aeb_c2c_grid_matrix.md`](../measurements/aeb_c2c_grid_matrix.md) を見ること。
> 設計軸=[adas_axis.md](../design/adas_axis.md)、要求=[aeb_requirements.md](../design/aeb_requirements.md) /
> `knowledge/requirements_vd_ad.yaml`、RED資産=`resources/xosc/verification/07_aeb/`。

## 0. これまでの到達点（フェーズ0）

- **機能軸**を確立: 「動機層(安全/快適/法規遵守/譲り合い) × 主体(AD/ADAS)」。安全=override が快適・法規の上を行く調停。
- **機能カタログ** `vd-func` FUNC-001..048（前方AEB=FUNC-001, VRU-AEB=FUNC-002）。
- **AEBを要求に落とした**: NCAP/R152 逆算スキーマ + `REQ-AD-001`(複合カットイン+制動) / 010(CCRs) / 011(CCRm/CCRb) / 012(VRU) / 013(誤作動抑止・negative) / 014(層調停) / 015(R152フロア)。
- **RED実装済**（test-first）: `07_aeb/cutin_hard_brake.xosc`（直進）＋ `cutin_hard_brake_curve.xosc`（R=300カーブ）。`aeb_safety_batch.yaml`（osi:true, policies:[lead]）。gt_sim_test で **overlap 実証**（OBB分離 0.00m, fail）。パラメトリック（EgoSpeed/LeadSpeed/LeadStartS/CutInTime/CutInDur/BrakeTime/BrakeRate）。

## 1. 核心的再フレーミング（設計の土台・重要）

RED実装中の実証で判明した2点が設計を決める:

1. **速度制限の罠**: verification 道路が town(50km/h) だと `respect_speed_limit` で Ego が制限速度に張り付き高速シナリオにならない → 07_aeb は motorway(130) の 2車線路（`straight_500m_2lane` / `curve_2lane_r300`）を新設済み。
2. **AEBギャップの本質**: `LeadVehicleAware` は既に **~11 m/s² の強い減速**を IDM の MAX_SPEED 経路で出せる（`comfort_decel=2` の天井を迂回）。⇒ **#34 は「減速度不足」ではなく「カットインの遅い検知」**（同一レーン `dLaneId==0` かつ `|dt|<=lateral_tol` のみ検知）が本質。

**⇒ AEBの主眼は「カットイン/横方向侵入の早期検知」。emergency_decel は副次**（既に強く止まれるので、早く気づけば回避できる）。
**⇒ [adas_axis.md](../design/adas_axis.md) §7 の「安全層=comfort_decel 天井を破る」は要修正**（天井は既に破れている。真の欠落は検知タイミング）。

## 2. スコープ

- **In（phase1）**: 前方AEB（FUNC-001）の cut-in / lead-brake 回避。**07_aeb 直進+カーブの RED を緑化**。負の要求（誤作動抑止）。config+GUI 露出。
- **Out（後続波）**: VRU-AEB（FUNC-002 / REQ-AD-012）、交差点内カットイン（安全override調停の試験台・要設計）、探索スイープ層、避けられない域の mitigation 精緻化。

## 3. アーキテクチャ設計

### 3.1 調停レイヤ（tier-tagged arbitration）— 安全 override の受け皿
- `PolicyConstraint` に `tier`（safety/compliance/courtesy/comfort）を付与。
- `ManeuverAwareSpeedPlanner` の制約合成（現状「最厳 MAX_SPEED 勝ち」＋ **全減速 comfort_decel 頭打ち**）を **tier別の減速上限で解く**に変更。安全tierのみ `emergency_decel` を許可、他tierは `comfort_decel`。
- 差し込み点候補: `ManeuverAwareSpeedPlanner.cpp` の STOP_AT_S ランプ（`sqrt(2*comfort_decel*dist)`）・後退パス到達可能性・ジャーク制限を tier に応じて comfort/emergency で切替。**実装前に spike で seam を確定**。

### 3.2 AEB policy（新規 `ITrafficPolicy`, 例: `AebSafety`）
- **検知（主眼）**: 同一レーン限定を外し、**横方向に侵入中の車**（`dt` が縮小方向 + 縦 TTC 小）を先行候補に含める。lateral-encroachment / lane-change 予測。`LeadVehicleAware` の探索を安全用途に拡張 or 別実装。
- **判定**: TTC と必要減速度 `a_req` ベース。`a_req > comfort_decel` かつ `TTC < 閾値` で発火。
- **介入**: safety-tier の制約（新種 `HARD_DECEL` / `EMERGENCY_STOP`、または STOP_AT_S に safety フラグ）を emit → 調停レイヤが emergency_decel で解く。
- **両面（SOTIF, REQ-AD-013）**: 衝突コース不在では発火しない（negative 要求をテストで守る）。

## 4. 実装ステップ（test-first / 順序）

1. **spike**: `emergency_decel` の差し込み seam を発見（プランナの減速上限を tier で切替できるか）。使い捨て。
2. **調停プリミティブ**: `PolicyConstraint.tier` 追加 + プランナが tier別 decel 上限で解く。既存挙動（全 comfort）は不変に保つ。
3. **AebSafety policy**: 早期検知 + TTC/a_req トリガ + safety制約 emit。
4. **config/flag**: `virtual_driver.json` に `policy_aeb_enabled`(default false) + `emergency_decel` / `aeb_ttc_threshold` 等。C++ `VirtualDriverConfig` の bool/double テーブルに追加。`gt_sim_test._POLICY_FLAG` と runner `_VD_POLICY_FLAG` に `aeb` を追加。`ControllerVirtualDriver` で `policy_aeb_enabled` ガード登録。
5. **RED緑化**: `aeb_safety_batch.yaml` を `policies:[lead, aeb]` にして **直進+カーブが PASS**（min_obb_separation_above > 0.5）。※避けられない域なら acceptance を mitigation（衝突速度低減）へ切替（REQ-AD-001 は両対応）。
6. **negative/回帰**: REQ-AD-013 の誤作動シナリオ（通常追従・LC・併走で emergency 不発火）を追加し PASS。既存 `06_lead_vehicle/*`・`car_following_traffic_control_batch`・回帰baseline が **非回帰**。
7. **GUI**: `VirtualDriverPanel` に AEB トグル + パラメータ（VD-GUI-PARITY, #33）。`virtual_driver_api` の known-keys に追加。
8. **docs/KG**: [adas_axis.md](../design/adas_axis.md) §7 修正、`vd-func:FUNC-001` を `status: built`、graph.yaml で policy(Aeb)→realizes→FUNC-001 の実装辺追加、`--render`。#34 を close。

## 5. 検証と matcher

- **既存で足りる**: `min_obb_separation_above`（衝突ゼロ）で 07_aeb 緑化を判定。`deceleration_profile_smooth`（快適・REQ-AD-014）。
- **新規（`proposal:P11`/`P12` 依存、本phaseで最低限）**:
  - `no_emergency_without_conflict` — 誤作動ゼロ（REQ-AD-013）。**必須**。
  - `impact_speed_reduction` — 避けられない域の mitigation 評価（REQ-AD-001/011 の高速側、NCAP カラーバンド相当）。
  - (任意) `ttc_min_above` / `a_req` 露出。
- **回帰ゲート**: ✅完了（2026-07-21, gate:aeb-safety-regression）。`aeb_safety_batch.yaml` を pre-merge 回帰の Step 2.6 に組込み済み。collision-free 不変条件（P12）との接続は未。

## 6. 校正の未決（実装時に確定）

- **avoid か mitigate か**: 直進 RED（Ego 30m/s, cut-in 5m前）は早期検知でどこまで完全回避できるか。物理的に不能なら acceptance を mitigation に切替（REQ-AD-001 acceptance は両対応済み）。テスト側パラメータの再校正も選択肢。
- **数値**: `emergency_decel`（8? タイヤグリップ上限≈9-10?）、`aeb_ttc_threshold`、検知の横方向範囲/予測ホライズン。すべて acc-test/07_aeb 実測でキャリブレーション。

## 7. リスク / 制約

- **R1 Clean Core**: これは GT_esmini 拡張（`EnvironmentSimulator` 非改変）。VirtualDriver 配下（`src/control/virtualdriver/`）で完結させる。
- **作業ツリーの並行 C++ 変更**: 現在 `ManeuverAwareSpeedPlanner.cpp` / `TrajectoryShortPlanner.cpp` / `ConflictPointResolver.cpp` 等に**私が触っていない並行プロセスの変更が混在**。実装着手前に素性を確認し衝突回避すること。コミットは **pathspec 指定**（`git commit -- <paths>`）厳守（共有index巻き込み防止・[[shared_git_index_race]]）。
- **ビルド/実行**: Protocol A Release + gt_sim_test の in-process DLL。長時間ビルドは detached 起動。
- **test-first**: `test-driven-development` スキルに乗せ、07_aeb の RED を緑化する形で進める。

## 8. 完了の定義（phase1 DoD）

- [ ] `07_aeb` 直進+カーブが PASS（`policies:[lead, aeb]`、avoid or mitigation）
- [ ] REQ-AD-013 誤作動ゼロ が PASS、既存 `06_lead_vehicle`/`phase3` 非回帰
- [ ] `emergency_decel`/TTC が config + GUI 露出（VD-GUI-PARITY）
- [ ] `vd-func:FUNC-001` = `status: built`、KG 実装辺追加、adas_axis §7 修正
- [ ] `#34` を close 可能

## 9. トレーサビリティ

`REQ-AD-001/010/011/013/014/015` → `vd-func:FUNC-001` → policy(AebSafety) → `07_aeb` matcher。
後続波: `FUNC-002` VRU（REQ-AD-012）、交差点（安全override調停）、探索スイープ（parametric × geometry）。
