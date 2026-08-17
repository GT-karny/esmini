# feature:F9 — SUMO 背景交通（実験機能）

SUMO のマイクロ交通流シミュレーションを背景交通としてシーンに入れる実験機能。
既定 OFF・常設ゲート非対象。ロードマップ（`tech_debt_audit_2026-06.md` §5 の F1〜F6）の外で
発生した後発機能で、F8 と同じく定義をこのファイルに置く。

- 作成: 2026-08-17
- 状態: **実装済み（既定 OFF・常設ゲート非対象）**。実装の所在と検証済みの範囲は §3 / §6。
- 用途: (a) 手動運転中の周囲交通（GT_Sim.exe / Web UI）、(b) VD 検証シナリオの背景交通

---

## 1. 方式

**GT 側に独自コントローラ `ControllerSumoTraffic` を新設し、リンク済みの libsumo を
プロセス内で直接叩く。** upstream の `ControllerSumo` は触らない（R1 Clean Core）。

`USE_SUMO` は既定 ON で、SUMO 1.6.0 の静的ライブラリ 28 本と `libsumo/` ヘッダが
すでにビルドに入っている（`externals/sumo/v10`、`_USE_SUMO` 定義済み）。
つまり**依存の追加はゼロ**で、GT 側から `libsumo::Vehicle` / `libsumo::Simulation` を呼べる。

登録は既存経路に相乗りする:

```cpp
#define CONTROLLER_SUMO_TRAFFIC_TYPE_NAME "GTSumoTrafficController"

scenarioengine::ScenarioReader::RegisterController(
    CONTROLLER_SUMO_TRAFFIC_TYPE_NAME, gt_esmini::InstantiateControllerSumoTraffic);
```

`resources/xosc/` の既存 4 本（`sumo-test.xosc` / `sumo-test_acc.xosc` /
`sumo_react_test.xosc` / `cut-in_sumo.xosc`）は `SumoController` を指したまま
upstream 実装で動き続け、GT 版は明示的に選んだシナリオだけが使う。

### 1-1. 命名 — なぜ GT 接頭辞を付けるか

xosc 側の選択は upstream のプロパティキー `esminiController` を通る
（`ScenarioReader.cpp:1157` が登録済みコントローラを引くキー）。
このキーは GT 製コントローラも共有していて、`VirtualDriverController` 131 件 /
`ManualDriveController` 29 件 / `RealDriverController` 10 件が既にこの形で書かれている。
**キー名自体は upstream の資産なので F9 では触らない**（変えるには ScenarioReader の
フォーク＝R1 違反と、既存 xosc 180 件超の書き換えが要る）。

危険はキーではなく**値**にある。当初案の `SumoTrafficController` は upstream の
`SumoController` と 1 語違いで、同じドメインで、実装が別物。しかも
**取り違えてもエラーが出ない** — 動いてしまい、§2-2 の方位ずれを抱えた背景交通が
静かに出てくるだけになる。C++ 側は `gt_esmini::ControllerSumoTraffic` と
`scenarioengine::ControllerSumo` で名前空間が効くが、**xosc の文字列に名前空間は無い**。

そこで値を **`GTSumoTrafficController`** とし、GT 製であることと役割（背景交通）の
両方を名前に語らせる。既存の GT コントローラに GT 接頭辞が無いのは upstream に
同名が無いからで、**upstream 同名が存在するのは SUMO が初めて**。
一貫性の破れではなく「区別が要る場所にだけ区別を置く」規則として扱う。
今後 upstream と名前が衝突するコントローラを足すときも同じ規則を適用すること。

### 却下した方式

| 方式 | 却下理由 |
|---|---|
| upstream `ControllerSumo` にパッチを当てる | R1 違反。かつ直しても §2 の欠陥は upstream 実装の構造に残る |
| Python ランナー（traci で外部 SUMO 1.27.1） | 連成品質は最も高いが GT_Sim.exe / Web UI で動かず、用途 (a) を満たさない |
| `externals/sumo` を 1.27.1 へ差し替え | externals は git 管理外で esmini 配布 URL から取得する。自前パッケージのホスティングか自前ビルドが要り、upstream `ControllerSumo`・CI・配布パッケージの全体に波及する |

