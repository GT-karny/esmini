---
name: sumo-authoring
description: SUMO 側の入力（.sumocfg / .rou.xml / vType / .add.xml、および xodr から生成する .net.xml）を「無言で壊れたものを作らない」ための2層保証スキル。手書き・netconvert 生成・randomTrips/duarouter での需要生成・既存ファイルの編集のいずれでも使う。SUMO の壊れ方はエラーも警告も出ないため、(1)機械チェッカーで担保できる部分＝回して確認、(2)チェッカーに含めきれない部分＝自分/生成ツール設計で守る、の2層を1か所で確認する。SUMO設定・sumocfg・net.xml・rou.xml・vType・netconvert・randomTrips・duarouter・背景交通・交通需要・feature:F9 に言及したら必ず使用する。
---

# sumo-authoring — 無言で壊れた SUMO 入力を作らないための2層保証

## この Skill の目的

SUMO の入力を**どんな手段で作っても**（手書き / netconvert で xodr から生成 /
randomTrips+duarouter で需要生成 / 既存の編集）、
**esmini と連成したときに無言で壊れているものを世に出さない**ための確認ハブ。

**SUMO 側の壊れ方は、ほぼすべてエラーも警告も出ない。**
車が路外を走る・ランプが分断される・全車が 0 km/h で出現する・SUMO 車が 1 台も出ない、
のいずれも `Success` と exit 0 で返ってくる。目視で気づける保証はないので、
**§A を回すことが唯一の担保**になる。

対象機能は `feature:F9`（SUMO 背景交通・実験機能）。
設計と実測値は [`GT_esmini/docs/features/sumo_background_traffic.md`](../../../GT_esmini/docs/features/sumo_background_traffic.md)。

---

## §0. 前提の確認（着手前に必ず）

### 0-1. SUMO ツールチェーンはこのリポジトリに入っていない

`netconvert` / `randomTrips.py` / `duarouter` / `sumolib` は**未インストール**（2026-08-17 時点）。
入れる先は `resources/scenario_authoring/requirements-authoring.txt` と同じ扱い＝
**オーサリング時のみの依存で、Python 開発凍結（CLAUDE.md §4）は適用されない**。

```powershell
DriverScript/.venv/Scripts/python.exe -m pip install eclipse-sumo sumolib traci
```

入れたらピンを requirements ファイルに記録すること（バージョン差が交通挙動を変えるため、
「入れた版が分からない」状態は再現性を殺す）。

### 0-2. esmini 内蔵 SUMO は 1.6.0。生成に使う SUMO とは別物

`externals/sumo/v10/include/config.h` の `VERSION_STRING "1.6.0"`（2020年）が
esmini に静的リンクされている実行系。pip で入る現行版は 1.2x 系で**5年差**がある。

- **生成（netconvert / duarouter）に使うのは現行版、実行するのは 1.6.0。**
- この差が §A4（vClass 語彙）と §B2 の一部を生んでいる。
- net format version も同梱 1.9 に対し現行生成は 1.20。

---

## §A. 機械チェッカーで担保できる部分 → 必ず回す

> **注意: A1〜A4 の実行スクリプトはこのリポジトリにまだ無い**（`feature:F9` 未実装）。
> ここに書いてあるのは「何を検査しなければならないか」の仕様。
> 実装するときは `scripts/` に置き、`/gates` のラダーとは別建て（実験機能なので常設ゲート非対象）。
> **検査を書いたら、意図的に壊したデータで発火することを実証してから完了とする**
> （通るデータで通ることだけ見た検査は、無いより悪い）。

| # | 検査 | 何を捕まえるか | 現状 |
|---|---|---|---|
| A1 | **XSD 検証** — `$SUMO_HOME/data/xsd/` の 54 スキーマで生成物を検証 | スキーマ宣言の誤りと構造違反。esmini 同梱の `multi_intersections.sumocfg` ですら `duarouterConfiguration.xsd` を宣言している（正しくは `sumoConfiguration.xsd`）ので、公式サンプルを手本にしても防げない | 未実装 |
| A2 | **連結性** — `sumolib` で到達可能エッジ数を数え、全エッジと一致するか | ランプの分断。`direct junction` 平坦化で合流のレーン対応が失われると到達 14/44 のような net ができるが、netconvert はエラーを出さない | 未実装 |
| A3 | **レーン中心の一致** — `odrplot` の車線境界から走行車線中心を復元し、SUMO レーン中心との距離を測る（許容 1.5 m 程度） | `--lefthand` 由来の路外走行。ズレは常に `2 × |レーン中心の t|` になる | 未実装（`odrplot.exe` はビルド済み: `build/EnvironmentSimulator/Applications/odrplot/Release/odrplot.exe`） |
| A4 | **vClass 語彙** — `allow`/`disallow` に 1.6.0 が知らない値が無いか | `container` / `cable_car` / `subway` / `aircraft` / `wheelchair` / `scooter` / `drone` を現行 netconvert が書く。1.6.0 は `Unknown vehicle class` で **exit 255** | 未実装 |
| A5 | **実行スモーク** — esmini headless で exit 0 と SUMO 車の出現数 | 上記をすり抜けた総合破綻 | **今すぐ回せる**（下記） |

### A5 の回し方

```powershell
build/EnvironmentSimulator/Applications/esmini/Release/esmini.exe `
    --osc <scenario.xosc> --headless --fixed_timestep 0.05 --disable_stdout `
    --logfile_path <log>
# exit 0 を確認したうえで、ログの "Add vehicle .* to scenario" の件数を数える
```

