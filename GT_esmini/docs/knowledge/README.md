# Project Knowledge Graph

開発項目・要求・ナレッジを型付きで繋ぐプロジェクト知識グラフ。ルート `CLAUDE.md` が掲げる "Repository Knowledge Graph" の実体化であり、散文ドキュメントに埋没していたID間関係を機械検証可能な形に固化する。

## 背景（2026-07-16 棚卸しの結論）

リポジトリには **22系統・約450以上のID**（提案P1-P45、負債監査103件、OSC1.4ギャップ136件、ODRプランP0-P10、フォークパッチ/マーカー等）が存在し、関係の大半は散文・コミットメッセージ・暗黙知に散在していた。棚卸しで確定した設計制約:

1. **名前空間衝突は既遂** — `P<n>` は5系統、`PR-` は2系統、`CORE-`/`WEB-` は2系統（tech_debt監査とlogging監査で独立採番）、`R<n>` は3系統（ルートCLAUDE.mdの同一ファイル内でも運用ルールと負債フェーズが衝突）。→ 本グラフでは**全参照に名前空間プレフィックス必須**。
2. **生成できる辺は生成する** — コミット→監査IDの辺は `(SUB-1)` 形式の慣行により約120本が自動抽出可能。散文だけの関係は腐る（例: ODRプランP1-P10の実装史はコミットにしか残っていない）。機械検証されている `[GT_ODR:*]` マーカー体系だけが生き残っている事実が傍証。
3. **提案レイヤーは実装トレーサビリティ皆無** — P1-P45へのコミット言及はゼロ。今後のコミットで `(proposal P<n>)` を書く慣行により前向きに埋める。

## ファイル構成

| ファイル | 役割 | 維持方法 |
| :--- | :--- | :--- |
| `namespaces.yaml` | ID体系レジストリ（22系統+予約枠）と辺型定義。**単一真実源** | 新ID体系の追加時のみ手編集 |
| `graph.yaml` | 手書きの判断系の辺のみ（merged-into / depends-on / concerns 等） | 小さく保つ。導出可能な関係は書かない |
| `concept_vocabulary.yaml` | ASAM OpenX Ontology 概念のコミット済みスナップショット（部分集合） | `openx:*` を参照する辺/ODD軸が必要になったとき随時追加 |
| `path_map.yaml` | パス→ID対応表（統一コミットワークフローの判定材料。mapped/exempt/unknownの3値分類） | 対応が安定して新出したら追記。lintがID妥当性を検証 |
| `graph_view.md` | **人間用ビュー**（Mermaid図＋type別辺一覧）。YAMLを直接読まないこと | **生成物・手編集禁止**。`--render` で再生成 |
| `requirements_vd_ad.yaml` | （予約・未作成）VD自動運転対応シーン要求の着地点 | 要求分析完了後に作成 |

## ノードとエッジ

- **ノード参照**: `<namespace-slug>:<local-id>`（例: `proposal:P13`, `audit-debt:CORE-1`, `audit-log:CORE-1`, `openx:Domain#Roundabout`）。ノード本体は再著述しない — `namespaces.yaml` の `source_of_truth` が指す既存ドキュメントが本体。
- **辺の型**は `namespaces.yaml` の `edge_types` で定義。手書き可能なのは curated のみ。generated（`implements`/`closes`/`refs`）は抽出ツールの出力専用で、`graph.yaml` への手書きはチェッカーが拒否する。

## 運用ワークフロー（R4 — CLAUDE.md §2）

全開発行為は知識グラフを通過する。標準ループと機械強制点:

```
着手前   --query <ns:id> --commits [--issues] で関連コンテキスト取得（/kgスキル）
作業中   コミットにID引用: (F6) (SUB-1) (proposal P13) fixes #30
           └ 強制: ガードフックRule 4（Claude、ID無しはask）+ commit-msgフック（手動、警告）
起票     本文に名前空間付きID必須（例: feature:F2 commit:<sha>）
           └ 強制: ガードフックRule 5（--body-file内容も検査、ID無しはask）+ Web UIはbug.ymlテンプレート
           └ 抽出: --extract-issues（参照の妥当性検査つき、Issue→ID辺の生成）
判断時   curated辺を graph.yaml に追加 → lint + --render
検証     ユニットゲートStep 0 + CI（test job）が lint をハード実行
```