---

## 2. upstream `ControllerSumo` の状態（このビルドで実測）

GT 版を書く根拠。**伝聞ではなく現ビルドで測った値**。

- ビルド: `v3.4.1_GTv0.14.1-88-2937062d` / Windows / Release
- シナリオ: `resources/xosc/cut-in_sumo.xosc`（`e6mini`、40 秒、`--fixed_timestep 0.05`）
- 計測: esmini 側 `--csv_logger`、SUMO 側 `<fcd-output>` を足した sumocfg で突き合わせ

### 2-1. 連成そのものは動く

headless で exit 0。SUMO 車は 8 台入る（`car1`/`bus1`/`truck1`/`truck1+`/`truck1++`/
`car2`/`car3`/`car4`）。連成は壊れていない。

**`revMinor="0"` でも SUMO 車は出る。** 「`revMinor="0"` だとエラーも警告もなく exit 0 で
完走し SUMO 車が 1 台も出ない」という報告があるが、`cut-in_sumo.xosc`（`revMinor="0"`）で
**再現しなかった**。原因は revMinor 以外にある。この罠を前提に設計しないこと。

### 2-2. 方位が度/ラジアン取り違えで SUMO へ渡る（確認）

`moveToXY()` は navigational degrees（0 = 北・時計回り）を取るが、
`obj->pos_.GetH()` の数学ラジアンがそのまま渡っている。

| t | esmini h [rad] | 正しい nav [deg] | SUMO の angle [deg] |
|---|---|---|---|
| 5.0 | 1.5674 | 0.194 | **1.570** |
| 20.0 | 1.5660 | 0.273 | **1.570** |
| 39.0 | 1.5245 | 2.653 | **1.520** |

SUMO 側の値がラジアン値そのものになっている。

**この道路では誤差が小さく見えるのが罠。** 東向き（h ≈ π/2）では正解 0.2° / 実際 1.57° で
差が 1.4° しかないが、これは偶然。h ≈ 0（+X 向き）なら正解 90° / 実際 0° で**誤差 90°**になる。
平坦・直線・東向きのシナリオで「だいたい合っている」と見えても、判定に使ってはいけない。

### 2-3. 位置の基準点が変換されない（確認）

SUMO の `getPosition3D()` / `moveToXY()` は前バンパー中心、esmini の `pos_` は
OpenSCENARIO 基準点。どちらの向きにも変換が無い。

Ego の bbox は `center_.x_=2.0` / `length=5.0` なので、正しく変換していれば
SUMO 側は esmini 基準点の **+4.5 m 前方**にいるはず。実測:

| t | 前方成分 | 横成分 |
|---|---|---|
| 5.0 | **-0.003 m** | +0.004 m |
| 20.0 | **-0.005 m** | +0.002 m |
| 39.0 | **-0.004 m** | +0.002 m |

ゼロ。変換されていない。

### 2-4. ★`setSpeed` は効く。効かなく見えるのは速度上限で刈られているため（確認）

**これが設計上いちばん重要な訂正。**
「`setSpeed()` は `moveToXY()` で遠隔制御される車両には無効」という報告があり、
libsumo 1.6.0 に `setPreviousSpeed()` が無い（確認済み）ため「1.6.0 では直せない」と
結論しかけたが、**実測すると別の話だった**。

Ego は t=13 から 10 秒かけて 3 → 20 m/s へ加速するシナリオ。

| 車線の制限速度 | 平均誤差 | 最大誤差 | 挙動 |
|---|---|---|---|
| 13.89 m/s（既定のまま） | 2.592 m/s | 7.560 m/s | **14.72 m/s で頭打ち** |
| 30.00 m/s（同じ net の 14 車線を書き換え） | 0.046 m/s | 3.000 m/s | **20 m/s まで完全追従** |

