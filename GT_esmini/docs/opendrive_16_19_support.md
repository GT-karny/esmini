# OpenDRIVE 1.6–1.9 対応ステータス表(最終・§10-5 承認材料)

- 作成: 2026-07-05(P9b、計画 §5 P9b の命名成果物)
- 対象コード: dev_v0.12 系(P0〜P9a マージ済 + feature/odr1619-p9b)
- 目的: **クラスタ 0-22 × 対応レベル L1-L5 の正直台帳**。保留レベル(L2 以上の未実装)を漏れなく開示し、計画 §8「保留台帳」の承認(§10-5)の材料とする。
- レベル定義(計画 §2): **L1**=パース+格納+属性粒度診断(サイレント欠落ゼロ)/ **L2**=ランタイム意味論 / **L3**=OSI ground truth / **L4**=可視化(GT_RoadGeom/ビューワー)/ **L5**=オーサリング+検証フィクスチャ。
- 機械検証: `scripts/run_odr_conformance.py --profile full --check-matrix` = **350 PASS / 0 FAIL / 13 XFAIL / 0 XPASS / 2 SKIP**(2026-07-05、SKIP=rm 期待 fail 2 フィクスチャの motion 層 by-design スキップ)、**[ODR-UNSUPPORTED] 要素+属性 = 0**(公式36+手書き26+生成+リポジトリ内資産、トレーサビリティ行列全行充足)。

## 1. プログラム終了判定(計画 §5 P9b)

| 判定 | 結果 |
|---|---|
| 全フィクスチャで要素+属性の [ODR-UNSUPPORTED]==0 | **達成**(P9b で最後の pinned 属性 `road/type@country`(公式 Ex_Railway-Station)を L1 格納で解消。唯一の manifest 残 = `removed16_neighbor_16` の `road/link/neighbor|removed16` は **by-design 恒久台帳**: 1.6 削除診断 [ODR-REMOVED-1.6] を実証するための専用フィクスチャで、UNSUPPORTED 系ではない) |
| トレーサビリティ行列全行充足 | **達成**(--check-matrix OK、空行なし) |
| conformance full 全緑 | **達成**(350P/0F/0XP) |

## 2. クラスタ×レベル 対応状況表

凡例: **●**=実装済 / **◐**=部分(注記参照)/ **—**=保留(§3 の保留台帳に記載)/ n/a=そのレベルが概念的に不適用。

