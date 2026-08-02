# ControllerVirtualDriver — シナリオ・道路オーサリング基盤 設計

| 項目 | 内容 |
| --- | --- |
| ドキュメント種別 | 調査 + 意思決定（プリ実装） |
| 関連ドキュメント | [roadmap.md](./roadmap.md) / [verification_environment.md](./verification_environment.md) |
| 状態 | Draft（調査・推奨確定済み、本実装は次セッション） |
| 最終更新 | 2026-06-06 |
| 対象 Phase | Phase 3d（対向車待ち）/ 3e（無信号交差点）以降の検証シナリオ量産基盤 |

---

## 1. 背景・目的

### 1.1 動機

VirtualDriver の検証は Phase 1〜3a/b/c までは **少数の手書き xosc + 既存 xodr** で回せた（[verification_environment.md](./verification_environment.md) §7、現状 20 シナリオ）。しかし Phase 3d 以降は質が変わる：

- **Phase 3d（対向車待ち・右折）**: 同じ交差点で、対向車の **タイミング・台数・速度・ギャップ** を変えた多数のバリエーションが必要。「ギャップ判断が妥当か」はアノテーション評価（[verification_environment.md](./verification_environment.md) §5.1-B）なので、**判断の分布を見るには物量が要る**。
- **Phase 3e（無信号交差点優先）**: 優先関係の異なる交差点（4-way / T / Y / 優先道路マーク有無）を多種類、それぞれで自車が優先側/非優先側の両ケース。

素手の手書きは破綻する。**シナリオ・道路ジェネレータの基盤**と、「何を網羅すべきか」の**タクソノミー**が要る。

### 1.2 本ドキュメントのスコープ