同一シナリオ・同一コードで、**車線の制限速度だけを変えると誤差が消える**。
`14.72 / 13.89 = 1.0598` は `speedFactor` と整合する。

つまり `setSpeed` は遠隔制御車にも効いており、要求値が
**車線制限速度 × speedFactor** を超えた分だけが刈られていた。
上限を外す口は `setSpeedMode()` で、**これは 1.6.0 に存在する**。

#### 実測: `setSpeedMode(0)` で上限は外れる（2026-08-17、両極性）

計測器は `GT_esmini/test/unit/sumotraffic/test_SumoSpeedModeProbe.cpp`（ctest ターゲット
`test_SumoSpeedModeProbe`、常設ゲート非対象）。**esmini も GT コントローラも通さず
libsumo だけを叩く**ので、結果を注入経路のせいにできない。両極性の差は
`setSpeedMode(id, 0)` の 1 文だけ。

条件: `e6mini.sumocfg`（全車線 13.89 m/s）、`DEFAULT_VEHTYPE` を `add()` で投入し、
毎ステップ `moveToXY()` + `setSpeed()`。指令は 2 秒 3 m/s → 10 秒で 20 m/s へ線形 → 8 秒保持。
step 0.05 s / seed 42。

| speed mode | 定常（t≥15 s）平均 | 全域ピーク | 指令 |
|---|---|---|---|
| 既定（31・呼ばない） | **13.172 m/s** | 13.172 m/s | 20.000 m/s |
| `setSpeedMode(id, 0)` | **20.000 m/s** | 20.000 m/s | 20.000 m/s |

同一 net・同一車線制限で、この 1 文の有無だけが 6.8 m/s を分ける。上限は API で外れる。
**§3-4 は設計どおりで確定**。

刈り取り値が §2-4 冒頭の 14.72 m/s でなく 13.172 m/s なのは `speedFactor` が車両ごとの
サンプルだから（13.172 / 13.89 = 0.9483、cut-in の Ego は 1.0598）。刈られる値が
「車線制限 × speedFactor」であることのほうが、特定の数値より効く。

計測器そのものの検証（`GT_SUMO_PROBE_CFG` で `<fcd-output>` 付き sumocfg に差し替え、
SUMO 自身の出力と突き合わせ）: 定常では最大 0.0019 m/s 差（fcd の小数 2 桁丸めの範囲）。
加速中のみ最大 0.09 m/s ずれるが、これは 1 ステップ分の加速度（1.7 m/s² × 0.05 s = 0.085）で、
値の不一致ではなく系列のラベル付けが 1 ステップずれているだけ。3 回連続実行で両極性とも同値。

GT コントローラ経由の end-to-end でも確認済み（§6）。

30 m/s 側で残った最大誤差 3.000 m/s は**注入直後の立ち上がり**で、
t=0.05 に 3.00 m/s の差、そこから約 1.2 秒かけて線形に 0 へ収束する。
SUMO は新規投入車を 0 から既定加速度で立ち上げるため。定常状態の誤差はゼロ。

### 2-5. ピッチ符号（未実測）

SUMO の `getSlope()` は登り正、OpenDRIVE/esmini のピッチは登り負。
`ControllerSumo.cpp` は反転せずに渡している（コード上は確認済み）。
`e6mini` が平坦（`World_Pitch_Angle` ≈ 3.7e-5）なため**このビルドでは未実測**。
勾配のある xodr で測ってから記録すること。

---

## 3. GT 版が満たすこと

§2 を踏まえた実装要件。**7 項目とも実装済み**（2026-08-17）。実体:

