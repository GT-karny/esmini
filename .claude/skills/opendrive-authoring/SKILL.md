---
name: opendrive-authoring
description: OpenDRIVE(.xodr)を「どんな手段で作っても規格外のものを作らない」ための規格適合保証スキル。手書き・XODR生成ツール/エクスポータの自作・GT_RoadGen等での大量生成・既存xodrの編集のいずれでも使う。作り方の工程指南ではなく、(1)機械チェッカーで担保できる部分＝回して確認、(2)チェッカーに含めきれない部分＝自分/ツール設計で守る、の2層を1か所で確認するためのもの。xodr作成・生成・オーサリングツール開発・ASAM OpenDRIVE規格適合・LHT/RHTや交差点の正しさに言及したら必ず使用する。
---

# opendrive-authoring — 規格外の xodr を作らないための2層保証

## この Skill の目的

OpenDRIVE を**どんな手段で作っても**（手書き / 自作の生成ツール・エクスポータ / GT_RoadGen 等の大量生成 / 既存の編集）、
**ASAM OpenDRIVE 規格に反したものを世に出さない**ための確認ハブ。作り方の手順書ではない。

規格適合は**2層**で担保する。この Skill は両方を1か所に集約する:

| 層 | 誰が担保するか | この Skill での扱い |
| :-- | :-- | :-- |
| **A. 機械チェッカーで担保できる** | XSD / qc-opendrive / GT gap checker が自動判定 | **§A: 必ずチェッカーを回す**（手書き出力にも、ツール生成物にも） |
| **B. チェッカーに含めきれない** | 機械では○×を定義できない → **人 / ツール設計が守るしかない** | **§B: 該当項目を知って、作る前に設計へ組み込む** |

**鉄則**: Aだけ・Bだけでは不十分。**A（チェッカー通過）＋B（チェッカーが原理的に見ない規約を自分で保証）＝規格適合**。
ツールを作るなら、Aは**生成物への検査ゲート（CI）**に、Bは**ツールが常に守る設計不変条件**にする。

---

## §A. 機械チェッカーで担保できる部分 → 必ず回す

「作った／生成した」ものは、手段を問わず以下に通す。詳細・各層が何を見るかは `references/checker_coverage.md`。

1. **XSD（構造の最低ライン）** — 要素/属性の型・出現数。schema19（v1.9）。
   `scripts/run_odr_conformance.py --profile quick`（schema + RM プローブ層）。
2. **GT gap checker（XSDが見逃す構造/参照/規約破綻・197ルール実装済み）** —
   ```
   DriverScript/.venv/Scripts/python.exe GT_esmini/docs/knowledge/opendrive_gap_checkers/gap_rule_check_master.py
   ```
   → `opendrive_gap_rule_report.md` の「GT自作の違反」に自分の出力が無いか確認。flag=review(助言)。
3. **RoadManager ロード（意味の最終裏取り）** — esmini/GT_Sim に headless ロードし経路が通るか。

**ツール自作時**: 生成パイプラインの最後に 1〜2 を**自動ゲート**として挿す（1ファイルだけなら master runner のループを真似て `run_checks(...)` を呼ぶ小ドライバを書く）。1件でも違反が出たら生成物を出さない。

**チェッカーが「実装済み＝機械で担保できる」規約の要点**（作る前に知っておくと手戻りゼロ。全カタログは `references/checker_coverage.md`）:
- レーンid: 中心=0 / 正=左・負=右（+s方向を見て）/ 中心から連番・欠番なし。
- LHT/RHT: `<road @rule>`（省略時RHT）。signal の `@orientation`(+/-) と `<validity>` 車線id符号を整合（RHT: `+`→負車線 / `-`→正車線、LHTは逆。driving車線が対象）。
- 幾何連続: `<geometry>` は s昇順・隙間なく連続、Σlength=road@length。
- リンク相互性: road A↔B の predecessor/successor は双方向。connecting road(`@junction!=-1`)は junction 経由（直接相互リンクしない）。
- id 一意・参照解決: 参照する id は必ず定義済み。

---

## §B. チェッカーに含めきれない部分 → 自分/ツール設計で守る

ここは機械が原理的に○×できない領域。**A を全部通しても規格外になりうる**のはここ。全件と「どう自衛するか」は `references/beyond_checker.md`。3系統ある:

### B1. 設計判断（機械検査が永久に不能）— 最重要
道路網全体＋現実の意味を人が判断しないと決まらない。gap checker は沈黙する。
- **交差点(junction)は接続が曖昧なときだけ使う**（`when_to_use`/`not_only_two`/`is_junction_needed`）。predecessor/successor 候補が2つ以上あるとき（合流・分岐・実交差点）だけ junction。一意なら直接リンク。
  - 注意: 「A→B は1対1」でも、**B の手前に別の道路も合流する（Bの入次数≥2）なら junction 必要**。片側だけ見て判断しない。
- **レーン接続は曖昧なら junction 経由**（`lane.link.use_junctions`/`no_link`）。一意なら lane 直リンク可。
- → ツール自作時: これらは**生成ロジックの分岐条件**（入次数/出次数を数えて junction 化を決める）に組み込む。

### B2. 幾何が絡む規約（GT gap checker 未実装＝別手段で担保）
XML属性だけでは判定できず幾何計算が要る（曲率・座標・多角形重なり・境界閉合・標高グリッド 等、47件）。
- 例: 参照線の折れ(kink)なし、道路同士/交差点の重なりなし、junction境界の閉合、connecting road の滑らかな接続。
- **担保手段**: (a) 構築時に不変条件として保証（連続geometryの接線を合わせる等）、(b) 幾何を評価する外部ツール（フル qc-opendrive 等）や RoadManager ロードで裏取り。

### B3. 読み取りソフト向けの規定（作者は無関係だが誤解しないため）
xodr の中身ではなくシミュレータの動作の指示（標高無視・直線補間・信号の優先適用 等）。**作者/ツールが xodr で"守る"対象ではない**。混同して不要な属性を足さない。

---

## 使い方（要約）

1. **作る前**: §B1 の設計判断を固める（特に junction の要否）。ツールなら生成ロジックへ。
2. **作る/生成する**: §A の実装済み規約（レーンid/LHT/幾何連続/リンク）に沿う。
3. **作った後**: §A のチェッカーを必ず回す（ツールなら自動ゲート）。§B2 は幾何裏取り。
4. **A通過＋B保証**で初めて「規格に基づいたもの」。

## 参照ファイル

- `references/checker_coverage.md` — §A の詳細。機械で担保できる実装済み規約カタログ（レーン/LHT/幾何/リンク/junction文脈）＋各チェッカーの回し方＋ツールのゲート化。
- `references/beyond_checker.md` — §B の詳細。チェッカーに含めきれない内容の全件（B1設計判断15 / B2幾何47 / B3ソフト向け）と、各項目を人/ツールでどう守るか。
- 権威台帳: `GT_esmini/docs/knowledge/opendrive_rule_ledger.yaml`（275ルール×status。implemented=機械で担保可 / gap_*=不可、理由付き。「この規約はAかBか」を引ける）。
