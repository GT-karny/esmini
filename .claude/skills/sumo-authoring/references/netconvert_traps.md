# netconvert の OpenDRIVE 取り込み — 無言破壊の一覧と対処

`.xodr` から `.net.xml` を作るときに開く。

## 出所と検証状況（先に読む）

この文書の内容は、**外部プロジェクトの調査記録に由来する**
（macOS 15.2 / SUMO 1.27.1 / esmini v3.6.0 / OpenDRIVE 1.8・LHT の実データで構築・実測）。
参照実装は `temp/SUMO-repo-2026-08-17.zip`（Git 管理外）。

**2026-08-17 更新**: 同 zip の `docs/phase1-network-fix.md` を読み直し、T7 の対処を差し替えた。
初版に書いてあった「最寄りノードへ `<join>`」は、**その後の調査で逆走接続の原因と特定され、
参照実装側で撤回されている**手である（→ §T7 の罠）。
併せて T9（合流の zipper 化）と §未文書挙動 を追加した。
初版が採っていた手は「途中経過を確定情報として写した」もので、
外部記録を引くときは**どの時点のスナップショットか**を確かめること。

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
| T7 | ランプ端が宙に浮く | direct junction の `overlapZone` の分だけ、合流点が本線ノードより手前に来る。netconvert はランプ端を**正しい位置に置く**が、本線側のその s にはノードが無い（実測: 最寄りの本線ノードまで 21.19 m）。`--junctions.join` は既定 10 m、かつデッドエンドを対象外とするため届かない | **本線をその位置で split する**。最寄りノードへの `<join>` は**やってはいけない**（→ §T7 の罠） |
| T9 | 流入ランプの車が本線手前で完全停止する | netconvert 既定の junction 型 `priority` は交差点のモデルで、ランプ側に `state="m"` を付けて本線の全通行に譲らせる | 合流ノードを **`zipper`（交互合流）** に変える。実測: ランプ上の停止（<0.1 m/s）299 サンプル → **0**。幾何は変わらないので `.xodr` との整合は保たれる |

esmini 側で追加で踏むもの:

| # | 症状 | 原因 | 対処 |
|---|---|---|---|
| T8 | esmini が exit 255 `Unknown vehicle class` | 内蔵 SUMO 1.6.0 が新しい vClass を知らない | 未知 vClass を除去（SKILL.md §A4） |

---

## §T7 の罠 — 「最寄りノードへ join」は無言で 4 本とも壊す

**T7 の対処を「次数 1 のノードを最寄りノードへ `<join>`」でやってはいけない。**
これは実際に採られて、実際に逆走接続を作った手である（参照実装の phase1 記録）。

- ランプ端を最寄り**ノード**へ寄せると、ランプ端が **21.19 m 上流へ引っ張られる**
  （本線に沿った合流点のずれは 16.00 m: 639.24 → 623.24）
- ところが同じランプ端から本線エッジの**折れ線への垂線距離はわずか 1.75 m**。
  見るべきは「最寄りのノード」ではなく **「最寄りのエッジ上の点」**だった
- 結果、接続生成は壊れたトポロジを忠実に辿り、内部レーンが **12 m 逆走**する
  接続を作る。実測で**進入・退出とも 178.8° の反転**
- `overlapZone` が共通なら **全ランプが同じ量ずれる**。実測 4 本すべてが
  `d_node 21.19 m / d_edge 1.75 m` で完全に同一だった。
  **物理的に反転して見えたのは 1 本だけ**で、残り 3 本は junction 形状計算が
  たまたま収束しただけの誤接続。1 本直して満足すると 3 本残る

### 正しい形: `<split id="既存ノードid">` で join 自体を消す

PlainXML の `<split>` 仕様は **"IDs of existing nodes may also be used"** と明記している。
**ランプ端ノードの id をそのまま split の id に再利用する**:

```xml
<edge id="-ic-main-w.434.10">
  <split pos="16.00" id="ic-ramp-entry-outer.ic-main-w"/>
</edge>
```

分割ノード＝ランプ端ノードになるので **join が不要**になり、

- 結合誤差が**原理的に 0.00 m**（「21.19 m のずれ」を「数 m のずれ」に縮めたのではない）
- join クラスタが 5 → 1、それに伴い §未文書挙動 の INVALID_DOUBLE 汚染も 5 → 1
- `-e`（split）と `-n`（join）を **1 パス**で通せる（→ §未文書挙動）

判定条件（参照実装の実測値。他のネットでは再調整が要る）:

- 垂足がエッジ**内部**（両端から 2 m 以上）
- **進行方向の差が 60° 以内** — 対向車道（約 10.5 m 先）を誤って掴まないための必須ゲート
- `d_edge × 2.0 < d_node` かつ `d_edge <= しきい値`

