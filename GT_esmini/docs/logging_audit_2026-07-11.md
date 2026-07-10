# ログ出力監査（2026-07-11）— stdout/stderr 規律と失敗原因の可視性

本書は、GT_esmini 全体（esmini コア / GT 拡張 / Python・Web 層）のログ出力を監査し、
「stdout と stderr の使い分けがおかしい」「シナリオ実行失敗時に原因が見えない」問題の
全所在と是正計画を固化するものである。調査は静的解析（C++ 基盤・Python 層）と
実測（失敗ケース別の stdout/stderr 分離観測）の 3 系統で実施した。

- **状態**: 監査完了・是正未着手（提案段階）
- **結論**: 根本原因の大半は **esmini コア側**にある（全レベル stdout 出力・エラー原因取得 API の欠如）。
  ただし R1 Clean Core ポリシーによりコアは修正せず、コアの既存内部 API
  （`txtLogger.RegisterCallback`）を GT_esminiLib からフックすることで**コア無変更で実質是正**する。
- **問題数**: コア 13 件（CORE-1〜13）/ GT 拡張 6 件（GT-1〜6）/ Python・Web 8 件（WEB-1〜4, PY-1〜4）

---

## 1. あるべき姿（本書が定義するストリーム規律）

| ストリーム | 役割 |
| :--- | :--- |
| **stdout** | ユーザーが要求した出力のみ（usage/ヘルプ、機械可読な結果データ） |
| **stderr** | 診断のすべて（進捗、info/warn/error ログ、エラーサマリ） |
| **exit code** | 0=成功、非0=失敗。失敗時は stderr 末尾に原因 1 行サマリ |
| **ファイルログ** | ジョブ（実行）単位に紐付け、詳細診断の一次ソース |
| **プログラム間** | テキストスクレイプではなく、レベル付きログコールバック＋最終エラー取得 API |

現状はこの規律のほぼ全項目に違反している（以降の各節）。

---

## 2. コア側の問題（EnvironmentSimulator — R1 により直接修正しない）

コアのロガーは spdlog ではなく自作 `TxtLogger`
（`EnvironmentSimulator/Modules/CommonMini/logger.hpp` / `logger.cpp`、
グローバル単一インスタンス `txtLogger`、fmt はフォーマットのみ使用）。
レベルは debug/info/warn/error、既定 info、`--log_level` で制御。
ファイルログは `log.txt`（`logfile_path` オプション、`SE_SetLogFilePath` はその設定糖衣）。

### CORE-1: 全レベルが stdout のみ、stderr コンソールシンク不在【最重要】
- `logger.cpp:356` `fputs(msg.c_str(), stdout);` — error も info もすべて stdout。
- stderr が使われるのは `CreateLogFile()` の例外ハンドラ内（logger.cpp:171, 177）のみ。
- 帰結: `2>` リダイレクトでは何も拾えず、stdout をパイプ処理すると結果とエラーが混流する。
  実測でも**全失敗ケースで stderr は 0 行**だった（§5）。
- 対処: GT ログコールバックでレベル別に振り分けて吸収（Phase 2）。upstream 貢献候補（§7）。

### CORE-2: XML 構文エラーが `[info]` レベルで出力される
- 実測ケース c（壊れた xosc）: `[info] ...broken.xosc: Error parsing start element tag at offset ...`。
  `[error]` ではなくタイムスタンプ欄も空 `[]`（ロガー初期化前の出力）。
- 帰結: severity タグで `[error]` を grep する監視は XML 構文エラーを取りこぼす。
- 対処: コールバック側で "Error parsing" パターンを error に昇格（Phase 2）。upstream 貢献候補。

