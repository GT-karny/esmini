# P3 `[GT_ODR:tl-gate]` 全資産 TrafficLight 分類監査(レビュー済み diff)

- 対象パッチ: `GT_esmini/src/road/GT_RoadManager.cpp` `[GT_ODR:tl-gate]`(plan P3、クラスタ 11)
- ゲート BEFORE(P1 時点): `lower(country)=="opendrive" && countryRevision<2013 && dynamic`
  (countryRevision は P1 `[GT_ODR:country-rev]` の legacy 保存形: 省略→0、明示値尊重)
- ゲート AFTER: `dynamic`(= `@dynamic=="yes"`)のみ。country / countryRevision は分類に不関与。
- 観測量: `dynamic_cast<roadmanager::TrafficLight>` の成否(= OSI traffic_light 存在と等価。
  `dynamic_signals_` 登録はゲート非依存 :4959-4962 のため demote 被害は cast 消費者に限定)。

## 監査方法(機械生成 + 二重検証)

1. **解析的モデル** `GT_esmini/scripts/odr_tl_classification_audit.py`: ゲートは signal 3 属性の純関数
   なので、DLL 無しで全ユニバースの BEFORE/AFTER を評価し diff を機械生成
   (出力: `GT_esmini/test/odr_fixtures/reports/tl_gate_audit.{json,md}`、gitignored、再生成可能)。
2. **C++ 実測との相互検証**: コミット済みユニバース(114 ファイル/443 signal)について、
   - BEFORE: パッチ**前**コードの `test_OdrAssetProbe` ゴールデン
     `golden/trafficlight_classification.json`(P1 凍結)と `--check-golden before` が**完全一致**。
   - AFTER: パッチ**後**コードで `GT_ODR_PROBE_UPDATE=1` 再取得したゴールデンと
     `--check-golden after` が**完全一致**(本コミットで凍結)。
   解析的モデルと dynamic_cast 実測が両側で一致 = diff は信頼できる。

## ユニバース(「いずれかのゲートが読み込む全 xodr」= 150 ファイル / 794 signal)

