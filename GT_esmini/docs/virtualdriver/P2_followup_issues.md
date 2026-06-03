# VirtualDriver Phase 2 — フォローアップ課題（次セッション着手予定）

| 項目 | 内容 |
| --- | --- |
| 状態 | **両件 解決済（2026-06-04, dev_v0.12, Protocol A build clean）** |
| 前提 | Phase 2 (`ManeuverAwareSpeedPlanner`) 完了・scenario05 overall pass・A2/V2 結合済 |
| スコープ外 | `PIDPurePursuitDriver` の縦制御アルゴリズム（凍結維持。jerk は V2 が gt_sim_test 側で解決済） |

Phase 2 完了後に確認された 2 件。どちらも横/縦プランニング層の問題で、互いに独立。
**課題2は Phase 1 hard-won の「preview アンカー / GetPose 自己位置」に触れるため要注意**
（[[virtual_driver_controller]] の「横追従の肝」参照）。

> **解決サマリ（2026-06-04）**
> - **課題1**: `ManeuverAwareSpeedPlanner` の junction キャップ/`kind="junction"` を、接続路の
>   turnRate ≥ `SHARP_TURN_RATE`(0.04 rad/m) の「実際に曲がる」接続路だけに限定（id キャッシュ付）。
>   検証: 新規 `cross_straight_junction.xosc`（直進=減速なし）＋既存 `decelerate_for_right_turn`（右折=減速）で両確認、anticipation batch 4/4 pass。
> - **課題2**: 前進時のみ制御基準点 dstate.x/y と preview アンカーを**同一 Lf** で前方（前軸）へシフト。
>   planner が実 Lf を `ShortPlannerSnapshot::control_point_offset` に echo→controller が同値で dstate シフト。
>   LC/LaneOffset 中は Lf=0 で Phase 1 挙動温存。config `control_point_offset`（0=auto=wheel_base, <0=無効, 既定有効）。
>   `vd_smoke`/`vd_anticipation_check` 非回帰 PASS。**フロント路面内の定量確認とオフセット実測チューニングは申し送り**（telemetry に front-bumper road-position 無し）。
> - 詳細手法・残課題は [[virtual_driver_controller]] の「P2 フォローアップ 2件 解決」節を参照。

---

## 課題1: 交差点を「直進」するときも不要に減速する

### 症状
無信号交差点を直進通過する際（および曲率の小さい緩カーブ）でも `ManeuverAwareSpeedPlanner`
が速度を落としてしまう。本来は直進・緩カーブではほぼ減速せず走るべき。

### 根本原因（特定済み）
[ManeuverAwareSpeedPlanner.cpp](../../src/control/virtualdriver/ManeuverAwareSpeedPlanner.cpp)
の前方スキャンが、**ジャンクション接続路なら無条件で `turn_speed`(=5.0) キャップ**を掛けている:

```cpp
const bool   on_junction = road && road->GetJunction() != ID_UNDEFINED;
const double v_turn      = on_junction ? cfg_.turn_speed : kUnconstrained;
double v_ceil = std::clamp(std::min({v_curve, v_lim, v_turn}), cfg_.min_speed, kUnconstrained);
```

直進でもジャンクションは「接続路(connecting road)」を通るため `on_junction=true` →
`turn_speed` で頭打ち → 減速。曲率(`v_curve`)だけなら直進接続路は κ≈0 で減速しないのが正しい。
`kind="junction"` 制約マーカーも直進で誤って立つ。