### CORE-3: 同一失敗が 2〜3 回重複ログされる
典型パス（xosc 不在の例）:
1. `ScenarioReader.cpp:314` `LOG_ERROR_AND_QUIT(...)` — 自らログしてから `std::runtime_error` を throw
2. `playerbase.cpp:1861` の catch が `LOG_ERROR("Exception: " + e.what())` で**再ログ**して return -1
3. `esminiLib.cpp:427` `InitScenario()` が戻り値を見て `LOG_ERROR("Failed to initialize scenario player")` で**再々ログ**
- 実測 d/e では `[error] Exception: [] [error] <同文>` という二重プレフィックス行も観測。
- 対処: 表示側は「log.txt 末尾の最終原因を抽出」する設計とし重複に依存しない（Phase 1）。upstream 貢献候補。

### CORE-4: エラー原因を機械的に取得する公開 API が存在しない【最重要】
- 失敗の戻りは一律 `int -1`。`SE_GetLastError` / `SE_GetLastErrorMessage` は**存在しない**
  （esminiLib.hpp 全走査で確認）。ログ用の公開コールバック C API も無い
  （Parameter/Object/Condition/StoryBoard/Image 用はあるがログ用は無い）。
- 内部 `scenarioEngine->GetInitStatus()`（playerbase.cpp:1870）も C API に露出していない。
- 呼び出し側の選択肢は ① stdout スクレイプ ② log.txt を読む ③ 静的リンクして
  `txtLogger.RegisterCallback()` を直接叩く（logger.cpp:106、最大 100 個。
  GT ユニットテストが実際に採用: `test_OdrVirtualJunction.cpp:133`, `CommonMini_test.cpp:411`）のみ。
- 対処: ③ の手法で GT_esminiLib が新 C API を提供（Phase 2 の柱）。**コア無変更で解決可能**。

### CORE-5: `--disable_stdout` 指定時、失敗がコンソール上で完全に沈黙する
- 実測: 失敗ケース b〜e ＋ `--disable_stdout` では stdout=バナー 5〜6 行のみ・stderr=0 行。
  原因は cwd の log.txt にのみ記録される。
- 帰結: コンソール出力を抑止したヘッドレス運用（まさに自動実行系）で最も原因が見えない。
- 対処: gt_lib.py は `--disable_stdout` ＋ログコールバック受信に移行するため実質解消（Phase 2）。

### CORE-6: exit code が失敗種別を問わず一律 -1
- 実測で全失敗ケース exit=-1（0xFFFFFFFF）。シェルにより 127（Git Bash）/255 系に見える点も罠。
- 非ゼロなので検知は可能。原因分類は exit code では不能 → 受容し、原因は CORE-4 対処で取得する。

### CORE-7: ロガー内部のストリーム不整合
- `CreateLogFile()` 内: ファイルオープン失敗（非例外）は `std::cout`（logger.cpp:150, 164）、
  例外は `std::cerr`（171, 177）。同一関数内で不一致。upstream 貢献候補（軽微）。

### CORE-8: 致命／非致命の判定が LOG レベルから区別できない
- `ScenarioReader.cpp`: カタログ「ロード」失敗は継続する `LOG_ERROR`（1554）なのに、
  カタログ「エントリ照合」失敗は throw する `LOG_ERROR_AND_QUIT`（1561）。
  ほかに継続系 error: コントローラ localize 失敗（1146）、エンティティ解決失敗（1669）、road 未検出（1818）。
- 帰結: 同じ `[error]` 表示で挙動（続行 vs 中断）が異なり、ログだけでは実行が死んだのか判別不能。
- 対処: 「失敗判定は exit code / rc、原因特定はログ」という役割分担を全層で徹底（本書全体の方針）。

### CORE-9: 正常系でも `[error]` が出る（error スクレイプは誤検知する）
- 実測 a1（正常終了 exit=0）でも `[error] Unsupported object type: rail-pole` 等（e6mini.xodr 由来）を観測。
- 帰結: 「stdout に error 文字列があれば失敗」というヒューリスティックは使用禁止。判定は exit code で行う。