- `GT_esmini/include/gt_esmini/control/ControllerSumoTraffic.hpp` / `src/control/ControllerSumoTraffic.cpp`
- 変換は純関数層 `include/gt_esmini/control/sumotraffic/SumoTransform.hpp` / `src/control/sumotraffic/SumoTransform.cpp`
  に切り出し、傘バイナリ `test_ScenarioReaderParsing` に 14 ケース（§6）

各項目の末尾に、どこまで実証したかを付記する。

1. **方位**: `GetAngleInInterval2PI(M_PI/2 - h) * 180/M_PI` で navigational degrees へ変換。
   逆向き（SUMO → esmini）は既存の `-getAngle()*M_PI/180 + M_PI/2` と対称。
   → `HeadingToSumoAngle` / `SumoAngleToHeading`。ユニット（§2-2 の実測 3 点＋恒等変換との
   識別＋h=0 で 90° 差）と end-to-end（§6）の両方で確認済み。
2. **基準点**: 双方向で `center_.x_ + length/2` を heading 方向へ射影して足し引きする。
   `Object::OverlappingFront()` が車体前端を求めるのと同じ式を使う。
   → `RefPointToFront` / `FrontToRefPoint`。end-to-end で前方成分 +3.92 m（§6）。
3. **ピッチ**: `getSlope()` を反転して渡す。
   → `SumoSlopeToPitch`。**符号規約はユニットで固定したが、実路面での確認は未**
   （§2-5 と同じ理由＝手元に勾配のある SUMO net が無い）。
4. **Ego 速度**: `setSpeedMode()` で上限を外したうえで `setSpeed()`。
   → **§2-4 で両極性を実測して確定**（既定 13.172 m/s 頭打ち / mode 0 で 20.000 m/s 追従）。
   `speed_mode` を負にすれば `setSpeedMode` を呼ばない＝upstream と同じ刈られる挙動に戻せる。
   これは設定の飾りではなく、この主張を後から再実証するための負の対照。
5. **注入直後の立ち上がり**: 投入時に初速を与える（vType の `departSpeed`、
   または投入直後 1〜2 ステップの速度を明示設定）。約 1.2 秒の 0 → 目標速度の
   ランプを消す。
   → `add()` の `departSpeed` にその時点の `obj->GetSpeed()` を渡す。
6. **ヨー**: SUMO 1.6.0 の `computeAngle()` は車線変更時のヨーオフセットを
   `--lanechange.duration` のアニメーション状態でしか出さず、sublane モデルでは出ない。
   **GT 側で位置履歴から heading を算出して上書きする**（SUMO の angle をそのまま
   使わない）。これは 1.6.0 の制限を GT 側で回避できる数少ない項目。
   → `HeadingFromDisplacement` + コントローラ側の 1 ステップ履歴。変位が 0.05 m 未満の間は
   直前の heading を保持し、履歴が無い最初の 1 回だけ SUMO の angle に落ちる。
   **ユニットのみ。車線変更中のヨーが実際に出ることは未確認**（車線変更する SUMO 需要を
   まだ作っていない。§6 の積み残し）。
7. **エンティティの削除**: `getArrivedIDList()` に載った ID で esmini 実体を消す前に、
   **自分が注入した ID かを確認する**。upstream はこの区別が無く、シナリオ定義の Ego が
   SUMO の「到着」判定で消える。GT 版は自分が `add()` した ID の集合を持ち、
   その中に無い ID は無視する。
   → `spawned_ids_` で判定。**コード上の実装のみで、Ego が SUMO 到着するシナリオでの
   再現・非再現は未測定**（20 秒のスモークでは到着が起きない）。

### 3-8. ホストオブジェクトの扱い（upstream との非対称・実装して分かったこと）

upstream の SUMO コントローラは `ScenarioReader` が `CONTROLLER_TYPE_SUMO` を特別扱いして
(a) テンプレート車を entities に入れない (b) コントローラを無条件で Activate する。
**user-range のコントローラ型（`GTSumoTrafficController` = 1003）にはこの特別扱いが効かない。**
R1 のため ScenarioReader を触れないので、GT 側の `Init()` で両方を自前で行う:

