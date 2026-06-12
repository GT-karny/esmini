# GT_esmini 技術的負債監査 & 開発ロードマップ

- 実施日: 2026-06-13(HEAD `8773c463`, branch `dev_v0.12`)
- 手法: 11領域並列監査 + 全指摘の敵対的再検証(25エージェント、1,036ツール実行)。**99件全件が証拠付きで確認済み**(refuted 0件。一部は数値補正のみの adjusted)
- 重大度: high 19 / medium 44 / low 36

---

## 1. 総評

コードベースは「動く」状態にあり、特に最新の VirtualDriver スタック(約2.9k LOC)はインターフェース分離・責務分割とも健全。Webバックエンドもモジュール化されており(main.py 236行+17ルーター)、TODO/FIXME密度は異例に低い。

一方で、負債は以下の5系統に構造的に集中している:

1. **フォークコピー3家系**(GT_OSIReporter 4.2k / GT_RoadManager 15k / roadgen 2.7k LOC)— upstream追従のたびに手動再同期が必要で、既に実害(セグフォルト修正の取り込み漏れ)が出ている
2. **凍結済みPython系の残置**— ビルド必須依存・~800行の二重実装(しかも堅牢化が凍結側にだけ存在)・Web/テスト/スクリプトへの結合
3. **ビルド/パッケージングの真実が3重化**— FMUターゲットはリンク不能で ALL_BUILD を壊しており、配布ZIPは設定ファイル欠落で開発環境と挙動が異なる
4. **手書きJSONパーサ5重実装**— 既に実バグ2件を生産(gear_ratio衝突、設定オフセット誤り)
5. **テスト不在**— 44.7k LOC に対しユニットテスト32ケース。CIはGTテストを一切実行していない

---

## 2. P0: 「既に壊れている」もの(即時修正)

