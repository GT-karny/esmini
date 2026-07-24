---
name: kg
description: プロジェクト知識グラフ（GT_esmini/docs/knowledge）の運用。`/kg` が呼ばれたとき、またはユーザーが知識グラフ・オントロジー・辺/関係の追加・OpenX概念・関連コンテキストの照会・要件トレーサビリティに言及したとき、および提案/監査ID/Issue/新機能に紐づく作業の着手前コンテキスト収集に使用する。
---

# kg — プロジェクト知識グラフの運用

## 前提

- 実体は `GT_esmini/docs/knowledge/`:
  - `namespaces.yaml`（ID体系レジストリ=単一真実源）/ `graph.yaml`（手書き辺）/
    `concept_vocabulary.yaml`（OpenX概念スナップショット）/ `graph_view.md`（**生成物・手編集禁止**）
  - **ノードカタログ**（各名前空間の source_of_truth）: `signal_catalog.yaml`（観測可能量・
    exposure/state 付き）/ `gate_catalog.yaml`（常設ゲート・covers/blocking 付き）/
    `claim_domains.yaml`（§2 行列の行定義）/ `function_catalog_vd_ad.yaml`（FUNC-001..075）/
    `requirements_vd_ad.yaml` / `scene_catalog_vd_ad.yaml`
  - 設計は同ディレクトリ README.md、**検証スパイン・命名規約は `capability_model.md`（§1/§4-§7.1）**。
- Pythonは常に `DriverScript/.venv/Scripts/python.exe`。
- **裸のIDは信用しない**: `P<n>`は5系統、`CORE-<n>`/`R<n>`等も多重定義。参照は必ず `名前空間:ローカルID`。
- **R4（CLAUDE.md §2）: 全開発行為はこのグラフを通過する。** 着手前クエリ → コミットにID引用 → 判断辺をgraph.yamlへ、が標準ループ。機械強制点は4つ: ①ガードフックRule 4 ②commit-msgフック（どちらも `scripts/check_commit_kg_ids.py` に委譲＝**受理文法は namespaces.yaml から生成**。ID引用なしはask/警告、wip/merge/fixup/--amend免除）③ユニットゲートStep 0のlint ④CIハードゲート。

## コマンド

```powershell
$kg = "scripts/check_knowledge_graph.py"
$py = "DriverScript/.venv/Scripts/python.exe"

# 照会（着手前コンテキスト収集。裸IDは一意なら自動解決、曖昧なら候補提示）
& $py $kg --query proposal:P13 --commits    # --depth N（既定2）/ --issues も可

# 構造lint（CI test job でハードゲート。値域チェック・命名規約4・spine件数サマリ含む）
& $py $kg

# ビュー再生成（namespaces/graph/カタログを編集したら必須 — lintがハッシュで陳腐化を検出）
& $py $kg --render

# 未検証台帳（「縦串の切れた列」④(a)/(b)・②刺激欠・⑥常設欠・coupling。報告のみ＝設計上ずっと非空）
& $py $kg --spine-report

# 能力モデル行列の生成ビュー（claim_domains × スパイン①-⑥。§2 手書き行列の後継）
& $py $kg --spine-matrix

# コミット前のID判定（下記「コミット＝統一窓口」）
& $py $kg --suggest

# コミット→ID言及の候補辺抽出
& $py $kg --extract-commits --out test_results/kg_commit_mentions.yaml
```

## 新機能・新資産を追加するときのチェックリスト（機能実装セッションはここから）

kg スキルは記帳作法。**「何を結線すれば検証可能な機能になるか」は検証スパイン**
（capability_model.md §1: ①主張→②刺激→③実装→④観測→⑤判定→⑥常設）が定める。

1. **着手前**: `--query` で関連ノード把握。`function_catalog_vd_ad.yaml` の該当 FUNC の
   status/layer/kind を確認（enabler なら `verify:` が④⑤の当て方を指定している）。
2. **命名（§7.1・lint が機械強制）**: 新資産・新IDに**序数・工程名を使わない**
   （`phase2_*`/`wave3_*` 不可、内容slugで）。恒久資産の由来は名前でなく `origin:` メタデータ。
3. **凍結の罠**: `scenario-variant` / `req-vd-ad` / `vd-func` 等の既存序数体系は
   「新IDを足さない」凍結（check_knowledge_graph.py の OPAQUE_LEGACY 側）。**新IDが必要なら
   実装前に AskUserQuestion で例外承認を取る**（FUNC-075 の前例）。新しいID*体系*の
   序数パターン新設は lint が拒否する（規約4）。
4. **結線**（graph.yaml、縦串3辺＋既存辺）: 刺激資産→ `stimulated-by`、matcher→ `observes` →signal、
   req/matcher→ `sustained-by` →gate、実装→ `realizes`、matcher→ `verifies`。
   新しい観測量は `signal_catalog.yaml` に exposure/state 付きで起こす（on-demand、
   canonical は OSI/HVD 面）。新しい常設ゲートは `gate_catalog.yaml`（covers/not_covers を正直に）。
5. **確認**: lint + `--render` グリーン → `--spine-report` の件数が意図どおり動いたか
   （辺を張ったのに減らない＝結線の書き方が台帳の数え方と合っていない）。

## 運用手順

### 着手前コンテキスト収集（このスキルの主目的）
提案/監査ID/Issue/機能に紐づく作業を始める前に `--query <ID> --commits` を実行し、依存・統合・過去コミットを把握してから着手する。