**exit 0 だけでは合格にしない。** SUMO 車が 0 台でも exit 0 で完走する経路が実在する。
必ず出現数を数え、期待値と突き合わせること。

---

## §B. チェッカーに含めきれない部分 → 自分/生成ツール設計で守る

### B1. 需要の設計（機械検査が効かない・最頻の「見た目の破綻」）

| 症状 | 原因 | 対処 |
|---|---|---|
| 車が画面の途中で湧く | `randomTrips.py` は既定で出発地を全域から一様に選ぶ | **需要を2段構え**にする。初期配置（`--fringe-factor 1` / `departPos="random_free"` / 生成後に `depart` を全て 0）と走行中の流入（`--fringe-factor max` / `-b 1`）を別ファイルにし、`.sumocfg` の `route-files` にカンマ区切りで両方指定 |
| 初期配置なのに走行中に湧く | エッジ先頭固定だと入りきらない車が繰り越される | 初期配置には **`departPos="random_free"` が必須** |
| 全車が 0 km/h で出現 | SUMO の `departSpeed` 既定は **0**（静止発進）。高速道路でも止まって出る | `departSpeed="max" departLane="best"` を **randomTrips と duarouter の両方**に指定 |
| 制限速度を超える車がいる | `speedFactor`（平均 1.0・標準偏差 0.1） | 厳密に収めるなら vType に `speedFactor="1.0"` |

### B2. 決定論性（用途で分ける — ここを間違えると回帰が作れない）

SUMO は既定で乱数を使う。**VD 検証の背景交通として使うなら、シードを固定しないと
ベースラインを凍結できない**（`feature:F9` → `gate:regression-gate` の conflicts-with 辺）。

- `.sumocfg` に `<seed value="N"/>`
- 需要生成側（`randomTrips.py` / `duarouter`）にもシードがある。**生成物を commit するなら不要**
- **凍結前に 3 回連続実行して完全一致を確認する。** 確認できるまでベースラインを作らない
- 手動運転の周囲交通（用途 a）ではシード固定は不要。混ぜないこと

### B3. esmini 連成の契約（xosc 側）

- SUMO トラフィックは **1 個の `ScenarioObject`** として宣言する。
  このオブジェクト自体は**エンティティにならない**（テンプレート扱いで、`CatalogReference` は
  3D モデルのフォールバックとしてのみ使われる）
- `.sumocfg` は Property の value ではなく **`<File filepath>`** で渡す
- 相対パスは **`.xosc` からの相対**
- `<step-length>` は esmini の `--fixed_timestep` と揃える
- `<time-to-teleport value="-1"/>` — 連成では瞬間移動を禁止する
- `<lateral-resolution value="0.5"/>` — sublane モデル。車線変更が滑らかになる
- **どちらの SUMO コントローラを指しているか、書く前に確認する。**
  `esminiController` の値が
  - `SumoController` → **upstream 実装**。方位が度/ラジアン取り違えで渡り、基準点も
    変換されない（`sumo_background_traffic.md` §2-2 / §2-3 で実測）。既存 4 本
    （`sumo-test.xosc` / `sumo-test_acc.xosc` / `sumo_react_test.xosc` / `cut-in_sumo.xosc`）はこちら
  - `GTSumoTrafficController` → **GT 版**（`feature:F9`）。新規に背景交通を書くならこちら

  **取り違えてもエラーは出ない。**動いてしまい、方位のずれた背景交通が静かに出るだけ

### B4. xodr → net.xml の無言破壊

netconvert の OpenDRIVE 取り込みは**公式サポート基準が 1.4** で、1.7/1.8 の機能は落ちる。
症状と対処は [`references/netconvert_traps.md`](references/netconvert_traps.md) に分けてある。
**xodr から net.xml を作るときは必ずそちらを開くこと。**

---

## §C. 伝聞を持ち込まない（実測で否定された話がある）

SUMO×esmini 連成には出所の異なる調査記録が流通しているが、
**このリポジトリのビルドで測り直すと結論が変わるものがある**。

| 流通している話 | このリポジトリでの実測 |
|---|---|
| `.xosc` の `revMinor="0"` だと SUMO 車が 1 台も出ない | **再現しない**。`cut-in_sumo.xosc`（`revMinor="0"`）で 8 台出る |
| `setSpeed()` は `moveToXY()` の遠隔制御車に効かない | **効く**。頭打ちの正体は「車線制限速度 × speedFactor」で刈られていたこと。車線側の上限を上げると誤差が消える（平均 2.592 → 0.046 m/s） |

測定手順と数値は `sumo_background_traffic.md` §2。
**新しい主張を足すときは、このビルドで測ってから書くこと。**

---

## 使い方（要約）

1. §0 で前提（ツールチェーン・バージョン差）を確認する
2. xodr から net.xml を作るなら `references/netconvert_traps.md` を開く
3. 需要と決定論性を §B1 / §B2 で設計する
4. §A を回す。A1〜A4 が未実装なら、**この作業で必要になった検査から実装する**
5. A5 のスモークは exit 0 と**出現数の両方**で判定する

## 参照

- 設計・実測: [`GT_esmini/docs/features/sumo_background_traffic.md`](../../../GT_esmini/docs/features/sumo_background_traffic.md)
- netconvert の罠: [`references/netconvert_traps.md`](references/netconvert_traps.md)
- 知識グラフ: `feature:F9`（`/kg` で照会）
