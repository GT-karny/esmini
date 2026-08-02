# アーカイブ: 完了した工程の記録

工程が終わった時点で凍結した文書を置く。
以後は更新しない。

## 現在の仕様を知りたいなら、ここではない

| 探しもの | 場所 |
| :--- | :--- |
| 現在の設計と判断 | [../design/](../design/) |
| シナリオの書き方 | [../guides/scenario_control_handoff_howto.md](../guides/scenario_control_handoff_howto.md) |
| 立っている数値 | [../measurements/measurement_discipline.md](../measurements/measurement_discipline.md) §8 |

## 収録文書

| ファイル | 内容 | 凍結した理由 |
| :--- | :--- | :--- |
| [phase2_midlong_telemetry_handoff.md](phase2_midlong_telemetry_handoff.md) | Phase 2 の midlong テレメトリ JSON 出力と `constraints[]` の実装依頼、および往復の回答 | 依頼した契約は実装に取り込み済み。キー名と参照系の現行仕様は `VirtualDriverTelemetryJson.cpp` が正典 |
| [phase2_followup_issues.md](phase2_followup_issues.md) | Phase 2 完了時点の残課題2件（交差点直進での不要な減速、追従基準点が後方にある問題） | 2026-06-04 に両件とも解決済み |
| [phase3_firm_stop_defect_report.md](phase3_firm_stop_defect_report.md) | 信号赤と STOP 標識で完全停止せずクロール通過する欠陥の報告と修正の往復 | `3b63d7e6` で修正、検証バッチ 10/10 pass でクローズ |
| [aeb_phase1_implementation_plan.md](aeb_phase1_implementation_plan.md) | AEB フェーズ1の実装計画 | `03e193b0` で `AebSafety` を実装し、回帰ゲートに搭載済み |

Phase 2 と Phase 3 は知識グラフの `vd-phase` 名前空間の正準 ID である
（正典は [../design/roadmap.md](../design/roadmap.md)）。
