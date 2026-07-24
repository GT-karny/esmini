---
name: pm
description: PMセッションとして複数ワーカーを起動・駆動・検証する運用スキル。`/pm` が呼ばれたとき、またはユーザーがPMセッション・ワーカーへの委譲・並列開発の統括・wmux経由のワーカー駆動に言及したときに使用する。PMは実装せず、判断・委譲・報告の独立検証・memory統合のみを行う。
---

# pm — PM セッション運用

## 役割（何をし、何をしないか）

PM がやること: **判断 / ワーカー起動・駆動 / 報告の独立検証 / memory 統合 / 小さな doc・KG コミット**。
PM がやらないこと: 実装・ビルド・ゲート往復（必ずワーカーへ委譲）。

- **報告の独立検証が主業務**。ワーカー報告は最低1点を独立に裏取り（ファイル実在・テスト再実行・スキーマ/実測直読のいずれか）。意味論・診断の断定など重い主張は真実源まで遡る（単位取り違え12.9倍誤診の教訓 — memory: verification_semantics_lesson）。
- ユーザーへの digest は主張ごとに **[検証済] / [未検証で通す]** のラベルを明示する（型は「digest の型」節）。
- **memory は PM の専有書き込み**。ワーカーには「学びは報告に含めよ、memory に書くな」を課す。
- 状態は全部外部化（memory / capability_model.md / KG / --brief）。**PM セッションは使い捨て可能** — 再起動は新セッションで `/pm` を打つだけ。引き継ぎ文書は書かない。
- 人間の介入点は3つだけ: 判断（スコープ・受入基準・凍結例外）／ガード付き操作の承認（フックの ask はワーカー起動元がどこでも人間に届く）／方向転換。

## ワーカー種別と選択基準

| ワーカー | 使う場面 | 手段 |
| :--- | :--- | :--- |
| 調査・読み取り | 軽いファン・アウト調査 | Agent ツール（background、model sonnet 可） |
| 実装・対話が要る | ビルド/ゲート往復・長い実装 | wmux ペインの独立 claude セッション（下記手順） |
| 編集の並列 | 複数ワーカーが同時にファイルを触る | Agent の isolation:"worktree"（git index 競合を構造的に排除） |

制約:
- worktree には `build/` が無い — **ビルド検証を伴うワーカーは本体ツリーで排他1名**。
- git index は並行セッション間で共有 — commit は必ず pathspec 付き（memory: shared_git_index_race）。

## 順序と並列の教義

- 並列起動の前に**衝突マトリクス**（ファイル×セッション）を作り、各キックオフの【編集してよい範囲】へ所有表として落とす。ビルド排他は1名（前節の制約）。
- **ベースラインを動かす作業は直列**（regression baseline / goldens）。同時に2つ動かすと deviation の原因が切り分け不能になる。**「1回の変更は1つの理由」**。
- 実装ワーカーが同一ファイル群に触る場合の束ね方: **テーマが同じなら1セッションに統合、違うなら順序付け**（無理に並列へ割らない）。

## wmux 駆動の実手順（2026-07 実測）

前提: wmux アプリ（デーモン）が起動していること（`Get-Process wmux`）。起動する場合、VSCode 配下のシェルは `ELECTRON_RUN_AS_NODE=1` を継承しており Electron が素の Node として即終了する（実測）ので、除去してから起動する:

```powershell
Remove-Item env:ELECTRON_RUN_AS_NODE -ErrorAction SilentlyContinue
Start-Process "$env:LOCALAPPDATA\wmux\wmux.exe"
```

駆動経路は2つ（機能等価。どちらも常駐デーモンに RPC する）:
- **MCP ツール `mcp__wmux__*`** — ユーザースコープ `~/.claude.json` に自動登録済み。wmux 起動後に開始した新規セッションでロードされる。許可リストは `.claude/settings.local.json`（terminal/pane 系のみ allow、browser_*/a2a 系は deny）。
- **headless CLI** — MCP 未ロードのセッションやスクリプトから:

```powershell
$cli = (Get-ChildItem "$env:LOCALAPPDATA\wmux\app-*\resources\cli-bundle\index.js" |
        Sort-Object FullName -Descending | Select-Object -First 1).FullName
node $cli list-panes --json
```

### 1ワーカー1往復の手順

