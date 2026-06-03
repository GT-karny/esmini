# 依頼: VirtualDriver Phase 2 テレメトリ（midlong）の JSON 出力 + constraints 追加

並行セッションで VirtualDriver 検証環境 V2（中長期判定の可視化・検証）を実装し、
フロント（v_target チャート + マニューバマーカー）と CLI 検証は完成・待機状態です。
結合に必要な C++ 側の出力を 2 点お願いします。**フロントの整合点は `client.ts` の
`MidLongProfile` 1 箇所のみ**なので、下記キー名・順序に厳密に合わせてください。

## 背景（読むもの）
- メモリ `verification_v2_midlong.md`（V2 全体仕様）
- `GT_esmini/include/gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp`（`MidLongPlannerSnapshot`）
- `GT_esmini/src/control/virtualdriver/VirtualDriverTelemetryJson.cpp`（`ToJson()`）
- `GT_esmini/web/frontend/src/api/client.ts`（`MidLongProfile` / `MidLongConstraint` — フロント側契約）

## 依頼 1: `ToJson()` に `midlong` セクションを追加（必須・優先）

現状 `VirtualDriverTelemetry::midlong` は構造体にあるが JSON 化されていません。
`ToJson()` に以下の形で出力してください（フレーム JSON のトップレベルに `midlong` キー）:

    "midlong": {
      "v_target_profile": [[s, v], [s, v], ...],   // (s[m], v_max[m/s]) のペア配列。順序は必ず [s, v]
      "constraints": [ ... ],                       // 依頼2。当面は空配列 [] でも可
      "valid": true
    }

- `v_target_profile` は `std::vector<std::pair<double,double>>` をそのまま `[[s,v],...]` に。
- `valid=false` のときは出さない or `valid:false` を出す（フロントは degrade 対応済み）。

これが出た時点で `VTargetProfileChart`（実速度 vs v_target(s) 重ね描き）がライブ/replay で動きます。

## 依頼 2: `MidLongPlannerSnapshot` に `constraints[]` を追加

プランナーが選定した制約点（カーブ start / 交差点 / 速度制限変化）を、**ワールド XY 付き**で
スナップショットに持たせ、上記 JSON の `constraints` に出してください。

VirtualDriverTypes.hpp の MidLongPlannerSnapshot に追加:

    struct MidLongConstraint {
        double      s    = 0.0;   // route s [m]
        double      x    = 0.0;   // world position [m]
        double      y    = 0.0;   // world position [m]
        double      v    = 0.0;   // その制約点での目標速度 [m/s]
        std::string kind;         // "curve" | "junction" | "speed_limit" | "stop"
    };
    // std::vector<MidLongConstraint> constraints;

JSON 側:

    "constraints": [
      {"s": 480.0, "x": 500.0, "y": 0.0, "v": 11.0, "kind": "curve"}
    ]

- `kind` 文字列はフロントの `MidLongConstraintKind`（'curve' | 'junction' | 'speed_limit' | 'stop'）と完全一致必須。
- v_target_profile は s のみで XY を持たないため、**マニューバマーカー（LiveSceneView 上の右折地点/カーブ予告）は
  この constraints[].x/y に依存**します。XY 必須。

## 検証ループ（そのまま使える）

シナリオ 05 が `resources/xosc/verification/05_anticipation/` に整備済みです（コントローラ埋め込み済）。
実装後、以下で一括実行 → pre-P2 で fail していた anticipation が pass に転じるか確認できます:

    py GT_esmini/scripts/verification/gt_sim_test.py batch \
       resources/xosc/verification/05_anticipation/anticipation_batch.yaml --out results/anticipation_v2
    # → batch_summary.md / 各 decel_report.png / verdict.json

期待:
- `decelerate_for_curve`（R60, landmark s=500, 目標 ~11 m/s）
- `decelerate_for_right_turn`（fabriksgatan junction R≈8m, landmark s=109 road3, 目標 ~4 m/s）

この 2 つの `speed_reduction_before_landmark` と `deceleration_profile_smooth` が pass になれば Phase 2 OK。
`speed_limit_change` は Phase 1 で既に pass（回帰監視用）。

## 注意（担当範囲）

- xosc / expectations / gt_sim_test / フロントはこちらが保持。**触らないでください**（結合は `client.ts` のみ）。
- 逆に **`MidLongPlannerSnapshot` の中身・`virtual_driver.json` の Phase 2 パラメータ・`ToJson()` は A2 担当**です。
- キー名 `midlong` / `v_target_profile`（順序 `[s,v]`）/ `constraints[]{s,x,y,v,kind}` だけ厳守いただければ、
  フロントは無改修で点灯します。

## 段階分割（任意）

依頼1だけ先行（constraints は後追い）でも `v_target` チャートは動きます。A2 の進捗に合わせて 2 段階でも可:
1. 依頼1（midlong + v_target_profile）→ チャート点灯
2. 依頼2（constraints + XY）→ LiveSceneView のマニューバマーカー点灯

実装後「シナリオ 05 で問題なく走るか（特に R60 カーブと junction 右折で逸脱せず減速できるか）」を
教えてください。Step 3（traffic_lights の青発進→突入逸脱回帰）はそれを受けて着手します。

