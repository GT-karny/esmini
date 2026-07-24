---
name: pm
description: PMセッションとして複数ワーカーを起動・駆動・検証する運用スキル。`/pm` が呼ばれたとき、またはユーザーがPMセッション・ワーカーへの委譲・並列開発の統括・wmux経由のワーカー駆動に言及したときに使用する。PMは実装せず、判断・委譲・報告の独立検証・memory統合のみを行う。
---

# pm — PM セッション運用

## 役割（何をし、何をしないか）

PM がやること: **判断 / ワーカー起動・駆動 / 報告の独立検証 / memory 統合 / 小さな doc・KG コミット**。
PM がやらないこと: 実装・ビルド・ゲート往復（必ずワーカーへ委譲）。

- **報告の独立検証が主業務**。ワーカー報告は最低1点を独立に裏取り（ファイル実在・テスト再実行・スキーマ/実測直読のいずれか）。意味論・診断の断定など重い主張は真実源まで遡る（単位取り違え12.9倍誤診の教訓 — memory: verification_semantics_lesson）。
- ユーザーへの digest は主張ごとに **[検証済] / [未検証で通す]** のラベルを明示する。
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
2. **ワーカー起動**: `node $cli send "claude" --submit --pane <ptyId>` → フォルダ信頼プロンプトが出たら `node $cli send-key Enter --pane <ptyId>`。
3. **タスク送信**: `node $cli send "<プロンプト>" --submit --pane <ptyId>`。日本語はそのまま通る（実測）。
4. **ターン終了検知**: `node $cli read-screen --pane <ptyId> --tail 40` を 3〜5 秒間隔でポーリング。
   - busy = 画面に `esc to interrupt` がある
   - idle = それが消え、末尾に入力ボックス（`>`）が戻っている
   - **tail は 40 以上**。応答本文は画面上部に残るため、小さい窓では入力ボックスしか見えず「無応答」と誤認する（実測済みの罠）。
   - `list-panes --json` の `agents[].agentStatus` は**プロセス生存**であってターン状態ではない（`/exit` 後も `running` が残留＝実測）。ワーカー死亡はシェルプロンプト（`PS …>`）復帰で検知する。
5. **追撃・対話**: 3–4 の繰り返し。実測レイテンシ: トリビアル応答で送信→完了 2〜4 秒、CLI 呼び出し毎に node 起動 ≈1 秒。
6. **ペイン識別**: rename コマンドは無い。`pane_set_metadata`（MCP）でロールを付与するか、ワーカー毎に `new-workspace --name <ロール名>` で分離する。

## キックオフ雛形（--brief 起点）

事実は生成させ、手書きは判断のみ（詳細は `/kg`）:

```powershell
DriverScript\.venv\Scripts\python.exe scripts\check_knowledge_graph.py --brief <ns:id>
```

送信プロンプト = ブリーフ出力 ＋ 手書き数行:
- スコープ（やる範囲・やらないこと）
- 受入基準（何が green なら完了か）
- 報告様式（「学びは報告へ。memory・スキル・CLAUDE.md には書くな」）

## 再起動

状態は外部にある。新セッションで `/pm` → memory index と `--brief` で文脈を復元する。

## 落とし穴

- wmux（openwong2kim/wmux）は生後数ヶ月・毎日リリースの若いツール。CLI/MCP 仕様は壊れうる — 挙動が変わったら `node $cli --help` で現物確認から入る。
- v3.31 時点の MCP 登録はバージョン固定パス（`app-3.31.0\…\mcp-bundle`）。自動更新で切れたら `node $cli mcp check` → `mcp register`（v3.33+ は安定パスに修正済み）。
- インストーラはディスク残量不足で exit -1（部分展開・`wmux.exe` が 0 byte）になる。導入・更新前に C: の空きを確認。
- **mailbox フォールバック**（wmux が使えない場合）: 交換ディレクトリのファイル受け渡し＋ワーカー側 `/loop` 巡回で代替できる。依存ゼロだが、対話レイテンシの下限は巡回間隔になる。本スキルでは「wmux 駆動の実手順」節だけがその場合の差し替え対象で、役割・検証・キックオフの規律は不変。