## 検証・照会・抽出

日常運用の手順書は `/kg` スキル（`.claude/skills/kg/SKILL.md`）。

```powershell
# 照会: あるIDに関係するもの（辺・コミット言及）を全部出す — 着手前コンテキスト収集
DriverScript/.venv/Scripts/python.exe scripts/check_knowledge_graph.py --query proposal:P13 --commits
#   裸ID（P13等）は一意に解決できる場合のみ自動補完、曖昧なら候補を提示して停止

# 構造lint（CI: test job Linux/Release でも実行、ハードゲート）
DriverScript/.venv/Scripts/python.exe scripts/check_knowledge_graph.py

# コミット→ID言及の抽出（候補辺の生成。曖昧トークンはambiguousフラグ付き）
DriverScript/.venv/Scripts/python.exe scripts/check_knowledge_graph.py --extract-commits --out test_results/kg_commit_mentions.yaml

# 人間用ビューの再生成（graph.yaml変更後に実行）
DriverScript/.venv/Scripts/python.exe scripts/check_knowledge_graph.py --render
```

チェッカーが強制する不変条件: slug一意・pattern適合・reserved名前空間への参照禁止・curated型のみ手書き可・`openx:*` は `concept_vocabulary.yaml` 収載済みのみ・重複辺禁止・source実在。

抽出器は**推測しない**: `CORE-3` のように複数名前空間にマッチするトークンは `ambiguous: true` で全候補を列挙する（衝突はこのリポジトリの文書化済み特性）。

## OpenX Ontology との関係

`concerns` 辺が開発項目（機能・ポリシー・matcher）とドメイン概念（`openx:Domain#*`）を橋渡しする。これは将来の ODDカバレッジ台帳（提案 P13）の軸定義と、Web UI の意味検索タグの共通基盤になる。注意: OpenX v1.0 はプレリリースで **OSI未整合**・`TrafficLight` クラス無し（`DynamicTrafficSign` で暫定対応）。詳細は `concept_vocabulary.yaml` 冒頭のcaveats参照。

## 将来拡張: VD自動運転対応シーン要求（予約済み・シーン起点2段構え）

要求は「対応すべきシチュエーション」から導出する（2026-07-17方針決定）。手順:

**第1段: シーンカタログ**（次セッションで着手）
1. 対応すべき走行シチュエーションを列挙し `scene_catalog_vd_ad.yaml` にシーンノード（`scene:SCN-<nnn>`）として起こす。列挙の軸はOpenX傘構造3本 = 道路トポロジー × 環境条件 × 交通参加者/行動（P13のODD軸と同一基盤 → 後のカバレッジ計測に直結）
2. シーン→`concerns`→OpenX概念で分類。既存資産との突合: 検証シナリオカテゴリ（01-09）、検証バッチ（car_following_traffic_control / junction_conflict 等）、道路カタログのgeometry_type
3. `namespaces.yaml` の `scene` を `status: active` へ

**第2段: 要求導出**
4. 各シーンから要求を導出し `requirements_vd_ad.yaml`（`req-vd-ad:REQ-AD-<nnn>`、statement / rationale / acceptance）へ。要求→シーンの辺で導出元を保持
5. `req-vd-ad` を `status: active` へ。機能/ポリシー→`realizes`→要求、matcher/expectations→`verifies`→要求
6. 完成形: 「概念 → シーン → 要求 → 実装 → 検証 → バグ」の縦串。「`Domain#Roundabout` に関わるシーン・要求・実装・テスト・Issueを全部出す」が引ける（提案P14の実質実現）

## コミット慣行（前向きの辺の自動化）

- 監査ID・F機能・R/Uフェーズは従来どおり `(SUB-1)` `(F6)` `(R5-U3)` をコミットメッセージに記載（既に定着）
- **新規**: 提案に紐づく作業は `(proposal P<n>)` と書く — 抽出器はこの形式のみを proposal 辺として拾う（裸の `P<n>` はODRプランP0-P10等と衝突するため）