### CORE-10: バナーの多重出力による stdout 汚染
- ロガー初回出力時のバージョン情報（logger.cpp:351-355）＋ `LogVersion`（303）が stdout に混入。
  `--disable_stdout` でもバナー数行は出る（実測 a2: 5 行）。受容（実害小）。

### CORE-11: 上流コードにもロガー迂回が存在
- `OSIReporter.cpp:427` `wprintf(...WSAGetLastError())` → stdout 直書き。
  レベルフィルタ・log.txt・コールバックのいずれも通らない。upstream 貢献候補（軽微）。

### CORE-12: パース失敗ログの位置情報・粒度不足
xosc / xodr のパース失敗で「何がどこで壊れているか」を特定するには粒度が足りない。

- **XML 構文エラー（well-formed 違反）**: xosc は `ScenarioReader.cpp:142`、xodr は
  `RoadManager.cpp:5661/5688` で pugixml の結果をそのままログ
  （形式: `Error parsing start element tag at offset (character position): 396`）。
  - 位置は**文字オフセットのみ**。行番号・列番号への変換はコードベースのどこにも存在せず、
    数 MB の xodr ではオフセット値だけでは人間には特定不能。
  - **この 2 箇所の本文にはファイル名が含まれない**（レベルは LOG_WARN）。
    実測ケース c で見えたファイル名付き `[info]` 行は、パラメータ分布ファイル候補として
    先に試行される `OSCParameterDistribution.cpp:63` の LOG_INFO であり、別経路。
    いずれにせよ構文エラーは error レベルでは出ない（CORE-2 と同根）。
- **意味論エラー（要素・値レベル）**: 要素名・属性値・road/junction id 付きで個別に
  LOG_ERROR/LOG_WARN が出る（`Unsupported object type: {}`、
  `Virtual connection {} in junction {} lacks <predecessor>/<successor>, skipping` 等、
  RoadManager.cpp に多数）。ただし DOM パース後のため **XML 内の位置情報（行番号/XPath）はゼロ**で、
  大半は非致命として黙って続行する（CORE-8/9 と連動）。
- **スキーマ検証はエンジン内に存在しない**: XSD 的に不正だが well-formed なファイルは
  構文チェックを素通りし、個別チェックに当たった要素だけ散発的に報告、残りは無言で無視される。
  厳密検証はエンジン外の ODR 適合ハーネス（`run_odr_conformance.py` schema レイヤー、xodr のみ）が
  外付けで担っているのが現状。xosc 側には相当物が無い。
- 対処: 行番号は `result.offset` からの変換（オフセット→行/列はファイル読みで機械的に計算可能）を
  Phase 2 のコールバック中継層または表示側で付与するのが現実的。upstream 貢献候補
  （pugixml オフセット→行番号変換＋ファイル名付与は本家にもそのまま価値がある）。

### CORE-13: xosc 失敗の診断性 — 参照系は実用、位置特定と「不発火」は非実用
シナリオ（xosc）側の失敗ログの実用性を分類すると:

- **実用レベル（値が名指しされる）— 参照解決系**:
  `Failed to look up entry {} in catalog {}`（ScenarioReader.cpp:1561）、
  `AddEntityAction: Failed to resolve entityRef {}`（2479）、`Failed to resolve road id {}`（2042/2073）、
  `Error: Trailer {} not found`（805）、`Expression syntax error: {}`（Parameters.cpp:469）。
  失敗した値そのものが出るため、xosc を grep すれば該当箇所に到達できる。
- **非実用（型は出るがインスタンスが特定できない）— 属性・要素エラー**:
  必須属性欠落は `missing required attribute: <要素名> -> <属性名>`（Parameters.cpp:487、
  ScenarioReader 内の required ReadAttribute 14 箇所から到達）、ほか
  `LateralDistanceAction: Mandatory attribute entityRef is missing`（3104）等。
  **どの Story/Act/Event/Action インスタンス内かは一切出ない**（行番号・XPath・包含要素名なし）。
  同型アクションが多数ある実務シナリオでは該当箇所を特定できない。
  `Unexpected element` 系（多数）は要素名のみで、大半は非致命として続行（CORE-8 と連動）。
