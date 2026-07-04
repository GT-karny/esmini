# OpenDRIVE 1.6–1.9 全要素対応計画

- 作成日: 2026-07-02
- 策定方法: 2段階のマルチエージェント調査・設計
  - 調査(11エージェント): 1.9 XSD解析 / ASAMリリースパッケージ採掘 / upstream RoadManagerカバレッジ監査 / GT側タッチポイント監査 / エコシステム調査 + 完全性クリティーク + 実測検証(公式サンプルのロード試験・OSI出力デコード・XSD世代間構造diff・スキーマ検証E2E)
  - 設計(9エージェント): 完全要素マトリクス / アーキテクチャ設計 / ランタイム改修点特定 / テスト戦略 → 計画起草 → 3視点敵対審査(実現性・網羅性・回帰/ガバナンス、指摘は全件コード検証) → 改訂
- ステータス: **P0〜P5 + P7 完了**(P0〜P5 は dev_v0.12 マージ済、P7 は feature/odr1619-p7 で実装完了・未マージ / P6 未着手 / F3 は別トラック並走)(§0 実装状況を参照)
- 総工数: **約15〜17週**(開発者1名+AIエージェント、各フェーズ独立マージ可能)

---

## 0. 実装状況(2026-07-03 時点)

| フェーズ | 状態 | 主要コミット | 内容 |
|---|---|---|---|
| **P0** | ✅ 完了 (2026-07-03) | 649c81a2, 88b14f8c | フィクスチャ60本(手書き18+生成6+公式36)、3層ハーネス `scripts/run_odr_conformance.py`(スキーマ80P/11XF・RM 88P/3XF・OSI 31P)、ゴールデン119本(二重生成バイト同一)、トレーサビリティ行列(18クラスタ充足+5明示保留)、回帰ゲートStep 1.5(ハード)+CI(ubuntuスキーマ+行列のみの保守的統合) |
| **P1** | ✅ 完了 (2026-07-03) | 2c13045a, 4aafef2a, 5a1aa8f4 | `gt_esmini::odr` OdrSideModel+属性粒度監査(ホワイトリスト88パス/289ペア、コントロール~113ファイル警告ゼロ・フィクスチャ20要素+23属性検出)、フォークパッチ**16/150行**([gt_roadmanager_patches.md](gt_roadmanager_patches.md))、ガバナンステスト(マーカーctest+`check_fork_drift.py`)、挙動不変証明(TrafficLight分類ベースライン: 意図された2フリップ以外114ファイル/428信号完全一致、RM/OSIゴールデン不変、phase3バッチper-scenario不変、validate_catalog 61/61) |
| **P2** | ✅ 完了 (2026-07-03) | 4a8d8f4b, 041c7c0b, dd2592c8 (merge b395ef00) | クラスタ3+16: [GT_ODR:lane-types] 5行(walking→SIDEWALK / curb→CURB / shared→BIDIRECTIONAL / slipLane→CONNECTING_RAMP、フォーク計 **21/150行**)、OdrLaneExtras(1.8レーン属性+access/rule/speed/sway/border L1、レガシー資産ではスパース=空)、border→width正規化(公開AddLaneWidth経由・型付きBuildSideModelオーバーロードで**フォーク改変ゼロ、lane-borderコンティンジェンシー不使用**、Ex_Lane-Border偽グリーン解消)、lane speed L2(ManeuverAwareSpeedPlanner min接続・レガシー経路ビット同一をphase3 per-scenario一致で証明)、OSI TYPE_UNKNOWNゼロ(fixture 13の`osi: true`+`osi_expect_no_unknown`機械検証、OSI層にフィクスチャopt-in拡張)、rm_lib/web同期+enum往復ctest(OdrLaneTypeSync)、ゴールデン意図変化15 RM+新規1 OSI(コントロールセット31件はバイト同一) |
| **P3** | ✅ 完了 (2026-07-03) | a005b1ac, 5eeca863, d81fcb9d, 98fc3d28 (merge 085b59f9) | クラスタ11+12: **[GT_ODR:tl-gate]**(TrafficLight=@dynamicのみ、全資産監査とatomic 1コミット — 解析的監査 `odr_tl_classification_audit.py` をC++プローブとbefore/after二重照合、コミット済みユニバース150ファイル/794信号でフリップ=P0フィクスチャ3件のみ・**本番資産0件**・ASAM公式46件昇格=実測破損#1修正、レビュー済みdiff [odr_p3_tl_gate_audit.md](odr_p3_tl_gate_audit.md))+`nr_lamps_`未初期化upstream潜在バグ修正(0灯TLのFFIクラッシュ)、**[GT_ODR:sig-pos]**(positionRoad=参照先road接続+zOffset/hOffset/pitch/roll上書き、positionInertial=センターライン評価逆写像(XYZ2TrackPosはOSIポイント依存でパース時不可)+場外WARN+skip、1.9 s/t省略backfill+診断)、**[GT_ODR:sig-ref]**(signalReference→参照側s/t/orientation/validityのクローン実体化、dynamic→TLクローン、dangling WARN+skip、フック時実体化で前方参照対応)、dependency/reference L1(OdrSignalExtras)、フォーク計 **48/150行**(P3追加27)・マーカー13、フィクスチャ19/20追加(62本)+OSI層opt-in(03/05/19)、公式RMゴールデン3本にクローンsign(UC_Motorway-Exit-Entry×2/UC_5Road_Junction)、conformance full 207P/14XF、phase3+phase3d+crosswalk per-scenario不変(mainビルド両走行比較)、web WS位相チャンネル耐性確認(OSIランプidキー) |
| **P4** | ✅ 完了 (2026-07-03) | 2af4f23b, 4ba14aef, b8b85628, b8b47793 (merge 09afd93c) | クラスタ10+13+14(**フォーク0行達成**: 62/150不変・マーカー15不変・EnvironmentSimulator diff完全ゼロ、既存OdrSignalExtras.cpp拡張で完結): semantics 15サブタイプL1(1.9参加者vehicle/person/animal含む、speed属性形式+子要素フォールバック両受理→単一表現正規化+EQ単体テスト)、**L2=国カタログ優先・semanticsフォールバック**(StopYieldSignAware、GetOSIType未分類時のみ参照、stop/stopLine→STOP・yield→GIVE_WAY、設計判断は[gt_roadmanager_patches.md](gt_roadmanager_patches.md) §6)、L3=OSI TrafficSign強化(未分類+priority→型補完、semantic speed→value/unit、カタログ未分類センチネルはパース時INT_MAX/OSIワイヤ0の両受理)、boards(staticBoard/vmsBoard/displayArea)L1+vmsGroup(1.9ルート直下)L1、header license+defaultRegulations L1(semanticsパーサ再利用)、**公式36本のsignal+header名前空間[ODR-UNSUPPORTED]==0**(UC_5Road_Junctionのsemantics 39ブロック吸収、残=junction15/object13はP5以降)、挙動フィクスチャ(ii) `semantic_stop_sign_full_stop`(de未収載type+priority=stopLine→stopped_at_stop_sign 4.35s pass、赤→緑確認)、フィクスチャ21/22+g7追加(ホワイトリスト107→214パス)、conformance full 214P/0F/13XF+matrix OK、phase3既存10件+phase3d 7件per-scenario不変、validate_catalog全数pass。speed/lane semanticsのL2はアクセサ止まり(ゾーン状態が必要、cluster-14デフォルト速度と同型の保留 — §8確認事項) |
| **P5** | ✅ 完了 (2026-07-03、merge 6f16e2dd) | 78b2a930, e1fcbda1, 68f37891, b258fbd3, e580ffb1, f494185c, e305233e, f68ee9f6 (merge 6f16e2dd) | クラスタ5+7+22: **[GT_ODR:junc-crossing]**(1.8 crossing junction を WARN 認識のまま JunctionType::DEFAULT 維持 + `IsOsiIntersection` の**空connection ガード**=接続ゼロのjunctionでゴーストTYPE_INTERSECTIONレーンを抑止=実測破損#3、フォーク計 **75/150行**・crossing dispatch 7 + guard 6・マーカー17)、**OdrJunctionExtras**(crossPath/roadSection/priority/controller/laneLink-layer L1、アクセサ `GetJunctionExtras`/`GetJunctionPriorities`=F3正典ソース、レガシー資産スパース)、**crossPath→CROSSWALK合成**(odr_side・フォーク0行: 全crossPathを閉4角OutlineCornerRoad CROSSWALK RMObjectに合成し交差路へAddObject、footprintは**走行キャリッジウェイ幅+0.5m/側**=authored crosswalk相当・歩道は除外=実測破損#4、synth id基底900000000+衝突skip、PedPath polyline)、OSIアクセプタンス(fixture 01/21/公式Ex_Pedestrian_CrossingにOSI opt-in+新機械検証3種 `osi_expect_no_intersection_lane`/`_lane_type_sidewalk_min`/`_stationary_min`、コントロールセットOSIゴールデン**バイト同一=ガードno-op証明**)、ゴールデン新規4本のみ(OSI 01/21/公式 + RM 21)、priority ctest走行(公式=非skip)、scene-09 crossPath変種バッチ(合成crosswalkでVDが歩行者に譲り停止・min_obb 1.54m=object版一致・**制御コード差分ゼロ**証明) |
| **P7** | ✅ 完了 (2026-07-04、feature/odr1619-p7・**未マージ**) | 9e52f82d, 648c4db8, 5ce441eb, ff6b7ce8, cd27b18e, c8ec76e7, dd0b8b83, 0987a781, 48925ab5, 47cb8a32, 95406594 | クラスタ19a/19b/17/18/8/9(オブジェクト/断面ジオメトリ第1弾): **[GT_ODR:curvelocal]×2**(1.9 `<curveLocal>` アウトラインを既存 OutlineCornerLocal へ弧長テッセレート委譲=`gt_esmini::odr::AppendCurveLocalCorners`、加えて上流が読まない**単数 `<object><outline>` 形の受入**=g4 が使う形、複数形資産は `next_sibling("outline")` が無名反復と等価でビット同一)+**[GT_ODR:repeat-cubics]×1**(離散 repeat の @bT/@cT/@dT 横三次 + detachFromReferenceLine を `AdjustRepeatInstancePose` で適用、横多項式なしのレガシーは即 false・s/t 無改変=ビット同一)、フォーク計 **90/150 行**(P7 追加 15=curvelocal 12 + repeat-cubics 3)・マーカー20。**OdrObjectExtras**(19a: outline 属性/markings/skeleton/borders、repeat 横多項式、19b: objectReference/bridge/material/@perpToRoad L1)+**合成**(objectReference→原オブジェクト複製で stationary count≥2、bridge=tunnel 経路並行の L1+OSI StationaryObject TYPE_BRIDGE=2、synth id 基底 objectReference=920000000/bridge=910000000)、**OdrJunctionGeom**(クラスタ8 junction `<boundary>`/`<surface>` L1 + 9 junctionGroup L1)、**shape/crossSectionSurface→superelevation 明示劣化**(クラスタ17: L1 格納 + θ=atan(b) 等価スーパーエレベーションへ WARN 付き劣化、ネイティブ t 依存 z 評価は保留、z_probe が t=±3 で z=±0.059988 を解析式 rm_expect_z と一致で機械検証)、**CRG L1+ファイル存在診断**(クラスタ18、評価なし)、**旗付き L3=authored junction boundary→OSI 交差点輪郭**(クラスタ8/9: `GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY` **デフォルト OFF**、ON 時のみ heuristic free-lane-boundary を authored 輪郭 1 本に差替え、OFF は完全 no-op で OSI ゴールデンバイト同一 — probe で OFF=[29,37,47,59]/ON=[62]4pt を機械検証)。ハーネス拡張: z-grid probe + stationary base_polygon dump(opt-in)。フィクスチャ 23/24/25/26+g8 追加(手書き 26 まで+生成 g8)、ゴールデン新規 9 本のみ(RM 23/24/25/26/g8 + OSI g4/10/11/26、既存全件は `--update-golden` 後も CRLF ゴーストのみ=回帰ゼロ)、conformance full **233P/0F/13XF/0XP**(quick 191P・smoke 3/3)、unit ctest 163/163、GT_RoadGen 公式 8 資産 exit0/.osgb>4KB、コントロール 3 資産 .osgb dev_v0.12 とバイト同一(0.0000%)、回帰ゲート(Step1/1.5 緑・Step2 は既知 TL 2件のみ fail=dev 基準一致・他 9 件 pass)、validate_catalog 61/61、7 フィクスチャの odrviewer スクリーンショット確認(bridge span / objectReference 2本 / object_details ボックス / g8 交差点 描画確認、23/24 の断面傾斜は微小で既定アングルでは非可視) |
| F3週 / P6・P8〜P10 | 未着手 | — | 次=**F3週**(junction優先権ポリシー: P5納品の `GetJunctionPriorities` アクセサを消費し ConflictPointResolver の Evaluate 状態モデルを改修 — 計画§5「F3」/§8実行順 P5→F3週→P6、F3 は P7 と並走)。その後 P6(virtual junction のパース時正規化)。P7 マージは本 WP 対象外(dev_v0.12 マージ待ち) |

- **マージ**: feature/phase3d-crosswalk(8a3ca458)→ feature/odr1619-p0p1(529a4523)→ feature/odr1619-p2(**b395ef00**)→ feature/odr1619-p3(**085b59f9**、2026-07-03、P2マージ済みdev先端へrebase後に単独着地・マージ木=検証済みブランチ木)→ post-P3クラッシュ修正(**d17cfea4**)→ feature/odr1619-p4(**09afd93c**、2026-07-03、dev先端2702d386ベースのためコンフリクトなしで着地)→ feature/odr1619-p5(**6f16e2dd**、2026-07-03、P4先行マージ済みdev先端1a051fefへrebase後に単独着地・マージ木=検証済みrebaseブランチ木で`git diff`空)の順で dev_v0.12 へマージ済。P4はマージ後ツリーで再ビルド+unit ctest+conformance quickを再検証済。P5はrebase後ツリー(P4のsemantics/OSI強化とunion解決: BuildSideModelディスパッチ両立・parser_coverage/whitelist再生成218パス・marker 17u/fork 75/150保持)で再ビルド+unit ctest(marker+OdrJunctionExtras 9件+P4のsemantics)+conformance full(**219P/0F/13XF**・fork-drift OK 75/150・matrix OK)+回帰ゲート(Step1/1.5緑・Step2は既知TL2件のみfail=dev基準と一致)+crosswalk/crosspathバッチ(7/7+1/1)+validate_catalog(61/61)を再検証済。ゴールデンは`--update-golden`後もP5新規4本・P4フィクスチャ含む全件がバイト同一(CRLFゴーストのみ)=回帰ゼロを確認、再取得コミット不要。P3はrebase後ツリーで再ビルド+unit ctest+conformance full(207P/0F)+回帰ゲート(挙動バッチper-scenario不変)+監査diff再取得(`--check-golden after`一致)を再検証済。併せてtest_OdrAssetProbeのbyte比較がautocrlf checkoutで壊れる潜在問題(P1由来)をEOL非依存化で修正(98fc3d28)。
- **承認済**(2026-07-02): §10-1 CMake swap-zone R1例外 / §10-2 フォーク150行上限 / §10-7 ASAM資産=**テスト時zip展開・展開物はコミットしない**(CIではofficial層自動SKIP)。**残承認**: §10-3・4(P6着手前)、§10-5・6(P9まで)。
- **実装時の実測差分(計画からの補正)**:
  1. 公式 `Ex_Slip_Lane`/`UC_T_Junction` のクラッシュ真因は junction 中断**以外** → **post-P3 のクラッシュ修正パスで解消済**(merge d17cfea4): UC_T_Junction=`SetAllValidLanes` の範囲外 s での throw([GT_ODR:sig-lanes-guard])、Ex_Slip_Lane=direct junction 検査 LOG_ERROR の **fmt プレースホルダ/引数不一致という upstream バグ**([GT_ODR:direct-junc-log]、RoadManager.cpp:6621)。両ファイル rm_init pass 化(rm 92P/1XF)、フォーク計 62/150。P5 での再訪は不要になった。
  2. `crossPath` は1.8 XSD上 **virtual junction** の持ち物(crossing junctionは `roadSection` のみ)— fixture 01 は両構造を併用。
  3. `defaultRegulations` はASAM出荷XSD自体の欠陥(abstract必須子)で1.8/1.9とも検証不能 → 恒久期待フェイル登録。
  4. `roadSurface` は `ObjectType` enum欠落(hpp凍結)のため Str2Type での静音NONE化に設計変更(パッチ表6)。
  5. 1.5 XSDのkeyref参照整合性により「スキーマ緑+RM中断」フィクスチャはrev 1.4で作成(fixture 02)。
  6. `Position::XYZ2TrackPos` はレーンOSIポイント(SetRoadOSIで後段生成)依存で**パース時に使用不可** → P3のpositionInertial逆写像はセンターライン評価(SetTrackPos+粗サンプル+三分探索)で実装(§5 P3の想定補正)。
  7. dangling signalReference は 1.8 XSD keyref(k_road_signals_signalId)がスキーマ棄却 → WARN+skip経路はtempファイル単体テスト+公式資産で担保(コミットフィクスチャには置けない)。
  8. `TrafficLight::SetTrafficLightInfo` は未対応type組合せで `nr_lamps_` を未初期化のまま残す(upstream潜在バグ、ゲート緩和で顕在化)→ フォークで `nr_lamps_=0` 修正、独立PR候補。
  9. **計画の「semantics speed=1.8子要素形式/1.9属性形式」前提は誤り**: 1.8.1/1.9とも属性形式 `<speed type value unit/>`(ASAM 1.8.1 §14.8で確認)。P4ノーマライザは子要素フォールバックも受理し単一表現へ正規化。同様にboardsの「両位置」も誤りで1.8/1.9ともsignal直下(配置移動なし)。staticBoardはスカラー属性なし(`t_road_signals_board`は`_OpenDriveElement`拡張)、`vmsGroup`は1.9でOpenDRIVEルート直下+abstract必須子=defaultRegulations同様の恒久schema-fail(fixture 21 XFAIL)。semantics 15番目のサブタイプは`explanatory`ではなく`supplementaryExplanatory`。
- 既存資産の既知不適合を台帳化(資産は無改変・XFAIL登録): `fabriksgatan_traffic_lights_ctrl.xodr`(revMinor=4+top-level controller)、catalog生成道路5本(空elevationProfile等)。

---

## 1. 背景と調査サマリ

### 1.1 現状カバレッジ

GT_RoadManager.cpp(upstream v3.3.0 RoadManager.cpp のフォークコピー、パッチ1-Aのみ)は実効的に **OpenDRIVE 1.5 相当 + 1.6/1.7の一部**(direct junction、文字列road ID、explicit roadmark line)のパーサ。`revMinor` は読むが使用箇所はroadMark色デフォルト1箇所のみで、**バージョンゲートが存在しない** — revMinor=9のファイルも無警告でロードされ、未知要素はサイレントに欠落する。

### 1.2 バージョン帰属の要点(XSD構造diffで確定)

- **1.8→1.9の真の差分は小さい**(新型10+変更17): レーンレイヤ(`<lanes layer>`×2)、signal/objectの`@temporary`/`@invalidated`、curveLocal滑らか輪郭、repeat横方向多項式bT/cT/dT、CRG xy/hオフセット、semantics参加者調和、laneSection `@length`。
- **「1.9の目玉」と誤解されがちな大物は1.7/1.8導入**: virtual junction=1.7、crossing junction/crossPath・signal semantics・crossSectionSurface・VMSボード・defaultRegulations・lane属性(direction/advisory/dynamic\*/roadWorks)・レーン型 walking/shared/slipLane=1.8。
- lane access/rule/speed/border、`<shape>`、surface/CRG、objectReference、bridge、railroadは **1.5以前から存在する未対応バックログ**。

### 1.3 実測で確認済みの破損(1.8/1.9イディオムファイル投入時)

1. **countryRevision反転バグ**(GT_RoadManager.cpp:4821-4824、upstream由来): `if (attr.empty()) country_revision = attr.as_uint()` — 属性を**明示すると読まれない**。alpha-2国コードや明示revisionを持つ動的信号がTrafficLightに昇格せず静的Signalへdemote → TrafficLightAware / OSI信号ストリーム / 横断歩道歩行者信号ゲートが盲目化。
2. **OSIレーン分類汚染**: `walking`/`curb`レーン型→LANE_TYPE_NONE→osi3でTYPE_UNKNOWN(1.5の`sidewalk`はNONDRIVING/SIDEWALKに正しく出る)。curbはenum(hpp:903)とOSIマッピングが存在するのにパース文字列が無い死にマッピング。
3. **幽霊intersectionとレーン消失**: crossing/virtual junctionがDEFAULT扱いに劣化し`<crossPath>`が落ちる → 接続ゼロjunctionをIsOsiIntersectionが「交差点とみなす」(RoadManager:5868-5872) → **空のTYPE_INTERSECTIONレーンがOSIに出力**され、横断路のレーンはOSIから完全消失、crosswalkのStationaryObjectも出ない。
4. **スキーマ検証の罠**: 1.9 XSDは`<?xml version="1.0"?>`宣言+`vc:minVersion="1.1"` → `run_schema_comply.py`はXML宣言でプロセッサを選ぶためXSD 1.0処理系が**空スキーマ**を読み全ファイル不合格。修理はupstreamの1.8前例(852351c1)どおりXML宣言を1.1へ上げてlocal_schema配置。

### 1.4 資産状況

リポジトリ内xodr: 1.4×18 / 1.5×8 / 1.6×3 / 1.7×2、**1.8/1.9資産ゼロ**。scenariogeneration 0.16.5はrevMinor="5"を出力し1.6+構文をオーサリング不可。ASAM公式1.9サンプル(例24本+ユースケース8組、revMinor=9含む)は `thirdparty/opendrive/1.9/` のzip内に確保済み。

---

## 2. 対応レベル定義と目標

| レベル | 定義 | 目標 |
|---|---|---|
| **L1** | パース+格納+診断(サイレント欠落ゼロ) | **1.6–1.9の全要素・全属性で達成**(機械検証、§3.3) |
| **L2** | ランタイム意味論(ルーティング/Position/VDポリシー/信号挙動) | 挙動関連クラスタで達成 |
| **L3** | OSI ground truth出力 | 挙動関連クラスタで達成 |
| **L4** | 可視化(GT_RoadGeomメッシュ/OSGビューワー) | 幾何関連クラスタで達成 |
| **L5** | オーサリング+検証フィクスチャ | 全クラスタ(トレーサビリティ行列) |

L2〜L4を保留するクラスタは§8の**保留台帳**に明記し、ユーザー拒否権の対象とする。

---

## 3. アーキテクチャ決定

### 3.1 ハイブリッド方式: OdrSideModel + 薄フォークフック

決定的制約: GT_RoadManager.cppは **.cppのみのフォーク**であり、`RoadManager.hpp`はpristine upstream(swap機構はEnvironmentSimulator/Modules/RoadManager/CMakeLists.txt:21-23で.cppだけ差し替え)。**ヘッダに新規データ構造を足せない**。

1. **OdrSideModel(GT側サイドパーサモジュール、新設)**: 同じxodrをGT側でもう一度pugixml走査し、road/junction/signal IDキーのサイドテーブルへ格納。semantics・boards/VMS・priority・crossPath・railroad・defaultRegulations・license等、既存ランタイム構造に触れないものは全部ここ。
   - 配置: `GT_esmini/src/road/odr_side/`(OdrSideModel / OdrSideParser / OdrCoverageAudit / OdrLaneExtras / OdrSignalExtras / OdrJunctionExtras / OdrRailroad)+ `include/gt_esmini/road/`
   - フック挿入位置: `gt_esmini::odr::BuildSideModel(doc, this)` を **CheckConnections()直前**(GT_RoadManager.cpp:5490)。CheckJunctionConnectionはjunctionを変異させる(ミラーConnection合成 ~:6680/:6746-6803)ため、GT合成junction/objectは修復パスより**前**に存在させる必要がある。RMのみのエントリポイント(GT_RM_Init / rm_lib.py / GT_RoadGen)はGT_Initを通らないため、フックはパーサ内が必須。
   - ライフタイム: OpenDriveインスタンスごと(ポインタキーのレジストリ、パースごとにクリア再構築)。グローバル状態なし、DLL間共有なし(GT_esminiLib.dllとesminiRMLib.dllは各々static link)。3-OS(GCC/AppleClang/MSVC)+Emscripten互換のコード規約。
2. **フォークへの薄フック**: 既存構造の埋め方を変える必要がある箇所のみ、`[GT_ODR:<id>]` マーカー付きの**ロジックフリー呼び出し**(実装はGT側ヘルパー)。

### 3.2 フォークパッチ予算とガバナンス

| パッチ | 行数目安 |
|---|---|
| include+フック | ~8 |
| junctionアボート耐性化(:5429-5437 return-false→WARN劣化) | ~4 |
| レーンレイヤ選択デリゲート(:4074) | ~22 |
| レーン型マッピング(:4221、既存enumへの接続のみ) | ~12 |
| countryRevision読取修正 | 1 |
| TrafficLightゲート緩和(:4887) | ~4 |
| signal positionRoad/Inertial | ~8 |
| signalReference | ~12 |
| crossing junction認識+IsOsiIntersection幽霊ガード | ~10 |
| curveLocal→OutlineCornerLocal | ~10 |
| repeat bT/cT/dT | ~10 |
| roadSurface Str2Type | 1 |

**基本 ~105–115行 / 約12ブロック。ハード上限150行**(超過は都度ユーザー承認)。事前承認済みコンティンジェンシー: lane-borderフォールバック~8 / P6分割ヘルパー~25 / lane @direction ~25。ガバナンス装置: パッチマニフェスト文書 `gt_roadmanager_patches.md`(マーカーID・関数名アンカー・upstream基底ab7c404d・PR状況)+ マーカー数ctestガード + P9でのresyncリハーサル。**この機構が下げるのはupstreamマージのコストであり、ドリフト自体はPR受理時のみ縮む**(受理ゼロ前提)。

### 3.3 網羅の機械検証: OdrCoverageAudit

- ホワイトリストは **(要素パス, 属性セット) のペア**。未対応は `[ODR-UNSUPPORTED] <path>` / `<path>@<attr>` 警告(1.8→1.9差分は属性レベルが大半のため属性粒度が必須)。
- additionalData族(userData/include/dataQuality)は全コンテナで初日からホワイトリスト。警告は(パス, road)単位でdedupe。
- **1.4/1.5コントロールセットは警告ゼロ必須**(本番ログにWARNスパムを出さない)。
- プログラム終了条件 = 全フィクスチャ(公式36+手書き~17+injector生成+リポジトリ内1.6/1.7)でカウント**0**。

---

## 4. 要素クラスタ一覧(0–22)

| # | クラスタ | 導入版 | 目標レベル | フェーズ |
|---|---|---|---|---|
| 0 | 対応済み1.6-1.9項目の回帰固定 | 1.6-1.9 | L5 | P0 |
| 1 | バージョン認識基盤+no-silent-drop診断 | 横断 | L1基盤 | P1 |
| 2 | スキーマ検証1.9+フィクスチャ供給 | ツール | L5 | P0 |
| 3 | レーン型+レーン属性(walking/curb/shared/slipLane、direction/advisory/dynamic\*/roadWorks) | 1.6+1.8 | L1-L4 (@direction L2は保留) | P2 |
| 4 | レーンレイヤ+temporary/roadworksパッケージ | 1.9 | L1+限定L2+L3+L5 | P8 |
| 5 | crossing junction+crossPath(歩行者横断) | 1.8 | L1+L2+L3+L5 | P5 |
| 6 | virtual junction | 1.7/1.8 | L1+L2+L3(正規化方式) | P6 |
| 7 | junction priority+laneLink overlapZone | ≤1.5+1.8 | L1+L2(F3の正典ソース) | P5(+F3週) |
| 8 | junction boundary/elevationGrid/planView等 | 1.8 | L1+旗付きL3(L4メッシュは保留) | P7 |
| 9 | junctionGroup(roundabout/interchange) | ≤1.5+1.8 | L1+ポリシーヒント | P7 |
| 10 | signal semantics族+参加者 | 1.8/1.9 | L1+L2+L3 | P4 |
| 11 | 動的信号近代化+countryRevisionバグ修正 | 横断 | L1+L2+L3 | P1(読取)+P3(ゲート) |
| 12 | signal配置+相互参照(positionRoad/Inertial、signalReference、dependency) | ≤1.5 | L1+L2+L3 | P3 |
| 13 | VMS/可変表示板 | 1.8/1.9 | L1(コンテンツL3は保留) | P4 |
| 14 | header license+defaultRegulations | 1.8 | L1(+任意L2) | P4 |
| 15 | additionalData: userData/include/dataQuality | ≤1.5 | L1 | P1+P9 |
| 16 | レーン詳細: access/rule/speed/border/sway | ≤1.5+1.8 | L1+L2(speed)+border正規化 | P2 |
| 17 | 横断面: shape+crossSectionSurface | ≤1.5+1.8 | L1+超高度近似(ネイティブz評価は保留) | P7 |
| 18 | surface/CRG(road/junction/object) | ≤1.5+1.7+1.9 | **L1のみ**(実評価は保留) | P7 |
| 19a | object幾何拡張: curveLocal/skeleton/borders/repeat多項式 | 1.8/1.9 | L1+L3+L4 | P7 |
| 19b | objectReference/bridge/misc属性 | ≤1.5+1.7+1.8 | L1+L3+L4 | P7(+P1) |
| 20 | railroad+station | ≤1.5 | **L1のみ**(不活性と明記) | P9 |
| 21 | 1.6の廃止/削除ハンドリング | 1.6 | L1診断+L5 | P1 |
| 22 | junction接続/laneLinkの1.9レイヤ属性 | 1.9 | L1(スロット予約P5、意味論P8) | P5+P8 |

---

## 5. フェーズ計画

> 全フェーズ共通の不変条件: run_gt_tests.ps1 全緑 / validate_catalog 全数 / phase3+phase3d(+crosswalk)バッチ **per-scenario不変** / RM+OSIゴールデン許容誤差内一致 / replayer .dat差分スモーク / odrviewerロードスモーク。フェーズごとに `[ODR-UNSUPPORTED]` の担当分カウントを0にする。

### P0 — 適合ハーネス+1.9スキーマ+フィクスチャ群(L5基盤) — 2週

- **目標**: パーサ変更前に機械検証可能な「赤」ベースラインを張る。
- **成果物**: `GT_esmini/test/odr_fixtures/`(official / handauthored ~17本 / generated / golden{rm,osi} / schema19(XML宣言1.1化した7 XSD)/ manifest.yaml)。手書きフィクスチャは crossing junction+roadSection、objectReference、bridge、lane rule/speed、lane属性、repeat多項式、positionRoad、VMSボード、temporary/invalidated、license/defaultRegulations、shared/slipLane/HOV、CRGオフセット、roadMark sway、include(エラー経路)、dataQuality/userData、不正junction接続(アボート再現)、1.8イディオム動的信号(demote再現)。**要素/属性→フィクスチャのトレーサビリティ行列**。3層ハーネス `run_odr_conformance.py`(RMプローブ=JSON 1e-6丸め、OSI=デコード後フィールドepsilon比較の**許容誤差ベースゴールデン**、`--update-golden`は単一レビュー済みdiffコミット規約)。`validate_xodr_schema.py`(upstream run_schema_comply.pyを無改変import+revMinor-9マッピング)。`odr_feature_injector.py`(priority_injector.pyの一般化)。回帰ゲートStep 1.5+CI組込。ASAMライセンス確認(再配布不可ならfetch+SHA256マニフェストにフォールバック)。
- **受入**: 全フィクスチャが自版XSDでスキーマ緑(1.9含む) / ロード可能フィクスチャでRM_Init==0(公式1.8の数本はP1耐性化まで期待フェイルとして凍結) / 1.4/1.5コントロールセットのベースラインゴールデン再現安定 / 行列に空行なし / 既存ゲート無変化緑。
- **リスク**: ASAM再配布権(初日ブロッカー)。Ex_SmoothObjectOutlineはrevMinor=5誤表記(期待フェイル登録)。

### P1 — OdrSideModel基盤+属性粒度監査+挙動不変修正+アボート耐性化 — 1.5週

- **目標**: アーキテクチャ骨格を**既存資産で挙動変化ゼロを証明した上で**投入。
- **スコープ**: クラスタ1・21全量・15(userData/dataQuality格納、includeは明確診断のハードエラー)。フォーク修正: **countryRevision読取修正は「明示値を尊重・省略時は0のまま」のレガシー保存形**(fabriksgatan / multi_intersections / straight_crosswalk_pedsig(scene 09)は全て省略→0でゲート通過中のため、素朴なabsent→2013修正は既存全TrafficLightを殺す — 審査クリティカル)。junctionアボート耐性化(公式1.8フィクスチャのロードに必要なためP5から前倒し)。roadSurfaceのStr2Type追加。未知outline corner診断。
- **成果物**: odr_side/一式+単一フック(CheckConnections直前)+(要素,属性)ペア監査+パッチマニフェスト+マーカー数ctest+replayer/odrviewerスモークのゲート化。**CMake swap-zone拡張(odr_side/\*.cpp追加)はR1新例外としてマニフェスト第1行+ユーザーサインオフ**。
- **受入**: repeat多項式フィクスチャで**属性レベル**の予測WARN一覧が一致 / 1.4/1.5コントロールセット警告ゼロ / countryRevision単体テスト(present→honored, absent→0)+TrafficLight分類プローブが全資産ユニバースで前後同一 / 不正接続フィクスチャWARN+EXIT==0 / 3-OSビルド緑 / マーカー数==マニフェスト行数、マーカー外フォークdiff==0。

### P2 — レーン型+border→width正規化+レーン詳細L1+OSI分類修正 — 1週

- **スコープ**: クラスタ3(walking/curb/shared/slipLane L1-L4、レーン属性はL1格納・@direction L2は保留)+16(access/rule/speed/sway L1、lane `<speed>`→LonProfilePlanner L2、**`<border>`→width正規化** — Ex_Lane-Borderは`<border>`×6/width 0本で、L1格納だけでは幅ゼロの偽グリーンがゴールデンに焼き付くため)。
- **成果物**: [GT_ODR:lane-types] :4221 ~12行 / border→width変換(width_i=border_i−border_{i-1}、GT側・公開Lane API経由、不足時は~8行フック事前承認済み)/ GT_OSIReporter_Geometryのsubtype出力 / GT_RoadGeom色テーブル / **同一コミットで** rm_lib.py+road_geometry_service.py:23-38 のビットマスク同期+相互チェックctest。
- **受入**: walking/curbフィクスチャのOSIゴールデンでTYPE_UNKNOWNゼロ / Ex_Lane-Border幅プローブがborder代数と1e-6一致 / LHTスモーク(パッチ1-A非摂動)/ webビューワーで歩行者レーン描画(PRスクリーンショット)。

### P3 — 動的信号: ゲート緩和(全資産監査とatomic)+signal配置/参照 — 1週

- **本計画最大の挙動回帰リスク点。** ゲート緩和と補償監査を**1コミット**で。
- **スコープ**: クラスタ11(TrafficLightゲート:4887緩和)+12(signalReferenceのSignalクローン実体化、positionRoad→参照先road接続、positionInertial→XYZ2TrackPos逆写像、dependency/reference L1、s/t欠落診断)。監査観測量は**dynamic_cast\<TrafficLight\>成功**(=OSI traffic_light存在)。dynamic_signals_登録はゲート非依存(:4959-4962)で、demote被害はcast消費者(TrafficSignalController:65/:151、GT_OSIReporter_Traffic:160)に限定 — 審査で事実修正済み。
- **監査ユニバース**: **ゲートが読み込む全xodr** = resources/xodr(31)+生成カタログ(scene 09のpedsig道路含む)+検証用道路+EnvironmentSimulator/Unittest/xodr(upstream CIが3本の動的信号ファイルをスワップ済みフォークで実行)+GTフィクスチャ。前後分類diffをレビューしゴールデン凍結。
- **受入**: 1.8イディオムdemote再現フィクスチャでOSI traffic_light>0(破損#1修正)/ scene 03+scene 09バッチper-scenario不変 / signalReference解決配置のRMゴールデン(UC_5Road_Junction等)/ 場外positionInertialはWARN+skip / OSI信号id≠xodr idの既知罠に対しweb WS位相チャンネル確認。

### P4 — signal semantics族+boards+header regulations — 1週(フォーク0行)

- **スコープ**: クラスタ10(semantics 15型L1、priority/speed/laneサブセットをStopYieldSignAware/TrafficLightAware/RouteSignalScanへ国カタログのフォールバック/オーバーライドとして接続、speedは1.8子要素+1.9属性の両対応、OSI TrafficSign強化)+13(boards両位置L1)+14(license/defaultRegulations L1)。
- **受入**: 公式36本のsignal名前空間で[ODR-UNSUPPORTED]==0 / 国カタログに無いsemantics priority=stopLine標識でVDが停止する挙動フィクスチャ(phase3型バッチ)/ semantics往復単体テスト。semantics vs 国カタログの優先順位は設計判断として文書化。

### P5 — Junction第1弾(スリム化): crossing junction+crossPath横断歩道+priorityソース — 1週

- **目標**: 幽霊TYPE_INTERSECTION破損の根絶。1.8 crossPath横断歩道を**出荷済みCrosswalkPedestrianAwareスタックにポリシー無改変で**流す。F3へ正典priorityソースを引き渡す。
- **スコープ**: クラスタ5+7+22スロット予約。boundary/elevationGrid/junctionGroupのL1はP7へ移動(スリム化)、アボート耐性化はP1で landing済み、**F3本体(ConflictPointResolver Evaluate改修)は隣接週に分離**。
- **成果物**: [GT_ODR:junc-crossing]+IsOsiIntersection空接続ガード ~10行 / OdrJunctionExtras / **crossPath→合成RMObject(CROSSWALK)**(OutlineCornerRoad footprint、公開`Road::AddObject`(hpp:3074)経由、tunnel合成前例:8585-8631、予約IDレンジ)+PedPathポリライン側面登録 / F3用priorityアクセサ。
- **受入**: Ex_Pedestrian_CrossingのOSIゴールデンで幽霊intersectionなし・横断路walkingレーン存在・crosswalk StationaryObject出力(破損#3/#4修正)/ RouteCrosswalkScanが合成CROSSWALKを**ポリシーコードdiffゼロ**で拾う(crossPath版crosswalk道路でscene 09マッチャ)/ IsOsiIntersectionガードは既存資産でOSI diff空 / phase3d+crosswalkバッチper-scenario不変。

### P6 — Junction第2弾: virtual junctionのパース時正規化 — 2週

- **方式**: mainRoadをsStart/sEndで分割しDEFAULT junctionを合成 → MoveAlongS/RoadPath/LaneIndependentRouter(未スワップのpristine upstream)が無改変で動く。ネイティブ中間接続意味論は保留(§8)。
- **Day-1スパイク**: 公開RoadManager.hpp APIのみで道路1本を分割できるか検証(幾何サブセグメント再パラメータ化、elevation/laneOffset再基準化、跨りlaneSection、signal/object 1個ずつ)— `Road::geometry_`はprivate(hpp:3181)。**不可の場合のコンティンジェンシー(+25フォーク行、最終手段は加算ヘッダマーカー例外)は今回の承認で事前決定**(週8での突発エスカレーションを回避)。
- **決定事項(実装前)**: 正規化でroad id/s座標が変わり、**分割域を越えるxosc RoadPosition/LanePosition参照が壊れる** → GT_ScenarioReaderでの旧id/s remap shim か「文書化された制限+検証時診断」かを事前決定。.datのid変化も文書化(ビルド内では自己整合、サイドテーブルで逆引き)。
- **受入**: 正規化ゾーン通過のRM走行ゴールデン / 合成junctionにミラー接続が存在(CheckConnections後トポロジ)/ xosc位置参照テスト(ゾーン内・越え、決定どおり)/ 正規化ゾーン内でConflictPointResolverが競合解決する挙動バッチ / 分割不変単体テスト(分割前後でz/幅/signal-sサンプル一致)/ 非virtualフィクスチャは全て許容誤差内不変。

### P7 — object幾何+横断面+CRG/bridge L1+junction残L1(網羅ウェーブ1) — 1.5週

- **スコープ**: 19a(curveLocalを既存OutlineCornerLocalへテッセレーション、outline属性、outline内markings、repeat bT/cT/dT+detachFromReferenceLine、skeleton/borders L1)/ 19b(objectReference、bridge=tunnel経路並行のL1+L3+L4、material/@perpToRoad)/ 17(shape+crossSectionSurface: L1格納+**等価superelevationへの明示WARN付き劣化**、ネイティブt依存z評価は保留)/ 18(CRG L1+ファイル存在診断+1.9オフセット、**評価なし**)/ P5から移動のクラスタ8 L1+9(+旗付きL3: authored boundaryによるOSI交差点輪郭、ヒューリスティックfallback温存)。
- **受入**: GT_RoadGenがEx_SmoothObjectOutline/Ex_TrafficIsland/Ex_Objects/Ex_CrossSectionSurface×4/UC_RoadShapeでexit 0+.osgb>4KB / crossSectionSurfaceのz-probeが宣言済み劣化意味論と1e-6一致 / curveLocalポリゴンの点数/巻き方向ゴールデン / 1.4/1.5の.osgbサイズ帯安定(並列キャッシュ経路非影響)/ 新幾何フィクスチャごとにビューワー目視。

### P8 — 1.9レーンレイヤ(permanent選択+GT側temporaryマージ)+temporary/invalidatedフラグ — 2週

- **方式(審査で改訂)**: Ex_Motorway_roadworksのtemporaryレイヤは全長7083mのうちs≈2000–5083のみ → **レイヤ単純選択は成立しない**。フォークフック[GT_ODR:lane-layers] :4074 ~22行は薄いデリゲートに留め、permanentモードはpermanentノードを返すだけ、temporary opt-inモードは**GT側でlaneSection/laneOffsetをs範囲マージした合成`<lanes>` DOM**を返す(マージロジックはOdrLaneExtras、合成DOMの寿命はインスタンス別サイドモデルが所有)。実行時レイヤ切替は保留(Webランナーは走行ごと再パース)。
- **スコープ**: クラスタ4+22意味論(lane link @layer、laneLink from/toLayer、laneValidity @layer)。@temporary/@invalidatedの(road,id)→フラグ集合をGT_OSIReporterとVD RouteSignalScan/RouteCrosswalkScanが参照(打ち消し標識を無効扱い)。
- **受入**: Ex_Lane_MultiLaneLayer(road 1)とEx_Motorway_roadworks(road 8)のs標本プローブ — permanentモードは全点permanent一致、temporary opt-inはs∈[2000,5083]内でtemporary・外でpermanent(**s=100/5500がマージ証明点**)/ 両rev9ファイルで[ODR-UNSUPPORTED]==0(属性含む)/ **レーングローバルID安定性ゴールデン**(AddLane/SetGlobalId :2375/:1237の逐次割当が静かな破壊モード)/ invalidated信号のOSI非出力(記録済み判断どおり)/ GT_RoadGen二重テッセレーションなし。

### P9 — railroad/station+include/userData残課題の確定+web公開+resyncリハーサル+ゼロ監査 — 1週

- **プログラム終了判定**: 全フィクスチャで要素+属性の[ODR-UNSUPPORTED]==0。
- **スコープ**: クラスタ20(switch/mainTrack/sideTrack/partner、station/platform/segment: L1+RM-API公開、**不活性と文書明記**)/ 15クローズ(P1の暫定処置を確定: includeは解決実装 or 診断付きハードエラーのまま仕様化をユーザー判断、userDataは注釈UIへ公開して消費側まで完結)/ 13/14残 / ルートループ網羅(junctionGroup/station/vmsGroup)/ web公開: GT_RM_\* C関数+rm_lib.py+annotation UIへのパース警告表示(dedupe済み)。
- **resyncリハーサル(命名成果物)**: upstream ab7c404d(またはv3.4)の新規コピーへ~12マーカーを関数名アンカーで再適用 → upstreamパースループとの被覆diffで「handled-by-upstream」状態へのホワイトリスト再基準化(upstream側がネイティブ対応した要素はGT処理を撤去し二重パース回避)→ ゴールデン再生成を単一レビューコミットで — の**書面チェックリスト**化。恒久的な**二重処理ガード**(合成ID重複なし・GTホワイトリストとupstreamパースの両属なし)をハーネスに常設。
- **成果物**: `GT_esmini/docs/opendrive_16_19_support.md` — クラスタ0-22×レベルL1-L5の対応状況表(**保留レベル全掲載の「あらゆる要素」正直台帳**、ユーザー拒否権用)。

### P10 — upstream還元+マージコスト削減トラック — 0.5-1週分散(P2-P9と並走)

§7参照。**フォーク面はPR受理と無関係に約1→12ブロックへ成長する。実際のドリフト削減はPR受理のみで、計画は受理ゼロ前提**(受理は純アップサイド)。

---

## 6. 既存ロードマップとの整合

- **F3(junction優先権)**: P5の**隣接週**に分離実施(デフォルト)。P5が`<priority>`のOdrSideModelアクセサを先に納品し、F3はそれを消費してConflictPointResolverのEvaluate状態モデル改修(複数競合ラッチ、メモリ推奨どおり)を1回で行う。F3が遅れてもpriorityデータは独立に着地。
- **U4(OSIライトポート)**: 独立。中途で入る場合はOSIゴールデンが揺れる → P0で定義する`--update-golden`単一レビューコミット規約をU4のPRにも適用。
- **次のupstream resync**: **P8完了後**に1回(フル[GT_ODR]セットでの再コピー)。P9リハーサルのチェックリストを使用。
- **R4**: 本計画のマニフェスト/マーカー/ctest機構+P9リハーサルは「パッチあたりマージコスト」を下げる。ドリフト自体の削減はP10のPR受理時のみ、と正直にフレーミング。
- **R3**: run_odr_conformance.pyは壊れているGT_Loader統合テスト群の再作成テンプレートになる(P0以降)。
- 実行順: P0→P1→P2→P3→P4→P5→(F3週)→P6→P7→P8→P9、P10並走。P4とP7はF3都合で入替可。

---

## 7. upstream還元トラック(PR一覧)

各PRはローカル[GT_ODR]パッチとバイト同一(受理→次回resyncでマーカー削除)。

| PR | 内容 | 見込み |
|---|---|---|
| PR-1 | countryRevision読取修正(**absent→0レガシー保存形** — これ以外の形はupstream自身のfabriksgatanデモ/CIを壊し却下される。upstream自身の `// why country_revision < 2013??` コメントを引用、回帰フィクスチャ添付) | 高 |
| PR-1b | TrafficLightゲート緩和本体(1.8/1.9動的信号の意味論についてメンテナ見解が必要) | 中 |
| PR-2 | レーン型 walking/curb/shared/slipLane 配線(死んでいるLANE_TYPE_CURB+既存OSIマッピングの接続として提示、TYPE_UNKNOWN実害を提示) | 中-高 |
| PR-3 | crossing junctionのWARN同等化+IsOsiIntersection空接続幽霊ガード+junctionアボートWARN劣化 | 中 |
| PR-4 | 最小signalReference(Signalクローン方式)(任意) | 低-中 |
| PR-5 | resources/schema/OpenDRIVE_1.9 local_schema+revMinor-9マッピング(upstream 1.8前例852351c1踏襲) | 高 |
| Issue | XSD 1.1のXML宣言罠の文書化+#592へフィクスチャコーパス提供コメント | — |

**還元しないもの**: OdrSideModelアーキテクチャ、レーンレイヤ、crossPath合成、semantics族、virtual junction正規化(upstreamに1.8/1.9ロードマップが無い現状では過大/意見的)。

---

## 8. 明示的スコープ外(保留台帳 — ユーザー拒否権対象)

**全項目ともL1(パース+格納+属性粒度診断)は達成**する。保留はL2以上のみ。

1. **CRG実評価**(クラスタ18のL1超): OpenCRGライブラリのvendor+Track2XYZ/標高評価書換が必要(爆風半径1位)。L1は属性格納+ファイル存在WARN。
2. **crossSectionSurface/shapeのネイティブz評価**(17): 同じく半径1位(剛体ロールモデルはt依存高さを表現不能)。P7は明示WARN付きsuperelevation近似(θ=atan(b)、L1データは`odr_side`の`OdrRoadLateralProfile`に格納済)。需要実証時に別スパイク。
   - **将来課題(2026-07-04 ユーザー指定・スコープ確定)**: 厳密化の目標到達点は**「車両が路面の曲面に沿って正しく傾く」まで**(座標精度だけでなくバンク走行の物理再現を含む)。
   - **なぜ半径1位か**: 現状 `Position::Track2XYZ`(GT_RoadManager.cpp:9737-9744、フォーク内)は横位置 `t` と高さを並べたベクトルに**回転行列を1回かけるだけ** → 断面は「1角度で傾いた平板」で `z(t)=中心線z+sin(roll)·t` の**tに対して直線**。曲面(2次/3次・複数ストリップの折れ面)は原理的に表せない。加えてピッチ/ポリゴン密度が使う z の1・2階微分(:7683-7689)が t 依存化し、**ロールとピッチが独立という前提が崩れる**。約20ファイルの位置クエリ・OSI点群・メッシュ(GT_RoadGeom)・センサが全てこの経路を通る。
   - **工数見積り(P6/P8同格の重量フェーズ)**:
     - スコープA=座標精度のみ(問い合わせ(s,t)のzが正確): **~2〜3.5週**。
     - スコープB=**車両バンク物理まで(=本課題の目標)**: A + VehiclePhysics/ピッチ・ロール再生系(制御車=コントローラ内部 / 交通車=VehiclePhysicsManager の2系統、pitch_roll_architecture 参照)への波及で **さらに +1〜2週 = 計 ~3〜5.5週**。
     - 内訳(A部): Day-1スパイク(0.5〜1日: `Track2XYZ`から`GetRoadLateralProfile`を引いて z(s,t)評価、**hpp不改変で済む見込みの確定**=最重要)/ コア評価書換(3〜5日、superelevationのみの道路は現行1バイト一致に退化する分岐必須)/ 微分項(2〜4日)/ 消費側追随+ゴールデン全再取得(3〜5日)/ 検証(2〜3日)。
   - **朗報/懸念**: L1データはP7で`odr_side`格納済+`Track2XYZ`はフォーク側 → **RoadManager.hpp改変(最終手段のヘッダ例外)は回避できる可能性が高い**(スパイクで確定)。懸念=コア最ホットパスへのフォーク追加で行数予算(P7時点90/150)を数十行消費 → **事前承認が要る規模**。着手前にスパイクでhpp回避可否とスコープを固めること。
3. **virtual junctionネイティブ中間接続**(6): 「junctionは道路端」不変条件をMoveAlongS/RoadPathで壊し、pristineなLaneIndependentRouter.cppの編集(R1違反)を強いる。正規化で等価挙動を提供。
4. **レーンレイヤ実行時切替**(4): ロード時選択のみ。走行ごと再パースがWebランナー運用と一致。
5. **lane @direction のL2**(3): 走行方向判断6箇所以上に波及しLHTホットスポット(パッチ1-A)と重なる。判断箇所サーベイのスパイク(+~25フォーク行、150行上限内で事前承認済み)を独立フェーズとして将来実施。
6. **railroad L2-L4**(20): 鉄道ランタイム/車両モデル/シナリオ需要なし。L1+API公開、不活性と文書化。
7. **VMS動的コンテンツ**(13): ライブ表示制御はシナリオエンジン機能でありパースの範囲外。静的ボードコンテンツのOSI TrafficSign値出力も保留(L1格納まで)。
8. **junction内部メッシュ**(8のL4): boundary+elevationGridからのメッシュ生成は消費者不在のGT_RoadGeom新機能。L1格納+旗付きL3輪郭まで。
9. **lane \<speed\>のOSI出力**(16のL3): VD LonProfilePlanner L2まで。osi3のレーン速度制限帰属はマッピング判断が必要。
10. **scenariogenerationライブラリ更新**: revMinor=5天井はodr_feature_injectorで回避。ライブラリ更新は別のオーサリングスタック判断。
11. **esmini_fmu(Protocol B)修理**: 既知の別件。CMake拡張は将来のGT_esminiLib_staticリンク修理を悪化させない形で書く。
12. **esminiJS**: 新ソースはEmscripten互換規約でビルド同等性を保つが、**未テスト**とマニフェストに明記。

---

## 9. 主要リスク

1. **既存資産での挙動回帰** — 最大はP3(ゲート緩和)とP8(OSIレーングローバルIDシフト)。緩和: P3のatomic監査+全ユニバース分類diff、P8のID安定性ゴールデン、全フェーズのper-scenario不変+replayer/odrviewerスモーク。
2. **フォーク成長 vs R4** — 1→~12ブロック/105-115行、上限150行(承認制)。マーカーctest+関数名アンカー+P9リハーサルで機械的再適用を証明。
3. **P6公開API実現性** — Road::geometry_はprivate。Day-1スパイクで判定、コンティンジェンシーは事前承認済み。
4. **ヘッダ制約** — RoadManager.hpp編集は禁止のまま。加算ヘッダマーカー例外はP6/P8の最終手段としてのみ存在。
5. **ASAMフィクスチャライセンス** — 初日ブロッカー。ダウンローダfallbackでCI緑維持(ネットワーク依存)。
6. **道路分割の正しさ(P6)** — 分割不変単体テスト+ゴールデン。xosc参照の事前決定で無言のシナリオ破壊を防止。
7. **ゴールデン保守** — 初日から許容誤差比較(RM 1e-6 / OSI epsilon、バイト同一は同一マシン限定)。--update-goldenレビュー規約でゴム印化を防止。
8. **upstream先行対応との二重処理** — #592+当方PRで plausible。「handled-by-upstream」ホワイトリスト状態+常設二重処理ガードで機械検出。
9. **「あらゆる要素」への圧力** — L1全達成は要素+属性粒度で機械検証。保留レベルはP9台帳で完全開示。
10. **単独開発者の直列化** — F3を独立週化済み。P5/P6がjunction集中区間。**どのフェーズ後に中断しても正味プラス**(P5終了≈8週で実測破損3件すべて修正済み)。

---

## 10. 着手前に必要な承認事項

1. **CMake swap-zone拡張**(EnvironmentSimulator/Modules/RoadManager/CMakeLists.txtへ odr_side/\*.cpp+GT includeディレクトリ追加)= 既存3行例外を超える**新R1例外**(P1マージ前)
2. **フォーク行数ハード上限150行**(基本105-115+列挙済みコンティンジェンシー)
3. **P6コンティンジェンシーの事前承認**(公開API不可→+25フォーク行、最終手段=加算ヘッダマーカー例外)
4. **P6のxosc位置参照方針**(remap shim か 文書化された制限+診断か)— P6実装前
5. **§8保留台帳の承認**(いずれかをスコープ内へ昇格させる場合は工数再見積り)
6. **`<include>`の扱い**(解決実装 or ハードエラー維持)— P9まで
7. **ASAMフィクスチャ再配布可否の確認** — P0初日

---

## 11. 経緯: 敵対審査による主要改訂(起草→最終)

- **[クリティカル/両審査一致]** countryRevision修正のP1/P3分割: 素朴なabsent→2013修正は既存全TrafficLight(scene 09歩行者信号ゲート含む)をdemoteすることをコード+資産で実証 → P1は挙動不変の読取修正(absent→0維持)、ゲート緩和はP3で監査とatomic。PR-1も同形に再構成。
- **[実現性メジャー]** P8レイヤ「選択」案は公式サンプル(temporaryがs2000-5083のみ)で不成立 → s範囲マージ設計に変更、P8を2週へ。
- **[実現性メジャー]** P6は過小見積り+xosc位置参照破壊が未対処 → 2週へ、決定事項+Day-1スパイク+受入テスト追加。
- **[実現性メジャー]** P5過積載 → boundary/elevationGrid/junctionGroupをP7へ、アボート耐性化をP1へ前倒し(公式1.8フィクスチャのP0ベースラインにも必要)、F3を独立週化。
- **[網羅性メジャー]** Ex_Lane-Borderの幅ゼロ偽グリーン → border→width正規化をP2に追加。
- **[網羅性メジャー]** フック位置をCheckConnections**後**→**前**へ(junction修復パスが合成junctionへミラー接続を張れるように。元の配置根拠 — odr_filename_未設定経路、RMのみエントリポイント、SetRoadOSI前 — は新位置でも同一に成立)。
- **[網羅性メジャー]** 要素粒度監査では1.9の属性レベル差分を見逃す → (要素,属性)ペアのホワイトリストへ。
- **[ガバナンスメジャー]** CMake拡張は新たなR1関連コア改変 → 第一級マニフェスト行+明示サインオフ。消費者リストにreplayer/odrviewer/odrplot/esminiJSを追補、replayer/odrviewerスモークをP1からゲート化。
- **[ガバナンスメジャー]** P3監査ユニバースが狭い → 「ゲートが読む全xodr」(Unittest/xodr含む)へ拡大、観測量をcast成功に修正。
- **[ガバナンスメジャー]** resync手順が約束止まり → P9の命名成果物(書面チェックリスト+二重処理ガード常設)へ。
- 工数計は約12-14週→**約15-17週**へ補正(実現性審査のキャリブレーション受入)。

---

## 12. 関連文書・出典

- [技術的負債監査 & ロードマップ](tech_debt_audit_2026-06.md) — R1/R4/F3等の前提
- 調査の一次資料: `thirdparty/opendrive/1.9/`(ASAM 1.9.0公式パッケージ)、`resources/schema/OpenDRIVE_1.5〜1.8`(世代間diffの基準)
- 実測に使った主要ファイル: GT_esmini/src/road/GT_RoadManager.cpp(:4074 lanes単一読取 / :4221 レーン型 / :4821 countryRevision / :4887 TLゲート / :5396 junction型 / :5429 アボート / :5490 CheckConnections)、GT_esmini/src/osi/GT_OSIReporter_Geometry.cpp、scripts/run_schema_comply.py