| # | クラスタ | L1 | L2 | L3 | L4 | L5 | 実装フェーズ / 注記 |
|---|---|---|---|---|---|---|---|
| 0 | 既対応 1.6-1.9 項目の回帰修正(direct junction / 文字列 road id / explicit roadmark) | ● | ● | ● | ● | ● | P1+クラッシュ修正パス([GT_ODR:direct-junc-log] upstream fmt バグ、[GT_ODR:sig-lanes-guard]) |
| 1 | バージョン認識基盤+サイレント欠落ゼロ診断 | ● | n/a | n/a | n/a | ● | P1 OdrSideModel+OdrCoverageAudit(ホワイトリスト 285 パス/714 ペア、1.4/1.5 コントロール警告ゼロ) |
| 2 | 1.9 スキーマ検証+フィクスチャ供給(ツーリング) | ● | n/a | n/a | n/a | ● | P0 3層ハーネス+local_schema(XSD 1.1 宣言昇格)。ASAM 資産=テスト時 zip 展開(§10-7) |
| 3 | レーン型+レーン属性 | ● | ◐ | ● | ● | ● | P2 [GT_ODR:lane-types](walking→SIDEWALK/curb→CURB/shared→BIDIRECTIONAL/slipLane→CONNECTING_RAMP)。**保留: lane @direction の L2**(§3-5) |
| 4 | レーンレイヤ+temporary/roadworks | ● | ● | ● | ● | ● | P8 [GT_ODR:lane-layers] permanent 選択+GT 側 s 範囲マージ。P9b で web API 公開。**保留: 実行時レイヤ切替**(§3-4) |
| 5 | crossing junction+crossPath(横断歩道) | ● | ● | ● | ● | ● | P5 [GT_ODR:junc-crossing]+CROSSWALK 合成(id 基底 9.0e8)。VD 譲り実証済(scene-09) |
| 6 | virtual junction | ● | ● | ● | ● | ● | **P6 ネイティブ**(R1 第2種緩和・in-place)。P9b で VJ メタデータ web API。**保留: v1 下位項目**(§3-3)。**PR-A〜D はローカル準備のみ・非提出**(2026-07-05 ユーザー決定) |
| 7 | junction priority+laneLink overlapZone | ● | — | n/a | n/a | ● | P5 OdrJunctionExtras(GetJunctionPriorities=F3 正典)。**L2 消費は F3(無期延期)**(§3-9) |
| 8 | junction boundary/elevationGrid 等 | ● | n/a | ◐ | — | ● | P7 OdrJunctionGeom L1+**旗付き L3**(authored boundary→OSI 輪郭、`GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY` デフォルト OFF)。**保留: 内部メッシュ L4**(§3-8) |
| 9 | junctionGroup(ラウンドアバウト) | ● | n/a | n/a | n/a | ● | P7 L1(junction_groups) |
| 10 | signal semantics 族+参加者 | ● | ● | ● | n/a | ● | P4 15 サブタイプ L1+**L2=国カタログ優先・semantics フォールバック**(stop/stopLine→STOP、yield→GIVE_WAY)+L3 型/速度補完。**保留: speed/lane semantics の L2**(ゾーン状態が必要、§3-10) |
| 11 | 動的信号近代化+countryRevision バグ修正 | ● | ● | ● | n/a | ● | P3 [GT_ODR:tl-gate](dynamic のみゲート)+[GT_ODR:country-rev]+nr_lamps_ 未初期化 upstream バグ修正。全資産分類監査 atomic |
| 12 | signal 配置+相互参照 | ● | ● | ● | n/a | ● | P3 [GT_ODR:sig-pos](positionRoad/Inertial)+[GT_ODR:sig-ref](signalReference クローン実体化)+dependency/reference L1 |
| 13 | VMS/可変表示板 | ● | — | — | n/a | ● | P4 boards+vmsGroup L1。**保留: 動的コンテンツ表示制御+ボード内容の OSI 出力**(§3-7)。vmsGroup ルート直下は XSD 欠陥で恒久 schema-fail(XFAIL 台帳) |
| 14 | header license+defaultRegulations | ● | — | n/a | n/a | ● | P4 L1(semantics パーサ再利用)。defaultRegulations は ASAM XSD 自体の欠陥で 1.8/1.9 とも検証不能=恒久期待フェイル。**L2(デフォルト速度等の適用)は保留**(cluster-14 ゾーン状態と同型) |
| 15 | additionalData: userData/include/dataQuality | ● | n/a | n/a | n/a | ● | P1 L1+P9a 注釈 UI 公開。**include=恒久ハードエラー仕様**(§10-6 確定、[ODR-INCLUDE] 診断+spec_fail 固定) |
| 16 | lane 詳細: access/rule/speed/border/sway | ● | ◐ | — | n/a | ● | P2 L1+border→width 正規化(フォーク0行)+lane speed L2(VD ManeuverAwareSpeedPlanner min 接続)。**保留: lane speed の OSI 出力 L3**(§3-9)、access/rule の挙動消費なし(格納のみ) |
| 17 | 断面: shape+crossSectionSurface | ● | ◐ | n/a | ◐ | ● | P7 L1+**明示 WARN 付き等価 superelevation 劣化**(θ=atan(b)、z-probe 機械検証)。**保留: ネイティブ t 依存 z 評価**(§3-2、将来課題=車両バンク物理まで・~3〜5.5週・計画 §8-2) |
| 18 | surface/CRG | ● | — | n/a | — | ● | P7 L1+ファイル存在診断+1.9 xy/h オフセット格納。**保留: CRG 実評価**(OpenCRG vendor 必要、爆風半径1位、§3-1) |
| 19 | object 幾何拡張+objectReference/bridge | ● | ● | ● | ● | ● | P7 [GT_ODR:curvelocal]×2+[GT_ODR:repeat-cubics]+合成(bridge 9.1e8 / objectReference 9.2e8)+GT_RoadGen 検証。連続アウトライン repeat 経路は横多項式非対象(文書化済みスコープ) |
| 20 | railroad+station | ● | — | — | n/a | ● | P9a L1+アクセサ+web 公開。**不活性と文書明記**(鉄道ランタイム/車両モデルなし、§3-6) |
| 21 | 1.6 廃止/削除処理 | ● | n/a | n/a | n/a | ● | P1 removed-in-1.6 テーブル([ODR-REMOVED-1.6] 診断)。恒久台帳: `removed16_neighbor_16` は診断実証フィクスチャ |
| 22 | junction connection/laneLink 1.9 layer 属性 | ● | n/a | n/a | n/a | ● | P5(fromLayer/toLayer)+P8(lane link @layer / validity @layer / laneSection @length シャドウ) |

