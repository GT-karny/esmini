# netconvert の OpenDRIVE 取り込み — 無言破壊の一覧と対処

`.xodr` から `.net.xml` を作るときに開く。

## 出所と検証状況（先に読む）

この文書の内容は、**外部プロジェクトの調査記録に由来する**
（macOS 15.2 / SUMO 1.27.1 / esmini v3.6.0 / OpenDRIVE 1.8・LHT の実データで構築・実測）。
参照実装は `temp/SUMO-repo-2026-08-17.zip`（Git 管理外）。

**このリポジトリの環境（Windows / SUMO 1.6.0 内蔵 / RHT 中心の道路）では未検証。**
使うときは症状の再現を確認してから対処を適用すること。
検証済みの事実は `GT_esmini/docs/features/sumo_background_traffic.md` §2 にあり、
そちらは**このビルドで実測した値**なので扱いが違う。

公式のサポート基準は **OpenDRIVE 1.4**（[SUMO 公式ドキュメント](https://sumo.dlr.de/docs/Networks/Import/OpenDRIVE.html)）。
1.7 / 1.8 の機能は落ちる。

---

## 症状と対処

**いずれもエラーや警告が出ない。** 気づくには実行して目視するか、SKILL.md §A の検査を回すしかない。

| # | 症状 | 原因 | 対処 |
|---|---|---|---|
| T1 | 異常終了 `Attribute 'connectingRoad' is missing` | `<junction type="direct">` 非対応。`NIImporter_OpenDrive.cpp` の属性表に `linkedRoad` が無く、junction の `type` も読んでいない | direct junction を削除し、各道路の `<link>` を road-to-road 参照へ書き換える（前処理） |
| T2 | ランプが本線から分断される | `onRamp` / `offRamp` / `entry` / `exit` を走行レーンと見なさない | レーン型を `driving` に正規化（前処理） |
| T3 | 車が幅 5 m の駐車帯を走る | `type="parking"` を走行車線として取り込む | `parking` → **`none`** に変更。`restricted` では**まだ取り込まれる**ので `none` が必要。幅は残すので他レーンの t オフセットは動かない（前処理） |
| T4 | 高速道路が 50 km/h 制限になる | `<road><type><speed>` を読まない。netconvert は `<speed>` を **`<lane>` 直下にある場合しか読まない** | `<road><type><speed>` を各走行レーンの `<speed>` へ複製（前処理） |
| T5 | 全車逆走 / レーンが最大 3.5 m 路外へ飛ぶ | `rule="LHT"` 非対応。`--lefthand` を付けるとレーン位置が壊れる | **netconvert 本体へのパッチ**（下記 §LHT） |
| T6 | 平坦化してもランプが繋がらない | 道路レベルの `<link>` は successor を 1 本しか持てず、合流・分流のレーン対応が失われる | 元の `.xodr` の `<laneLink>` から netconvert の connection file（`-x`）を生成 |
| T7 | ランプ端が宙に浮く | 分離帯のある本線ではランプ端と本線ノードが離れる（実測 21.19 m）。`--junctions.join` は既定 10 m、かつデッドエンドを対象外とするため届かない | 次数 1 のノードとしきい値内の最寄りノードの組を `<join nodes="..."/>` として出力（node file） |

esmini 側で追加で踏むもの:

| # | 症状 | 原因 | 対処 |
|---|---|---|---|
| T8 | esmini が exit 255 `Unknown vehicle class` | 内蔵 SUMO 1.6.0 が新しい vClass を知らない | 未知 vClass を除去（SKILL.md §A4） |

---

## パイプラインの形

前処理と後処理で netconvert を **3 回**通す。

```
<road>.xodr
  │
  ├─1─ 前処理           T1 平坦化 / T2 ランプlane型 / T3 駐車帯 / T4 制限速度
  │
  ├─2─ netconvert       --opendrive.position-ids --opendrive.signal-groups
  │                     （LHT なら パッチ版 + --lefthand）
  │
  ├─3─ ノード結合       T7。<join> を出して netconvert 2nd pass（-n）
  │
  ├─4─ 接続復元         T6。laneLink から connection file を出して 3rd pass（-x）
  │
  └─5─ vClass 除去      T8
```

**3 と 4 が別パスなのは、ノード結合（`-n`）と接続指定（`-x`）を同時に渡せないため。**

### netconvert のフラグ

- `--opendrive.position-ids` は**必須級**。エッジ ID が OpenDRIVE road ID 由来（`0.0.00` 等）になる。
  esmini 同梱の net.xml もこの手順で作られている（`e6mini.net.xml` のエッジ ID が完全一致することで確認済み）
- `--opendrive.signal-groups` で xodr のコントローラ情報から信号プログラムを生成できる
- `netOffset` は **esmini が正しく補償するので気にしなくてよい**（実測 Δ=0.000 m）。
  `--offset.disable-normalization` は不要
- `proj_create: vgridshift` 警告は無害（高さ方向の測地系変換が無視されるだけ）

### T6 の実装で外さない点

- **どちらが from かを意味論で決めない。** laneLink をレーン同士の隣接関係として扱い、
  SUMO 側のノード接続（`eA.to == eB.from`）で向きを決める。符号規約の解釈ミスに強くなる
- netconvert が laneSection をさらに分割する（実測 403.24 → 434.10 等）ため、
  エッジは名前から計算せず**共有ノードを持つ組を探索**して特定する

### レーン規約（パッチ後・実測）

```
edge "-R.<s>" = road R の右(-t)レーン群、-s 方向
edge  "R.<s>" = road R の左(+t)レーン群、+s 方向
lane index 0  = 基準線から最も外側
```

---

## §LHT 左側通行（T5 の詳細）

**このリポジトリでは当面問題にならない。** 道路は RHT 23 本 / LHT 1 本（`e6mini-lht.xodr`）で、
生成カタログ 10 本は全 RHT。LHT 道路に背景交通を入れる必要が出たときだけ読む。

### 何が起きるか

| | 走行方向 | レーン配置 |
|---|---|---|
| `--lefthand` なし | -t が +s（**右側通行**になる） | 正しい |
| `--lefthand` あり | +t が +s | **最大 3.5 m 路外へ** |
| パッチ後 `--lefthand` | +t が +s / -t が -s | 正しい |

ズレは常に `2 × |レーン中心の t|`（3.5 m 幅なら 3.5 m）。
**左右対称な本線では反対側にも実在の車線があるため露見せず、片側 1 車線のランプで露見する。**

### 原因

LHT 実装は「Y 方向に鏡像化 → 右側通行として構築 → 鏡像を戻す」方式
（`NBNetBuilder::compute()` の冒頭と末尾の `mirrorX()`）。
その**間**で `computeLaneShapes()` がレーン形状を作り直すが、
`NBEdge::computeLaneShape()` の `move2side(offset)` は **offset が常に正**で、
鏡像フレームであることを知らない。結果レーンが幾何の反対側へ展開される。

### 対処

`NIImporter_OpenDrive.cpp` の 4 箇所を直す。インポータが「右レーン群 = +s 方向」を
決め打ちしているのを、LHT では**向きだけ入れ替える**。鏡像化による反対側展開が相殺され、
`move2side` も `laneMap` も変更不要になる。

```cpp
const bool lefthand = oc.getBool("lefthand");
currRight = new NBEdge("-" + id, lefthand ? sTo : sFrom, lefthand ? sFrom : sTo, ...,
                       lefthand ? rightGeom.reverse() : rightGeom, ...);
currLeft  = new NBEdge(id, lefthand ? sFrom : sTo, lefthand ? sTo : sFrom, ...,
                       lefthand ? leftGeom : leftGeom.reverse(), ...);
```

laneSection 間の接続方向も入れ替える（右は curr→prev、左は prev→curr）。

パッチ本体は参照実装の `scripts/netconvert-lefthand-opendrive.patch`。
**適用には SUMO をソースからビルドする必要がある**（Windows では未検証）。

### 上流に出すなら残っている作業

レーン符号から向きを推定する箇所が他にもある（`fromLast`/`toLast` 判定、
信号の向き `signal.maxLane < 0`、`buildConnectionsToOuter`、`setEdgeLinks`）。
SUMO テストスイートも未実行。

関連 issue（いずれも未解決）:
[#7692](https://github.com/eclipse-sumo/sumo/issues/7692)（2020年〜）/
[#16940](https://github.com/eclipse-sumo/sumo/issues/16940)（2025年）/
[#16283](https://github.com/eclipse-sumo/sumo/issues/16283)（OpenDRIVE 1.8 対応）

---

## その他の実測メモ（外部調査由来）

- **netconvert のバージョン差で交通挙動が変わる。** 同梱 net.xml（0.32.0 製）と
  自作 net.xml（1.27.1 製）で同一シナリオを 40 秒走らせると、SUMO 車の位置が
  1.8〜51 m ずれる。**既存の同梱 net.xml と自作 net.xml は混ぜられない**
- **曲線路では s/t の直線近似が破綻する。** 「ランプの向きが逆に見える」といった観察から
  通行方向を推測すると誤る。基準線に対する t の実測で判定すること
- `<stop>` は `edge=` ではなく `lane=` 指定でないと 1.6.0 が
  `A stop must be placed on a ... lane` を出す（**これもエラーだが exit 0 で続行する**）
