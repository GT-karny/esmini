# 検証可能性 能力モデル（repo横断）— 設計DRAFT

> **status: DRAFT（フェーズ0・KG本体非改変）**
> 立ち位置は `adas_axis.md` / `scene_survey_openx.md` と同じ「カタログ手前の設計軸」。
> 本書は **graph.yaml / namespaces.yaml をまだ改変しない**。§4-§5 の新namespace/新辺は
> 承認後に登録する（登録後は必ず lint + `--render`）。
>
> **動機**: このリポジトリは *シミュレータ* である。ゆえに完全性の問いは
> 「その運転機能が**在るか**（横軸）」だけでなく「その挙動を**刺激・観測・判定・常設**できるか
> ＝検証可能か（縦軸）」の2次元。機能インベントリ（`vd-func` §横軸, B1-B6ギャップ）は
> 前者。本書は後者＝**検証スパイン**を定義し、repo全体の主張ドメインに縦串を通す
> 能力モデルを与える。

## 0. 3面アーキテクチャと結合契約（最上位フレーム）

このリポジトリは一枚岩ではなく、**独立に価値を持つ3つの面**として設計・保守すべき。
各面は差し替え/単体利用できることが望ましく、面間は **正規インターフェースだけ** で結ぶ。

| 面 | 標準役割 | 本質 | 単体価値 |
| :-- | :-- | :-- | :-- |
| **面1 OSI吐き出しOpenX対応シム** | World / テストベンチ | 世界(OpenDRIVE/OpenSCENARIO/OpenX)をモデル化し GroundTruth(OSI)を吐く**実行エンジン** | SUT無しでも「OSI/OpenXシム」として製品 |
| **面2 自動運転VD** | SUT（被検体） | 運転挙動スタック（B1-B6の穴はここ） | *just-another-controller* として差し替え可能 |
| **面3 自動運転検証環境** | Assessment / オラクル | 刺激（テスト資産）・観測・判定・常設。**面1と面2の両方を測る** | 別ADスタック/別シムにも向けられる harness |

### 0.1 スパイン層の面所有（②の継ぎ目を決着）

- **③実装** ＝ 面2（Face1自身がSUTのときは面1エンジン）
- **④観測** ＝ 面1（OSI/HVD emit）
- **①主張・⑤判定・⑥常設** ＝ 面3
- **②刺激** ＝ **決着: シナリオ/道路"資産"は面3のテスト資産**（面1の適合を試すODR fixtures も、面2を試すVDシナリオも、等しく検証資産）。**"任意の妥当な xosc/xodr を正しく実行し OSI を吐く"能力は面1**（そしてそれ自体が面3の検証対象たる面1の主張）。

→ 帰結: **本書全体は面3の視点**。§2 行列の "主張ドメイン" は **面1∪面2 の主張**、①⑤⑥列は全部面3。面3が面1(適合)と面2(挙動)を測る地図である。

### 0.2 正規インターフェースと結合契約

| 越境 | 正規インターフェース |
| :-- | :-- |
| 面1 → 面2 | controller / sensor API（シムがSUTを駆動、SUTが世界に作用） |
| 面1 → 面3 | **OSI GroundTruth / HVD ストリーム（＝`signal`）が唯一の観測IF** |
| 面2 → 面3 | **直結禁止**。面3は面2を面1のOSI越しに黒箱として観測する |

**契約**: 面をまたぐ辺は上記IFノードを経由必須。**面3→面2の直結 ＝ coupling debt**。

### 0.3 現状の結合負債（"密結合しすぎ"の実体）

- **面3が面2のVDテレメトリ・ブリッジ(48202, `telemetry.jsonl`)を主観測源**にしている
  （`vd_recorder`→`vd_metrics`）＝面3⇄面2直結、OSI(面1)迂回。**pitch/roll誤認はこの症状**。
- 検証資産が `*_vd_ad` 命名 ＝ 面3に面2(VD)が焼き込み（SUT非依存でない）。
- web backend が VD設定と intertwined。
- **対照（手本）**: 面3が面1を測る系（ODR conformance schema/RM/OSI、census/drift）は
  OSI/スキーマ経由で**綺麗に脱結合済み**。behavioral系(面3⇄面2)へこのやり方を移植するのが目標。

### 0.4 面の対称性

面3は2方向に向く。両者を**同じ観測IF(OSI/HVD)に統一**すれば検証環境がSUT非依存になる:
- **面3 → 面1（適合検証）**: OSI/schema経由でクリーン（＝§3 "最強スパイン" の正体）。
- **面3 → 面2（挙動検証）**: 現状テレメトリ直結で結合過多（＝負債）。

### 0.5 KG表現

各namespaceに **`face:` タグ**（`1`|`2`|`3`|`cross`）を付す:

| 面 | namespace |
| :-- | :-- |
| **面1** | `openx` `odr-*` `fork-*` `lineage` `signal`(OSI出力契約) |
| **面2** | `vd-func` `policy` `vd-phase` |
| **面3** | `req-vd-ad` `matcher` `gate` `scenario-variant` `scene` `vd-verif` `f1-milestone` |
| **cross** | `feature`(F1-6) `proposal` `audit-*` `directive` |

**coupling-audit（派生lint）**: 面をまたぐ辺が正規IFノードを経由しているか検査。
面3→面2直結辺、面3が非`signal`を観測する経路を **debt** として列挙。

## 1. 検証スパイン（縦軸・6層）

シミュレータが「この挙動を再現している」と主張するとき、機能ごとに次の縦串が
通っていて初めて **検証可能な主張** になる。1層でも欠けた列は「裏付けの無い主張」。

| # | 層 | 問い | 既存の担い手（KG名前空間 / 資産） |
| :- | :-- | :-- | :-- |
| ① | **主張・受入** | 何を再現すると言い、定量的合否基準は？ | `req-vd-ad` / `feature` / NCAP・R152出典 |
| ② | **刺激・シナリオ** | その状態に追い込む xosc/xodr を作れるか | `scene` / `scenario-variant` / `policy`(xosc) / `f1-milestone` / roadgen |
| ③ | **実装・挙動** | コントローラがその挙動を出すか | `vd-func` / `policy` / 各Controller |
| ④ | **観測・テレメトリ** | その量が **OSI GroundTruth / HVD** に emit され、消費面に届いているか | **未一級市民**（`signal` として §4 で新設）。canonical は OSI/HVD の .proto 面、`vd_metrics.py` frame はその**派生投影**。OSIスキーマ＝観測完全性の物差し（③に対する OpenX と同じ役割） |
| ⑤ | **判定・matcher** | 観測量から合否を出せるか | `matcher`(14) |
| ⑥ | **常設・ゲート** | 回帰ゲートで恒久的に守られるか | **KGに不在**（`gate` として §4 で新設）。現状 root CLAUDE.md §5 / scripts / CI のみ |

圧縮すると **主張 → 刺激 → 実装 → 観測 → 判定 → 常設**。
KGは①③⑤を厚く、②を半分、**④⑥が構造的な穴**。④⑥こそ「デモ」と「検証可能な
シミュレータ」を分ける層。

### 1.1 ②刺激の妥当性 — 二面的コンフォーマンス（構文 ∧ 意味論）

面1を「OpenX対応シム」と称する以上、コンフォーマンスは **入力(ODR/OSC)と出力(OSI)の両境界** で
「構文 ∧ 意味論」でなければならない。**XSDパスは構文フロアに過ぎない**（意味論的に壊れた道路も
XSDは通る）。②刺激の資産は3段の妥当性を持ち、各資産ノードに validity=`syntactic|semantic|intent`
＋どの検査が適用済みかの scope を付す。

| 段 | OpenDRIVE | OpenSCENARIO |
| :-- | :-- | :-- |
| **構文(XSD)** | `validate_xodr_schema` / `run_schema_comply` | `run_schema_comply`(xosc) |
| **意味論(規格の意味)** | rm/motion層は golden＝*change-detection であって真偽ではない*（fixture限定）。**採用可: ASAM `qc-opendrive`(OSS/MPL2.0, 22 check)** が lane-link セクション跨ぎ相互性・junction接続(5)・幾何連続(contact_point/s昇順/no_horizontal_gaps)を規則化済。**未実装＝lane-id符号規約・RHT/LHT走行方向・road間 lane reciprocity**（標準§10/§11に意味論は規範定義済→自作checker bundle化が筋）。 | **ほぼ皆無（gap）**: 採用可の `qc-openscenario` はあるが、エンティティ/位置解決・ルート接続・トリガ到達の網羅は要確認 |
| **意図適合(主張を発火するか)** | catalog validation の run-probe（telemetry frames>0 ＝弱い代理） | 同左（弱い） |

- **意味論オラクルは面1エンジン(esminiRMLib)を参照器に使う**のが要点: rm/motion は「esmini がこの道路を
  どう解決/走破するか」を golden 固定する。**#31(RoadPath 右折レーン接続失敗)は motion層が突く領域**
  （RoadManager tie-break 罠を含む）。OSC側には等価の面1ベース意味論プローブが無い。
- **回帰オラクル ≠ 真偽オラクル（最重要の限界）**: golden は「資産＋エンジンが前回と一致」を見るだけで、
  **資産とエンジンが揃って間違っている**（lane-link を両者同じ流儀で誤解釈、LHTを両者同じく取り違え）を
  検出できない。独立GTが無い所では golden は「両者が互いに一致」を言うだけ＝**検証になっていない**。
  真の意味論検査には、esmini解決に由来しない **独立のルールベースlinter**（link相互性・連続性・幾何/標高
  連続・LHT-hand整合 を規格から符号化）が要る ＝ **ODRもOSCと同クラスのgap**（ODRは"回帰のみ"、OSCは"皆無"）。
- **LHT は最難case（意味論 ∧ フォーク絡み）**: LHTは @rule / レーン順序 / 走行方向 / 標識orientation から
  *意味論的に導かれる*性質で **XSDに存在しない**。repo唯一のLHTツールは `check_core_census`/`check_fork_drift`
  ＝ **`[GT_LHT]` フォーク予算の会計であって正しさ検査ではない**。独立GT不在ゆえ fixture
  (`signals_orientation_lht_rht.xodr` 等)は手調整。LHT正しさ検証は **fork governance と絡めた専用アプローチ**が要る。