### Artifactビューの更新
graph.yaml / namespaces.yaml / concept_vocabulary.yaml を変更したturnの締めに、`--render` 後の graph_view.md を**既存の同一Artifact URLへ再公開**する（URLと詳細手順はClaudeメモリ `kg-artifact` にある — 公開リポジトリのためURLはGit管理しない。別セッションからは `url` パラメータ必須、忘れると新URLが発行される。「latest を未閲覧」エラーが出たら WebFetch で現物を確認してから再公開＝生成ビュー同士なら新しい render が正）。

### 辺の追加（graph.yaml）
1. 追加できるのは **curated型のみ**: merged-into / supersedes / depends-on / shares-design-with / complements / conflicts-with / concerns / informed-by / upstream-candidate / realizes / verifies ＋ **縦串3辺 stimulated-by / observes / sustained-by**。implements/closes/refs は抽出専用 — 手書きするとlintが拒否する。
2. 書くのは「導出できない判断系の関係」だけ。コミットメッセージやマニフェストから導出できる関係は書かない（腐るため）。
3. `note:`（なぜその関係か）と `source:`（関係が述べられている文書）を付ける。根拠は「実際に発火/観測する」こと — 名前の類似で張らない（名前は嘘をつく）。
4. 編集後: lint → `--render` → 両方グリーンを確認。

### OpenX概念の追加（concept_vocabulary.yaml）
`openx:Domain#X` を辺で参照する前に語彙へ収載（lintが強制）。出典は `thirdparty/ASAM_OpenXONTOLOGY_BS_*.zip` 内の `OpenXOntology.ttl`（346クラス、再配布可ライセンス）。一括インポートせず必要時に追加。**v1.0はOSI未整合・TrafficLightクラス無し**（DynamicTrafficSignで暫定）等のcaveatsは語彙ファイル冒頭を参照。

### 新しいID体系の登録（namespaces.yaml）
slug / id_pattern（fullmatch正規表現）/ source_of_truth / status / **face タグ（1|2|3|cross）** を定義。
- **規約4（§7.1）: id_pattern を序数・連番だけで構成しない**（`Phase[0-4]` 等は lint が新設を拒否）。内容slugにする（例: `spine-work` の `[a-z][a-z0-9]*(-[a-z0-9]+)+`）。**順序は ID でなく depends-on 辺で表す**。
- パターンは他名前空間との偽マッチを避けて絞る（例: audit-osc14はCamelCase 2コブ以上を要求）。登録後 lint + `--render`。commit-msg フックは namespaces.yaml から文法を生成するので自動追随する。

### Issue連携（起票側ハーネス）
- 起票は `gh issue create -R GT-karny/esmini`。**本文に名前空間付きIDを必ず含める**（例: `feature:F2 policy:conflict commit:<sha>`）— ガードフックRule 5が本文（`--body-file`はファイル内容ごと）を検査し、ID無しはask。Web UI起票はテンプレート `.github/ISSUE_TEMPLATE/bug.yml` の関連ID欄が同じ役割。
- 起票前に `--query` で関連ノードを特定してから本文に書く（着手前クエリと同じ習慣）。
- 修正コミットは `fixes #<n>` — GitHub自動クローズ + `--extract-commits` の両方で辺になる。
- `--extract-issues` でIssue本文の参照を辺として抽出＋妥当性検査。`--query <ref> --issues` で照会にIssue言及を含められる（要gh・ネットワーク）。

### コミット＝統一窓口（KG判断を人間に依頼しない）
コミット前に必ず `--suggest` を実行し、3値判定に従う:
- **mapped** → 提示された候補IDのうち妥当なものをメッセージに引用。**推奨形式は名前空間修飾**: `vd-func:FUNC-001` `signal:ego_lane` `spine-work:vertical-wiring` `fixes #<n>`（裸の `F6`/`SUB-1` 等の legacy 形も通るがフックが「修飾せよ」の hint を出す。裸の序数 `Phase3` 等は所属明記の hint）
- **exempt** → ID無しでそのままコミット（ガードRule 4も黙る）
- **unknown** → 自分で判定: 「半年後に背景を探す変更か？」— YESなら関連IDを探して引用（無ければIssue起票を検討）、NO（GUI微修正・typo級）ならID無しで通しaskを自己承認
**この判定をユーザーに質問しないこと**。迷ったらIDを付けない側に倒す（誤った辺 > 欠けた辺）。候補IDの自動挿入はしない（精度の防波堤）。
パスと概念の対応が安定して新出したら `path_map.yaml` に追記する（lintがID妥当性を検証）。

## 落とし穴

- graph.yaml / namespaces.yaml / カタログを編集して `--render` を忘れる → **lint/CIが落ちる**（ハッシュ照合）。lintのSTALEメッセージに従い再生成する。
- 抽出結果の `ambiguous: true`（例: CORE-3 = audit-debt|audit-log）を機械的にどちらかへ倒さない — 曖昧なまま扱うのが仕様。
- graph_view.md への直接編集・generated型辺の手書き・reserved名前空間への参照は、いずれもlintが検出して拒否する。
- `--spine-report` の件数が「0＝クリーン」に見えたら疑う — **0件は検知器が母数を取り違えている第一候補**（台帳は真実源から数える。graph出現で代用しない）。
- 検査・語彙を変えたら**意図的な違反データで発火を実証**してから完了とする（`scripts/test_check_knowledge_graph.py` に恒久化。反転した検知器は無いより悪い）。
