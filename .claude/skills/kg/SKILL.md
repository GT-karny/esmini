---
name: kg
description: プロジェクト知識グラフ（GT_esmini/docs/knowledge）の運用。`/kg` が呼ばれたとき、またはユーザーが知識グラフ・オントロジー・辺/関係の追加・OpenX概念・関連コンテキストの照会・要件トレーサビリティに言及したとき、および提案/監査ID/Issueに紐づく作業の着手前コンテキスト収集に使用する。
---

# kg — プロジェクト知識グラフの運用

## 前提

- 実体は `GT_esmini/docs/knowledge/`: `namespaces.yaml`（ID体系レジストリ=単一真実源）/ `graph.yaml`（手書き辺）/ `concept_vocabulary.yaml`（OpenX概念スナップショット）/ `graph_view.md`（**生成物・手編集禁止**）。設計とルールは同ディレクトリの README.md。
- Pythonは常に `DriverScript/.venv/Scripts/python.exe`。
- **裸のIDは信用しない**: `P<n>`は5系統、`CORE-<n>`/`R<n>`等も多重定義。参照は必ず `名前空間:ローカルID`。
- **R4（CLAUDE.md §2）: 全開発行為はこのグラフを通過する。** 着手前クエリ → コミットにID引用 → 判断辺をgraph.yamlへ、が標準ループ。機械強制点は4つ: ①ガードフックRule 4（ID引用なしの`git commit -m`はask。wip/merge/fixup/--amend免除）②commit-msgフック（手動コミットに警告、`cp scripts/git-hooks/commit-msg .git/hooks/`で導入）③ユニットゲートStep 0のlint ④CIハードゲート。

## コマンド

```powershell
$kg = "scripts/check_knowledge_graph.py"

# 照会（着手前コンテキスト収集。裸IDは一意なら自動解決、曖昧なら候補提示）
DriverScript/.venv/Scripts/python.exe $kg --query proposal:P13 --commits
#   --depth N（既定2）/ --commits でコミット言及も表示

# 構造lint（CI test job Linux/Release でハードゲート）
DriverScript/.venv/Scripts/python.exe $kg

# ビュー再生成（graph.yaml / namespaces.yaml を編集したら必須 — lintがハッシュで陳腐化を検出）
DriverScript/.venv/Scripts/python.exe $kg --render

# コミット→ID言及の候補辺抽出（曖昧トークンは ambiguous:true で全候補列挙）
DriverScript/.venv/Scripts/python.exe $kg --extract-commits --out test_results/kg_commit_mentions.yaml
```

## 運用手順

### 着手前コンテキスト収集（このスキルの主目的）
提案/監査ID/Issue/機能に紐づく作業を始める前に `--query <ID> --commits` を実行し、依存・統合・過去コミットを把握してから着手する。

### Artifactビューの更新
graph.yaml / namespaces.yaml / concept_vocabulary.yaml を変更したturnの締めに、`--render` 後の graph_view.md を**既存の同一Artifact URLへ再公開**する（URLと詳細手順はClaudeメモリ `kg-artifact` にある — 公開リポジトリのためURLはGit管理しない。別セッションからは `url` パラメータ必須、忘れると新URLが発行される。メモリ喪失時は Artifact list で検索）。

### 辺の追加（graph.yaml）
1. 追加できるのは **curated型のみ**（merged-into / supersedes / depends-on / shares-design-with / complements / conflicts-with / concerns / informed-by / upstream-candidate / realizes / verifies）。implements/closes/refs は抽出専用 — 手書きするとlintが拒否する。
2. 書くのは「導出できない判断系の関係」だけ。コミットメッセージやマニフェストから導出できる関係は書かない（腐るため）。
3. `note:`（なぜその関係か）と `source:`（関係が述べられている文書）を付ける。
4. 編集後: lint → `--render` → 両方グリーンを確認。

### OpenX概念の追加（concept_vocabulary.yaml）
`openx:Domain#X` を辺で参照する前に語彙へ収載（lintが強制）。出典は `thirdparty/ASAM_OpenXONTOLOGY_BS_*.zip` 内の `OpenXOntology.ttl`（346クラス、再配布可ライセンス）。一括インポートせず必要時に追加。**v1.0はOSI未整合・TrafficLightクラス無し**（DynamicTrafficSignで暫定）等のcaveatsは語彙ファイル冒頭を参照。

### 新しいID体系の登録（namespaces.yaml）
slug / id_pattern（fullmatch正規表現）/ source_of_truth / status を定義。パターンは他名前空間との偽マッチを避けて絞る（例: audit-osc14はCamelCase 2コブ以上を要求）。登録後 lint + `--render`。

### Issue連携（起票側ハーネス）
- 起票は `gh issue create -R GT-karny/esmini`。**本文に名前空間付きIDを必ず含める**（例: `feature:F2 policy:conflict commit:<sha>`）— ガードフックRule 5が本文（`--body-file`はファイル内容ごと）を検査し、ID無しはask。Web UI起票はテンプレート `.github/ISSUE_TEMPLATE/bug.yml` の関連ID欄が同じ役割。
- 起票前に `--query` で関連ノードを特定してから本文に書く（着手前クエリと同じ習慣）。
- 修正コミットは `fixes #<n>` — GitHub自動クローズ + `--extract-commits` の両方で辺になる。
- `--extract-issues` でIssue本文の参照を辺として抽出＋妥当性検査（存在しない名前空間・pattern不一致・語彙未収載openxを `problem:` 付きで報告、invalid有りはexit 1）。`--query <ref> --issues` で照会にIssue言及を含められる（要gh・ネットワーク）。

### コミット＝統一窓口（KG判断を人間に依頼しない）
コミット前に必ず `--suggest` を実行し、3値判定に従う:
- **mapped** → 提示された候補IDのうち妥当なものをメッセージに引用（`(F6)` `(SUB-1)` `(R5-U3)` `(proposal P<n>)` `fixes #<n>` 形式。裸のP<n>は衝突のため抽出対象外）。候補が実態に合わなければunknownとして判断
- **exempt** → ID無しでそのままコミット（ガードRule 4も黙る）
- **unknown** → 自分で判定: 「半年後に背景を探す変更か？」— YESなら関連IDを探して引用（無ければIssue起票を検討）、NO（GUI微修正・typo級）ならID無しで通しaskを自己承認
**この判定をユーザーに質問しないこと**。迷ったらIDを付けない側に倒す（誤った辺 > 欠けた辺）。候補IDの自動挿入はしない（精度の防波堤）。
パスと概念の対応が安定して新出したら `path_map.yaml` に追記する（lintがID妥当性を検証）。

### VD自動運転シーン要求の着地（予約済み・未着手）
分析完了時: ① `requirements_vd_ad.yaml` に `req-vd-ad:REQ-AD-<nnn>` ノードを起こす → ② namespaces.yaml の `req-vd-ad` を `status: active` へ → ③ 要求→`concerns`→OpenX概念、機能/ポリシー→`realizes`→要求、matcher→`verifies`→要求 で接続。手順詳細は knowledge/README.md §将来拡張。

## 落とし穴

- graph.yaml / namespaces.yaml を編集して `--render` を忘れる → **lint/CIが落ちる**（ハッシュ照合）。lintのSTALEメッセージに従い再生成する。
- 抽出結果の `ambiguous: true`（例: CORE-3 = audit-debt|audit-log）を機械的にどちらかへ倒さない — 曖昧なまま扱うのが仕様。
- graph_view.md への直接編集・generated型辺の手書き・reserved名前空間への参照は、いずれもlintが検出して拒否する。
