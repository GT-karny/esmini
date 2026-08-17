# feature:F9 — SUMO 背景交通（実験機能）

SUMO のマイクロ交通流シミュレーションを背景交通としてシーンに入れる実験機能。
既定 OFF・常設ゲート非対象。ロードマップ（`tech_debt_audit_2026-06.md` §5 の F1〜F6）の外で
発生した後発機能で、F8 と同じく定義をこのファイルに置く。

- 作成: 2026-08-17
- 状態: **設計確定・未実装**
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

> **未実証**: `setSpeedMode()` で実際に上限が外れることは、まだ測っていない。
> 上の実験は net.xml 側を書き換えて上限を動かしただけで、API で外したわけではない。
> 実装フェーズの最初のタスクとして、両極性（モード既定 → 刈られる／モード解除 → 追従）を
> 実測してから設計を確定すること。

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

§2 を踏まえた実装要件。

1. **方位**: `GetAngleInInterval2PI(M_PI/2 - h) * 180/M_PI` で navigational degrees へ変換。
   逆向き（SUMO → esmini）は既存の `-getAngle()*M_PI/180 + M_PI/2` と対称。
2. **基準点**: 双方向で `center_.x_ + length/2` を heading 方向へ射影して足し引きする。
   `Object::OverlappingFront()` が車体前端を求めるのと同じ式を使う。
3. **ピッチ**: `getSlope()` を反転して渡す。
4. **Ego 速度**: `setSpeedMode()` で上限を外したうえで `setSpeed()`。§2-4 の未実証項目を
   実装の最初に潰す。外れないと分かった場合は「SUMO 車が Ego の速度を見誤る」ことを
   既知の制約として明記し、シナリオ側を制限速度に合わせる運用でしのぐ。
5. **注入直後の立ち上がり**: 投入時に初速を与える（vType の `departSpeed`、
   または投入直後 1〜2 ステップの速度を明示設定）。約 1.2 秒の 0 → 目標速度の
   ランプを消す。
6. **ヨー**: SUMO 1.6.0 の `computeAngle()` は車線変更時のヨーオフセットを
   `--lanechange.duration` のアニメーション状態でしか出さず、sublane モデルでは出ない。
   **GT 側で位置履歴から heading を算出して上書きする**（SUMO の angle をそのまま
   使わない）。これは 1.6.0 の制限を GT 側で回避できる数少ない項目。
7. **エンティティの削除**: `getArrivedIDList()` に載った ID で esmini 実体を消す前に、
   **自分が注入した ID かを確認する**。upstream はこの区別が無く、シナリオ定義の Ego が
   SUMO の「到着」判定で消える。GT 版は自分が `add()` した ID の集合を持ち、
   その中に無い ID は無視する。

---

## 4. 設定と既定値

`GT_esmini/config/sumo_traffic.json`（`ConfigLoader` が `exe_dir/config/` から解決）。
`auto_light.json` / `manual_drive.json` と同じ流儀。

```jsonc
{
  "enabled": false,          // 実験機能。既定 OFF
  "sumocfg": "",             // .xosc の <File filepath> が優先。ここは既定値
  "seed": 42,                // 決定論性。0 以下でランダム
  "step_length": 0.05,       // esmini の fixed_timestep と揃える
  "inject_ego": true,        // esmini エンティティを SUMO へ注入するか
  "override_heading": true,  // §3-6。SUMO の angle でなく位置履歴から算出
  "speed_mode": 0            // §3-4。0 = 全チェック無効
}
```

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
  `category_` は CAR 固定（upstream の既知の不整合）。GT 版でも同じなら明記する。
- **公式の位置づけ**: esmini の SUMO 連成は upstream が
  "experimental level and has not been used a lot" としている。実験機能として扱う根拠。

---

## 6. 検証

実装フェーズで詰める。現時点で決まっていること:

- **ユニット**: 座標・方位・基準点の変換は純関数に切り出し、傘バイナリ
  （`test_ScenarioReaderParsing`）に載せる。両極性（正しい変換 / 恒等変換）を
  テストで固定する。§2 の実測値が回帰の錨になる。
- **常設ゲートには載せない**（実験機能）。載せるとしたら決定論性の確認後。
- **スモーク**: 背景交通ありの xosc を 1 本作り、exit 0 と SUMO 車の出現数を見る。
- **計器の罠**: `--csv_logger` は wide 形式で、**エンティティの出入りで行の列数が変わる**。
  先頭ブロックが Ego とは限らない。列位置を固定で決め打ちすると静かに別の列を読む
  （本調査でも一度踏んだ。`World_Position_X` はエンティティ名から +11、`World_Heading_Angle` は +24）。
  名前を探してからオフセットで読むこと。

---

## 7. 関連

- コンフィグ作成: `/sumo-authoring` スキル（xodr → net.xml の規格適合と、需要 / vType の作法）
- upstream への還元候補: §2-2 / §2-3 / §2-5 は upstream `ControllerSumo` のバグでもある。
  `GT_esmini/upstream_pr/` の PR-A〜D と同じ流儀で PR 化できる（§2-4 の訂正込み）。