同一エッジへの複数分割は 1 つの `<edge>` に複数 `<split>` としてまとめる。

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
  ├─3─ ランプ端の接地   T7。<split id="既存ノードid"> を出して 2nd pass。
  │                     join が要る箇所（料金所など node-node が正当な所）が
  │                     あれば -e と -n を同じパスで渡す（→ §未文書挙動）
  │
  ├─4─ 接続復元         T6。laneLink から connection file を出して 3rd pass（-x）
  │
  ├─4b─ 合流の zipper化 T9
  │
  ├─5─ vClass 除去      T8
  │
  └─6─ 幾何の検証       逆走接続・折り返し形状・孤立エッジ。
                        **ここで落としてから経路生成へ進む**
```

**3 と 4 が別パスなのは、接続指定（`-x`）を同じパスで渡せないため。**
3 の中で `-e`（split）と `-n`（join）は同居できる——ただし §未文書挙動 の順序制約がある。

**6 を経路生成の手前に置くこと。** 壊れたネットの上で作った経路は
「エッジ間に接続が無い」形で後から落ちるか、落ちずに走って無言で変な挙動になる。
参照実装には `check_net.py`（逆走接続 / 折り返し形状 / 孤立エッジの 3 検査、
非ゼロ終了する CLI）がある。SKILL.md §A2 と射程が重なるので、
A2 を実装するときはこれを出発点にできる。

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

## §未文書挙動 — 調べても出てこない netconvert の実測知見

### `-n`（join）と `-e`（split）の同時指定は、id 検証の順序が決まっている

```
netconvert -s p1.net.xml -n joins.nod.xml -e splits.edg.xml -o out.net.xml
→ Error: Unknown junction 'split_...' in join-cluster.
```

**join ファイルの id 検証は、`<split>` によるノード生成より先に走る。**
`--opendrive-files` から直接 split + join を同時適用しても同じ。
つまり **「split で作った新ノード id」を join ファイルから参照することはできない**。
§T7 の「既存ノード id を再利用する」形が要るのはこのため。

### 2 パス方式（split → 再読込 → join）は y 座標を汚染する

「`-e` で split → 出力を `-s` で再読込 → `-n` で join」も成立しそうに見えるが、
**join クラスタの代表点 `y` が `-1073741824.00` になる**。
これは `-2^30` ＝ SUMO の `INVALID_DOUBLE_VALUE`。
汚染は `<location>` の境界計算へ伝播し `Network contains very large coordinates` 警告まで出る。

因果の裏付け（実測）: split 前は 0 件、join を行ったパス以降で発生し、
**join クラスタ数と汚染件数が一致する**（5 クラスタ → 5 件、1 クラスタ → 1 件、
残った 1 件はその唯一のクラスタそのもの）。

汚染されるのは junction の代表点 1 座標だけで、その junction の `shape` も内部レーンも
数値的に正常＝**走行には影響しないが、目視でも検査でも気づけない**。

### 分割エッジの id 規則

前半 = **元 id をそのまま**、後半 = `<origID>.<pos>`（`idAfter` の既定値。
小数点以下は自動整形され `pos=16.00` なら `.16`）。

```
-ic-main-w.434.10       前半（16.00 m）
-ic-main-w.434.10.16    後半（187.64 m）
```

**これが `.rou.xml` を壊す。** ネットを作り直すたびにエッジ id が変わるので、
経路をハードコードした rou ファイルは
`No connection between edge 'A' and edge 'B'` で落ちる。
参照実装では、この理由でハードコード経路の rou を `route-files` から外す判断をしている。
**経路はエッジ id をベタ書きせず、生成時に作り直す前提で持つこと。**

### 中途半端な分割点は `--opendrive.min-width` が作っている

`s=434.10` のような laneSection 境界でもテーパー完了点でもない位置に split が入るのは、
**`--opendrive.min-width`（既定 1.8 m）**のため。
幅 0 → 3.5 m へ線形増加する lane が 1.8 m を跨ぐ点で切られる:

```
403.24 + 60 × 1.8 / 3.5 = 434.10
```

---

## その他の実測メモ（外部調査由来）

- **netconvert のバージョン差で交通挙動が変わる。** 同梱 net.xml（0.32.0 製）と
  自作 net.xml（1.27.1 製）で同一シナリオを 40 秒走らせると、SUMO 車の位置が
  1.8〜51 m ずれる。**既存の同梱 net.xml と自作 net.xml は混ぜられない**
- **曲線路では s/t の直線近似が破綻する。** 「ランプの向きが逆に見える」といった観察から
  通行方向を推測すると誤る。基準線に対する t の実測で判定すること
- `<stop>` は `edge=` ではなく `lane=` 指定でないと 1.6.0 が
  `A stop must be placed on a ... lane` を出す（**これもエラーだが exit 0 で続行する**）