補足:
- **web 公開**(P9a+P9b): 監査警告 / userData / semantics / priority / crosswalk / railroad / **レーンレイヤ(モードラッチ含む)/ virtual junction メタデータ** を GT_RM JSON C API+odr-metadata REST+注釈 UI パネルで閲覧可能。
- **wasm/esminiJS**(P9b): odr_side 全群を emscripten ビルドへ配線([GT_ODR:cmake] APPEND リストから configure 時抽出=ドリフト不能)。em++ 5.0.2 で全対象 TU 非互換ゼロ。ブラウザスモーク PASS(レーンレイヤ+VJ)。**手動スモーク運用・CI レグなし**(計画どおり)。

## 3. 保留台帳(L2 以上の未実装 — §10-5 承認対象。全項目 L1 は達成済み)

計画 §8 と同一実体。番号は §8 に対応。

1. **CRG 実評価**(18): OpenCRG ライブラリ vendor+Track2XYZ/標高評価書換が必要(爆風半径1位)。現状=属性格納+ファイル存在 WARN。
2. **crossSectionSurface/shape のネイティブ z 評価**(17): 同じく半径1位。現状=明示 WARN 付き superelevation 近似。**将来課題スコープ確定済**(2026-07-04 ユーザー指定): 目標=「車両が路面の曲面に沿って正しく傾く」まで(バンク走行物理含む)、見積 ~3〜5.5 週、着手前 Day-1 スパイク必須(hpp 不改変見込みの確定)。計画 §8-2 に詳細。
3. **virtual junction v1 保留下位項目**(6、P6 設計書 §9): kind-2(topological)接続のルーティング使用(parse/store のみ)/ orientation 方向フィルタ強制 / main-road span 上の junction-id 報告(v1 は false/−1、upstream #592 へ提起する設計だったが PR 非提出により未提起)/ VJ アンカーでの junctionSelectorAngle ランダム分岐(v1 はルート駆動のみ)/ lockOnLane 完全対応。
4. **レーンレイヤ実行時切替**(4): ロード時選択のみ(env `GT_ODR_LANE_LAYERS` プロセス毎ラッチ)。走行ごと再パースの Web ランナー運用と一致。
5. **lane @direction の L2**(3): 走行方向判断 6 箇所以上+LHT ホットスポット(パッチ 1-A)重複。判断箇所サーベイのスパイク(+~25 フォーク行、事前承認済み枠)を独立フェーズとして将来実施。
6. **railroad L2-L4**(20): 鉄道ランタイム/車両モデル/シナリオ需要なし。L1+API 公開、不活性と文書化。
7. **VMS 動的コンテンツ**(13): ライブ表示制御はシナリオエンジン機能でパース範囲外。静的ボード内容の OSI TrafficSign 値出力も保留(L1 格納まで)。
8. **junction 内部メッシュ**(8 の L4): boundary+elevationGrid からのメッシュ生成は消費者不在の GT_RoadGeom 新機能。L1+旗付き L3 輪郭まで。
9. **lane `<speed>` の OSI 出力**(16 の L3)+**junction priority の消費**(7 の L2=F3): 前者は osi3 のレーン速度制限帰属のマッピング判断待ち。後者は **F3 週(無期延期)** — GetJunctionPriorities アクセサは不活性で待機、ConflictPointResolver Evaluate 状態モデル改修(複数競合ラッチ)と同時実施推奨。
10. **speed/lane semantics の L2**(10)+**header defaultRegulations の適用**(14): 「標識通過後に上書きされるまで持続」のゾーン状態インフラが必要(同型 2 件)。アクセサ止まり。
11. **scenariogeneration ライブラリ更新**: revMinor=5 天井は odr_feature_injector で回避済み。ライブラリ更新は別のオーサリングスタック判断。

## 4. 既知債・特記事項(承認材料としての開示)

| 項目 | 状態 |
|---|---|
| **PR-VJ A〜D 非提出** | 2026-07-05 ユーザー決定。ブランチ pr/vj-a..d+upstream_pr/ に保管(131/131 緑)。**R4(ドリフト削減)アップサイドは縮小**=第2種 vj-* 編集(hpp74+cpp530+router127+looming8)は resync 毎に再適用が必要(リハーサル+チェックリストで機械化済み)。P10 の再評価はユーザー判断。 |
| **F3 無期延期** | ODR 計画側依存ゼロ。priority データは P5 で着地済み・不活性待機。 |
| **dat.py v5 未対応** | 記録側(esmini v3.4.0+)は DAT v5、読者 scripts/dat.py は v4 のみ。**P9b で明示エラー化**(v5 検出→HTTP 409+理由、dat.py exit(-1)=SystemExit のサーバ死も防御)。**フル v5 リーダは未実装の既知債**。 |
| **esmini_fmu(Protocol B)破損** | 既知の別件(BLD-1/SUB-3)。P9b の wasm CMake 拡張は将来の GT_esminiLib_static リンク修理と独立(コア CMakeLists から抽出する方式で悪化させない)。 |
| **挙動ゲート既知 FAIL 2 件** | phase3 `red_stop_green_go` / `green_no_stop` は pre-P6 からの VD 信号ポリシー未成熟(青後発進せず)。ODR 計画対象外(F 系スコープ)。ゲート解釈=「この 2 件を超える新規 FAIL ゼロ」。 |
| **恒久 XFAIL 13 件** | スキーマ層のみ: ASAM XSD 自体の欠陥(defaultRegulations/vmsGroup の abstract 必須子)、意図的不正フィクスチャ、既存資産の既知不適合(fabriksgatan_traffic_lights_ctrl 等)。manifest に全件根拠付き台帳化。 |
| **[GT_ODR:osi-path] 恒久 R1 例外** | GT の OSI 3.7.0 継続の間 upstream 収束不能(upstream は 3.5.0 を FATAL 強制)。台帳 §0b。 |
| **theme-apex prebuild 既知バグ(P9b 発見・未修理)** | `packages/theme-apex` の clean スクリプトが dist のみ削除し tsconfig.tsbuildinfo を残すため、`npm run build`(prebuild フック)が **毎回空ビルド**になり frontend ビルドが TS2307 で失敗する。回避=tsbuildinfo 削除後に tsc 直叩き(P9b はこの回避で実施)。恒久修理は clean に tsbuildinfo 削除を足す 1 行(web 側の別コミット判断)。 |
| **wasm カバレッジ=手動スモーク** | `GT_esmini/web/wasm/smoke/index.html`+resync チェックリスト項 26。CI レグ化は別判断(計画どおり)。 |

## 5. ガバナンス最終値(2026-07-05、機械真実源=台帳 §7 manifest)

- フォーク第1種: **100/150 行**・`[GT_ODR:` リテラル総数 75(古典 21 マーカー+vj-* 54)・fork-drift 94。
- 第2種: hpp **74/75** / pristine cpp **530/550** / router **127/220** / looming **8/10** / OSIReporter 0/30(PR-D 予約、未使用)。
- 常設ガード: census(二側)/ fork-drift / **resync-guards(P9b 新設: whitelist ドリフト・重複パス・handled-by-upstream 矛盾・合成 ID 離間)** が conformance 全プロファイルに内蔵、ctest `OdrForkPatches.*`+**`OdrResyncGuards.*`(P9b 新設: 合成 ID 重複ゼロ・再パース蓄積ゼロ)** が unit ゲートに内蔵。
- resync 手順: [odr_resync_checklist.md](odr_resync_checklist.md)(P9b 乾式リハーサル実績: 7 ファイル/126 ハンク全再適用一致)。

## 6. P9b 受入エビデンス

| 受入 | 結果 |
|---|---|
| (i) conformance full 全緑・UNSUPPORTED==0・matrix OK | 350P/0F/13XF/0XP/2SKIP、resync-guards/census/fork-drift 全 OK |
| (ii) web スモーク | [images/p9b_odr_metadata_virtual_junction.png](images/p9b_odr_metadata_virtual_junction.png) / [images/p9b_odr_metadata_lane_layers.png](images/p9b_odr_metadata_lane_layers.png)(REST 実測: VJ 888 span 95-105 / temp 被覆 [40,360]) |
| (iii) wasm | em++ 14 TU 全緑・リンク 71/71・[images/p9b_wasm_browser_smoke.png](images/p9b_wasm_browser_smoke.png)(SMOKE PASS: MultiLaneLayer perm 幅 3.750+fixture 23 ロード/描画/アンカープローブ) |
| (iv) resync チェックリスト+二重処理ガード常設 | 本書 §5+[odr_resync_checklist.md](odr_resync_checklist.md) |
| (v) 最終ステータス表 | 本書 |
| (vi) 不変スイート | 最終報告に記載(ALL ビルド / unit ctest / 回帰ゲート / validate_catalog / スモーク) |