---

## ✅ 実装報告（A2 担当 / 2026-06-03 / commit 後追記）

両依頼とも実装・配信済み。**フロントは無改修で点灯可能**（キー名・順序を契約どおり厳守）。

### 依頼1（必須）: `midlong.v_target_profile` — 完了
`ToJson()`（[VirtualDriverTelemetryJson.cpp](../../src/control/virtualdriver/VirtualDriverTelemetryJson.cpp)）にトップレベル `midlong` を追加。形状:
```json
"midlong": { "valid": true,
  "v_target_profile": [[s, v], ...],          // [s,v] ペア配列（s=ego からの前方距離[m], v=上限[m/s]）
  "constraints": [ {"s":..,"x":..,"y":..,"v":..,"kind":".."} ] }
```
- C-API `GT_GetVirtualDriverTelemetry` と UDP 48202 ライブ配信は**同一シリアライザ**＝replay/live 同形状。
- ※旧 commit (`d51942f3`) で一時的に `v_target_curve`（`[{s,v}]`）で出していたが、本契約 `v_target_profile`（`[[s,v]]`）に**置換済み**。`client.ts` の `MidLongProfile` と一致。

### 依頼2: `MidLongConstraint{s,x,y,v,kind}` — 完了
- [VirtualDriverTypes.hpp](../../include/gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp) に `MidLongConstraint` 追加、`MidLongPlannerSnapshot::constraints` に格納。
- `ManeuverAwareSpeedPlanner` が前方スキャン中に**ワールド XY 付き**で制約点を選定（連続する同種制約区間ごとに「最も遅い点」を 1 つ）。`kind` ∈ `"curve" | "junction" | "speed_limit"`（`"stop"` は Phase 3 で使用予定、現状未出力）。
- 実測例（fabriksgatan 右折）: `{kind:"junction", s≈104, v≈4.3, xy≈(18.9,-5.4)}`。

### シナリオ 05 走行結果（`gt_sim_test batch`）
| シナリオ | speed_reduction | lane_keep / steer | deceleration_profile_smooth | 総合 |
| :-- | :-- | :-- | :-- | :-- |
| `decelerate_for_curve` (R60, target 11) | **pass** (s=500 で 10.91 m/s) | lane_keep **pass** | **fail** (jerk 10.93 ≤3.0) | fail |
| `decelerate_for_right_turn` (junction R≈8, target 4) | **pass** (s=109 で 4.90 m/s) | steer **pass** (max 0.85) | **fail** (jerk 22.22 ≤4.0) | fail |
| `speed_limit_change`（回帰監視） | **pass** | lane_keep **pass** | **pass** (jerk 2.33) | **pass** |

**質問への回答（R60 / junction で逸脱せず減速できるか）→ YES。**
- カーブ・交差点の **300m 手前から先読み減速**し目標速度に到達（`speed_reduction` pass）。
- **車線逸脱なし**（`lane_keep` pass）・**ステア飽和なし**（max 0.85 < 0.98）。Phase 1 残課題（青発進→交差点突入→誤接続路逸脱）は解消。

### `deceleration_profile_smooth` (jerk) が落ちる根本原因＝**縦制御 PID のブレーキ離し過渡**（A2 範囲外）
- jerk スパイクは**減速完了点**（速度が目標に到達しブレーキを離す瞬間、accel が −2→0 へ ~0.3s で遷移）に発生。**速度参照(v_target)由来ではない**: 参照側を jerk 制限しても actual speed の jerk は減らず、むしろ減速応答が鈍って junction の `speed_reduction` を悪化させたため、参照 jerk 制限は不採用に戻した。
- これは凍結中の `PIDPurePursuitDriver`（縦制御）＋物理のクローズドループ過渡で、`speed_limit_change`（緩減速・直線）が 2.33 で pass する一方、急減速＋旋回を伴うカーブ/交差点では PID のブレーキ離しが jerk 6〜8（実走、末尾録画アーティファクト除く）になる。
- 報告 jerk 値（10.93 / 22.22）には **録画末尾の凍結フレーム**（reaccel 途中で記録終了→中央差分の境界アーティファクト）が含まれ過大。実走の最大 jerk は curve≈6.9 / turn≈7.8。

### ご相談（jerk をどう扱うか — V2 側の判断をお願いします）
A2/Phase2 のスコープ（プランナー＋テレメトリ）では jerk≤3 は達成困難です。次のいずれかを希望されますか:
1. **jerk ゲートを減速フェーズに限定**（landmark 通過後の再加速/ブレーキ離し過渡を除外）— gt_sim_test は V2 保持なので調整をお願いします。
2. **しきい値の緩和**（実走 jerk ~7 を許容、または smooth_window 拡大）。
3. **別タスクで縦制御ドライバを平滑化**（PID→フィードフォワード/2次フィルタ等。現在 `PIDPurePursuitDriver` は凍結指定のため要解凍合意）。

調整パラメータ（A2 既定値）: `max_lateral_accel=2.0`（target 11 に整合）, `comfort_decel=2.0`, `comfort_jerk=1.5`, `scan_distance=300`, `scan_step=2.0`。すべて `config/virtual_driver.json` で外部調整可。