### 修正方針
`turn_speed` キャップ（と `junction` 制約の発行）を **「その接続路が実際に曲がる」ときだけ**に限定。
直進接続路は曲率任せ（実質減速なし）。曲がる/直進の判定は既存ロジックを流用:
- **RouteDrive のコネクタ turnRate 判定**（[ControllerRouteDrive.cpp:331-344](../../src/control/ControllerRouteDrive.cpp#L331)）:
  接続路の `p0(s=0)` と `pE(s=length)` の heading 差 / length ≥ `SHARP_TURN_RATE(0.04 rad/m)` なら turn。
- または `ControllerVirtualDriver::DetectJunctionTurn` の heading-delta（`GetDrivingDirection` 差 > 0.10 rad）。

スキャンが接続路に入った時点で 1 回だけ turn/straight を判定（同一 road id はキャッシュして毎サンプル再計算回避）。

### 影響ファイル / 検証
- `ManeuverAwareSpeedPlanner.cpp`（`on_junction`/`v_turn` ブロック + `kind` 分類）。自己完結、`turn_speed` config は維持。
- 検証: 「交差点直進」xosc を新規作成し減速しないこと、既存 `decelerate_for_right_turn`（右折）は従来どおり減速することを `gt_sim_test` で両確認。`scripts/vd_anticipation_check.py` 非回帰。

---

## 課題2: 追従基準点がリア側のため、交差点旋回でフロントが路外へはみ出す

### 症状
交差点を曲がるとき、車両フロントがレーン外（路外）へはみ出す。前進走行中は基準点をフロントに置きたい。

### 根本原因（特定済み）
追従の基準点が**車両原点（リア寄り）**。
- 自己位置 = [RealVehicleBackend::GetPose](../../src/control/common/RealVehicleBackend.cpp#L66)
  = `real_vehicle_.posX_/posY_`（`object->pos_` XY と一致＝esmini 参照点）。
- preview は**その原点の route s のレーン中心**にアンカー（[TrajectoryShortPlanner.cpp](../../src/control/virtualdriver/TrajectoryShortPlanner.cpp) の `SetLanePos(...,0)`）。
- [PIDPurePursuitDriver.cpp](../../src/control/virtualdriver/PIDPurePursuitDriver.cpp) は `state.x/y`（=原点）から
  lookahead と cross-track を測る。

旋回時、原点（リア寄り）がレーン中心に乗ると、フロント（原点+前方≈半車長）はより大きな半径を描き **外側へ膨らむ → 路外**。

### 修正方針
**前進時は制御基準点をフロント側（前軸/前バンパ）にオフセット**。PID の*アルゴリズム自体は変えず*、
入力（基準位置と preview アンカー）を前方シフトする（凍結を尊重）:
- 制御点 `P_ctrl = origin + L_fwd · (cos h, sin h)`。`L_fwd` は原点→前軸/前バンパ距離（≈ wheel_base か length/2。
  原点が rear-axle か bbox-center かを `RealVehicle` 実装で確認して決める。`control_point_offset` として config 化＋実測チューニング推奨）。
- `ControllerVirtualDriver::Step` の `dstate.x/y` を `P_ctrl` にする **かつ** preview を `P_ctrl` の route s でアンカー
  （pos_ を `L_fwd` 前進させた点でレーン中心 offset0）。**x/y/h/speed は同一ソース厳守**（hard-won）。
- 前進時のみ適用（`speed > 閾値`）。停止/後退時は従来（原点）にフォールバック（ユーザ要件「前に向かって走行している際は」）。

### 影響ファイル / リスク / 検証
- `ControllerVirtualDriver.cpp`（dstate 設定）、`TrajectoryShortPlanner`（前方 s でのアンカー）、新 config `control_point_offset`。
  PID 本体は不変。
- **リスク（高）**: Phase 1 hard-won の preview lane-center アンカー + GetPose 自己位置に直接触れる。
  LC 収束・ルート追従が劣化しないこと、原点とアンカー s の整合（前方シフト後も「同一点基準」）を厳守。
- 検証: `vd_smoke.py`（LC/巡航/停止 非回帰）、交差点右折でフロントバンパ XY が路面内に留まること（前バンパ点を別途算出して road 判定 or lane_keep を厳格化）。
- メモリ [[virtual_driver_controller]] の「横追従の肝」に判断を追記すること。

---

## 着手順の提案
1. **課題1 を先に**（自己完結・低リスク・効果明確）。
2. 課題2 は hard-won 領域なので、課題1 完了後に腰を据えて（基準点オフセットの最小実装→非回帰確認→チューニング）。