- **無音（最悪）— 実行時の「条件が発火しない」**:
  トリガ発火時は良質なログが出る（`OSCCondition::Log` OSCCondition.cpp:188-198 —
  条件名・値・delay・`GetAdditionalLogInfo()` の実測値、発火エンティティ列挙 :375）。
  しかし**発火しなかった条件については评価値のログが一切存在しない**（debug レベルにも
  周期的な条件評価ログは無い。:295 の `Registered {} value` は一部条件の値登録のみ）。
  「シナリオはロードされ走るが何も起きない」という実務上最頻の失敗モードで診断材料がゼロになる。
- 対処: インスタンス文脈（包含 Story/Act/Event 名）付与と条件評価の周期 debug ログは
  コア改修が必要なため upstream 貢献候補。フォーク側の現実解は、VD テレメトリ／OSI 経由の
  状態可視化と、Phase 1 の log.txt 抽出で「最後に遷移した storyboard 要素」を併記する運用。

---

## 3. GT 拡張側の問題（GT_esmini — 修正対象）

Grep 全数調査: ロガー迂回の直接出力は **GT_Sim/main.cpp 44 件（ロガー使用ゼロ）**、
`GT_esmini/src/` 全 13 ファイルに cout/printf 系 47 件＋ cerr 系 22 件（LOG_* と混在）。

### GT-1: GT_Sim.exe が 100% ロガー迂回、致命エラーが stdout に出る
- `GT_esmini/GT_Sim/main.cpp`: 起動バナー（281, stdout）、`PrintUsage()` 全 printf（87-107, stdout）、
  **致命失敗 `"Failed to initialize GT_esmini"` も printf=stdout（445）**。
  警告類のみ stderr（--hz 344、--param 404/485、リネーム 151、キャプチャ 599）。
- あるべき姿: usage/バナーは stdout のまま、エラー・警告は stderr ＋ 失敗時最終行に原因サマリ（Phase 3）。

### GT-2: GT_esminiLib.cpp の無条件デバッグトレースがリリースでも stderr に常時出力
- `[GT_esmini] GT_InitWithArgs called with argc=...`（681, 683）、`Calling SE_InitWithArgs...`（854, 856）、
  `Sanitizing filename`（725）等。レベル制御・log.txt 記録・コールバック配信のすべてをバイパス。
- 実測: GT_Sim 正常実行でも stderr に 5 行出る。**現状「stderr に出る唯一のもの」が消すべきノイズ**という逆転。
- 対処: LOG_DEBUG 化（Phase 3）。

### GT-3: 同一ファイル内で cout/cerr の使い分け基準が無い
- `GT_esminiLib.cpp`: ステータスは `std::cout`（"GT_Init: AutoLight enabled" 936 等、計 13 件）、
  診断・エラーは `std::cerr`（"Failed to create sanitized scenario file." 564/739、
  "Failed to reload XOSC..." 625/917 等、計 13 件）。機能ごとに恣意的。
- 対処: 全て LOG_* に統一（Phase 3）。

### GT-4: 周辺モジュールの直接出力
- `GT_esmini/src/io/GT_UDP.cpp`: ソケットエラーを `std::cerr` 直書き（42, 81 ほか計 7 件）。
- `GT_esmini/src/osi/GT_OSIReporter.cpp:369`: `wprintf(L"send failed...")` → stdout。
- 対処: LOG_* 化（Phase 3）。

### GT-5: GT_esminiLib.dll がログコールバックを未使用、原因を呼び出し側へ渡す手段が無い
- `GT_esminiLib.cpp` に `RegisterCallback` 使用 0 件。CORE-4 の唯一の回避路（静的リンク＋内部 API）を
  DLL 自身が使っておらず、`GT_InitWithArgs` 失敗は rc=-1 の数値のみが Python 側へ届く。