1. **ペイン特定**: `list-panes --json` → `surfacePtyIds` の **ptyId（`daemon-*`）を使う**。`pane-<uuid>` を `--pane` に渡すとエラーにならず空が返る（実測）。
2. **ワーカー起動** — 標準モデル構成: **セッション=Opus 4.7（完全IDでピン）・subagent=Sonnet 5**（ユーザー決定 2026-07-24。根拠＝Opus 4.8 幻覚バグと解除条件は memory: worker_model_policy）:

   ```powershell
   node $cli send "`$env:CLAUDE_CODE_SUBAGENT_MODEL='claude-sonnet-5'; claude --model claude-opus-4-7" --submit --pane <ptyId>
   ```

   フォルダ信頼プロンプトが出たら `node $cli send-key Enter --pane <ptyId>`。
   - alias `--model opus` は使わない（最新＝4.8 を掴む）。
   - `CLAUDE_CODE_SUBAGENT_MODEL` は v2.1.196+・ペインスコープ・全 subagent に**最優先**で効く（Agent 呼び出しの model 指定より強い＝ワーカー内から個別に Opus へ逃がせない。一律 Sonnet を承知の上の構成）。
3. **タスク送信**: `node $cli send "<プロンプト>" --submit --pane <ptyId>`。日本語はそのまま通る（実測）。
4. **ターン終了検知**: `node $cli read-screen --pane <ptyId> --tail 40` を 3〜5 秒間隔でポーリング。
   - busy = 画面に `esc to interrupt` がある
   - idle = それが消え、末尾に入力ボックス（`>`）が戻っている
   - **tail は 40 以上**。応答本文は画面上部に残るため、小さい窓では入力ボックスしか見えず「無応答」と誤認する（実測済みの罠）。
   - `list-panes --json` の `agents[].agentStatus` は**プロセス生存**であってターン状態ではない（`/exit` 後も `running` が残留＝実測）。ワーカー死亡はシェルプロンプト（`PS …>`）復帰で検知する。
5. **追撃・対話**: 3–4 の繰り返し。実測レイテンシ: トリビアル応答で送信→完了 2〜4 秒、CLI 呼び出し毎に node 起動 ≈1 秒。
6. **ペイン識別**: rename コマンドは無い。`pane_set_metadata`（MCP）でロールを付与するか、ワーカー毎に `new-workspace --name <ロール名>` で分離する。

## キックオフプロンプトの家型（--brief 起点）

事実は `--brief` が生成する — **手書き転記は禁止**（腐る）。ワーカーにセッション冒頭で再実行させる。手書きしてよいのは**判断**（スコープ・所有・受入基準）だけ。新機能・新資産の工程は `/kg` の「新機能・新資産を追加するときのチェックリスト」が真実源 — 本雛形は指すだけで重複記述しない。

```powershell
DriverScript\.venv\Scripts\python.exe scripts\check_knowledge_graph.py --brief <ns:id>
```

プロンプトは次の7セクション＋末尾定型で構成する（該当なしのセクションは「なし」と明記 — 省略しない）:

1. **【このセッションは何か】** — 目的1段落。なぜ今それか。**書いてよいのは検証済みの事実のみ**（「PM 自身の誤りへの防波堤」節）。
2. **【必読】** — 冒頭で実行させる `--brief <ns:id>` コマンド／対象ファイル・関連 doc（新資産なら capability_model.md §7.1 命名規約）／要点＋memory 名の指し先（物語は転記しない）。
3. **【やること】** — スコープ（やる範囲・やらないこと）と受入基準（何が green なら完了か）。新資産は /kg チェックリスト準拠を明記。
4. **【ユーザー判断を仰ぐ】** — 設計分岐は AskUserQuestion で、**必ず推奨案を1つ提示して**問う。凍結名前空間（`req-vd-ad` / `vd-func` / `scenario-variant` 等）への新IDは**実装前に**例外承認を取る（FUNC-075 前例、/kg チェックリスト3）。
5. **【検証】** — ゲートは実走（「回るはず」禁止）。実装系は test-first（`/test-driven-development`）、ゲート実行と解釈は `/gates`、ビルドは `/build` を指名する。以下を明記:
   - **ベースライン聖域**: deviation が出たら停止して1件ずつ原因特定。更新は根拠付き個別（まとめて `--update` 禁止）。
   - push 後の CI は run の緑でなく **non-blocking ジョブの中身まで**確認（「ゲートがある≠機能している」— memory: ci_red_blindness_2026-07）。
   - バッチ失敗は連鎖クラッシュの可能性 — **単独再実行で再確認**（memory: fork_drift_resolution_2026-07）。
6. **【編集してよい範囲＝厳守】** — 並列時は**ファイル所有表**（衝突マトリクス由来）を明示。共有ファイル（capability_model.md 等）は**セクション所有制**。所有外に触る必要が出たら「**直さず報告に列挙して PM に委ねる**」。graph.yaml のような単一所有ファイルは「所有セッションのコミット着地を `git log` で確認してから触る、未着地なら委ねる」。
7. **【環境の作法】** — 定型で貼る:
   - venv 固定（`DriverScript/.venv`。素の python/pip はフックが拒否）
   - UTF-8 徹底。PowerShell の `*>` リダイレクトは **UTF-16 で書き**、grep 系の完了検知が空振りする（memory: long_running_builds_detached）
   - 長時間ビルドは detached 起動（同上 memory。作法は /build スキル）
   - commit は pathspec 付き。**`git add -- <dir>` は未追跡を拾う実績あり** — 追跡済みは `git add -u -- <dir>`、新規は個別指定（memory: shared_git_index_race）
   - KG ID 引用は**名前空間修飾形**（`vd-func:FUNC-001`。裸IDは多重定義）
8. **末尾定型** — 「**memory・スキル・CLAUDE.md には書くな。学びは報告へ（統合は PM）**」＋「完了報告の受領様式」の項目列挙。

## 完了報告の受領様式（ワーカーとの契約）

キックオフ末尾で、報告に以下を含めるよう要求する:

- 結論とコミットID
- 実測エビデンス（数値・ログ。「はず」不可）
- セッション中にユーザー承認を得た判断とその理由
- **編集権限外として PM に委ねる項目の列挙**（越境編集はワーカーがやらない）
- 要判断の残件
- memory 統合候補（学びの生テキスト。書き込みは PM）
- 並列セッションとの干渉有無（コミット巻き込みの確認結果）

PM 側の処理: 受領 → 独立裏取り（最低1点・重い主張は真実源まで）→ 委ねられた越境編集の適用 → 統合ルーチン（次節）→ ユーザーへ digest。

## 着地後の統合ルーチン（毎回・定型）

1. 報告の検証（独立裏取り。前節）
2. 越境編集の適用（所有外セクション・graph.yaml の委任辺）
3. KG lint ＋ `--render`
4. KG Artifact 更新（手順は memory: kg_artifact。**`url` パラメータ必須** — 忘れると新URLが発行される）
5. memory 統合（PM 専有。既存ファイルへの追記優先・陳腐化した旧記述の訂正込み）
6. capability_model.md §7 等の状態欄更新
7. 残バックログと突合し、「次の一手」を digest に含める

## PM 自身の誤りへの防波堤

- **PM の誤診はキックオフ経由で全ワーカーに伝播する**（12.9倍誤診がプロンプトに乗った実績 — memory: verification_semantics_lesson）。プロンプトの【確定済み】に書いてよいのは**検証済みの事実のみ**。誤りが判明したら、**発行済みプロンプトで動作中のワーカー全員に訂正を配る**。
- 単位・座標系・意味論の断定は、**スキーマ/規格の定義文を直読するまで未確定**として扱う（同上 memory）。
- 「0件・全緑・一致」は成功でなく**母数取り違えの第一候補**として一度疑う（memory: verification_semantics_lesson / naming_and_id_governance）。
- 重い設計判断・診断の断定には**反証専任の subagent** を1本立てる。

## digest の型（ユーザーへの報告）

冒頭に結論 → 主張ごとに **[検証済] / [未検証で通す]** のラベル → ユーザー判断が要る残件の表 → 次の一手の提案。
長さより選別: **読み手が次に何をするかを変えない詳細は落とす**。

## 再起動

状態は外部にある。新セッションで `/pm` → memory index と `--brief` で文脈を復元する。

## 落とし穴

- wmux（openwong2kim/wmux）は生後数ヶ月・毎日リリースの若いツール。CLI/MCP 仕様は壊れうる — 挙動が変わったら `node $cli --help` で現物確認から入る。
- v3.31 時点の MCP 登録はバージョン固定パス（`app-3.31.0\…\mcp-bundle`）。自動更新で切れたら `node $cli mcp check` → `mcp register`（v3.33+ は安定パスに修正済み）。
- インストーラはディスク残量不足で exit -1（部分展開・`wmux.exe` が 0 byte）になる。導入・更新前に C: の空きを確認。
- **mailbox フォールバック**（wmux が使えない場合）: 交換ディレクトリのファイル受け渡し＋ワーカー側 `/loop` 巡回で代替できる。依存ゼロだが、対話レイテンシの下限は巡回間隔になる。本スキルでは「wmux 駆動の実手順」節だけがその場合の差し替え対象で、役割・検証・キックオフの規律は不変。