- ホストの ScenarioObject をテンプレートとして保持し、active なら `deactivateObject()` で
  シーンから外す。`removeObject()` は**削除する**ので使えない（3D モデルのテンプレートが消える）。
- 自分で `Activate()` する。エンジンは `Active()` なコントローラしか `Step()` しない。

所有権も非対称になる。upstream のテンプレートは `addObject()` を通らないのでコントローラが
所有者だが、GT 版のホストは `addObject()` を通っていて **`Entities` が所有する**
（`~Entities()` が `object_` と `object_pool_` の両方を delete する）。
デストラクタで delete してはいけない。

なお通常はホストは最初から inactive で、`deactivateObject()` は空振りする
（entity が active になるのは `<Init>` に Private アクションがある場合だけ）。
それでも呼ぶのは、ホストに TeleportAction を書いたシナリオで路上に幽霊車が残るため。

---

## 4. 設定と既定値

`GT_esmini/config/sumo_traffic.json`（`ConfigLoader` が `exe_dir/config/` から解決）。
`auto_light.json` / `manual_drive.json` と同じ流儀。

```jsonc
{
  "enabled": false,          // 実験機能。既定 OFF
  "sumocfg": "",             // .xosc の <File filepath> が優先。ここは既定値
  "seed": 42,                // 決定論性。0 以下で SUMO の乱数に任せる
  "step_length": 0.05,       // esmini の fixed_timestep と揃える。0 以下で .sumocfg の値
  "inject_ego": true,        // esmini エンティティを SUMO へ注入するか
  "override_heading": true,  // §3-6。SUMO の angle でなく位置履歴から算出
  "speed_mode": 0            // §3-4。0 = 全チェック無効／負で setSpeedMode を呼ばない
}
```

シナリオ側 Property で 1 件ずつ上書きできる（キーは `enabled` / `injectEgo` /
`overrideHeading` / `seed` / `speedMode` / `stepLength` / `overrideVehicleScaleMode`、
別ファイルを使うなら `ConfigFile`）。**JSON はインストール全体の既定、Property はその
シナリオの意思**という分担で、背景交通が要るシナリオは自分で `enabled=true` を書く。
既定 OFF の意味は「明示的に名指ししたシナリオでしか動かない」ではなく
「名指ししてもなお JSON か Property で ON にするまで SUMO を読み込まない」。

### 決定論性

用途 (b)（VD 検証の背景交通）では回帰ベースラインの自己決定論性が前提になる。
SUMO は既定で乱数を使うため、**シードを固定しないとベースラインを凍結できない**。

- `.sumocfg` に `<seed value="N"/>` を書く、または `seed` 設定で渡す
- `randomTrips.py` / `duarouter` の需要生成側にもシードがある（生成物を commit するなら不要）
- **凍結前に 3 回連続実行して自己決定論性を確認する。** 確認できるまでベースラインを作らない

用途 (a)（運転体験）ではシード固定は不要。設定で分ける。

---

## 5. 境界と既知の制約

- **R1**: `EnvironmentSimulator/` と `OSMP_FMU/` は無改変。GT 版は
  `GT_esmini/src/control/ControllerSumoTraffic.cpp` +
  `GT_esmini/include/gt_esmini/control/ControllerSumoTraffic.hpp` に閉じる。
- **SUMO 1.6.0（2020年）の天井**: 新しい vClass（`container` / `cable_car` / `subway` /
  `aircraft` / `wheelchair` / `scooter` / `drone`）を知らず、現行 netconvert が書いた
  net.xml を読むと `Unknown vehicle class` で **exit 255**。
  → net.xml 生成時に未知 vClass を除去する（`/sumo-authoring` スキルの検証層が担う）。
  net format version も同梱 1.9 に対し現行生成は 1.20 と開いている。