- 対処: Phase 2 の柱（`GT_SetLogCallback` / `GT_GetLastError` 新設）。

### GT-6: 正常終了時に `[error] Failed closing socket 10093` が出る
- 実測: GT_Sim 正常系（exit=0）の stdout 末尾で観測（WSANOTINITIALISED — シャットダウン順序の問題）。
  CORE-9 と同じく error スクレイプ誤検知の原因。終了順序を修正するか warn に降格（Phase 3 で対応）。

---

## 4. Python / Web 層の問題

### WEB-1: バックエンドに logging 設定がゼロ【配布物でログ喪失】
- `GT_esmini/web/backend/` 全体に `basicConfig`/`dictConfig`/ハンドラ/レベル設定が皆無（grep 確認）。
  各モジュールは `logging.getLogger(__name__)` のみで、uvicorn の既定ルート設定に全面依存
  （`main.py:231` の `uvicorn.run` も `log_config`/`log_level` 未指定）。
- 帰結: **PyInstaller/Electron 配布経路では `uvicorn.run` を通らずルートハンドラ不在** →
  ジョブ起動（simulation_runner.py:599）・失敗（:646）・kill 診断まで含む
  `_logger.info/warning/error` が黙って捨てられ得る。構造的に最大のギャップ。

### WEB-2: GT_Sim.exe のファイルログがジョブに紐付かず、UI に真因が届かない【最重要（Web）】
- `_build_cmd`（simulation_runner.py:397-449）は `--logfile_path` を渡さない →
  esmini の詳細ログは `cwd=REPO_ROOT`（Popen cwd, :602）の **`log.txt` 1 ファイルを毎回上書き**。
- 一方 UI へ返る `error_message` は「stderr の先頭 2000 文字」（:640）だが、
  CORE-1 により**コアは stderr にほぼ何も書かない**ため、UI のエラーは薄いか GT-2 のノイズのみ。
  真因が書かれた唯一の場所（log.txt）は上書きされ、ジョブとの対応も失われる。
- なお stdout/stderr 自体は `results/<job>/stdout.txt`/`stderr.txt` に保存される（:629-630）。
- 対処: ジョブ毎 `--logfile_path` ＋ UI エラーは log.txt 末尾の `[error]`/Exception 行抽出へ（Phase 1）。

### WEB-3: `import sys` 欠落の確定バグ（Windows のキャンセル/kill で NameError）
- `simulation_runner.py` の import は asyncio/json/logging/os/shutil/signal/subprocess/threading/uuid/aiosqlite
  のみだが、`sys.platform` を **791, 811, 827, 882 行**（`cancel_simulation` / `kill_all_running`）で使用。
- 実プロセス kill 時のみ通るパスのため休眠中だが、Windows で初回キャンセルした瞬間に
  `NameError: name 'sys' is not defined` となり失敗処理自体が失敗する。即修対象（Phase 1）。

### WEB-4: ブリッジ/レコーダ失敗が UI に一切出ない
- `_logger.warning` のみ（simulation_runner.py:590/597/610）でサーバーログ止まり。
  WEB-1 併発時は完全に無音。対処: ジョブの警告フィールドに集約（Phase 1 で軽く、本修正は任意）。

### PY-1: gt_sim_test.py — DLL 出力と Python print が同一 stdout に混流・分離不能
- `gt_lib.py:45` の `ctypes.CDLL` は GT_InitWithArgs/GT_Step/GT_Close/telemetry/OSI の 5 関数のみバインド。
  **ログコールバック無し・`--disable_stdout` も渡さない**（:255）→ DLL がコンソール直書きし、
  ハーネスの `[run]/[compare]/[assert]/[batch]` print（gt_sim_test.py:320 ほか）と fd1 上で混流。
  リダイレクトでは分離不可能。