- **verdict-trust の前提条件**: 面3→面2（や面3→面1）の pass/fail は、走らせた資産が意味論妥当性を
  通っている場合のみ信頼できる。**意味論不正な資産上の判定は void**（garbage-in）。「道路が壊れて
  いるのか、エンジン/VDが壊れているのか」の弁別（LHT/#31系バグの切り分け）はこの資産意味論検査が担う。
- **試走実測（2026-07-17, `asam-qc-opendrive` 1.0.0 を隔離venvで GT LHT fixture 6件に適用）**:
  (1) `4way_*` fixture は **schema失敗**（空`<elevationProfile>`／`<signal>`のwidth・height必須欠落／
  country=`DE`が`e_countryCode`不適合）＝**esminiは黙って読むがXSD違反**＝構文フロアの穴を実証。
  (2) **schema失敗は precondition で意味論18 check を全SKIP**＝schema衛生が全検証のゲート（cascade）。
  (3) `signals_orientation_lht_rht` は10 check完走・issue 0 だが、**その10件に LHT/lane-id 意味論は含まれない**
  （未実装）＝「**緑＝沈黙であって正しさではない**」を実証（verdict-trust の生きた例）。
- **意味ルール網羅台帳 v0 完成（2026-07-18, ローカル機械パース）**: ASAM Annex F v1.9.0（`map_rules.html`）を
  直読み → 規範ルール **総数 275**（初版の335はWeb要約の過大計数、訂正済）。`qc-opendrive` 1.0.0 で一致 **16**
  ＝**被覆 5.8%・gap 259**（規範の94%が off-the-shelf 未チェック＝「抜け漏れ」の実数）。トップ別 gap: road 200/211・
  junctions 53/57・ids 3/3・header 2/2。**欲しかった LHT/lane-id/reciprocity/参照整合は全て275に含まれ status=gap**
  （`road.signal.validity.left/right_hand_traffic_lane_ids`・`road.lane.lanes_numbered_correctly`・
  `road.linkage.both_sides_consistency`・`ids.only_ref_defined_ids` 等）＝標準定義済→自作bundleで実装可・upstream貢献候補。
  成果物: `opendrive_rule_ledger.yaml`（275×status, 機械可読の権威）／`opendrive_rule_gap_report.md`／生成器
  `scratchpad/build_rule_ledger.py`。**これは分母の1ソース**（標準Annex F）。
- **分母の第2層＝prose義務（2026-07-18, `opendrive_prose_obligations_v1.9.0.md`）**: 章本文(06-15)の規範義務を抽出。
  当初 'shall' のみ296→**キーワード拡張(must/required/prohibited/cardinality)で364**（formalize済144/★prose専用220）。
  ただし拡張で**ノイズも増**（nav・属性表bleed）＝**recall↑precision↓、抽出はレビュー補助であって clean rule set でない**。
  **Annex D は独自UID実質ゼロ**。**★属性表のRequired/Type抽出(B案)は却下＝XSDが既に強制する内容とほぼ重複**（"XSDが見逃すミス"を
  潰す目的に非効率）。