本セッションは **調査と意思決定の文書化に専念**。プロトタイプ評価のための最小コードのみ実施済み（[§3](#3-scenariogeneration-実力評価レポート)）。本実装・ロードマップ更新は次セッション以降。

### 1.3 確定済み意思決定（本セッションの AUQ）

| 論点 | 決定 | 影響 |
| --- | --- | --- |
| Phase 3e の `<priority>` レコード不在 | **生成後ポスト処理で注入** | 生成基盤に priority インジェクタを1機能追加（[§5.3](#53-priority-ポストプロセッサphase-3e-対応)） |
| 生成物（xodr/xosc）の管理 | **生成スクリプト + 生成物の両方をコミット** | カタログを「生成物ファースト」で構成（[§6.1](#61-ディレクトリ構造)） |
| 層1（3d/3e 必須）の物量 | **中規模（各シーン 10〜30 件）** | **パラメトリック量産がほぼ必須** → 生成器に seed/param スイープ機能を要求 |

---

## 2. 網羅タクソノミー

「現実の交通で起こりうる **道路類型 × シチュエーション類型**」をどこまで網羅するかのカバレッジ設計。**無限に広げず、層1/2/3 で優先度を切る**のが本章の主眼。

### 2.1 道路類型（geometry × signage × lanes）

| # | 類型 | 既存 xodr 資産 | 層 |
| --- | --- | --- | --- |
| G1 | 直線（車線数/幅違い） | `straight_500m*`（plain/roadmarks/signs） | 1 |
| G2 | 単一カーブ（曲率違い） | `curve_r100` / `decelerate_curve_r60` / `crest-curve` | 1 |
| G3 | 連続カーブ | `curves` / `striaghtAndCurves` / `curves_elevation` | 2 |
| G4 | **T 字交差点** | （無し）★生成対象 | **1** |
| G5 | **4-way 交差点** | `fabriksgatan*`（信号付き含む） | **1** |
| G6 | Y 字交差点 | （無し）★生成対象 | 2 |
| G7 | X / 多枝交差点 | `multi_intersections` | 2 |
| G8 | ロータリー / ラウンドアバウト | （無し） | 3 |
| G9 | 高速 合流・分岐・車線減少 | `highway_example_with_merge_and_split` / `two_plus_one` | 2 |
| G10 | 信号（traffic light） | `fabriksgatan_traffic_lights*` | 1 |
| G11 | 一時停止標識（STOP） | `straight_stop_sign` | 1 |
| G12 | 譲れ標識（YIELD） | `straight_yield_sign` | 1 |
| G13 | 優先道路マーク（priority road） | （無し）★生成+post-process | **1**（3e） |
| G14 | LHT / RHT 切替 | `e6mini-lht` / `e6mini` | 2 |
| G15 | 速度制限変化点 | （xosc 側 `speed_limit_change` で代用中） | 2 |
| G16 | 勾配 × 曲率 | `curves_elevation` / `crest-curve` | 3 |
| G17 | トンネル / 特殊構造 | `tunnels` / `velodrome` | 3 |

**現状の穴（層1で埋めるべき）**: 単独の **T 字交差点**（G4）が存在しない。4-way は `fabriksgatan` 系のみで優先関係のバリエーションが無い。**priority road マーク**（G13）を持つ無信号交差点が皆無 → Phase 3e の前提が揃っていない。

### 2.2 シチュエーション類型（actor × intent × timing）

| # | 軸 | バリエーション | 層 |
| --- | --- | --- | --- |
| S1 | Ego 意図 | 直進 / 左折 / 右折 / U ターン / 車線変更 | 直進左右折=1, LC=1, U=3 |
| S2 | 先行車 | 無し / 定速 / 減速 / 停止 / 急停止 | 1（06 で既存） |
| S3 | **対向車** | 無し / 1 台 / 複数 / ギャップ有 / ギャップ無 | **1（3d 本丸）** |
| S4 | 横断車（交差） | 優先 / 非優先 / 信号待ち | 1（3e）/ 2 |
| S5 | 歩行者 | 横断 / 歩道待機 | 3 |
| S6 | 信号 phase | 赤保持 / 赤→青 / 黄判断 / 青通過 | 1（03 で既存） |
| S7 | 緊急 | 飛び出し / 前方急停止 / 割り込み | 2〜3 |

### 2.3 カバレッジ層

#### 層1 — Phase 3d/3e 必須最小セット（本基盤の最初のターゲット）

**道路**: G4(T), G5(4-way priority変種), G13(priority road) を生成で確保。既存 G1/G2/G10/G11/G12 は流用。

**シナリオ**（パラメトリック量産、各 10〜30 件）:

| シーン | 生成パラメータ（スイープ軸） | 想定件数 | 評価 |
| --- | --- | --- | --- |
| `07_oncoming_yield`（3d 右折対向車待ち） | 対向車の {初期距離, 速度, 台数, ギャップ長} | 各交差点 ×10〜30 | アノテーション |
| `08_unsignalized_junction`（3e 優先判断） | {自車が優先/非優先, 交差車の到達タイミング, 交差点類型} | 各類型 ×10〜30 | アノテーション |

> 中規模（各シーン 10〜30 件）を素手で書くのは非現実的 → **パラメトリック生成が層1の成立条件**。

#### 層2 — Phase 4 仕上げ用拡張セット

- G3 連続カーブ / G6 Y字 / G7 X / G9 高速合流分岐 / G14 LHT-RHT 対応 / G15 速度制限
- S1 U ターン / S4 横断車の信号待ち / S7 割り込み・前方急停止
- 混合交通（複数 actor 同時）、エッジケースのアノテーション拡充

#### 層3 — 将来構想（列挙のみ・拡張余地）

- G8 ラウンドアバウト / G16 勾配×曲率の複合 / G17 トンネル内挙動
- S5 歩行者インタラクション / S7 飛び出し
- 自然交通流（SUMO 連携の本格活用、[sumo_ego_awareness メモリ] の延長）
- OpenDRIVE 実地図インポート（実都市データ）

---

## 3. `scenariogeneration` 実力評価レポート

### 3.1 概要

[scenariogeneration](https://pypi.org/project/scenariogeneration/) は esmini 開発者発の Python ライブラリ（pyoscx）。OpenDRIVE(.xodr) と OpenSCENARIO(.xosc) を **相互リンクして生成**する。

| 項目 | 評価結果 |
| --- | --- |
| 最新版 | **0.16.5**（PyPI、評価時点でインストールされたバージョン。release_notes は 0.16.4=2026-03-11 記載） |
| OpenSCENARIO カバレッジ | V1.0 完全、V1.1/V1.2 大半。**既存 .xosc のパースも可能**（V0.7.0〜） |
| xodr 生成 | `CommonJunctionCreator` / `DirectJunctionCreator` / `create_junction_roads` で**ジャンクション自動生成**（接続路・レーンリンク・幾何計算込み） |
| 信号・標識 | `Signal` / `SignalReference` で OpenDRIVE `<signal>` 生成可。crosswalk/stop line の Object Marking 対応 |
| **priority レコード** | **❌ 未サポート**（release_notes に記載なし。プロトタイプ生成 xodr で `<priority>` = 0 件を実測確認） |
| esmini 連携 | `esmini_runner` ヘルパ同梱（headless 実行・timestep 指定対応） |
| 依存 | scipy / lxml / pyclothoids / xmlschema 等（純 Python、pip 一発） |

### 3.2 開発凍結（python_dev_freeze）への抵触判断

**抵触しない。** 凍結対象は **PythonDriverController / Embedded Python / DriverScript の「ランタイム駆動機能」**（[python_dev_freeze メモリ]）。scenariogeneration は：

- **オーサリング/ビルド時ツール**であり、出力は静的な XML（xodr/xosc）。
- 生成物を消費するのは esmini の C++ コア。**GT_esmini ランタイムに Python 依存を一切追加しない**。
- 既存の検証 venv（`DriverScript/.venv`）には既に pyyaml/matplotlib/osi3 が入っており、その**開発ツール系譜の延長**（`gt_sim_test` と同じ立て付け）。

→ pip install は許容範囲内。`DriverScript/requirements.txt` ではなく、**検証ツール用の依存**として別管理するのが筋（[§6](#6-カタログメタデータ設計)）。

### 3.3 プロトタイプ評価（実物）

`scratch/scenariogeneration_eval/gen_t_junction.py` で **T 字交差点 + ego 接近路の信号** を生成し、esmini headless で走破確認した。

**生成コード量とメンテ性**:

| 指標 | 値 |
| --- | --- |
| 生成スクリプト | **81 行**（非空白・非コメント） |
| 出力 xodr | 282 行（6 roads = 3 leg + 3 connecting road、junction 1、signal 1、connection 6） |
| 出力 xosc | 60 行 |
| **比** | **~80 行の可読 Python → 342 行の XML** |

**esmini 互換性（実測）**:
```
esmini.exe --osc t_junction.xosc --headless --fixed_timestep 0.05
→ Loaded OpenDRIVE: t_junction.xodr / Ego teleport OK / 25.05s で stop trigger / EXIT=0
```
ジャンクション接続路・レーンリンク・信号配置がそのまま esmini で読めて走った。**互換性問題なし**。

**学習曲線**: 単純な T 字 + 信号で詰まったのは API シグネチャ2箇所のみ（`Vehicle` が `Axles` ラッパでなく front/rear axle を個別引数、`AbsoluteSpeedAction` の引数順）。`inspect.signature` で即解決。ジャンクションは `add_incoming_road_circular_geometry` + `add_connection` の宣言的記述で、幾何計算は完全に隠蔽される。

### 3.4 限界・ワークアラウンド

| 限界 | 影響 | ワークアラウンド |
| --- | --- | --- |
| **`<priority>` 未生成** | Phase 3e の優先判断が成立しない | **生成後 lxml ポストプロセッサで注入**（決定済み、[§5.3](#53-priority-ポストプロセッサphase-3e-対応)） |
| 信号-停止線-レーンの対応付けは手動 | 信号位置 s/t を自前で指定 | カタログ側でパラメータ化（既存手書き xodr の値を踏襲） |
| OpenSCENARIO の GT 拡張要素は当然非対応 | VirtualDriverController の注入は別途 | 既存 `gt_sim_test._prepare_policy_xosc` と同じ「生成後 XML 注入」パターンを流用 |
| 3D モデル参照などプロジェクト固有の相対パス | esmini が warn（致命的でない） | 生成テンプレでプロジェクト規約のパスを埋め込む |

### 3.5 評価結論

**採用に値する。** ジャンクション幾何の自動計算とパラメトリック生成（量産）の親和性が高く、esmini 互換性は実測で確認済み。唯一の本質的欠落（priority）は決定済みのポストプロセッサで埋まる。GT 固有要素（VirtualDriverController 注入）は既存の XML 後処理パターンと完全に整合する。

---

## 4. 代替案比較

| 観点 | 軽量: Jinja2 テンプレ | **中量: scenariogeneration** | 重量: Web GUI 編集 | ハイブリッド |
| --- | --- | --- | --- | --- |
| 道路（xodr）生成 | ❌ 幾何計算を自前。ジャンクション座標・レーンリンクを手計算は地獄 | ✅ 自動生成 | △ 既存資産の可視編集は可、新規ジャンクション幾何は別途 | ✅ scenariogen |
| シナリオ（xosc）量産 | ✅ テンプレ + ループで容易 | ✅ param スイープ | △ 1件ずつ手作業、量産に不向き | ✅ scenariogen |
| **3d/3e の物量（各10〜30件）** | △ xosc は捌けるが xodr が壁 | ✅ **捌ける** | ❌ 手作業は破綻 | ✅ 捌ける |
| 学習・実装コスト | 低（既存知識） | 中（API 習得済み、プロト実証済み） | **高**（フロント実装が重い） | 中 |
| priority 対応 | 自前 XML 操作 | post-process | 自前 | post-process |
| esmini 互換 | テンプレ次第 | ✅ 実測済み | 既存資産依存 | ✅ |
| メンテ性 | テンプレ肥大化リスク | ✅ 宣言的・型付き | UI 保守が継続コスト | ✅ |
| 既存 Web 資産活用 | — | — | ◎ LiveSceneView 等 | △（将来） |
| dev-freeze 抵触 | 無し | 無し（[§3.2](#32-開発凍結python_dev_freeze-への抵触判断)） | 無し | 無し |

**却下理由**:
- **軽量(Jinja2)**: xosc は書けても **xodr 幾何を手計算する壁**で 3d/3e の道路類型（T/Y/priority交差点）が現実的に作れない。
- **重量(Web GUI)**: 1件ずつの可視編集は **中規模量産（各10〜30件）と本質的に相性が悪い**。実装コストも最大。ただしアノテーション/可視化は別途必要（既存 LiveSceneView 拡張、[verification_environment.md](./verification_environment.md) §3）であり、**「生成」と「閲覧・ラベル付け」は別レイヤー**として扱う。

---

## 5. 推奨アーキテクチャ

### 5.1 採用案: scenariogeneration + priority ポストプロセッサ + パラメトリック量産（生成物コミット）

```
┌─────────────────────────────────────────────────────────────┐
│  road_catalog/ (生成器 + パラメータ)                          │
│    gen_<roadtype>.py  ──┐                                     │
│                          ↓  scenariogeneration                │
│                     <name>.xodr (生成物, コミット)             │
│                          ↓  priority_injector.py (3e のみ)     │
│                     <name>.xodr (+<priority>, コミット)        │
└─────────────────────────────────────────────────────────────┘
                          ↓ roadfile 参照
┌─────────────────────────────────────────────────────────────┐
│  scenario_templates/ (生成器 + param スイープ)                │
│    gen_<scene>.py  ── seed/param sweep ──┐                    │
│                                           ↓                   │
│              <scene>__p001.xosc ... pNNN.xosc (生成物, コミット)│
│                          + <scene>.meta.yaml (生成パラメータ記録)│
│                          + <scene>.expectations.yaml / .annotation_required.yaml │
└─────────────────────────────────────────────────────────────┘
                          ↓
        既存基盤へ: gt_sim_test batch / アノテーション UI（[§7](#7-既存基盤との接続方針)）
```

**採用理由**:
1. **xodr 幾何の自動生成**が 3d/3e の道路類型（T/Y/priority）量産の唯一現実的な手段（[§4](#4-代替案比較)）。
2. **esmini 互換を実測で確認済み**（[§3.3](#33-プロトタイプ評価実物)）。
3. **param スイープ**が中規模量産（各10〜30件）の要件に直結。
4. 唯一の欠落 priority は **薄い lxml ポストプロセッサ**で解決（既存の XML 後処理パターン `_prepare_policy_xosc` と同系）。
5. dev-freeze 非抵触（オーサリング/ビルド時ツール）。

### 5.2 生成器の責務分離

- **road catalog 生成器** (`road_catalog/gen_*.py`): 道路類型1つ = スクリプト1本。パラメータ（曲率・車線数・交差角・信号位置）を引数化。出力は安定 ID 命名（[§6.3](#63-カタログ-id-体系)）。
- **scenario 生成器** (`scenario_templates/gen_*.py`): シーン1つ = スクリプト1本。road catalog の xodr を `roadfile` 参照し、actor タイミング/速度/台数を **seed + param grid** でスイープ。**VirtualDriverController の注入**は生成時 or 既存 `_prepare_policy_xosc` 流の後処理。

### 5.3 priority ポストプロセッサ（Phase 3e 対応）

scenariogeneration が出した xodr を lxml で開き、`<junction>` 配下の各 `<connection>` に対応する `<priority>` レコード（`<road>` 要素の `<junction>` 参照ではなく OpenDRIVE の junction priority）を、**生成パラメータで指定した優先関係**に基づいて注入する。薄い純関数（`road_catalog/priority_injector.py`、推定 50〜100 行）。生成 → 注入 → コミットのパイプラインに1段挟むだけ。

> 補足: roadmap §Phase 3e は「priority 不在時は道路幅/車線数 heuristic で代用」を fallback に挙げているが、本基盤では**生成側で正しい priority を注入できる**ので、3e の検証は heuristic 頼みでなく**真値 priority でテストできる**（heuristic の検証は別途、priority を意図的に抜いた変種で行えばよい）。

---

## 6. カタログ・メタデータ設計

### 6.1 ディレクトリ構造

決定「生成物もコミット」に合わせ、**生成器と生成物を同居**させる（diff 可視・即実行可）。

```
resources/
  xodr/                              # 既存の手書き/流用 xodr はそのまま
  scenario_authoring/                # ★新設: 生成基盤のルート
    road_catalog/
      gen_t_junction.py              # 道路生成器（類型1=1スクリプト）
      gen_4way_priority.py
      priority_injector.py           # 3e priority 注入（共通）
      generated/                     # ★生成物 xodr（コミット対象）
        t_junction__a90.xodr
        4way_priority__main_ns.xodr
        <name>.road.meta.yaml        # 道路メタ（生成パラメータ・類型ID）
    scenario_templates/
      gen_07_oncoming_yield.py       # シーン生成器（param スイープ）
      gen_08_unsignalized.py
      generated/                     # ★生成物 xosc（コミット対象）
        07_oncoming_yield__p001.xosc ... p030.xosc
        07_oncoming_yield__p001.meta.yaml
    requirements-authoring.txt       # scenariogeneration 等（検証ツール依存・dev-freeze外）
    README.md                        # 再生成手順・命名規約
```

> 既存の `resources/xosc/verification/01..08/` は**手書き資産の正典として維持**。生成物は `scenario_authoring/.../generated/` に置き、**バッチ manifest 側で両者を混在参照**できる（[§7](#7-既存基盤との接続方針)）。生成物を `verification/07_*/08_*` に直接吐く案もあるが、手書きと生成物の出自を分けた方がバージョニング（[§6.5](#65-バージョニング)）で破綻しない。

### 6.2 メタデータ schema（YAML）

各生成物に伴走する `*.meta.yaml`（[verification_environment.md](./verification_environment.md) §7 の `*.notes.md` を構造化したもの）:

```yaml
catalog_id: 07_oncoming_yield__p012      # 一意ID（[§6.3]）
kind: scenario                           # road | scenario
road_ref: 4way_priority__main_ns         # scenario の場合の道路カタログ参照
phase: 3d                                 # 関連 VirtualDriver Phase
tests_for: "右折時の対向直進車ギャップ判断"   # 何をテストするためか
expected_behavior: "対向車を待ち、十分なギャップで通過"  # 期待挙動（自然言語）
evaluation: annotation                    # auto(expectations) | annotation
generator:
  script: scenario_templates/gen_07_oncoming_yield.py
  seed: 12
  params: { oncoming_count: 3, oncoming_speed: 11.0, first_gap_s: 2.5, lht: false }
generated_at_commit: <git short hash>     # 再現性
```

道路カタログ側 `*.road.meta.yaml` は `kind: road` + `geometry_type`（タクソノミー G番号）+ `signage` + `priority`（注入有無）を持つ。

### 6.3 カタログ ID 体系

`<category>_<scene>__<variant>` 形式。**アノテーション結果との紐付けキー**（後で過去ラベルとマッチング、[§7](#7-既存基盤との接続方針)）。

- 道路: `<geometry>__<key params>` 例 `4way_priority__main_ns`（南北が優先道路）、`t_junction__a90`（交差角90°）
- シナリオ: `<NN_scene>__p<NNN>` 例 `07_oncoming_yield__p012`。`p###` は param grid のインデックス（seed/params は meta.yaml が正典）。

ID は **ファイル名 = catalog_id** で一致させ、`meta.yaml` の `catalog_id` と冗長に持つ（grep/突合の両対応）。

### 6.4 バリデーション（自動チェック）

生成物が**「esmini で読めて Default で走破できる」**ことを CI/手元で自動確認:

```
validate_catalog.py:
  for each generated xodr/xosc:
    1. esmini.exe --osc <xosc> --headless --fixed_timestep 0.05 → EXIT==0 を確認
    2. Default コントローラで完走（Ego が経路を最後まで走る）を確認
    3. road.meta / scenario.meta の必須フィールド存在チェック
    4. catalog_id とファイル名の一致チェック
  → validate_report.md（Claude が Read 可能）
```

[§3.3](#33-プロトタイプ評価実物) の手順を全件ループ化したもの。`gt_sim_test` の in-process ランナーを流用すれば DLL 経由で高速に回せる。

### 6.5 バージョニング

生成物をコミットするので、**カタログ進化時の過去結果互換**が論点:

- `meta.yaml` の `generated_at_commit` で「どのコードで生成されたか」を記録。
- 生成器を変更したら **catalog_id を据え置きつつ再生成 → diff レビュー**。挙動が変わる変更（道路幾何変更等）は **新 variant ID を切る**（旧 ID のアノテーション結果を温存）。
- アノテーション DB（[annotation_store メモリ系・既存 `annotation_match.py`]）は catalog_id をキーに保持。**ID 不変なら過去ラベル継続、ID 変更なら needs-review に落とす**運用。

---

## 7. 既存基盤との接続方針

| 接続先 | 方針 |
| --- | --- |
| **`gt_sim_test batch`** | 生成物 xosc を batch manifest（[既存 `car_following_traffic_control_batch.yaml` 形式]）に列挙。`generated/` 配下を glob して manifest を自動生成する薄いスクリプトを追加（`scenario_authoring/build_manifest.py`）。policies/osi フラグは meta.yaml の phase から導出可能。 |
| **アノテーション UI** | 生成物の `catalog_id` を登録キーに。既存の新規 `api/annotation.py` / `services/annotation_store.py` / `VerificationAnnotatePage.tsx`（作業ツリーに未コミットで存在）と meta.yaml を接続。1 実行結果 = 1 catalog_id で紐付け。 |
| **`expectations.yaml`** | **単純系（auto 評価）は生成器が expectations も同時生成**できる（landmark s, target_speed 等は生成パラメータから自明）。3d/3e は `annotation_required.yaml` を生成（何にラベルが要るかを宣言）。**自動生成を視野に入れる**が、層1の 3d/3e は評価=annotation なので expectations 自動生成の主戦場は層2の単純系。 |
| **esmini 本家 既存 xodr/xosc** | **取り込む（流用）**: G1/G2/G5/G9/G10/G11/G12/G14 は既存資産で足りる（[§2.1](#21-道路類型geometry--signage--lanes)）→ road catalog から `road_ref` で参照するだけ。**生成対象は穴のみ**（T/Y/priority交差点）。既存の手書き検証 xosc（01〜06）は**正典として残す**、捨てない。 |

---

## 8. 実装ロードマップ（次セッションへの申し送り）

優先度順。**層1（3d/3e 必須）成立までを最初のマイルストーン**とする。

### M-A: 基盤スケルトン（0.5〜1 日）
1. `resources/scenario_authoring/` ディレクトリ + `requirements-authoring.txt`（scenariogeneration 固定版）+ README。
2. プロトタイプ `scratch/scenariogeneration_eval/gen_t_junction.py` を `road_catalog/gen_t_junction.py` に昇格・整形（パラメータ引数化）。
3. `validate_catalog.py`（[§6.4](#64-バリデーション自動チェック)）の最小版。

### M-B: priority ポストプロセッサ（0.5〜1 日）
4. `priority_injector.py`（[§5.3](#53-priority-ポストプロセッサphase-3e-対応)）。4-way / T で優先道路を指定 → `<priority>` 注入。esmini 読込 + roadmanager 抽出（roadmap §Phase 3e）で検証。

### M-C: 層1 道路カタログ（1〜2 日）
5. `gen_4way_priority.py` / `gen_t_junction.py`（交差角・車線・優先関係をパラメータ化）。G4/G5/G13 を生成。
6. road.meta.yaml 自動出力 + catalog_id 命名確定。

### M-D: 層1 シナリオ生成（2〜3 日）
7. `gen_07_oncoming_yield.py`（対向車 param スイープ、各 10〜30 件）。VirtualDriverController 注入。
8. `gen_08_unsignalized.py`（優先/非優先 × タイミング、各 10〜30 件）。
9. scenario.meta.yaml + annotation_required.yaml 同時生成。

### M-E: 既存基盤接続（1〜2 日）
10. `build_manifest.py`（generated/ → batch manifest）。
11. アノテーション UI への catalog_id 登録接続（[§7](#7-既存基盤との接続方針)）。

> M-A〜M-B は Phase 3e 実装（roadmanager priority 抽出）と並行可能。M-C〜M-D は Phase 3d/3e の**検証データ供給**なので、当該 Phase 実装の直前に置く。

---

## 9. 残課題・将来検討事項

| # | 課題 | メモ |
| --- | --- | --- |
| 1 | priority インジェクタの優先関係表現 | 交差点ごとの「どの road が優先か」を road.meta.yaml でどう宣言するか（road_id ペア列挙 vs 高レベル `main_road: ns`）。M-B 着手時に確定。 |
| 2 | param スイープの妥当性（無駄打ち回避） | 各10〜30件を「意味のある分布」にする。ギャップ長を等間隔でなく**判断境界付近を密に**サンプルする設計。アノテーション効率に直結。 |
| 3 | アノテーション負荷 | 中規模（数百件規模になり得る）。UI 側の一括再生・キーボードラベリング等の効率化（[verification_environment.md](./verification_environment.md) §6.3）が前提。 |
| 4 | LHT/RHT の生成軸 | `e6mini-lht` は手書き既存。生成側で LHT 版を出すか、param `lht: true` で対応するか（[driver_view_native メモリ]/[pitch_roll_architecture] とは別系統）。層2。 |
| 5 | 信号-停止線-レーン対応の自動化 | 現状は s/t 手指定。将来 stop line Object Marking と連動させると 3b/3c の生成も楽になる。 |
| 6 | SUMO 連携との棲み分け | 自然交通流は SUMO（[sumo_ego_awareness メモリ]）、決定論的検証は本基盤。3d の「複数対向車」を SUMO で出す案もあるが、**再現性重視なら本基盤の param スイープが優位**。 |
| 7 | expectations 自動生成の範囲 | 層2の単純系でどこまで auto 評価に倒せるか（[verification_environment.md](./verification_environment.md) §11.3「単純系で済むケースを増やす努力」と整合）。 |

---

## 付録: プロトタイプ成果物（評価後削除前提）

- `scratch/scenariogeneration_eval/gen_t_junction.py` — T字+信号 生成スクリプト（81 LOC）
- `scratch/scenariogeneration_eval/t_junction.xodr` / `.xosc` — 生成物（esmini headless 走破確認済み、EXIT=0）

本実装着手時に `road_catalog/` へ昇格（M-A）。scratch は評価用一時物。
