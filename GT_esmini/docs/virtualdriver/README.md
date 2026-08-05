# ControllerVirtualDriver ドキュメント

目的から引くための索引である。
文書の一覧ではないので、探しものが下の表にないときは各ディレクトリを直接見てほしい。

## シナリオを書く

| したいこと | 読むもの |
| :--- | :--- |
| シナリオの中で AI と人のあいだで運転を渡す | [guides/scenario_control_handoff_howto.md](guides/scenario_control_handoff_howto.md) |
| 横は人、縦は AI のように分担させる | 同上 §4（内部の仕組みは [design/domain_split_ownership.md](design/domain_split_ownership.md)） |
| どの `ConfigFile` を書けばよいか調べる | 同上 §7（実機シナリオにヘッドレス用を書く事故がここに集まる） |
| 動く実例を探す | 同上 §10 |

## 走らせる設定を調整する

| したいこと | 読むもの |
| :--- | :--- |
| 手介入の検出感度を自分のホイールに合わせる | [guides/ffb_override_tuning.md](guides/ffb_override_tuning.md) |
| 操舵躍度の上限や AUTO_RESUME の戻り方を変える | 同上 §6, §7 |
| しきい値を下げてよい範囲を知る | 同上 §3（根拠の数値は [measurements/measurement_discipline.md](measurements/measurement_discipline.md) §8） |

## 実機で試す

**どれも先に安全上の注意（各文書の 0 章）を読むこと。**

| 構成 | 読むもの |
| :--- | :--- |
| 横=人 / 縦=AI（ハンドルに力は出ない） | [field-test/realwheel_split_test.md](field-test/realwheel_split_test.md) |
| 横=AI / 縦=人（ハンドルに力が出る） | [field-test/realwheel_reverse_test.md](field-test/realwheel_reverse_test.md) |
| AUTO_RESUME の戻り方を体感で判定する | [field-test/resume_merge_user_check.md](field-test/resume_merge_user_check.md) |
| **実機でしか確かめられない未消化項目**（F7 の残差検出 R-1〜R-3／手動運転中 ADAS の R-4〜R-8） | [field-test/realmachine_open_items.md](field-test/realmachine_open_items.md) |

## 実装する

| 知りたいこと | 読むもの |
| :--- | :--- |
| 所有権と移管まわりを触る前に何を踏むか | [design/control_ownership_pitfalls.md](design/control_ownership_pitfalls.md) |
| VD 全体の構成とフェーズ計画 | [design/roadmap.md](design/roadmap.md) |
| 挙動検証の枠組み（単純系と複雑系の分け方） | [design/verification_environment.md](design/verification_environment.md) |
| シナリオと道路を量産する基盤 | [design/scenario_authoring_foundation.md](design/scenario_authoring_foundation.md) |
| 移管を `ActivateControllerAction` で作ったときの判断 | [design/scenario_control_handoff_design.md](design/scenario_control_handoff_design.md) |
| ドメイン別の所有台帳と upstream 欠陥 A/B | [design/domain_split_ownership.md](design/domain_split_ownership.md) |
| 移管で出た3症状の原因と、実装後も残る制約 | [design/control_ownership_defects.md](design/control_ownership_defects.md) |
| 三角ボタンの AUTO⇄MANUAL トグル | [design/button_mode_toggle_design.md](design/button_mode_toggle_design.md) |
| AUTO_RESUME の合流軌道生成 | [design/resume_merge_trajectory_design.md](design/resume_merge_trajectory_design.md) |
| 機能を動機層と主体で層別する軸 | [design/adas_axis.md](design/adas_axis.md) |
| AEB の要求（NCAP と R152 からの逆算） | [design/aeb_requirements.md](design/aeb_requirements.md) |
| 注釈データセット類似度による自動判定（未実装） | [design/annotation_similarity_design.md](design/annotation_similarity_design.md) |
| VD テレメトリを OSI 拡張へ出さない理由 | [design/osi_telemetry_extension_decision.md](design/osi_telemetry_extension_decision.md) |
| 信号とSTOP標識の停止位置を停止線基準にする | [design/stop_line_stop_target.md](design/stop_line_stop_target.md) |
| 駐車枠の探索・選定と駐車マヌーバの実行、出庫マヌーバ（未実装・設計のみ） | [design/parking_maneuver_design.md](design/parking_maneuver_design.md) |
| 駐車機能の検証観点・刺激資産・matcher拡充計画 | [design/parking_verification_plan.md](design/parking_verification_plan.md) |
| signal分類カタログの国別切替と、新しい国の追加方法 | [guides/signal_country_catalog_howto.md](guides/signal_country_catalog_howto.md) |

## 数値を引く

`measurements/` は測定の記録である。
凍結して扱い、後から書き換えない。

| 探しもの | 読むもの |
| :--- | :--- |
| どの数値が立っていて、どれが撤回済みか | [measurements/measurement_discipline.md](measurements/measurement_discipline.md) §8 |
| 計測を始める前の規律と器具の検定手順 | 同上 §1, §5 |
| AEB の Car-to-Car グリッド採点 | [measurements/aeb_c2c_grid_matrix.md](measurements/aeb_c2c_grid_matrix.md) |
| 実機ホイールで移管を測った一次記録 | [measurements/realwheel_handover_results_2026-07.md](measurements/realwheel_handover_results_2026-07.md) |
| 手動運転中 ADAS の回帰ベースラインを凍結してよいと判断した根拠 | [measurements/manualdrive_adas_determinism_2026-08-06.md](measurements/manualdrive_adas_determinism_2026-08-06.md) |

## 経緯をたどる

完了した工程の記録は [archive/](archive/README.md) にある。
現在の仕様を知りたいときの参照先ではない。

## この構成についての約束

- `guides/` と `field-test/` は現行版だけを書く。過去の経緯は残さない。
- `design/` は判断の記録である。実装が終わっても消さず、状態行に実装コミットを書く。
- `measurements/` は一次証拠である。手順書の改訂で数値が消えないように分けてある。
- 文書名に工程名や序数を使わない。`measurements/` の日付は測定日を指す。
- `design/roadmap.md`、`design/verification_environment.md`、`design/scenario_authoring_foundation.md`、
  `design/adas_axis.md`、`design/aeb_requirements.md`、`measurements/aeb_c2c_grid_matrix.md` は
  知識グラフ（`docs/knowledge/`）から機械参照されている。
  移動や改名をするときは `namespaces.yaml` と `graph.yaml` を同時に直し、`--render` をかけること。
  CI のハードゲートが実在を検査する。