- 失敗時は `RuntimeError(f"GT_InitWithArgs failed (rc={rc})")`（:272）— **数値 rc のみ**で原因文字列なし。
- 数少ない良例: テレメトリ実データは stdout でなく `telemetry.jsonl`（ファイル）に書く設計、
  および no-telemetry 時の stderr ヒント（:322 「ego に VirtualDriverController を割り当てたか？」）。
- 対処: Phase 2（コールバック受信＋disable_stdout）で根治。進捗行の stderr 移行は同時に実施。

### PY-2: gt_sim_test の exit code 仕様の穴
- `compare` サブコマンドは**常に exit 0**（:1342）。`run` は frames>0 で 0（:1338）、
  `assert`/`batch` は verdict ∈ {pass, needs-review} で 0。compare だけ失敗を返せない。

### PY-3: validate_catalog / ゲートスクリプトの失敗詳細トリミング
- `validate_catalog.py`: サブプロセス失敗の詳細は「stderr（空なら stdout）の**末尾 3 行**」のみ
  （:192, :214）。CORE-1 により stderr はほぼ空 → stdout 末尾 3 行に真因が入る保証なし。
  最終 `[ERROR] Validation failed` のみ stderr（:390）。
- `run_gt_tests.ps1` / `run_regression_gate.ps1`: 全出力 `Write-Host`（情報も失敗も同一扱い）。
  exit code 連鎖は正しい。
- 対処: 詳細抽出を log.txt 末尾 [error] 行ベースへ（Phase 1 と同型の抽出関数を共有）。

### PY-4: run_odr_conformance.py のスクレイプ依存（既知の回避策・現状維持）
- DLL プローブを**意図的に別プロセス化**し（stdout 洪水・クラッシュ隔離のため、:24, :230, :565-597）、
  merged stdout+stderr を正規表現で `[ODR-UNSUPPORTED]` 等マーカー解析（:600-646）。
- 現状リポジトリ内で唯一「C++ 出力を計画的に捕捉」している箇所だが、本質はテキストスクレイプ。
  Phase 2 完了後にコールバック方式へ移行可能（ただし動作実績があるため急がない）。

---

## 5. 実測結果（failure モード別の実挙動）

環境: `build/.../Release/esmini.exe`（v3.4.1-506）、GT_Sim は DriverScript/bin のステージング品。
全ケース `--headless --fixed_timestep 0.01`、exit code は PowerShell `$LASTEXITCODE`。

| ケース | exit | stdout | stderr | log.txt | 原因が一目で分かるか |
| :--- | :--- | :--- | :--- | :--- | :--- |
| a1. 正常（cut-in.xosc） | 0 | 105 行（全部 `[info]`＋バナー） | **0 行** | 生成（stdout のミラー） | — |
| a2. 正常＋`--disable_stdout` | 0 | 5 行（バナーのみ） | 0 行 | 生成（フル） | — |
| b. xosc 不在 | -1 | 17 行: 探索パス4件 `[info]` → `[warn]` → `[error] Couldn't locate OpenSCENARIO file` | **0 行** | 原因記載あり | ○（info に埋没気味） |
| c. 壊れた XML | -1 | 6 行: **`[info]`** `Error parsing start element tag at offset ...` | **0 行** | 原因記載あり | △（CORE-2） |
| d. xodr 不在 | -1 | 23 行: `[error] Failed to locate OpenDRIVE file` ＋同文 Exception 行（重複） | **0 行** | 原因記載あり | ○ |
| e. カタログ解決不能 | -1 | 22 行: `[error] Failed to look up entry ... in catalog VehicleCatalog` ＋重複 | **0 行** | 原因記載あり | ○（rail-pole ノイズ同居） |
| b〜e＋`--disable_stdout` | -1 | バナーのみ。**エラー完全不可視** | 0 行 | **原因はここにのみ** | ×（沈黙失敗） |

GT_Sim.exe 比較:

