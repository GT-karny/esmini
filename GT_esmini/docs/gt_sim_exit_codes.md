# GT_Sim exit code とストリーム規律

logging_audit_2026-07-11.md §6 Phase 3-2/3-3 の実装仕様（`GT_esmini/GT_Sim/main.cpp`）。

## Exit code

| code | 意味 | 発生箇所 |
| :--- | :--- | :--- |
| **0** | 成功（シナリオ完走、または `--help` 表示） | メインループ正常終了 / help 表示 |
| **1** | シナリオ初期化・実行失敗 | `GT_InitWithArgs` が非 0 を返した |
| **2** | 引数エラー（usage 表示を伴う） | 引数なし起動（`argc < 2`） |

main.cpp の無名 enum `GT_SIM_EXIT_OK / GT_SIM_EXIT_FAILURE / GT_SIM_EXIT_USAGE` で定義。

注意: コア esmini（esmini.exe / SE_ API）の exit code は失敗種別を問わず一律 -1
（監査 CORE-6、受容）。本規律は GT_Sim.exe のみに適用される。

## ストリーム規律

| ストリーム | 内容 |
| :--- | :--- |
| **stdout** | usage/ヘルプ、起動バナー、キャプチャ結果行（`GT_Sim: Captured frames = N`） |
| **stderr** | 警告・エラー・進捗診断のすべて。**失敗時は最終行に `ERROR: <原因 1 行>`** |

失敗時の `ERROR:` 行は `GT_GetLastError()`（GT_esminiLib の Phase 2 新 API、
error レベル最終ログメッセージを返す）から取得する。詳細が無い場合は
`ERROR: Failed to initialize GT_esmini (no detail available)` にフォールバックする。

機械監視の指針: 失敗判定は exit code、原因特定は stderr 最終 `ERROR:` 行
（または log.txt 末尾の `[error]` 行）。stdout の error 文字列 grep は使用しない（CORE-9）。