| ID | 内容 | 影響 | 工数 |
|---|---|---|---|
| SCR-1 | upstream v3.0 マージが `scripts/dat.py` を削除 → `result_service.py:28` の import が**2026-04-21以降サイレント失敗**。`except Exception: return None` で握り潰し | Web の DAT→CSV 変換・メトリクスが約2ヶ月無言で死んでいる | S |
| CTL-3 | `RealVehicle.cpp:196` の `line.find("gear_ratio")` が `"reverse_gear_ratio": 1.5` に部分一致 → legacy `gear_ratio_` (3.5) が 1.5 に上書き | RealDriver/PythonDriver の OSI パワートレイン RPM 下限が **約2.33倍ずれて出力中** | S |
| CTL-2 | `HeadingCorrectionManager::LoadProfiles` がサブ文字列相対インデックスをファイル絶対の `extractBlock` に渡す(兄弟コピー `VehiclePhysicsManager.cpp:204` には修正済みの同コードが存在) | 現状はデフォルト値と設定値が偶然一致しており潜在。**設定変更がサイレント無視される** | S |
| CORE-1 | OSIフォークに upstream セグフォルト修正 `7a0844b1`(lane_link null チェック、esmini #780/#781)が未移植。`GT_OSIReporter_Geometry.cpp:382` に修正前パターンが残存 | 交差点処理でのクラッシュリスク。in-tree の upstream ファイルには修正済み | S |
| CORE-7 | `src/roadgen/`(2,731 LOC)が**6日間 untracked** のまま、追跡済み CMakeLists が既に参照 | CMakeLists だけコミット/stash すると configure 破壊。作業消失リスク | S |
| MSC-3 | `build_package.py` の CONFIG_FILES に `virtual_driver.json` / `route_drive_controller.json` が無い | **配布ZIPの VirtualDriver(主力機能)が調整済みゲインを失い無警告で別挙動** | S |
| FE-1 | `@osce/theme-apex` の dist/ が gitignore され、どのビルドスクリプトもビルドしない | **フレッシュクローンからフロントエンドがビルド不能** | S |
| VD-7 | RouteSignalScan が道路境界1スキャンステップ以内の信号をスキップ | 停止線制約の間欠的喪失(Phase3 firm-stop の信頼性に直結) | S |

これらは全て S 工数。**1〜2日でまとめて潰せる**。

---

## 3. 構造的負債の詳細(テーマ別)

### T1: フォークコピー家系(upstream同期負債)

- **GT_OSIReporter**(7ファイル 4,215 LOC): upstream OSIReporter.cpp の全47メソッドを再実装、非空行の84.6%が逐語一致。upstream の `9a2b159d` / `49f183df`(2026-03)も未移植 [CORE-1]
- **GT_RoadManager.cpp**(15,095 LOC): **実効パッチ十数行〜29行のための全文コピー** [CORE-2/BND-4]。さらにヘッダが「LHTパッチ1-B/1-C適用済み」と**虚偽記載**(実際は未適用)[CORE-3]
- **roadgen**(2,731 LOC): upstream roadgeom.cpp と99.5%一致 [CORE-7]
- **esminiJS(境界違反・最重要)**: コア `esminijs.cpp` が `gt_esmini/...hpp` を **#include する逆依存**(+867行のGT改変がコア内に存在)。v3.0.2マージ後に修理コミットが必要になった実績あり = R1規約「Clean Core」の実質的破綻箇所 [BND-1/FE-10/MSC-4]
- コア `esminiLib.cpp` にも GT デバッグ fprintf と SE_OpenOSISocket の機能変更が混入 [BND-2]
- 対称的に **GT_ScenarioReader はクリーンなサブクラス実装でフォーク負債ゼロ** — これが目指すべきパターン

### T2: 凍結Python系(v0.8凍結)の残置コスト

- `find_package(Python3 REQUIRED)` + `GT_ENABLE_EMBEDDED_PYTHON=OFF` 指定で **FATAL_ERROR** — 凍結機能が必須ビルド依存のまま [SUB-1]
- ControllerPythonDriver(1,290 LOC)は ControllerRealDriver の~800行クローン。**null チェック・heading 正規化・duration クランプ等の堅牢化が凍結側にのみ存在し、現役側に未バックポート** [CTL-1/SUB-2]
- Webバックエンドが import 時に DriverScript/scripts へ結合(実際に使うのは~25行のXMLヘルパー1個)[WEB-6/SCR-7]
- テスト資産の大半(ユニット25件、統合30/39件、専用CIジョブ)が凍結機能向け [TST-7/MSC-5]
- 凍結検証ツールチェーン~6,160 LOC が現役スクリプト置場に残置、しかも dat.py 削除で既に動作不能 [SCR-2]

### T3: ビルド/パッケージング

- `esmini_fmu` はソースリスト手動コピーがドリフトし**リンク不能**、かつ ALL_BUILD に含まれるため **CLAUDE.md Protocol A のフルビルドが失敗する**(build_package.ps1 は4ターゲット明示指定で回避中)[BLD-1/CTL-6/SUB-3]
- パッケージング手順が3重化(SKILL.md / build_package.ps1 / build_package.py)し既に分岐: SKILL.md は誤った venv・**存在しないCMakeフラグ `-DUSE_SDL2`**・Electron 取り込み前に ZIP 化、の3点で壊れたパッケージを生成 [BLD-2]
- グローバル `*.dll/*.exe/*.zip` ignore が追跡済み thirdparty に穴を開け、**フレッシュクローンは GT_Sim.exe が起動不能**(python312.dll 等が untracked)[BLD-3]
- 誰も使わない gt_* 内部静的ライブラリ5個 → **全GTソースが3回コンパイル** [BLD-4]
- 配布ZIPに MPL-2.0/EPL-2.0/BSD のライセンス文書が一切同梱されていない(コンプライアンス上の要修正)[Critic-1]

### T4: コピペ重複(C++/Web/FE)

- 手書きJSONパーサ×5(+VirtualDriver系に3)— 共有パーサ導入で根絶 [CTL-3/VD-2]
- TransitionDynamics shape 評価×4、交差点ターン方向判定×3、OSI/ライトグルー×3-4 [VD-3/VD-4/CTL-8/CTL-9]
- 単一呼び出しファサード4クラス8ファイル(純粋転送)[CTL-5/SUB-4]
- Web: `vd_bridge.py` ≒ `sv_bridge.py`(~210行、死んだレジストリ込み)、`vd_verify.py` は CLI のポートコピー [WEB-3/WEB-4]
- FE: WebSocketフック×3、SimulationRequest 組立×4、新旧ライブダッシュボード並存、762行の LiveSceneView、28 useState の RunForm [FE-3/FE-4/FE-5/FE-9/FE-11]

### T5: テスト/CI

- CI(run_tests.sh)は **upstream の9バイナリのみ実行、GTテストはゼロ** [TST-1]
- GT_RoadManager(15k)・OSIフォーク(5.4k)・CLAUDE.md が「ホットスポット」と明記する ManualDrive/Kinematic/LHT に**ユニットテストが1件も無い** [TST-2]
- GT_Loader 統合テスト39件は「クラッシュしなければ合格」でアサーション無し [TST-3]
- Webバックエンド8.3k LOC に pytest ゼロ(dev extras には宣言済み)[WEB-7/TST-5]
- VD Phase3 検証ハーネス(gt_sim_test batch)は再現可能で機械判定可能なのに**手動実行のみ・テスト戦略文書に不在** [TST-8]

### T6: 衛生(抜粋)

- `.temp.xosc` サニタイザの一時ファイルが資産ディレクトリに漏出(45ファイル、1件はgit追跡済み、回避ハックが5箇所に転移)[TODO-9]
- TerrainTracker は完全スタブで有効化手段なし、ドキュメントは動作機能として記載 [CTL-4]
- `GT_ENABLE_OSI_MOTION_REQUEST` はどの CMake も定義せず、7ファイル10箇所の #ifdef が恒久死蔵 [TODO-2]
- script/ と scripts/ の二重ディレクトリ(ルート・GT_esmini 双方)[SCR-3]
- マージ済みローカルブランチ21本未削除 [Critic-3]
- `LhtRhtHelpers.hpp`(LHT判定の正準ヘルパー)が**どこからも include されていない** — LHTホットスポットの再発要因 [Critic-4]

---

## 4. リファクタリング・ロードマップ

> 原則: 挙動安全網(R1のCIゲート)を先に張ってから大きい統合に入る。各段は独立にマージ可能。

### R0: 止血(1〜2日)
P0表の8件 + 追加衛生:
- dat.py 復元(`git show 9ea6992e^:scripts/dat.py > GT_esmini/scripts/dat.py`)+ 握り潰し except にログ追加
- gear_ratio 完全一致マッチ修正 / HCM オフセット修正 / CORE-1 nullチェック移植 / VD-7 境界条件修正
- roadgen を CMakeLists 変更と同一コミットで track(upstream基底SHAをヘッダに記録)
- CONFIG_FILES に virtual_driver.json / route_drive_controller.json 追加 + 「config/全追跡ファイル網羅」アサーション
- theme-apex prebuild フック追加
- .gitignore: scratch/ 追加、`!thirdparty/...` 例外 or 取得スクリプト方針決定 [BLD-3]
- 追跡済み `.temp.xosc` を `git rm`、温存中の tsbuildinfo 等も除去 [MSC-7]

**受入基準**: フレッシュクローン→ビルド→GT_Sim起動→パッケージ作成が一発で通る。

### R1: ビルド一本化 + CIゲート(3〜5日)
- `esmini_fmu`: GT_esminiLib_static へのリンクに切替(ソースリスト二重管理廃止)。当面は `EXCLUDE_FROM_ALL` で ALL_BUILD を即時回復 [BLD-1]
- パッケージング単一化: SKILL.md を `scripts/build_package.ps1 -Version` の薄いラッパーに。`-DUSE_SDL2`(実在しない)全削除、CLAUDE.md のスキルパス修正 [BLD-2]
- CI に GT テスト追加: `ctest -R '(test_|GT_esmini_)'` を run_tests.sh または新規ジョブへ [TST-1]
- **gt_sim_test batch を回帰ゲート化(V4の前倒し)**: phase3_batch.yaml を CI/コミット前フックで実行 — 以降の全リファクタの安全網 [TST-8]
- gt_* 未使用静的ライブラリ5個の削除(コンパイル3重→1重)[BLD-4]
- 配布ZIPへ LICENSE + 3rd_party_terms_and_licenses/ 同梱 [Critic-1]

**受入基準**: `cmake --build build --config Release`(ALL)成功。CI赤/緑がGTコードの挙動を反映する。

### R2: 凍結Python系の切り離し(1週間)
1. PythonDriver 側にのみ存在する堅牢化(null チェック・heading 正規化・duration クランプ)を **ControllerRealDriver へバックポート** [SUB-2]
2. `GT_ENABLE_EMBEDDED_PYTHON` を実オプション化(デフォルトOFF、FATAL_ERROR削除、find_package/リンク/テストを条件化)[SUB-1]
3. Web の import 時結合解消: `absolutize_scenario_paths`(~25行)をバックエンドへ vendor、`generate_python_variant` は遅延 import 化、`rm_lib` ctypes ラッパーを GT_esmini 管理下へ移設 [WEB-6/SCR-7]
4. 凍結検証ツールチェーン~6,160 LOC・関連テスト・「Python Driver (Recommended)」APIサーフェスを `archive/` へ移動 [SCR-2/WEB-5/TST-7]

**受入基準**: Python開発ヘッダ無しの環境で通常ビルドが通る。Webサーバが DriverScript 不在でも起動。

### R3: 重複統合(1〜2週間、機能開発と並走可)
- 共有JSONユーティリティ導入(最小トークナイザ or nlohmann/json)→ 8箇所移行 [CTL-3/VD-2]
- `control/common/` へ: TransitionDynamics 評価器、交差点ターン判定、OSI/ライトグルー、GetCurrentModuleDirectory [VD-3/VD-4/CTL-8/CTL-9/CTL-10]
- ファサード4クラス削除(直接呼び出し化)[CTL-5/SUB-4]
- ManeuverAwareSpeedPlanner::Plan(244行5段)のステージ分割 + 純粋ロジックのユニットテスト追加 [VD-5/VD-6]
- VirtualDriverConfig: キー定義の単一ソース化(41キー×3箇所編集の解消)、未公開5フィールドの公開可否決定 [VD-1]
- Web: vd_bridge/sv_bridge 統合、vd_verify を CLI 共有モジュール化 [WEB-3/WEB-4]
- FE: useWebSocketStream 共通フック、SimulationRequest ビルダー共通化、旧 OsiLivePanel 系廃止、LiveSceneView レイヤー分割 [FE-3/FE-4/FE-5/FE-9]
- `.temp.xosc` ライフサイクル修正(専用tempディレクトリ+起動時清掃)→ 5箇所のスキップハック削除 [TODO-9]
- TerrainTracker / GT_ENABLE_OSI_MOTION_REQUEST scaffolding の削除判断 [CTL-4/TODO-2]

### R4: フォーク維持戦略(継続的・各1〜3日)
- **同期ゲートスクリプト**: 各フォークファイルに基底SHAを記録し、upstream差分に未移植コミットがあれば CI で警告 [CORE-1 fix案]
- GT_RoadManager: 実パッチ(十数行)を抽出文書化し、虚偽のLHTパッチ記載を修正。可能なら GT_ScenarioReader 型のサブクラス/フック方式へ漸進移行 [CORE-2/CORE-3]
- esminiJS の GT 改変(+867行)を `GT_esmini/web/wasm/` へ移設、コアには注釈付き数行スワップのみ残す [BND-1]
- esminiLib.cpp の fprintf 除去・SE_OpenOSISocket 変更のフック化 [BND-2]
- ホットスポットのシームテスト: Kinematic/ManualDrive ステップ関数、AutoLight 状態機械、OSI ゴールデンメッセージ比較。LhtRhtHelpers.hpp を実際に採用 [TST-2/Critic-4]
- ブランチ清掃(マージ済み21本)、scripts/CLAUDE.md・GT_esmini/docs インデックス更新 [Critic-3/SCR-6/MSC-6]

---

## 5. 機能開発ロードマップ

直近60コミットの軌跡: 本製品は「esmini + 手動運転拡張」から「**フル物理の仮想人間ドライバー + 機械実行可能な検証工場**」へ移行中。Phase 3a–c(信号/標識/先行車)と注釈UIまで完成、次の中心は交差点交渉。

### F1: 検証データ供給ラインの確立(最優先・実装決定済み)
`scenario_authoring_foundation.md` の M-A…M-E を実装(scenariogeneration 0.16.5 採択済み、プロトタイプ動作確認済み):
- `resources/scenario_authoring/` 骨格、道路カタログ生成(T字路 xodr は**現在リポジトリに1本も無い**)、`priority_injector.py`(scenariogeneration は `<priority>` を出力しないため必須)
- シーン07/08 のパラメトリック量産(各10–30バリアント)→ **完成済みの注釈UIに初めてデータが流れる**
- 根拠: 注釈パイプラインは built but starved — 現コーパスは手書き24本のみ

### F2: Phase 3d — 対向車ギャップ受容(製品の本丸)
- `ConflictPointResolver` ポリシー(現状コード未着手、roadmap.md に設計のみ)
- TTCベースのギャップ判定、LHT/RHT 規則。検証シーン `07_oncoming_yield/` を F1 の量産基盤で同時整備
- リスク: 高(roadmap.md 自身の評価)。**着手前に R1 の回帰ゲートを必須とする**

### F3: Phase 3e — 無信号交差点の優先権
- OpenDRIVE `<priority>` 抽出(GT_RoadManager 側)+ `JunctionPriority` ポリシー
- 検証シーン `08_unsignalized_junction/`。F1 の priority_injector が前提

### F4: V4 — CI自動回帰 + レポート生成
- gt_sim_test batch の CI 組込(R1 で前倒しした分の完成形)+ 注釈データセットとの類似度マッチング自動判定
- 「Claude Code 自身が検証ループを回せる」という verification_environment.md §6.4 の設計意図の実現

### F5: Phase 4 仕上げ(UX・公開品質)
- ポリシー判断のリアルタイムGUIパネル、front-bumper 位置テレメトリ(P2申し送り)、任意FFB、OSI拡張フィールドへのテレメトリ
- **設定エルゴノミクス修正**: policy_*_enabled の絶対パス ConfigFile 注入回避(パス解決バグ)を正規修正 — 現状ユーザーが普通に有効化すると踏む
- ドキュメント刷新: README/CLAUDE.md は **直近60コミット(VirtualDriver/RouteDrive/Kinematic/検証スタック)が全く記載されていない**。HostVehicleData velocity の deprecated フィールド脱却、Electron アイコン

### 推奨順序(リファクタと統合)

```
週1     : R0 止血 → R1 ビルド/CIゲート
週2     : F1 シナリオ量産基盤(+R2 を裏で並走)
週3-4   : F2 Phase 3d(回帰ゲート有効状態で)
週5     : F3 Phase 3e + F4 CI回帰完成
週6以降 : F5 仕上げ + R3/R4 を継続消化
```

---

## 6. 全指摘一覧(ID索引)

詳細な証拠・修正案は監査ログ参照。severity: H=high, M=medium, L=low / effort: S/M/L

| ID | Sev | Eff | タイトル(要約) |
|---|---|---|---|
| CTL-1 | H | L | ControllerPythonDriver が RealDriver コアロジックの~500行クローン |
| CTL-2 | H | S | HeadingCorrectionManager 設定パースのインデックスバグ(潜在) |
| CTL-3 | H | M | 手書きJSONパーサ5重実装、gear_ratio 衝突バグ(実害中) |
| CTL-4 | M | S | TerrainTracker 完全スタブ・有効化不能のまま2ターゲットにコンパイル |
| CTL-5 | M | S | 単一呼び出し転送ファサード4クラス8ファイル |
| CTL-6 | H | M | GT_OSMP_FMU のソースリスト陳腐化 → FMU リンク不能 |
| CTL-7 | L | S | RealDriver/ObservedVehiclePhysics の死にメンバ・死に分岐 |
| CTL-8 | M | M | OSI/ライトグルーを3-4コントローラで再実装 |
| CTL-9 | M | M | 交差点ターン判定×3、TransitionDynamics×2 |
| CTL-10 | M | S | GetCurrentModuleDirectory の ad-hoc extern 宣言×5 |
| CTL-11 | L | S | 肥大ファイル+コメントアウトデバッグ残骸(AutoLight) |
| SUB-1 | H | M | 凍結Python スタックが必須ビルド依存(OFF指定で FATAL_ERROR) |
| SUB-2 | H | L | ~800行重複、堅牢化修正が凍結側のみに存在 |
| SUB-3 | H | M | FMU ハンドコピーリストのドリフト(CTL-6と同根) |
| SUB-4 | L | S | realdriver スタックの純粋転送4クラス |
| SUB-5 | M | S | motion_request レベルが恒久コンパイルアウトなのに UI/スキーマに残存 |
| SUB-6 | M | M | ManualDriveConfig の死に/誤配線ノブ(ffb_enabled 等) |
| SUB-7 | L | S | GT_UDP_Sender が upstream UDPClient を再実装 |
| SUB-8 | L | S | 共有コンポーネントが common/ でなく manualdrive/ に配置 |
| VD-1 | M | M | 41キー設定の3箇所編集、5フィールド未公開 |
| VD-2 | M | S | 行ベースJSONパーサ×3(VD系) |
| VD-3 | M | S | TransitionDynamics 評価器×4 |
| VD-4 | M | M | 交差点ターンジオメトリ重複(逐語×2、ロジック×3) |
| VD-5 | M | M | ManeuverAwareSpeedPlanner::Plan 244行5段モノリス |
| VD-6 | M | M | 最複雑コンポーネント(fold/ramp/パーサ/シリアライザ)テストゼロ |
| VD-7 | M | S | RouteSignalScan 道路境界近傍の信号スキップ(間欠) |
| VD-8 | L | M | テレメトリJSON契約の3言語手動同期・エスケープ/NaN無防備 |
| VD-9 | L | M | 1台あたり毎フレーム最大5回の独立ルートウォーク |
| VD-10 | L | S | クロスファイル整合義務のあるマジック定数散在 |
| VD-11 | L | S | PolicyConstraint YIELD/WAIT_UNTIL が宣言のみ |
| CORE-1 | H | M | OSIフォークに upstream セグフォルト修正未移植 |
| CORE-2 | M | M | GT_RoadManager 15,095行コピーの実パッチ十数行 |
| CORE-3 | M | S | GT_RoadManager ヘッダの LHT パッチ虚偽記載 |
| CORE-4 | M | M | GT_Init / GT_InitWithArgs の初期化パイプライン二重化+ドリフト |
| CORE-5 | M | L | GT_esminiLib.cpp god-TU(esminiLib.cpp を丸ごと include、509行関数) |
| CORE-6 | L | S | archive/temp_junction_logic.cpp 孤児 |
| CORE-7 | H | S | roadgen 2,731行 untracked、CMakeは参照済み(+第3のフォーク家系) |
| WEB-1 | M | M | osi3 が未宣言ランタイム依存(凍結 DriverScript の副作用で充足) |
| WEB-2 | M | S | gRPC gencode が protobuf>=6.31.1 要求、pyproject 下限 5.26 |
| WEB-3 | M | M | vd_bridge ≒ sv_bridge ~210行コピー(死にレジストリ込み) |
| WEB-4 | M | M | vd_verify.py が検証CLIのポートコピー |
| WEB-5 | M | S | 凍結機能のAPI面が「Recommended」表示で残存 |
| WEB-6 | H | M | バックエンドが import 時に凍結 DriverScript/scripts へ結合 |
| WEB-7 | M | M | バックエンド8.3k LOC にテストゼロ |
| WEB-8 | L | S | ポート8000×3箇所、9100×7+ファイルにハードコード |
| WEB-9 | L | S | PyInstaller hiddenimports が17中6モジュールのみ |
| WEB-10 | L | M | SQLite スキーマのバージョニング無し ad-hoc PRAGMA |
| WEB-11 | L | S | zip/前提チェックの py/ps1 二重実装 |
| FE-1 | H | S | theme-apex dist/ gitignore → フレッシュクローンビルド不能 |
| FE-2 | M | M | npm run lint 赤(20エラー) |
| FE-3 | M | M | 新旧ライブダッシュボード並存 |
| FE-4 | M | S | SimulationRequest 組立×4、型のインライン重複 |
| FE-5 | L | S | WebSocket フック×3 の~40行同型コード |
| FE-6 | L | S | 死にエクスポート(Tabs.tsx、makeMockMidLong) |
| FE-7 | L | S | ランタイム依存が devDependencies に誤配置 |
| FE-8 | L | S | 未使用 electron-builder ツールチェーン |
| FE-9 | M | M | LiveSceneView 762行 god-file |
| FE-10 | L | M | GTソースがコア esminiJS ターゲットにコンパイル(BND-1関連) |
| FE-11 | M | M | SimulationRunForm 28 useState |
| FE-12 | L | S | frontend/README が Vite テンプレートのまま |
| SCR-1 | H | S | dat.py 削除 → Web メトリクス4月からサイレント死 |
| SCR-2 | M | M | 凍結検証ツールチェーン~6,160行が現役置場に残置(既に非動作) |
| SCR-3 | L | S | script/ と scripts/ の二重ディレクトリ(2箇所) |
| SCR-4 | M | M | osi3 バインディング97ファイル×2 バイト同一 vendoring |
| SCR-5 | L | S | 一回限りプローブスクリプト残置、scratch/ 20MB が ignore 外 |
| SCR-6 | L | S | scripts/CLAUDE.md 陳腐化(削除済み dat.py を記載) |
| SCR-7 | M | L | Web が凍結 DriverScript パッケージに依存(186ファイルの archive 阻害) |
| SCR-8 | L | - | upstream スクリプト群はほぼ vanilla(健全) |
| BLD-1 | H | M | esmini_fmu リンク不能 + ALL_BUILD 内 → 文書化済みフルビルド失敗 |
| BLD-2 | H | S | パッケージング3重真実、SKILL.md は壊れたパッケージを生成 |
| BLD-3 | H | M | 半vendor状態のバイナリランタイム(フレッシュクローン起動不能) |
| BLD-4 | M | M | 未消費 gt_* 静的ライブラリ5個 → 3重コンパイル |
| BLD-5 | M | S | デバッグログ・276KB生成ヘッダ・迷子 script/ が git 追跡 |
| BLD-6 | L | S | .gitignore 欠落(scratch/、プローブ) |
| BLD-7 | M | L | 埋込Python のビルド配線(SUB-1 のビルド面) |
| BLD-8 | L | S | ルート build.ps1 が第4のビルド入口 |
| TST-1 | H | M | GTテストが ctest 登録済みなのに CI/スクリプトから実行されず |
| TST-2 | H | L | 最大リスクモジュール群(15k RoadManager 等)テストゼロ |
| TST-3 | M | M | GT_Loader 39件がアサーション無し(クラッシュしなければ合格) |
| TST-4 | L | S | 死にテストファイル simple_test.cpp |
| TST-5 | M | M | バックエンド pytest ゼロ(WEB-7同根) |
| TST-6 | M | S | 本番コードが test/ 内 comparison_thresholds.yaml を読み書き |
| TST-7 | M | M | テスト資産の大半が凍結機能向け |
| TST-8 | M | S | VD検証ハーネスが手動のみ・テスト戦略文書に不在 |
| TST-9 | L | S | .temp.xosc 追跡+再帰生成汚染 |
| BND-1 | H | L | esminiJS にコア→拡張の逆依存(+867行のGT改変がコア内) |
| BND-2 | M | M | コア esminiLib.cpp に GT fprintf + SE_OpenOSISocket 変更 |
| BND-3 | L | S | コア Unittest ディレクトリに凍結機能のテスト資産 |
| BND-4 | M | M | スワップ方式フォークコピーの構造記録(CORE-2 補強) |
| BND-5 | L | S | コア Controller.hpp に GT enum 残置(型ID規約不整合) |
| MSC-1 | M | S | ルートのデバッグ/temp 追跡クラッタ |
| MSC-2 | M | L | externals/osi 184MB git コミット |
| MSC-3 | H | S | パッケージが virtual_driver.json 等を未同梱 → 配布版が別挙動 |
| MSC-4 | L | M | esminiJS CMake への GT ソースハードワイヤ(BND-1関連) |
| MSC-5 | L | S | 凍結機能テスト~30 ctest がデフォルト登録 |
| MSC-6 | L | S | docs インデックスが VirtualDriver 文書を未掲載 |
| MSC-7 | L | S | tsbuildinfo / .temp.xosc の git 追跡 |
| MSC-8 | L | S | untracked 作業残骸の git status 汚染 |
| MSC-9 | L | S | GT_Sim/CMakeLists.txt はどこからも include されない死物 |
| Critic-1 | M | S | 配布ZIPにライセンス/notice 文書ゼロ(MPL/EPL/BSD 義務) |
| Critic-2 | L | S | CLAUDE.md のスキルパス誤記、旧式フラットスキル2件、settings.local.json 追跡 |
| Critic-3 | L | S | マージ済みブランチ21本未削除、シェル危険なブランチ名1件 |
| Critic-4 | L | S | 未使用ヘッダ2件(IGroundTruthPublisher、**LhtRhtHelpers** — LHTホットスポットの正準ヘルパー) |