| ケース | exit | stdout | stderr |
| :--- | :--- | :--- | :--- |
| 正常 | 0 | 161 行（コアログ全部＋末尾 `[error] Failed closing socket 10093`） | 5 行（GT-2 のトレース） |
| xosc 不在 | -1 | 20 行（コアのエラー＋最終行 `Failed to initialize GT_esmini`） | 6 行（トレース＋`SE_InitWithArgs returned: -1`） |

補足: GT_Sim.exe は DLL 未ステージング環境では **stdout/stderr とも 0 バイトのまま 0xC0000135 で即死**
（原因表示なし）。起動前の DLL 存在チェックは対処不能（プロセス起動自体が失敗）だが、
呼び出しスクリプト側で「exit=0xC0000135 → DLL ステージング不足」と翻訳する価値はある。

**機械監視の現状最適解**（是正前の運用ワークアラウンド）:
失敗判定は「exit code ≠ 0」、原因特定は「log.txt 末尾の `[error]`/Exception 行」。
stderr 監視・stdout の error 文字列 grep はどちらも機能しない（CORE-1/2/9）。

---

## 6. 是正計画（3 フェーズ、コア無変更）

### Phase 1 — 即効: Web の失敗可視化と確定バグ（目安 0.5〜1 日）
| # | 内容 | 解消する問題 |
| :--- | :--- | :--- |
| 1-1 | `simulation_runner.py` に `import sys` 追加 | WEB-3 |
| 1-2 | ジョブ毎に `--logfile_path results/<job>/log.txt` を付与 | WEB-2（上書き・非紐付け） |
| 1-3 | UI の `error_message` を「stderr 先頭 2000 字」から「ジョブ log.txt 末尾の `[error]`/Exception 抽出（重複除去・最終原因優先）」へ変更。抽出関数は validate_catalog とも共有 | WEB-2, PY-3, CORE-3 の表示側吸収 |
| 1-4 | バックエンドに logging 設定（dictConfig＋回転ファイルハンドラ、配布経路でも有効） | WEB-1, WEB-4 |

### Phase 2 — 本丸: ログコールバック基盤（目安 2〜3 日）
| # | 内容 | 解消する問題 |
| :--- | :--- | :--- |
| 2-1 | GT_esminiLib 初期化時に `txtLogger.RegisterCallback()` をフックし、新 C API を公開: `GT_SetLogCallback(fn)`（レベル付き全ログ配信）/ `GT_GetLastError(buf, len)`（error レベル最終メッセージ保持） | CORE-4, GT-5 |
| 2-2 | `gt_lib.py` で 2-1 をバインドし Python `logging`（→stderr）へ中継。DLL には `--disable_stdout` を渡してコンソール直書きを停止。`GT_InitWithArgs failed (rc=-1)` に原因文字列を併記 | PY-1, CORE-1/5 の実質解消 |
| 2-3 | コールバック中継層で "Error parsing" 系パターンを error に昇格し、文字オフセットを対象ファイルの行/列番号に変換して付記 | CORE-2, CORE-12 の吸収 |
| 2-4 | gt_sim_test の進捗行（`[run]` 等）を stderr へ移行、`compare` の exit code 是正 | PY-2, ストリーム規律 |

### Phase 3 — GT 拡張の出力一掃（目安 2〜3 日）
| # | 内容 | 解消する問題 |
| :--- | :--- | :--- |
| 3-1 | GT_esminiLib.cpp の無条件トレースを LOG_DEBUG 化、cout/cerr/wprintf 直書き（src/ 全体 約70 件）を LOG_* に統一 | GT-2, GT-3, GT-4 |
| 3-2 | GT_Sim/main.cpp: usage/バナーは stdout 維持、警告・エラーは stderr。失敗時は stderr 最終行に `ERROR: <GT_GetLastError の内容>` を 1 行出力 | GT-1 |
| 3-3 | GT_Sim の exit code 規律を文書化（0=成功 / 1=シナリオ初期化失敗 / 2=引数エラー）し main で明示 return | CORE-6 の GT 層での改善 |
| 3-4 | 終了時 `Failed closing socket 10093` の修正（WSACleanup 順序）または warn 降格 | GT-6 |