| 起源 | ディレクトリ | ファイル数 |
|---|---|---|
| コミット済みプローブユニバース(ctest 凍結) | resources/xodr(26)+ road_catalog/generated(5)+ DriverScript/resources/xodr(2)+ EnvironmentSimulator/Unittest/xodr(57)+ odr_fixtures/handauthored(18)+ odr_fixtures/generated(6) | 114 |
| ASAM official(ローカル展開、適合ハーネス RM/OSI 層が読む。再配布不可のためゴールデン外) | odr_fixtures/official/** | 36 |

- `resources/xosc/verification/`(検証用道路)と `scenario_authoring/scenario_templates/generated/`
  に .xodr は存在しない(シナリオは road_catalog/generated の道路を参照 — ユニバースに含有済み)。
- scene 09 の `straight_crosswalk_pedsig__mid.xodr`(歩行者信号 type=1000002)はユニバース内・**フリップなし**。

## 結果: フリップ 49 件 / 794 signal(その他 745 signal は前後同一)

### コミット済みユニバース: 3 件のみ(すべて GT 作成の P0 フィクスチャ。本番資産のフリップは 0)

| ファイル | signal | 属性 | 根拠(レビュー) |
|---|---|---|---|
| handauthored/03_dynamic_signal_demote_18.xodr | id=1 `tl_de_2021` | dynamic=yes, country=DE, rev=2021 | **意図されたフリップ**(フィクスチャ (a): 1.8 イディオム alpha-2 国コード demote の再現用)。dynamic=yes なので TrafficLight が正。 |
| handauthored/03_dynamic_signal_demote_18.xodr | id=2 `tl_odr_2021` | dynamic=yes, country=OpenDRIVE, rev=2021 | **意図されたフリップ**(フィクスチャ (b): 明示 countryRevision demote の再現用 = 実測破損 #1 の最小形)。 |
| handauthored/05_vms_boards.xodr | id=1 `gantry_vms` | dynamic=yes, country=DE, rev 省略 | **仕様どおりのフリップ**: VMS は動的表示体で `dynamic=yes` が正しいオーサリング。OpenDRIVE の意味論上 dynamic=可変状態信号であり TrafficLight 昇格は一貫。type=1000001 は TL 型テーブルに存在(3 灯生成)。VMS 固有の L3 意味論は P4(クラスタ 13)の管轄で、本フェーズでは OSI traffic_light(3 灯)としての出力に変わるのみ。走行シナリオでの参照ゼロ(フィクスチャ専用資産)。 |

- **本番資産(resources/ / Unittest/ / DriverScript/ / scenario_authoring/)のフリップは 0 件** —
  既存の全動的信号は `country="OpenDRIVE"` + countryRevision 省略で既に TrafficLight(ゲート前後で同値)。
  よって phase3/phase3d/crosswalk バッチ・upstream テスト・web スタックの挙動入力は不変。

### ASAM official: 46 件(すべて Signal→TrafficLight 昇格 = 実測破損 #1 の修正そのもの)

| ファイル | 件数 | 属性パターン | 根拠(レビュー) |
|---|---|---|---|
| use_cases/UC_T_Junction.xodr | 12 | dynamic=yes, country=OpenDRIVE, **rev=2013 明示** | ASAM 公式は `countryRevision="2013"` を明示し `2013<2013`=false で demote されていた(実測破損 #1)。車両/歩行者信号とも TrafficLight が正。 |
| use_cases/UC_LHT_Complex-TrafficLights.xodr | 12 | 同上 | 同上(ファイル名どおり信号機ユースケース)。 |
| use_cases/UC_X_Junction.xodr | 7 | 同上(country=DE の歩行者信号 2 件含む) | 同上 + alpha-2 国コード形。 |
| use_cases/UC_5Road_Junction.xodr | 7 | 同上 | 同上。P3 の signalReference RM ゴールデン対象ファイル。 |
| use_cases/UC_Simple-X-Junction-TrafficLights.xodr | 4 | 同上 | 同上。 |
| examples/Ex_Slip_Lane.xodr | 4 | 同上 | 同上(なお本ファイルは RM_Init クラッシュの XFAIL 凍結中 — P5 再訪。分類 diff は DOM 解析による)。 |

## 併発修正: 0 灯 TrafficLight の FFI クラッシュ(upstream 潜在バグ)

`TrafficLight::SetTrafficLightInfo` は `traffic_light_type_map` に無い type 組合せで `nr_lamps_` を
**未初期化のまま**残す(hpp は pristine のためデフォルト初期化子も無い)。`GetNrLamps()` がゴミ値を
返し、OSI reporter / VD ポリシーの `GetLamp()`(`lamps_.at`)が throw → C API 境界で 0xe06d7363。
ゲート緩和により「任意 type の動的信号」が TrafficLight 化されるため P3 で顕在化(fixture 03 の
OSI プローブで実測)。`[GT_ODR:tl-gate]` ブロック 2 として `nr_lamps_ = 0` を設定(独立 upstream PR 候補)。

なお 0 灯 TrafficLight は OSI に per-lamp エンティティを 1 つも出さない(pre-P3 は誤って
traffic_sign として出ていた)。既知の劣化として記録し、type カタログ強化は P4(クラスタ 10/13)で扱う。
P0 フィクスチャ 03/05 は `value="0.0"` が複合 type(`1000001-0.0`)を灯火型テーブル外にしていたため
value 属性を除去(fabriksgatan と同形へ。分類 diff・RM ゴールデンに影響なし、OSI 観測が有効化)。

## 消費者影響レビュー(フリップが波及する cast 消費者)

- `TrafficSignalController.cpp`(:65/:151)/ `GT_OSIReporter_Traffic.cpp`(:160)/ VD
  `TrafficLightAware`・`RouteCrosswalkScan`・`CrosswalkPedestrianAware` / `GT_esminiLib.cpp`(:1602):
  いずれも影響はフリップした signal を積んだ道路を使う場合に限定。本番資産フリップ 0 のため
  既存シナリオ・バッチ・web WS 位相チャンネルは per-scenario 不変(受入 (ii)、ゲートで機械検証)。
- OSI 静的 GT では、フリップ信号は traffic_sign → traffic_light(lamps)へ移動する
  (`UpdateStaticTrafficSignals` の分岐)。1.5 コントロールセット(31 本)にフリップは無く、
  traffic_sign 一覧は不変(受入 (i) 後段、OSI ゴールデン一致で機械検証)。

## 凍結

- 本コミットで `golden/trafficlight_classification.json` を AFTER 状態に更新(ゲート緩和と同一コミット
  = atomic 規律)。以後 `test_OdrAssetProbe` ctest が AFTER 分類を凍結する。
- 再生成手順: ビルド後 `GT_ODR_PROBE_UPDATE=1` で `test_OdrAssetProbe` を 1 回実行 →
  `odr_tl_classification_audit.py --check-golden after` が OK であること。