- **§1.1a 検出可能性で見たミスの分類（ユーザー核心＝checker/XSDが見逃すミスを潰す）**。
  **重要な境界: これは"資産監査"（xodr/xosc それ自体が正しいか）。「標準合法だが消費者(esmini/VD)が壊れる」(#31/LHT遷移)は
  資産の欠陥でなく面1シミュレータの欠陥＝別枠(面1適合監査、既存 ODR conformance rm/motion・census・fork governance の領分)。**
  | ミス層 | 捕捉者 | 打ち手（資産監査） |
  |:--|:--|:--|
  | 構文(必須欠落/型) | **XSD** | 済 |
  | formalize済み意味ルール | **checker(Annex F)** ※実装すれば | **gap 259 実装＝最大の near-term ROI**（今どの checker も捕まえない＝"現行機械チェックが見逃すミス"の本体） |
  | ルール存在だが未formalize | 今は誰も | prose抽出で発掘(限定・補助) |
  | **資産固有だがルール化困難** | **XSD/checker では無理** | **値域内非現実(幅0.1m/住宅路200km/h)→妥当性境界・外れ値／資産内整合(LHT @rule↔lane-id↔標識配置)→メタモルフィック(RHTミラー)** |
  | ~~標準合法だが消費者破綻~~ | ~~面1シミュレータ側~~ | **別枠**（資産監査でなく面1適合監査：実走rm/motion oracle・差分＝既存プログラム） |
  → **"checker/XSDが見逃す資産ミス"の答え＝(a)gap259実装 ＋ (b)資産固有の非ルール技術（妥当性境界・資産内メタモルフィック）**。
  消費者破綻(#31/LHT遷移)は面1監査へ分離。prose/表抽出は補助。

- **β' phase-1 妥当性/外れ値 linter 実装（2026-07-18, `scratchpad/plausibility_lint.py` → `opendrive_plausibility_report.md`）**:
  xodr を直パースし XSD/checker 通過の**非現実な authored 値**を検出（駆動レーン幅・速度・幾何長・曲率・勾配＋コーパス分布）。
  **全コーパス適用（dist/build/thirdparty除外＝ソース208 files, 1521→208はdist配布コピー除外）**: flag 79（大半 upstream/official/校正）。
  **GT自作の妥当性債は小さくクリーン＝actionable 5-6件のみ**: XML非整形の壊れ資産2件(`resources/xodr/virtual_junction_23.xodr`・
  `GT_esmini/test/scenarios/lht_junction_osi_intersection.xodr`)／`parking_demo.xodr` 12m幅driving×2(駐車場をdriving誤type疑い)／
  `soderleden.xodr` R=2.5m急曲率。**学び: 妥当性検査の本質的難所＝校正/誤検出制御**（0幅=テーパー正当・junction急曲率正当＝"ルール化困難"の理由）。
  方法論: **公式ASAM=校正セット／自作=監査対象／dist配布コピーは除外**。次=**β' phase-2 資産内メタモルフィック**（LHT↔RHTミラーで @rule↔lane-id↔標識の内部矛盾を炙る）。

- **α+β' 一気通貫 gap-rule/整合 checker（2026-07-18, `scratchpad/gap_rule_check.py` → `opendrive_gap_rule_report.md`）**:
  qc未実装の Annex F 規範ルール **8種**（α）を自作＝参照整合`only_ref_defined_ids`／id一意`id_unique_in_class`／相互リンク
  `both_sides_consistency`／lane-id`lanes_numbered_correctly`+`lane_order_no_gaps`+`center_lane_id`／幾何連続
  `geometry.elem_asc_order`+`refline_no_gaps`+`length_sum_geometries`。＋**β' phase-2 メタモルフィック核**=`rule_hand_uniformity`（同一ファイル
  RHT/LHT混在＝内部矛盾）。ソース208で校正後 **16 violations（GT自作8）**。
  **動作実証（2つの意図的fixtureで）**: `02_invalid_junction_connection`（未定義ref）＋`signals_orientation_lht_rht.xodr`（RHT/LHT混在）を正検出。
  **本物候補**: `resources/xodr/soderleden.xodr` road 7 相互リンク非対称（実地図・唯一の要レビュー）。
  **健全確認**: 幾何連続・id一意はGT違反0。**校正教訓（再）**: 意味checkerは**junction文脈認識必須**（connecting road除外しないと991FP）
  ＝β'妥当性境界と同型「文脈で誤検出制御」。現況＝gap 259 のうち8ルール実装。将来: qc-framework bundle化してupstream貢献候補。

## 2. repo横断 能力モデル行列（主張ドメイン × スパイン）

モジュールトポロジ（GT_esmini/CLAUDE.md §1: control/scenario/osi/io ＋ 道路層）から
「再現していると主張する挙動」を行に取る。（● 充足 / ◐ 部分 / ○ 希薄 / ✕ 欠 / ? 要確認）

**①列の凡例（2026-07-20 決定, §8-3）**: `†` ＝ **deferred（将来の目標。今は要求化しないが
スコープ外ではない）**。`✕`/`○` と違い「意図的に空けている」ことを示す。要求化する主張は
**OSI-GT 正当性のみ**を先行させる（面1の中核主張＝ここが崩れると他の全 verdict が void になるため）。
ManualDrive 忠実度・Kinematic は deferred（実車比較データ等の前提が揃ってから）。

**④列の凡例（2026-07-20 追加, §2.3 の emit 基準再採点）**: ④は「観測できるか」を
**emit を canonical** に採点する。記号に欠の種別を添える —
`(a)` ＝ *未emit*（OSI/HVD のどこにも出ていない＝真の観測不能）、
`(b)` ＝ *emit済みだが未配線*（frame/matcher/gate に届いていない）、
`(b′)` ＝ *誤配線*（届くが値が壊れている＝**(b) より悪い**。観測できているつもりで verdict を汚す）、
`(–)` ＝ *非該当（別面）*（OSI 以外の正当な観測面を持つ。例: RM ctypes プローブ）。

| 主張ドメイン | ①主張 | ②刺激 | ③実装 | ④観測 | ⑤判定 | ⑥常設 |
| :-- | :-: | :-: | :-: | :-: | :-: | :-: |
| **VD 自動運転挙動** | ◐ | ◐ | ● | ◐(b優勢) | ◐ | ◐ |
| ManualDrive（FFB / 合図） | ○† | ◐ | ● | **○(b′)** ←◐から降格 | ○ | ✕ ←◐から訂正 |
| Kinematic / RouteDrive | ○† | ◐ | ● | ○(b′/a) | ○ | ✕ ←◐から訂正 |
| **RealVehicle 物理（pitch/roll）** | ✕ | ○ | ● | ◐(b) ＋交通車(a) | ✕ | ✕ |
| AutoLight（F6 環境ヘッドライト） | ● | ◐ | ● | ◐(b) | ◐ | ◐※opt-in |
| TrafficSignalController | ○† | ◐ | ● | ● | ◐ | ○ |
| **OSI-GT 出力**（中核主張） | ○→**要求化する** | – | ● | **◐(a)** ←●から降格 | ◐ | ○ |
| OpenSCENARIO 拡張パース | ◐ | ◐ | ● | **◐** ←「–」から訂正 | **○** ←◐から降格 | **○** ←●から降格 |
| **RoadManager LHT / ODR 1.6-1.9** | ● | ● | ● | ◐(–/b) | ● | ● |

### 行ごとの根拠（cell の出典と "?" の残し）

> **2026-07-20 の再採点で変えたセルと理由**（詳細な signal 単位の台帳は §2.3）:
> - **OSI-GT出力 ④ ● → ◐(a)**: 「観測層そのものだから●」は**循環論法だった**。GT が
>   *正しい*ことを言うための観測量（タイムスタンプ単調性・フレーム欠落率・動的オブジェクト数
>   の整合）は emit 側にも消費側にも**存在しない**（`count_osi_messages.py` はメッセージ**数**を
>   数えるのみで内容非検査）。中核主張なのに自己観測が(a)未emit。
> - **ManualDrive ④ ◐ → ○(b′)**: HVD に届く `steering_angle` が
>   `GT_HostVehicleReporter.cpp:141` の `steering_input_to_wheel_ratio`(既定12.9) を無条件適用され、
>   既に実舵角(rad)を渡す ManualDrive（`ControllerManualDrive.cpp:194-196`）では**約12.9倍に破壊**される。
>   加速度も `GT_HostVehicleReporter.cpp:237-292` が `egoState->pos_.GetAcc*()` で**毎フレーム上書き**し、
>   RealVehicle の値は外に出ない。**誤配線(b′)＝emit されているのに信頼できない**。
> - **ManualDrive / Kinematic ⑥ ◐ → ✕**: `test/CMakeLists.txt` と integration 26本を全照合した結果、
>   **ManualDrive・Kinematic・RouteDrive・pitch/roll に言及する自動テストは unit/integration いずれにも
>   1件も無い**。従来の「単体ゲート一部」は誤り（AutoIndicator/PIDPurePursuit は VD 側の部品）。
> - **OpenSCENARIO拡張パース ④ – → ◐ / ⑤⑥ ● → ○**: R5-U3 以降 `GT_ScenarioReader.cpp:37-181` の
>   パース対象は **TrafficSignalController 系のみ**（LightStateAction は upstream ネイティブへ移管済）。
>   その結果は `roadmanager::TrafficLight` → OSI `traffic_light`（`GT_OSIReporter_Traffic.cpp:254-264`）に
>   出るので④は非該当ではない。一方 ctest `test_ScenarioReaderParsing` は名前に反し
>   **現行パース対象を一切カバーせず** `VehicleLightBridge`/`ScenarioLightRegistry` の残置テストのみ
>   ＝「CI常設で厚い」は**空振り**。唯一の検出手段は E2E(phase3_batch)。

- **VD自動運転挙動**: ① `req-vd-ad`(12, ただし7機能分のみ) ② `scene`(18)＋`scenario-variant`(56)＋
  `policy`(xosc/verification) だが coverage=partial/none 多数 ③ `vd-func`(48)/`policy`(6)。
  横に **B1-B6（ミッション/マニューバ発起/意図伝達/縮退/予測/SOTIF）が空白** ④ `vd_metrics` frame
  (speed/accel/jerk/s) ＋ OSI GT(OBB) ⑤ `matcher`(14) ⑥ regression baseline(`phase3_expected.yaml`,
  非ブロッキング)。→ 列は最も厚いが横の穴と④⑥の薄さ。
- **ManualDrive**: ③ SDL2/FFB/IndicatorFSM 実装済 ④ HVD 出力(inputs/powertrain/ADAS) ⑤ 単体テスト
  AutoIndicator/PIDPurePursuit はあるが「FFB忠実度」等の主張matcherは無 ⑥ 単体ゲート一部。①主張が
  そもそも希薄（人間入力デバイスの"忠実度"を要求化していない）。
- **Kinematic / RouteDrive**: ③ 実装済＋integration(GT_Loader) ④ lane_id/indicator は OSI に汎用経路で
  出る（`GT_OSIReporter_Moving.cpp:777`, `:512`）が、**telemetry frame を作るのは VD だけ**なので
  frame/matcher には一切届かない。舵角は ManualDrive と**同じ 12.9倍破損経路**（`GT_HostVehicleReporter.cpp:141`
  × `ControllerKinematic.cpp:445-447`）＝(b′)。動的 pitch/roll は `VehiclePhysicsManager` が担うが
  **既定 `enabled_=false`**（`VehiclePhysicsManager.hpp:71`, opt-in `--vehicle-physics`）＝(a)。⑤⑥ 空。
  **訂正**: 従来根拠にした「横アクション可視性ギャップ（storyboard由来のみ拾う）」は
  **RouteDrive×Kinematic に関しては既にパッチ済み**（`ControllerKinematic.cpp:215-235` が
  `GetActiveLaneChangeAction()` を明示取得）。memory `kinematic_action_visibility` は
  「非storyboard所属アクションを持つ第三のコントローラが将来来たら再発する設計上の脆弱性」として
  読み替えるべきで、現行の④欠落の理由ではない。
- **RealVehicle 物理(pitch/roll)** ← **"検証未配線"の典型（当初✕→◐に訂正）**: ③ 制御車=コントローラ内部＋
  交通車=VehiclePhysicsManager の2系統で実装済（`pitch_roll_architecture`）。**④ orientation(roll/pitch/yaw)は
  OSI/HVD に emit 済み**（`GT_HostVehicleReporter.cpp` vehicle_motion / `GT_OSIReporter.cpp` obj orientation）。
  だが **派生テレメトリ frame が pitch/roll を落とし**（frame=x/y/speed/s のみ）、**⑤ それを読む matcher が無い** →
  ⑥ gate無、「ビューワー目視未」。→ *観測不能ではなく* **「emit済みだが検証パスに未配線」**。`signal` を frame契約に
  限定していたらこれを④✕（観測不能）と誤分類していた＝OSIを canonical にすべき最強の根拠。
- **AutoLight(F6)**: ① `feature:F6` ② scene modifiers illumination(partial) ③ 実装済(default OFF)
  ④ OSI 灯火出力（ネイティブ化, upstream U-work） ⑤⑥ integration 11本(6 AutoLight＋5 F6, opt-in)。
  「ビューワー目視未」。②環境刺激の系統展開が未。
- **TrafficSignalController**: ④ OSI 信号 per-frame WS 配信は厚い（`osi_streaming_specifics`）
  ⑤ `collect_osi_light_metrics.py` ⑥ 常設ゲート化は? 要確認。
- **OSI-GT出力**: シミュレータの中核主張＝GroundTruthの正しさ。③実装＝観測層そのもの。
  ⑤ `count_osi_messages`/`osi2csv`/`osiviewer` は道具だが「GT出力が正しい」を判定する
  matcher/goldenは無い（`count_osi_messages.py` は件数のみ）。① GT正当性の要求化は希薄。
  **④ の実体（再採点）**: オブジェクト内容の emit は厚い（moving/stationary/traffic_light/HVD 充足）が、
  **GT 自身の健全性 signal が(a)未emit**（時刻単調性・欠落率・entity数整合。`osi_bridge.py:45-78` に
  再構成失敗時の `_reset()` はあるが失敗率を外に出さない）。加えて **traffic_sign は(b)**:
  OSI には出ている（`GT_OSIReporter_Traffic.cpp:56-235`, 静的GT初回のみ）のに、Web は
  `road_geometry_service.py:84-93` が「遅延購読者が初回フレームを見逃す」ためと**明示コメント付きで
  OSI を迂回し xodr を再パース**＝正規IFを捨てている実例。`light_state` の
  「未設定 vs OFF」（`has_lightstate_action_` ラッチ, `GT_OSIReporter_Moving.cpp:402-412`）を突く
  テストも無い。
- **OpenSCENARIO拡張パース**: ④ 現行パース対象＝TrafficSignalController系のみで、結果は OSI
  `traffic_light` に出る（上記訂正参照）＝非該当ではなく ◐。⑤⑥ `test_ScenarioReaderParsing` は
  **現行パース対象を検証していない**（`VehicleLightBridge`/`ScenarioLightRegistry` の残置テストのみ）
  ＝「CI常設で厚い」は空振り。検出は E2E(`phase3_batch.yaml` → matcher `stopped_at_signal`)のみ。
  → **ctest 名と実内容の乖離は単独で actionable**（名前が主張を偽装している）。
- **RoadManager LHT / ODR** ← **最強スパイン＝手本**: ① `odr-cluster`(#0-22)/`odr-pending`
  ② `odr_fixtures`＋roadgen ③ フォークパッチ ⑤ conformance harness(schema/RM/OSI 3層)
  ⑥ ODR conformance gate＋census(`check_core_census.py`)＋drift＋fork-sync。namespaces.yaml 自身が
  fork-marker を「本グラフの設計手本」と明記。

### 2.1 横軸の是正 — B1-B6 の格納形 ＝ `layer` / `kind` 直交軸（2026-07-20 決定, §8-1）

**決定**: `tier`（動機層）に `mission` を足さない。代わりに `function_catalog_vd_ad.yaml` へ
**直交2軸**を追加した。

| 軸 | 値 | 意味 |
| :-- | :-- | :-- |
| `layer` | `strategic` / `tactical` / `operational` / `-` | **計画階層**（Michon / OpenX ActivityByLevel）。どこへ行くか / いつ何を発起するか / どう出すか |
| `kind` | `behavior` / `enabler` | 観測可能な運転挙動か、それを下支えする内部能力（予測・ODD監視・不確実性）か |

- `tier` は「与えられたタスクを *どう* 遂行するか」を修飾する動機のまま不変。
  **ミッション性は `layer: strategic` ＋ `tier: '-'` の組で表現**する（`tier` の定義を壊さないため）。
- `kind: enabler` を分けた理由は**検証スパイン側の都合**: enabler は単体では OSI に挙動として
  現れない → ④観測・⑤判定の当て方が behavior と根本的に違う（挙動matcherでは掴めない）。
  FUNC-074（機能限界の自己認識と外部通知）は、この「enabler を観測可能にする」ための機能。

**炙り出された数字（74機能, 機械集計）** — これが「目的地到達軸の欠落」の実体:

| layer | built | partial | none | oos | 計 |
| :-- | :-: | :-: | :-: | :-: | :-: |
| **strategic** | **0** | 2 | 8 | 0 | 10 |
| tactical | 1 | 5 | 27 | 1 | 34 |
| operational | 6 | 7 | 15 | 0 | 28 |
| `-`（横断） | 0 | 0 | 1 | 1 | 2 |

→ **実装済(built)7件は operational 6 / tactical 1 に集中し、strategic は built ゼロ**。
初版48機能が「所与の経路上での挙動調停」だけを深掘りしていたことの定量的裏付け。
追加した FUNC-049..074（26件）が B1-B6 の補完:

| ピラー | ID | 中核と既知欠陥 |
| :-- | :-- | :-- |
| **B1 Mission/経路計画** | FUNC-049..054 | FUNC-049 目的地ルーティング＝`partial`（RoadPath 基盤、**issue #31 で右折レーン接続失敗→ルート truncate**）。FUNC-053 経路上障害物回避＝**発端の「避けてルート維持」軸の本体** |
| **B2 マニューバ発起** | FUNC-055..060 | **VD は LC を自発発起できない**（FUNC-020 は実行側、発起は storyboard 由来のみ）＝ layer 軸で初めて分離できた欠陥 |
| **B3 意図伝達** | FUNC-048, 061..063 | AutoIndicator/IndicatorFSM は在るが**発起側が無いため storyboard LC にしか同期しない**（FUNC-061 partial） |
| **B4 縮退/ODD** | FUNC-010, 064..066 | ODD監視(enabler)→段階縮退→MRM の三段が全て未 |
| **B5 意図予測** | FUNC-067..070 | ConflictPointResolver の予測walkerが唯一（partial）だが**交差点調停専用で汎用予測層でない** |
| **B6 SOTIF** | FUNC-071..074 | FUNC-071 知覚劣化モデルは **`oos`（reason=`face1-scope`）＝面1シミュレータの領分**。3面契約が剪定に効いた最初の実例 |

**`status` 粒度（§8-4 決定）**: `none`(未実装＝ロードマップ・分母に残す) と `oos`(対象外) を維持し、
**`oos` には `oos_reason` を必須化**（語彙: `no-human-driver` / `perception-ideal` / `physics-layer` /
`face1-scope` / `road-data-missing`）。「前提が揃えば着手する」ものは `oos` にせず `none` に置く
（例: FUNC-008 ブラインドスポット介入は知覚劣化モデル待ちの `none`）。現状 `oos` は2件
（FUNC-005 純警報=`no-human-driver` / FUNC-071 知覚劣化=`face1-scope`）のみ。

### 2.2 `kind: enabler` の検証形式 — 3分割で決着（2026-07-20, §8-5）

**問い**: enabler(8機能)は単体では挙動として OSI に現れない。(a) 内部状態を `signal` として emit するか、
(b) それを使う behavior 側で間接検証するか。

**調査（OSI 3.7.0 実物・コード確認済み）**: 受け皿は *一部しか* 実在しなかった。

| 出したい中身 | OSI 3.7.0 の受け皿 | 判定 |
| :-- | :-- | :-- |
| 機能の作動状態 | `HostVehicleData.vehicle_automated_driving_function[]` ＝ `Name`(24種の ADAS 列挙: AEB / ACC / Highway Autopilot 等) ＋ `State`(ACTIVE/STANDBY/AVAILABLE/UNAVAILABLE/ERRORED) ＋ `custom_name`/`custom_state` ＋ `DriverOverride` | **正規欄あり** |
| 数値の内部量（TTC・a_req・自信度・tier） | 同 `custom_detail` ＝ `KeyValuePair{string,string}` のみ。**repo全体で未使用** | **汎用スロットのみ**（文字列型） |
| **他車の軌道予測** | **無い**。`MovingObject.future_trajectory` は存在するが **規格コメントが「被検体に見せるべきでない(should not be made available to the stack under test)」と明示** ＝ GT(世界の真実)側の欄 | **受け皿なし** |

**決定 ＝ 一律 (a)/(b) ではなく3分割**:

| 対象 | 方式 | 根拠 |
| :-- | :-- | :-- |
| 作動状態（FUNC-064 ODD監視 / FUNC-074 機能限界 ＋ 既存の AEB・ACC 等 behavior 側も） | **(a) 正規欄 `vehicle_automated_driving_function`** | 規格ネイティブ・発明ゼロ・他ADスタックとの比較可能性を保つ |
| 判断の数値（FUNC-072 不確実性・AEB の TTC/a_req・調停 tier） | **(a') `custom_detail` ＋ キー命名規約を文書化** | 唯一の汎用スロット。**規約を決めないとゴミ箱化する**ため命名規約とセットで導入 |
| **他車の予測（FUNC-067..070）** | **(b) 挙動から間接検証**。生の予測は telemetry に *非正規・デバッグ用* と明記して残し、**verdict には使わない** | OSI に受け皿が無い。`future_trajectory` への流用は **GroundTruth の意味を壊す**（運転AIの予測が「世界の真実」として記録される）＝ §0.3 で是正中の結合負債を別の場所で再生産する行為であり、規格自身が禁じている |

→ 帰結: **`signal` の exposure に `debug` を追加**する（`osi`/`hvd`/`frame`/`derived`/`debug`）。
`debug` は「観測はできるが **verdict-trust の対象外**」を意味する。予測の生データはここに置く。
これで「切り分けのための可視性」と「判定の正当性」を混ぜずに両立できる。

#### 2.2a 調査で判明した既存の配線抜け（enabler以前の問題・actionable）

**W1-W3 は 2026-07-20 に実装・解消済み**（下表の「状態」列）。W4 は一部のみ解消。

| # | 事実（file:line 確認済み） | 影響 | 状態 |
| :-- | :-- | :-- | :-- |
| **W1** | `ControllerVirtualDriver::GetADASStates` は **空のスタブ**（`ControllerVirtualDriver.hpp:66`）。`GT_esminiLib.cpp:1458-1490` は `size()>=24` のときだけ転送するため、**VD は AD機能状態を OSI に1つも出していない** | **AEB(FUNC-001, 実装済・テスト緑)ですら外から作動を観測できない**。規格に正欄があるのに空＝④観測の(b)未配線の最大例 | **実装済** |
| **W2** | `PolicyConstraint` は `tier`(SAFETY/COMPLIANCE/COURTESY/COMFORT) を持つが `VirtualDriverTelemetryJson.cpp:81-82` が **JSON化時に tier を落としている** | tier調停の結果が外から見えない。**1行の欠落**で AEB の「安全層として効いたのか」が検証不能 | **実装済** |
| **W3** | `AebSafety.cpp:114-127` の `ttc` / `a_req` は **ローカル const のまま破棄**。生き残るのは bool 結果のみ | 「なぜ作動した/しなかった」の切り分け不能 | **実装済** |
| **W4** | `custom_detail` / `custom_state` / `DriverOverride` / 実 `Name` 列挙（常に `NAME_OTHER` 固定）/ `route` / `vehicle_motion.current_curvature` が **populate されていない**（`GT_HostVehicleReporter.cpp:343-350`） | HVD 側の未使用余地。(a)/(a') の受け皿はここ | **一部**（`custom_detail` と実 `Name` 列挙は W1/W3 で解消。`custom_state`/`DriverOverride`/`route`/`current_curvature` は未着手） |

##### 実装（2026-07-20, VD側 W1-W3）

- **W2** — `VirtualDriverTelemetryJson.cpp` に `tier_str` を追加し `policy.constraints[].tier` を出力。
  フロントエンド型（`client.ts` `PolicyConstraintTier`）も追随。追加のみ＝後方互換。
- **W3** — 衝突コースゲートを **純関数 `aeb::EvaluateGate(cfg, gap, v_close)`** として `AebSafety.hpp` に抽出
  （候補選択は道路網が要るので `Evaluate()` に残置。安全判断だけを切り出した）。診断KVチャネル
  `PolicyDetail`（`TrafficPolicySnapshot::detail`）を新設し、**作動しなかったフレームでも**
  `gt.aeb.{gap_m,v_close_mps,ttc_s,a_req_mps2,triggered}` を出す（負の判断こそ従来診断不能だった）。
- **W1** — 純関数 `BuildAdasFunctionReport(flags, snapshot)`（`AdasFunctionReport.hpp/.cpp`）で
  policy → OSI 行へ写像 → `ControllerVirtualDriver::GetADASFunctions()` → 新API
  `GT_HostVehicleReporter::AddADASFunctionEx()` → `vehicle_automated_driving_function[]`。

**キー命名規約（`custom_detail`, §2.2 (a') の決着）**: `gt.<機能>.<量>_<SI単位>`（例 `gt.aeb.ttc_s`）、
値は固定小数3桁の10進文字列（真偽は `"true"/"false"`）。**単位は値でなくキーに埋める**（消費側が素の数値を
パースできる）。**`gt.dbg.*` は verdict-trust 対象外**＝§2.2 の exposure=`debug` に対応し、接頭辞1つで
lint が verdict 経路から機械的に除外できる。規約本体は `PolicyDetail.hpp` に常駐。

**policy → `Name` 写像**: `aeb`→`NAME_AUTOMATIC_EMERGENCY_BRAKING` / `lead_vehicle`→`NAME_ADAPTIVE_CRUISE_CONTROL`、
標準名の無い4種（`traffic_light`/`stop_yield`/`conflict_point`/`crosswalk`）は `NAME_OTHER` + `custom_name`
（近い響きの標準値へ**押し込まない**＝列挙を信じる消費者に誤報しないため）。加えて VDスタック自体を
`NAME_URBAN_DRIVING` の集約1行で出す。`State` は **ACTIVE（今フレーム constraint を出した）/ STANDBY（有効だが静穏）/
UNAVAILABLE（config で無効）** の3値＝「AEBは見張っていて撃たなかった」と「そもそも切ってあった」を別物として残す。

**転送条件**: `size()>=24` の固定24枠ラベル経路は RealDriver/PythonDriver 用に**そのまま保存**し、VD は
`AddADASFunctionEx` の新経路を使う（回帰ゼロ＋`custom_detail` を同経路で運べる）。
`control` 層は `osi` に依存できない（GT_esmini/CLAUDE.md §2）ため OSI列挙値は `AdasFunctionReport.hpp` に
**ミラー**し、両方を見る唯一の場所 `GT_esminiLib.cpp` で `static_assert` により実 .proto へピン留めした
（OSI が動いたら実行時でなく**ビルドで落ちる**）。

**実測（`cutin_hard_brake.xosc`, policy=lead,aeb, HVD UDP 1200フレーム復号）**:
`gt.aeb` が `name=AUTOMATIC_EMERGENCY_BRAKING` で ACTIVE 20フレーム、`custom_detail` に
`ttc_s=0.661 / a_req_mps2=13.611 / gap_m=11.884 / triggered=true`。無効な4 policy は UNAVAILABLE、
`gt.virtual_driver` は全1200フレーム ACTIVE。**＝AEB の作動が初めて OSI 越しに観測可能になった。**
ゲート: ユニット PASS（新規17テスト）／ODR quick PASS=306 FAIL=0 XPASS=0／phase3 ベースライン照合
**deviations=0（12シナリオ）**＝ telemetry JSON へのフィールド追加は後方互換と実測確認。

##### 残る債務（本作業で意図的に触っていない）

- ~~`GT_esminiLib.cpp` の `adasNames[]` の並びが **OSI `Name` 列挙順とずれている**~~ → **2026-07-20 解消**（下記）。
- 他5 policy の `custom_detail` は未実装（W3 は AEB のみ）。規約と口は共通なので追加は各 policy の1-2行。

##### 実装（2026-07-20, 固定24枠の `Name` 整合）

`GT_esminiLib.cpp` の `adasNames[]` は `ControllerRealDriverUtils.hpp` の `kAdasNames`（受信側の
`custom_name`→index 逆写像）の**重複コピー**であり、しかも OSI `Name` 列挙とずれていた。ずれ方は単純ではなく、
**index 0-12 は `osi = index+2` で成立するが 13/14 (`NIGHT_VISION`/`HEAD_UP_DISPLAY`) で入れ替わり、
15-19 で再びずれる**（＝「オフセット2」に見えるのが罠）。

- 2つの配列を `kAdasSlots`（`{label, osi_name}` の単一表, `ControllerRealDriverUtils.hpp`）に統合。
  **index 順は RealDriver の往復ワイヤ契約**（`test_ControllerRealDriverUtils.cpp:21-23` が `ACC==8` を固定）
  なので**並べ替えず**、各スロットに OSI `Name` 値を持たせる形にした。
- 送出を `AddADASFunction`(→ `NAME_OTHER` 固定) から `AddADASFunctionEx` に変更し、**実 `Name` 列挙を出す**。
  `custom_name` も従来どおり併記するため受信側の逆写像は不変＝**外部クライアント無影響**。
- W1 と同じく `control` は `osi` に依存できないので `osi_name` は int ミラー、
  **`GT_esminiLib.cpp` の `constexpr` 全24スロット照合 + `static_assert`** で実 .proto にピン留め
  （OSI が動いたらビルドで落ちる）。ユニットテスト `test_AdasSlotTable.cpp`（5本）でラベル↔列挙の対応と
  ワイヤ契約 index を固定。

W1-W3 は **enabler の議論と独立に効いた**（対象は built 済みの behavior）。§7 フェーズ4 の
パイロットとして pitch/roll に先行して完了した実例＝「④の欠は(b)配線であって(a)観測不能ではない」の実証。

### 2.3 ④観測の棚卸し（emit 基準・2026-07-20）

§2 の9主張ドメインについて、「その主張を裏付けるのに**必要な観測量**」を実装から導出し、
**OSI/HVD に emit されているか**（file:line 確認）→ **frame/matcher/gate に届いているか** の順で
採点した結果。W1-W4（§2.2a）と同一のものは参照に留め再記述しない。

判定記号: **(a)** 未emit ／ **(b)** emit済み未配線 ／ **(b′)** 誤配線（値が壊れて届く）／
**●** 充足 ／ **(–)** 非該当（別面に正当な観測経路あり）。

#### D1 VD 自動運転挙動

| signal | emit（file:line） | 消費到達 | 判定 | exposure |
| :-- | :-- | :-- | :-: | :-- |
| ego pose/speed | OSI Moving base ／ HVD `GT_HostVehicleReporter.cpp:260-289` | frame `ego.*` → matcher 多数 | ● | osi,hvd,frame |
| ego 縦加速度 | OSI `GT_OSIReporter_Moving.cpp:772-774` | frame に無し。`vd_metrics.py:253-259` が**コメントで「Telemetry carries no acceleration」と明言**し speed の中心差分で自前推定 | **(b)** | osi |
| ego jerk | OSI に jerk 概念なし | speed の**二階微分**で近似（`vd_metrics.py:274-285`） | (a) | derived |
| ego pitch/roll | HVD `:255-256,267-268,276-277,285-286` | frame に無し | (b) | hvd |
| front_bumper 局所化(F5) | frame まで到達（`VirtualDriverTelemetryJson.cpp:20-24`） | **`vd_metrics.py` に参照 0件** | (b) | frame |
| lead vehicle gap | STOP_AT_S 枝のみ `c.s` に載る（`LeadVehicleAware.cpp:97-99`）。追従継続枝は `c.s=0` 固定(`:113`)で距離を捨てる | 停止時のみ frame 到達 | (b)部分 | frame |
| traffic light phase | OSI `GT_OSIReporter_Traffic.cpp:282` | `_gt_to_scene`(`gt_sim_test.py:190`)→`stopped_at_signal` の require_red | ●（`capture_osi` 有効時のみ） | osi,frame |
| traffic light `assigned_lane_id` | OSI `:286-288` | `_gt_to_scene` の辞書に**含まれない**（`gt_sim_test.py:185-192`） | (b) | osi |
| stop/yield sign id・classification | OSI `:96-97`(P4 意味論 fallback `:187-220`) | `_gt_to_scene` は `traffic_sign` を**一切パースしない** | (b) | osi |
| 横断歩道 footprint | OSI `StationaryObject.base_polygon`(`GT_OSIReporter.cpp:829-850`) ただし type は `TYPE_OTHER` 固定(`:791-796`) | `_gt_to_scene` は `stationary_object` を**一切パースしない** | (b) | osi |
| 歩行者 velocity ベクトル | OSI `GT_OSIReporter_Moving.cpp:767-769` | `_gt_to_scene` が `sqrt(vx²+vy²+vz²)` のスカラーに潰す(`gt_sim_test.py:171`)＝**「離れつつあるか」の符号情報が消える** | (b) | osi |
| 歩行者用信号 phase | OSI lamp `:281-284`（内部で `FoldPedPhase`） | frame/matcher とも非消費 | (b) | osi |
| 交差点 `<priority>` | **OSI に無い**。`OdrSideModel` で xodr 直読み(`ConflictPointResolver.cpp:336-346`) | 非到達 | (a) | derived |
| 交差点 governing other_id / region span / PET | 内部のみ(`ConflictPointResolver.cpp:551-672, 647-656`) | 非到達 | (a) | debug |
| stop FSM phase(APPROACH/HOLD/CREEP) | 内部のみ(`StopYieldSignAware.cpp:38-82`) | 非到達 | (a) | debug |
| AEB 作動フラグ / constraint source | `PolicyConstraint.source`(`AebSafety.cpp:120-128`) | frame → `no_emergency_without_conflict`(`vd_metrics.py:811-846`) | ● | frame |
| AEB ttc/a_req ／ tier ／ ADAS states | — | — | W3 / W2 / W1 と同一 | — |

**新規ギャップ 17件**（(a) 9 / (b) 8、W1-W4 重複を除く）。**単一の構造的原因が支配的**:
`gt_sim_test.py:133-194` の `_gt_to_scene` が **moving_object と traffic_light しか変換しない**
（`traffic_sign` / `stationary_object` は丸ごと非対応、velocity はスカラーに潰す）。
OSI 側は出しているのに **scene 変換層でせき止められている**のが (b) の大半。
もう一つのパターンは、各 policy の「なぜ止まったか」（信号id・標識種別・歩行者信号・conflict相手）が
**`policy.constraints[].source` という文字列タグ1本に圧縮**され識別子と状態値が落ちること
＝ **W2(tier欠落)と同型の情報損失が policy 横断で反復**している。

#### D2 ManualDrive ／ D3 Kinematic・RouteDrive ／ D4 RealVehicle 物理

| signal | emit（file:line） | 消費到達 | 判定 | exposure |
| :-- | :-- | :-- | :-: | :-- |
| **HVD `steering_angle`** | `ControllerManualDrive.cpp:192-197` が**タイヤ舵角(rad)**を渡し、`GT_HostVehicleReporter.cpp:141` が `steering_input_to_wheel_ratio`(既定12.9) を適用して **OSI ハンドル角**へ変換 | Web `HvdGaugePanel.tsx:55`（`clamp(±540°)`＝ハンドル角として描画）。0.61rad×12.9=451° で整合 | **●** | hvd |
| HVD acceleration | `RealVehicleBackend::BuildHVD` の値を `GT_HostVehicleReporter.cpp:237-292` が `egoState->pos_.GetAcc*()` で**毎フレーム上書き**（コメントに "Always OVERWRITE" と明記） | RealVehicle の正確な加速度は外に出ない | **(b′)** | hvd |
| Kinematic wheel_angle | `ControllerKinematic.cpp:445-447` → `GT_esminiLib.cpp:1516-1531`（`GetWheelAngle()`＝タイヤ舵角 rad, 既定上限1.047rad） | 同じ ratio 経路＝**正しい**。ただし上限1.047rad(60°)はハンドル角換算774°で540°ロック超過＝**タイヤ角上限そのものが非現実的**（別件） | ● | hvd |
| **VD wheel_angle** | `ControllerVirtualDriver.cpp:587-588` が ManualDrive と**同一パターン**（`current_hvd_.vehicle_steering_wheel().angle()`＝RealVehicleBackend のタイヤ舵角）を渡す | 同じ ratio 経路＝**正しい** | ● | hvd |
| **RealDriver wheel_angle**（★2026-07-20 是正） | 旧: `ControllerRealDriver.cpp:745` が**正規化指令値 [-1,1]** を渡し ratio 適用で 12.9rad(739°)＝真値451°の**1.64倍(=1/steer_gain)**。現: `real_vehicle_.wheelAngle_`（他3経路と同一の物理タイヤ舵角）を渡す | `HvdGaugePanel.tsx` | ●（修正済） | hvd |
| FFB 内部量(steering_pos/vel, lat_accel) | `RealVehicleBackend.cpp:121-134`、`SDLFFBSink.cpp:158-176` が同じ hvd を直読み | **自己完結ループ**（外部配信不要） | ●（FFB 用途） | debug |
| throttle/brake/gear/rpm/torque/灯火 | `ControllerManualDrive.cpp:188-247` → HVD ／ OSI `GT_OSIReporter_Moving.cpp:414-512` | `HvdGaugePanel.tsx` | ● | hvd,osi |
| ManualDrive ADAS states | `ControllerManualDrive.hpp:36` で恒久的に空 | `GT_esminiLib.cpp:1486` の `size()>=24` ガードで無害にスキップ | (a)**設計上の非該当**（W1 と異なりバグではない） | hvd |
| override domain state (lat/lon manual vs scenario) | OSI/HVD に相当欄なし | 非到達 | (a) | debug |
| Kinematic 曲率 κ | 内部のみ(`ControllerKinematic.cpp:380-394`) | debug_log のみ | (a)（意図的な中間量） | debug |
| lane_id / indicator (Kinematic/RouteDrive) | OSI `:777` / `:512` | **VD 以外は telemetry frame を生成しない**ため matcher 非到達 | (b) | osi |
| 制御車 dynamic pitch/roll | `RealVehicle.cpp:410-430` → `SetInertiaPos` → OSI `GT_OSIReporter_Moving.cpp:756-757` | frame/matcher なし | (b)（既知） | osi,hvd |
| **交通車 dynamic pitch/roll** | `VehiclePhysicsManager.cpp:107-108` だが **既定 `enabled_=false`**(`VehiclePhysicsManager.hpp:71`, opt-in `--vehicle-physics`) | 通常運用では**そもそも計算されない** | **(a)** | osi,hvd |

**steering_angle の (b′) 判定は撤回（2026-07-20 再検証で反転）**: 当初「`GT_HostVehicleReporter.cpp:141` の
ratio 無条件乗算により ManualDrive / VD / Kinematic の3ドメインが12.9倍に破壊される＝最も費用対効果の高い
単一修正点」と採点したが、**誤診だった**。

決め手は OSI の欄の定義。`osi_common.proto:1026-1035` の `VehicleSteeringWheel.angle` は
**「ハンドル角 [rad]」であってタイヤ舵角ではない**。したがって `steering_input_to_wheel_ratio`(12.9) は
**ステアリングギア比**そのもの（`RealVehicle.hpp:71` のコメントも "Corolla steering ratio"）で、
`タイヤ角 rad × 12.9 → ハンドル角 rad` は **OSI 規定どおりの正当な変換**である。
検算: `steer_gain=0.61rad` → 451°（ロック2.5回転＝実車的）。消費側 `HvdGaugePanel.tsx:55` も
`clamp(±540°)` でハンドル角として描画しており、**表示側は元から正しかった**。

実際に壊れていたのは、当初「正当」と整理した **ControllerRealDriver 経路のみ**。ここだけが正規化指令値
[-1,1] を渡していたため 12.9rad(739°)＝真値の1.64倍になっていた。是正は
`ControllerRealDriver::GetInputsForOSI` が `real_vehicle_.wheelAngle_` を渡す形に変更（他3経路と同一の
真実源＝物理のタイヤ舵角）。**外部UDP APIの契約 [-1,1]（`docs/realdriver/api_reference.md:41`）は入口で不変**。

**教訓（採点手順への反映）**: 「値が N 倍おかしい」と見えたとき、**先に OSI 側の欄の定義（単位と何の量か）を
確定させる**こと。今回は入力側の単位だけを4経路突き合わせて多数決的に判断したため、少数派＝正解の経路を
バグと誤認し、正しい3経路に (b′) を付けた。**多数決は単位の正しさの根拠にならない。**
なお §2 の再採点コメント（`ManualDrive ④ ◐ → ○(b′)`）の steering 部分も本節により**無効**
（acceleration の上書き (b′) は射程外・未検証のまま有効）。

**⑥の実測**: `test/CMakeLists.txt` と integration 26本の全照合で、**この3ドメインに言及する自動テストは
unit/integration いずれにも 0件**。回帰検知は目視スモークのみ ＝ ⑥ を ◐ から ✕ に訂正した根拠。

**未確認（要検証・推測）**: `VehiclePhysicsManager.cpp:50-59` のスキップ判定リストに
`CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME` が**含まれていない**（RealDriver/PythonDriver/ManualDrive のみ除外）。
VD は自前で絶対 pitch/roll を `SetInertiaPos` する（`ControllerVirtualDriver.cpp:344-350`）ため、
`--vehicle-physics` 有効時に `SetPitchRelative`/`SetRollRelative` が**二重に走る**懸念。
リスト欠落は file:line で確定した事実、干渉の実害は `EvaluateHPR` の評価順に依存し**未検証**。

#### D5 AutoLight ／ D6 TrafficSignalController ／ D7 OSI-GT 出力

| signal | emit（file:line） | 消費到達 | 判定 | exposure |
| :-- | :-- | :-- | :-: | :-- |
| brake / indicator / low_beam | `AutoLightController.cpp:231-374,411-862` → OSI `GT_OSIReporter_Moving.cpp:433-512` | WS `osi_stream.py:75-77` | ● | osi,frame |
| **reversing_light / high_beam** | OSI `:443-445, 485-487` に正しく populate | WS の `_extract_lights()`(`osi_stream.py:65-83`) が**抽出していない**（`collect_osi_light_metrics.py` 経由でのみ観測可） | **(b)** | osi |
| environmental_conditions | OSI `GT_OSIReporter_Environment.cpp:38-86` 毎フレーム | WS `_gt_to_json` が**抽出せず**、gRPC のみ到達 | (b) | osi |
| AutoLight 灯火所有権(AUTO/MANUAL/SCENARIO) | `ExtraEntities.hpp:100-126` の `LightSource`。**OSI に概念が無い** | 非到達 | (a) | debug |
| トンネル内判定 | `AutoLightController.cpp:177-198`（RoadManager 直読み） | 非到達 | (a) | debug |
| 信号 lamp mode/color/icon | OSI `GT_OSIReporter_Traffic.cpp:281-283`、毎フレーム(`GT_OSIReporter.cpp:723`) | WS `osi_stream.py:139-152` | ● | osi,frame |
| 信号 phase 解決失敗(signalId未解決 / controller参照不明) | `TrafficSignalController.cpp:39-83, 102-192` は **LOG_WARN 止まり**、失敗件数を機械可読に出さない | 非到達 | (a) | debug |
| **GT タイムスタンプ単調性** | emit 側に保証・検査コード**なし** | `count_osi_messages.py` は件数のみで内容非検査 | **(a)** | debug |
| **フレーム欠落率(UDP再構成失敗)** | `osi_bridge.py:45-78` に `_reset()` はあるが**カウンタ化して外に出さない** | 非到達 | **(a)** | debug |
| **動的オブジェクト数の整合**(entity数 vs GT moving_object数) | 突合コードなし。WS `osi_stream.py:157` の `object_count` は表示用で期待値比較せず | 非到達 | **(a)** | debug |
| traffic_sign | OSI `GT_OSIReporter_Traffic.cpp:56-235`（静的GT 初回のみ） | **Web は `road_geometry_service.py:84-93` が明示コメント付きで OSI を迂回し xodr 再パース**。WS も抽出せず。検証は `run_odr_conformance.py` OSI層の件数比較のみ | **(b)** | osi |
| `light_state` の「未設定 vs OFF」 | `has_lightstate_action_` ラッチ(`GT_OSIReporter_Moving.cpp:402-412`)で一度も変更が無いと**フィールドごと出ない** | 消費側は `HasField` で整合。**この区別を突くテストは無し** | ●(emit)／⑤欠 | osi |

**D7 が本セッション最大の再採点**: 「観測層そのものだから ④=●」は循環論法で、
**GT の健全性を観測する signal は3つとも(a)未emit**。面1の中核主張がここで裏付けを失う。

#### D8 OpenSCENARIO 拡張パース ／ D9 RoadManager LHT・ODR

| signal | 観測面 | 判定 |
| :-- | :-- | :-: |
| TrafficSignalController パース結果 | `roadmanager::TrafficLight` → OSI `traffic_light`(`GT_OSIReporter_Traffic.cpp:254-264`) | ◐（emit は成立。ctest は非カバー＝⑤の穴） |
| ODR schema 層 | XSD（`run_odr_conformance.py:652-683`）＝OSI 非経由 | **(–)** ● |
| ODR rm / motion 層 | `esminiRMLib` ctypes 直接プローブ（`run_odr_conformance.py:230-470`, `rm_lib.py:181-208`）＝OSI 非経由 | **(–)** ●（ただし §1.1 の通り *回帰* であって真偽ではない） |
| ODR osi 層 | `GT_esminiLib` `SE_GetOSIGroundTruth` を実デコード（`run_odr_conformance.py:472-561`） | **(b)** — 実装済みだが `--profile full` の**手動実行のみ**でゲート未配線 |

→ ODR 列の④が ◐ なのは「観測が弱い」からではなく、**別面(–)で足りている部分**と
**OSI層がゲートに繋がっていない(b)部分**の混成。(–) を導入したことで、この行を
不当に減点せずに済むようになった。

### 2.3a coupling-audit 第一弾 — verdict の面2依存の実数

§0.3 で「面3が面2のテレメトリを主観測源にしている」と定性的に述べた負債の**実数**。
`vd_metrics.py::eval_must` の全分岐（実数 **14 matcher**）を観測フィールドで分類した。

| 判定源 | 件数 | matcher |
| :-- | :-: | :-- |
| **(i) 面2 VD テレメトリ直結**（結合負債） | **10 / 14** | speed_above·below / min_speed_above / no_constraint_kind / lane_keep / lane_change_count / deceleration_profile_smooth / speed_reduction_before_landmark / steer_not_saturated / stopped_at_stop_sign·signal(主判定) / no_emergency_without_conflict |
| (ii) 面1 OSI GroundTruth 直結（正規IF） | 4 / 14 | maintained_following_distance / min_separation_above / min_obb_separation_above / impact_speed_below |

- 純テレメトリのみなら **9/14**。`stopped_at_signal` は主判定がテレメトリで
  `require_red` サブチェックのみ OSI（`vd_metrics.py:565-595`）＝**混在型**。
- OSI 側4件はいずれも `frame["scene"]`（`_gt_to_scene` が埋める）依存で、
  **`--osi` / batch `osi: true` が無いと None → matcher は skip**（`vd_metrics.py:611-613,660-662`）
  ＝ **正規IF側は「条件付きでしか効かない」**。
- `check_phase3_regression.py` 自体は観測量を持たず `verdict.json` の
  `event/status/detail` を読むだけ（`:75-119`）＝ **判定根拠は完全に上流に従属**。
  `phase3_expected.yaml` も matcher 名＋status を凍結するのみで、面2依存の実態を追認する構造。
- トランスポートは2系統だがどちらも起点は面2: batch 経路は ctypes
  `GT_GetVirtualDriverTelemetry`（`gt_lib.py:220-228`）、Web ライブ表示は UDP 48202
  （`config.py:134` `VD_LISTEN_PORT`）。OSI は別途 UDP 48198。

**結合負債の実数 ＝ 14 matcher 中 10（71%）が面3→面2 直結**。
§0.4 が言う「面3を SUT 非依存にする」目標に対し、置換対象の母数がこの10件。
うち **加速度系（deceleration_profile_smooth）は OSI に emit 済み**
（`GT_OSIReporter_Moving.cpp:772-774`）＝ **配線を差し替えるだけで面1経由に移せる最有力候補**。

## 3. メタ所見

1. **検証成熟度が主張ごとに極端に不均一**。道路/ODR層は①〜⑥ほぼ全通し（census＋conformance＋
   drift/sync二重ガード）。一方 behavioral/physics 層（VD横の穴・pitch/roll・AutoLight目視）は
   ④観測⑤判定⑥常設が薄い/空。
   **ただし道路/ODR層の"強さ"は回帰(change-detection)であって意味論的真偽ではない**（§1.1）: golden は
   資産＋エンジンが揃って誤っている場合を捉えられず、独立ルールlinterもLHT正しさオラクルも無い。
   ＝「最強スパイン」でも②入力の意味論真偽には穴がある。
2. **このリポジトリの本当の穴は「機能不足」より「実装済みだが縦串の切れた主張の散在」**。
   pitch/roll がその純粋形（③実装● / ④ OSI・HVDに emit 済み◐ / ⑤matcher無 / ⑥gate無）。
   ここで重要なのは **④の"欠"には2種ある**こと: (a) *未emit*＝真の観測不能 と、(b) *emit済みだが
   消費面(frame/matcher/gate)に未配線*＝検証配線の欠落。pitch/roll は(b)。両者を混同しないため
   `signal` は OSI/HVD を canonical に置く必要がある（frame限定では(a)と(b)を潰す）。
3. **目標像＝道路層のやり方の移植**: 生観測(`signal`)を一級市民化し、常設ゲート(`gate`)を
   ノード化して、「観測できない/一度検証したきりの主張」を graphクエリで検出可能にする。
4. **「深掘り」の再定義**: 機能を1つ深掘りする作業＝その列を①〜⑥まで縫うこと。追跡単位は
   「機能ごと」でなく **「主張 × 縦層のセル」**。

## 4. 新namespace仕様（**2026-07-20 登録済み**）

> **状態: フェーズ1で登録完了**。`signal`(face:1, **47ノード**) / `gate`(face:3, **14ノード**) を
> namespaces.yaml に登録し、実体を `signal_catalog.yaml` / `gate_catalog.yaml` に起こした。
> 全30名前空間に `face:` タグを付与、edge_types に §5 の3辺を追加。lint + `--render` グリーン。
>
> **seed 表からの差分（登録時に実測で訂正した点）**:
> - **W1/W2/W3 は 2026-07-20 に着地済み**のため、`adas_states`・`policy_constraint_tier`・
>   `aeb_ttc`/`aeb_a_req` は **(a) ではなく (b)**（emit 済み・読む matcher が無い）で起こした。
>   下表の (a) は W1-W3 着地前の値。
> - 下表は1行に2量を束ねた行（`ego_pose`/`ego_speed` 等）があるため、1観測量=1ノードに
>   分解すると 25行 → **47ノード**。§2.3 の D1-D9 で file:line 確認済みのものだけを起こした
>   （on-demand 原則は維持）。
> - `manualdrive_adas_states` を「(a) だが**設計上の非該当**（人間が運転するので AD機能状態が
>   無い＝W1のようなバグではない）」として明示ノード化。派生レポート(§6)が (a) を機械的に
>   「観測不能の是正対象」と数えると誤検出になるため。
> - `gt_timestamp_monotonicity` 等の GT 健全性3件は下表通り exposure=`debug` で起こしたが、
>   **これは暫定**。emit された暁には面1の自己観測＝verdict に使うべき量であり、
>   `debug`(verdict-trust対象外) のままにはできない旨をノートに明記した。

| slug | entity_type | 意味 | source_of_truth（実在） | seed 例 |
| :-- | :-- | :-- | :-- | :-- |
| `signal` | observable | 観測可能量（生の量）。**canonical=OSI GroundTruth / HostVehicleData 面**、frame はその派生投影 | **OSI `.proto`（GroundTruth / HostVehicleData）を正の面**とし、`GT_OSIReporter*` / `GT_HostVehicleReporter` が populate。`vd_metrics.py` frame は派生投影の一つ §2.3 で実測した seed（下表） |

**seed（2026-07-20, §2.3 の棚卸しで実在確認したものだけ起こす）**。on-demand 原則は維持
（OSI .proto の bulk import はしない）。`state` 列は §2.3 の判定をそのまま持ち込む:

| signal 候補 | exposure | state |
| :-- | :-- | :-- |
| `ego_pose` / `ego_speed` | osi,hvd,frame | ● |
| `ego_orientation`(roll/pitch/yaw) | osi,hvd | (b) |
| `ego_accel_long` | osi | **(b)** ← 差し替え最有力（§2.3a） |
| `ego_jerk` | derived | (a) |
| `front_bumper_localization` | frame | (b) |
| `obb_separation` / `object_poses` | osi,frame | ● |
| `pedestrian_velocity_vector` | osi | (b)（scene 変換でスカラー化） |
| `traffic_light_state` | osi,frame | ● |
| `traffic_light_assigned_lane` | osi | (b) |
| `traffic_sign_classification` | osi | (b)（Web は xodr 再パースで迂回） |
| `crosswalk_footprint` | osi | (b) |
| `hvd_inputs`(throttle/brake/gear) / `hvd_powertrain` | hvd | ● |
| `hvd_steering_angle` | hvd | **(b′)** 誤配線 |
| `hvd_acceleration` | hvd | **(b′)** 上書きで欠損 |
| `vehicle_lights`(brake/indicator/low_beam) | osi,frame | ● |
| `vehicle_lights_reversing` / `vehicle_lights_high_beam` | osi | (b)（WS 未抽出） |
| `environmental_conditions` | osi | (b) |
| `adas_states` | hvd | (a) ＝W1 |
| `policy_constraint_tier` | frame | (b) ＝W2 |
| `aeb_ttc` / `aeb_a_req` | debug→(a') `custom_detail` | (a) ＝W3 |
| `gt_timestamp_monotonicity` | debug | **(a)** 未emit（中核主張の自己観測） |
| `gt_frame_loss_rate` | debug | **(a)** |
| `gt_entity_count_consistency` | debug | **(a)** |
| `junction_priority` | derived | (a)（OSI に受け皿なし・xodr 直読み） |
| `conflict_prediction_span`/`pet` | **debug** | (a)（§2.2 の(b)方針＝verdict 不使用） |

**seedは on-demand**（OSI .proto を bulk import しない＝openx 346概念と同じ轍を踏まない。主張/ matcher が
参照した signal だけ起こす）。各 `signal` に **exposure** を付す: `osi` / `hvd` / `frame` / `derived` /
**`debug`**（どの面に届いているか）。これで「OSIにemit済みだが frame未投影」（pitch/roll型）が一目で分かる。
**`debug` は §2.2 で追加**＝「観測できるが **verdict-trust の対象外**」（面2の内部を覗くデバッグ経路。
他車予測の生データ等。判定に使うと面3→面2直結の結合負債になるため、lint で verdict 経路から除外する）。
| `gate` | test-gate | 常設検証ゲート（回帰で恒久的に走る単位） | root `CLAUDE.md` §5 ＋ `scripts/` | unit / regression / phase3-baseline / odr-conformance / catalog / integration / fork-census / fork-drift / fork-sync / ci |

**`signal` と `matcher` の区別**: `signal`＝生の観測量（speed, pitch, OBB分離…）、
`matcher`＝その量に閾値/窓を当てた判定（speed_above, min_obb_separation_above…）。
`matcher -[observes]-> signal` で結ぶ。**pitch/roll は matcher 以前に signal が無い**のが
検証不能の根。

## 5. 新 curated 辺（**2026-07-20 追加済み**）

既存 curated（realizes / verifies / concerns / depends-on 等）に加え、縦串3種:

| 辺 | from → to | 意味 |
| :-- | :-- | :-- |
| `stimulated-by` | req / scene → `scenario-variant` / `policy` | ②刺激: その主張を発火させる資産 |
| `observes` | `matcher` → `signal` | ④観測: matcher が読む生量。**signal 不在＝この辺が引けない列** |
| `sustained-by` | req / `matcher` → `gate` | ⑥常設: どの常設ゲートで守られるか |

（既存 `verifies` は matcher→req のまま。上3種はそれぞれ別の縦層をつなぐ。）

### 5.1 初回結線の実測（2026-07-20・**引けなかった辺が本体**）

`observes` 17辺 / `sustained-by` 6辺 / ゲート構造 10辺 を graph.yaml に投入。
**引けなかった辺の方が情報量が多い**（一次記録は graph.yaml 末尾のコメント）:

- **`observes` を引けなかった matcher 3種**: `lane_keep`・`lane_change_count`（ego lane_id は
  OSI にも出ているが matcher が読むのは frame の `ego.lane`＝面2由来。**同名の量が2面に
  別経路で存在し、検証は面2側を見ている**）／`steer_not_saturated`（`driver.steer`＝面2の
  操舵指令。乗り換え先になるはずの `signal:hvd_steering_angle` が (b′) 誤配線のため、
  **面2直結の解消には先に(b′)修正が要る**という依存が辺で可視化された）／
  `no_constraint_kind`（`midlong.constraints[].kind`＝面2プランナ内部・OSI に受け皿なし）。
- **`sustained-by` を引けなかった matcher 9種**: 常設ゲート `gate:phase3-baseline` が実際に
  発火させるのは **16 matcher 中6件のみ**（phase3_batch.yaml の12シナリオ分 expectations を
  機械集計、2026-07-20実測）。`min_obb_separation_above`・`impact_speed_below`・
  `no_emergency_without_conflict` 等は phase3d/phase3e/07_aeb の**手動マニフェスト専用**＝
  **AEB(REQ-AD-001家族)を判定する matcher が常設ゲートに1件も乗っていない**。
  さらに `min_separation_above` はどの資産からも参照されない（実測0件）**デッド matcher**。
- **ゲート構造で判明した非自明**: `check_core_census` / `check_fork_drift` /
  `check_resync_guards` は独立スクリプトに見えて `run_odr_conformance.py:1556-1558` が
  **プロファイル分岐より前で無条件に内包**しており、CI の schema-only 起動でも走る
  （＝実質は常時ブロッキング）。`gate:odr-conformance-full` は逆に quick の上位集合なのに
  **どのラダーにも配線されていない**（§2.3 D9 が OSI層を (b) とした理由の実体）。

## 6. 派生レポート「縦串の切れた列」（前回の未深掘り検出の拡張）

前回定義した「realizes先を持たない機能＝未深掘り」を、④⑥まで拡張して自動検出する。
`scripts/check_knowledge_graph.py` に派生レポート節を足す（手動フラグは持たない＝腐り防止）:

- **観測欠（2種を区別）**: `③実装あり` の主張について —
  (a) **未emit**: 対応 signal が OSI/HVD いずれにも存在しない → *真の観測不能*。
  (b) **未配線**: signal は exposure=`osi`/`hvd` だが `frame`/`derived` に無く、かつ `observes` する matcher も無い →
  *emit済みだが検証パス未接続*（pitch/roll 型）。(a)と(b)は打ち手が全く違う（センサ/物理拡張 vs 配線）。
- **常設欠**: `⑤matcherあり` かつ `sustained-by 先 gate が無い` 要求 → 「一度検証したきり」。
- **刺激欠**: `scene coverage!=covered` かつ `stimulated-by 資産が無い` → 発火できない主張。
- **資産妥当性欠（§1.1）**: `stimulated-by 先の資産` が意味論層（ODR rm/motion, OSC意味論）未適用のまま
  verdict に使われている経路 → **verdict-trust 破れ**。特に **OSC意味論は全滅**なので「XSDのみで走っている
  xosc」を列挙する（最優先の debt）。

出力は「主張 × 欠けた縦層」のリスト。これが恒久の「未検証台帳」になる。

## 7. 段階プラン

- **フェーズ0（本書）**: 検証スパイン定義／repo横断行列／新namespace・辺の仕様。**KG本体非改変**。
- **フェーズ1 ✅完了（2026-07-20）**: `signal`(47) / `gate`(14) を namespaces.yaml に登録し
  §4 seed から起こした。edge_types に §5 の3辺を追加、全30 namespace に `face:` タグを付与。
  初回結線 33辺（§5.1）。lint + `--render` グリーン。
  **フェーズ1で判明した積み残し**（フェーズ3の lint 設計に直結）:
  - `face:` タグと catalog の中身（exposure/state/covers）は **現行 lint が一切検証しない**
    （`check_knowledge_graph.py` は namespace の `source_of_truth` の実在しか見ず、
    ノードファイルを開かない）。値域チェックはフェーズ3で実装する。
  - `gate:unit-ctest` の実測で **root CLAUDE.md §5 の「25 unit sources」は陳腐化**（実測28本・
    TEST/TEST_F 278件）。`test_ScenarioReaderParsing.cpp` 単体の13テストと傘バイナリ28本を
    混同しない（`GT_ScenarioReader` 非カバーという結論自体は正しい — 実測: GT_esmini/test 配下を
    `TrafficSignalController|GT_ScenarioReader` で grep して0件）。
- **フェーズ2**: 既存の厚い列（VD-AD の built 4機能・AutoLight・ODR）を縦串で結線し、
  行列を **生成ビュー化**（scene×func×spine を3ソースから render）。
- **フェーズ3**: 派生レポート（§6）＋ **coupling-audit（§0.5）** を lint に追加 →
  「縦串の切れた列」と「面3→面2直結の結合負債」を CI で可視化。
- **フェーズ4**: 空スパインの主張（pitch/roll 等）を1列ずつ縫う（signal露出→matcher→gate）。

## 8. 未決事項（レビューで詰める）

### 決着済み（2026-07-20, ユーザー判断）

1. **~~B1-B6 の格納形~~** → **決着: `layer`(strategic/tactical/operational) ＋ `kind`(behavior/enabler)
   の直交2軸**。`tier` に `mission` は足さない（動機層という定義を保つ）。詳細と定量結果は **§2.1**。
2. **~~行（主張ドメイン）の粒度~~** → **決着: 行爆発は起きない**。B1-B6 は §2 の行を増やすのではなく
   `vd-func` の**属性**として格納された（74機能 × layer/kind）。§2 の行は「VD 自動運転挙動」1行のまま、
   その内訳は catalog 側の軸で切る。
3. **~~①主張が希薄なドメインを要求化するか~~** → **決着: OSI-GT 正当性のみ先行して要求化**。
   ManualDrive 忠実度・Kinematic は **deferred（`†`）＝将来の目標であってスコープ外ではない**。
   §2 行列の①列に `†` 表記を導入した。OSI-GT を優先する理由は面1の中核主張であり、
   ここが崩れると他の全 verdict が void になるため（§1.1 verdict-trust と同じ構造）。
4. **~~機能 status のスコープ粒度~~** → **決着: `none`/`oos` を維持し `oos_reason` を必須化**（§2.1 末尾）。
5. **~~`kind: enabler` の検証形式~~** → **決着: 3分割**（作動状態=OSI正規欄 / 数値=`custom_detail`＋命名規約 /
   他車予測=間接検証＋`debug` exposure）。OSI 3.7.0 の実物調査に基づく。詳細と既存の配線抜け W1-W4 は **§2.2**。

### 未決（次に詰める）
- ~~`signal` の source_of_truth を frame契約に置くか OSI/.proto まで広げるか~~ → **決着: OSI/HVD を
  canonical**、frame は派生投影、exposure タグで面を区別（§4）。根拠は pitch/roll の④誤分類回避（§2/§3）。
- `gate` を要求単位で結ぶ(`sustained-by`)か、matcher単位で結ぶか（粒度の一貫性）。
- パイロット列: **pitch/roll物理**（空スパインの威力実証）と **VD-AD**（B1経路計画と接続）の順序。
- **`custom_detail` のキー命名規約**（§2.2 の(a')に必須）: 文字列KVなので規約が無いとゴミ箱化する。
  接頭辞（`gt.` 名前空間）・単位の埋め込み方・数値精度・**どのキーが verdict に使ってよいか**の区別を決める。
  実装の第一号は AEB の TTC/a_req（W3）が自然＝実例を持って規約を固める。
- **`vehicle_automated_driving_function` の粒度**: VD の policy 6種を OSI の `Name` 列挙24種へどう写すか。
  `policy:aeb`→`NAME_AUTOMATIC_EMERGENCY_BRAKING`、`policy:lead`→`NAME_ADAPTIVE_CRUISE_CONTROL` は自明だが、
  `policy:conflict`/`policy:crosswalk`/`policy:stop_yield` に対応する標準名が無い（`NAME_OTHER`＋`custom_name` 行き）。
  また `GT_esminiLib.cpp` の `size()>=24` 転送条件（W1）を維持するか作り直すか。
- **OSI-GT 正当性の要求化の形**（上記決着3の実装）: `req-*` を新設するか `feature` に載せるか。
  受入基準の候補＝ `osi-validation`(.proto由来ルール) 適用 ／ 既知シナリオでの GT 値の解析解比較 ／
  フレーム欠落・単位・座標系の不変条件。**独立GTが無い所で golden に逃げない**こと（§1.1 の轍）。
- **意味論検査＝採用+自作の混成（ODR/OSC・§1.1）**: 独立linterは自作でなく **ASAM qc-framework を採用**するのが筋。
  ODR側 `qc-opendrive`(22 check)で lane-link/junction/幾何連続を即獲得、OSC側 `qc-openscenario`、出力側 `osi-validation`。
  **残差＝lane-id符号/RHT-LHT/road間reciprocity は標準(thirdparty/opendrive §10/§11)に規範定義済→自作 checker bundle 化**
  （framework上・任意言語）＝**upstream貢献候補**。
- **LHT正しさ検証＝2層**（§1.1 最難case）: (静的資産) @rule↔lane-id符号↔走行方向の整合＝qc checker bundle 自作
  （標準: RHTは`<right>`負id/`<left>`正id、LHTはミラー）。(動的挙動) エンジン/VDのLHT挙動＝**メタモルフィック
  (RHT↔LHTミラー対称)＋差分(別エンジン参照)**。いずれも **fork governance(census/drift=会計)とは分離**。
- **意味論検査の適用範囲**: ODR rm/motion golden を conformance fixture 宇宙から
  **VD検証/カタログの実資産**へ広げるか（verdict-trust を全資産に効かせるため）。