検証: 各 Phase 後に §5 の実測マトリクスを再実行し、
「stderr にエラーが出る／stdout 混流が消える／UI に真因が出る」を before/after で確認する。
Phase 3 後は `/gates`（ユニット＋回帰）を通すこと。

### スコープ外（受容）
- CORE-6（exit 一律 -1）: コア API 契約。GT_GetLastError で原因取得できれば分類 exit code は不要。
- CORE-8（致命/非致命の混在）: コアの設計判断。判定は rc、原因はログ、の役割分担で吸収。
- CORE-10（バナー stdout 汚染）: 実害小。
- PY-4（ODR ハーネスのスクレイプ）: 動作実績あり。Phase 2 完了後の任意移行。

---

## 7. upstream 貢献候補（esmini/esmini への issue/PR の種）

R1 によりフォークでは修正しないが、本家に還元価値があるもの:

| 問題 | 提案内容 | 温度感 |
| :--- | :--- | :--- |
| CORE-1 | コンソールシンクのレベル別振り分け（warn/error → stderr）またはオプション化 | 高（CLI 慣習との乖離） |
| CORE-2 | XML パースエラーの error レベル化 | 高（監視の取りこぼし） |
| CORE-12 | パース失敗ログへのファイル名付与＋文字オフセット→行/列変換 | 高（デバッグ実用性） |
| CORE-13 | 属性/要素エラーへの包含インスタンス（Story/Act/Event 名）付与、条件評価値の周期 debug ログ | 中〜高（「動かないシナリオ」の診断） |
| CORE-3 | LOG_ERROR_AND_QUIT → catch → 戻り値判定の三重ログ解消 | 中 |
| CORE-4 | `SE_GetLastErrorMessage()` または公開ログコールバック C API | 中（本家にも需要があるはず） |
| CORE-7 | logger.cpp 内の cout/cerr 不整合 | 低（ついで） |
| CORE-11 | OSIReporter.cpp:427 の wprintf → LOG_* 化 | 低（ついで） |

---

## 8. 主要ファイル索引

| 層 | ファイル | 関連問題 |
| :--- | :--- | :--- |
| コア | `EnvironmentSimulator/Modules/CommonMini/logger.cpp` / `logger.hpp` | CORE-1/2/7/10、フック点（RegisterCallback: logger.cpp:106） |
| コア | `EnvironmentSimulator/Libraries/esminiLib/esminiLib.cpp`（InitScenario 413-448 / SE_SetLogFilePath 463 / SE_LogToConsole 988） | CORE-3/4/5 |
| コア | `EnvironmentSimulator/Modules/PlayerBase/playerbase.cpp`（catch 1834-1873） | CORE-3 |
| コア | `EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/ScenarioReader.cpp` | CORE-3/8 |
| GT | `GT_esmini/GT_Sim/main.cpp` | GT-1 |
| GT | `GT_esmini/src/core/GT_esminiLib.cpp` | GT-2/3/5、Phase 2 実装点 |
| GT | `GT_esmini/src/io/GT_UDP.cpp`、`GT_esmini/src/osi/GT_OSIReporter.cpp` | GT-4 |
| Web | `GT_esmini/web/backend/services/simulation_runner.py` | WEB-1〜4 |
| Web | `GT_esmini/web/backend/main.py`（uvicorn.run 231） | WEB-1 |
| Py | `GT_esmini/scripts/verification/gt_sim_test.py` / `gt_lib.py` | PY-1/2、Phase 2 実装点 |
| Py | `resources/scenario_authoring/validate_catalog.py`、`scripts/run_odr_conformance.py` | PY-3/4 |