- **左側通行**: netconvert は `rule="LHT"` を読まず、`--lefthand` を付けるとレーンが
  最大 3.5 m 路外へ出る（SUMO 側の未解決 issue [#7692](https://github.com/eclipse-sumo/sumo/issues/7692)）。
  ただし**このリポジトリの道路は RHT 23 本 / LHT 1 本**（生成カタログ 10 本は全 RHT）なので
  当面の制約にならない。LHT 道路で背景交通が要るときは netconvert 側のパッチが要る。
- **カテゴリ**: SUMO 車は vClass に応じた 3D モデルが選ばれるが esmini 内部の
  `category_` は CAR 固定。**GT 版も同じにした**（upstream の既知の不整合をそのまま踏襲）。
  直すには OSC カテゴリと 3D モデル選択の対応を作り直す必要があり、F9 の射程外。
- **libsumo はプロセスグローバル**: `Simulation::load()` は既存のシミュレーションを
  置き換える。1 プロセスに SUMO コントローラは 1 つだけ。GT 版は 2 つ目のインスタンスが
  ロード済みを検出したら **エラーを出して inert のまま留まる**（黙って奪い合わない）。
  upstream の `ControllerSumo` と同居させた場合はこの検出が効かない（あちらは無条件に
  load する）ので、同じシナリオに両方を書いてはいけない。
- **GT_Sim.exe ではコントローラをカタログ参照にできない**（F9 以前からの既存欠陥）。
  GT のサニタイザは xosc 内の `filepath` / `path` だけを絶対化して一時ディレクトリに書き出す。
  **カタログファイル内の相対パスは絶対化されない**ため、`ControllerCatalog` 経由で
  `<File filepath="../sumo_inputs/*.sumocfg"/>` を渡すシナリオは
  `Failed to localize controller file` で **exit 1** になる。
  実測（2026-08-17）: `cut-in_sumo.xosc`（カタログ参照）は GT_Sim で exit 1 / SUMO 車 0 台、
  同じファイルを vanilla `esmini.exe` で走らせると exit 0 / 6 台。
  `sumo-test.xosc`（インライン宣言）は GT_Sim でも exit 0 / 100 台。
  → **F9 のシナリオはコントローラをインライン宣言する**（スモーク xosc がそうしてある）。
  サニタイザ側の修正は別件。
- **公式の位置づけ**: esmini の SUMO 連成は upstream が
  "experimental level and has not been used a lot" としている。実験機能として扱う根拠。

---

## 6. 検証

### 実施済み（2026-08-17）

- **ユニット**（傘バイナリ `test_ScenarioReaderParsing`、14 ケース、緑）:
  `GT_esmini/test/unit/sumotraffic/test_SumoTransform.cpp`。
  §2 の実測値を錨にし、**両極性**を固定してある——「正しい変換の値」だけでなく
  「変換を省いた（＝upstream の）値」も書いてある。東向き道路では正解 0.194° と
  恒等 1.570° の差が 1.4° しかなく、"だいたい合っている" で欠陥が生き延びたのがまさに
  そこだから。h=0 で 90° 差になることも別ケースで固定した。
- **speed mode プローブ**（ctest `test_SumoSpeedModeProbe_default_mode` /
  `_cleared_mode`、両極性・緑・3 回連続同値）: §2-4 を参照。
- **スモーク**: `resources/xosc/sumo_background_traffic_gt.xosc`。
  GT_Sim headless / `--fixed_timestep 0.05` / 20 秒で **exit 0 かつ SUMO 車 6 台**
  （`car1`〜`car4` / `bus1` / `truck1` ＝ `e6mini.rou.xml` の需要と一致）。
  exit 0 だけでは合格にしない——upstream も車 0 台で exit 0 を返す。
- **既定 OFF の負の対照**: 同じ xosc から `enabled` Property を外すと exit 0 のまま
  `disabled ... No SUMO traffic` を出して SUMO 車 0 台。テンプレートのホスト車も
  シーンに現れない（csv_logger の `Number of Vehicles: 1` ＝ Ego のみ）。
- **既存シナリオの不変**: `sumo-test.xosc` は GT_Sim で exit 0 / 100 台のまま、
  `cut-in_sumo.xosc` は vanilla `esmini.exe` で exit 0 / 6 台のまま
  （GT_Sim では F9 以前からカタログ参照が壊れている。§5）。
- **end-to-end の変換確認**: スモーク xosc の絶対パス複製を、`<fcd-output>` 付きの
  sumocfg と `--csv_logger` の両方を取って突き合わせた。Ego は `car_white`
  （bb center_x 1.4 / length 5.04 → 前端は基準点 +3.920 m）。

  | t [s] | 前方成分 [m] | 横成分 [m] | SUMO angle [deg] | 正しい nav [deg] | SUMO が見た速度 [m/s] |
  |---|---|---|---|---|---|
  | 5.0 | **+3.925** | +0.003 | 0.200 | 0.197 | 8.020 |
  | 10.0 | **+3.917** | 0.000 | 0.250 | 0.248 | 16.520 |
  | 15.0 | **+3.924** | -0.001 | 0.450 | 0.446 | **20.000** |
  | 20.0 | **+3.920** | -0.002 | 0.790 | 0.786 | **20.000** |

  §2-3 の upstream 実測（前方成分 -0.003〜-0.005 m ＝ 変換なし）と §2-2（angle が
  ラジアン値そのまま = 1.570）に対し、3 項目とも直っている。angle の 0.003° 差は
  fcd 出力が小数 2 桁で丸まるため。速度は車線制限 13.89 m/s を超えて 20.000 m/s まで
  追従しており、§3-4 が実運用経路でも効いている。

### 積み残し（実装したが未実証）

- **§3-6 ヨー上書き**: 車線変更する SUMO 需要をまだ作っていないので、
  「SUMO の angle では出ないヨーが GT 側で出る」ことを実データで見ていない。
  ユニットは関数の入出力しか押さえていない。
- **§3-3 ピッチ符号**: 勾配のある SUMO net が無く、§2-5 と同じ理由で未測定。
- **§3-7 削除ガード**: Ego が SUMO 到着する長さのシナリオを回していない。
- **決定論性**: プローブ単体は 3 回連続同値だが、**GT コントローラ経由の
  シナリオ実行では未確認**。常設ゲート化の前提はこちら（§4・graph.yaml の
  `feature:F9 -> conflicts-with -> gate:regression-gate`）。

### 方針

- **常設ゲートには載せない**（実験機能）。載せるとしたら上の決定論性の確認後。
- **計器の罠**: `--csv_logger` は wide 形式で、**エンティティの出入りで行の列数が変わる**。
  先頭ブロックが Ego とは限らない。列位置を固定で決め打ちすると静かに別の列を読む
  （本調査でも一度踏んだ。`World_Position_X` はエンティティ名から +11、`World_Heading_Angle` は +24）。
  名前を探してからオフセットで読むこと。ヘッダの `Number of Vehicles:` は
  **開始時点の数**で、後から入る SUMO 車を含まない。
- **計器の罠 (SUMO 側)**: `getSpeed()` は投入直後の 1 ステップだけ
  `INVALID_DOUBLE_VALUE`（-1.07e9）を返す。平均や最大にそのまま混ぜると桁で壊れる。
  `<fcd-output>` の数値は既定で小数 2 桁に丸められるので、突き合わせの許容差はそこで決まる。

---

## 7. 関連

- コンフィグ作成: `/sumo-authoring` スキル（xodr → net.xml の規格適合と、需要 / vType の作法）
- upstream への還元候補: §2-2 / §2-3 / §2-5 は upstream `ControllerSumo` のバグでもある。
  `GT_esmini/upstream_pr/` の PR-A〜D と同じ流儀で PR 化できる（§2-4 の訂正込み）。
