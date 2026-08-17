## §0. 前提の確認（着手前に必ず）

### 0-1. 配布版でできること・できないこと

| やりたいこと | 配布版で可能か |
|---|---|
| `.rou.xml` の需要（どのエッジに・いつ・何台・初速）を書く / 直す | **可能**（テキストエディタだけ） |
| `.sumocfg` を書く / 直す（route-files・step-length・seed） | **可能** |
| `vType`（車種・加減速・`speedFactor`）を書く / 直す | **可能** |
| 既存の net で交通を流して確認する | **可能**（`bin\GT_Sim.exe`） |
| `.xodr` から `.net.xml` を生成する | **不可**。netconvert を同梱していない |
| §A1〜A4 の機械チェッカーを回す | **不可**。実装が存在せず、`sumolib` / `odrplot` も同梱していない |

`netconvert` / `randomTrips.py` / `duarouter` / `sumolib` は同梱されていない。
配布版に入っている Python は組み込み版（`bin/python312.*`）で **pip が無い**ため、
その場で入れることもできない。

**net.xml が新しく要るときは、開発環境（リポジトリ）側で生成して
`resources/sumo_inputs/` に置いたものを配布版へ持ち込む。**
同梱済みの net は `resources/sumo_inputs/` にある
（`e6mini` / `fabriksgatan` / `multi_intersections` / `straight`）。
既存 net の上に需要を書くぶんには、ここまでの制約は掛からない。

### 0-2. 同梱 SUMO は 1.6.0

`bin\GT_Sim.exe` に静的リンクされている SUMO は **1.6.0（2020年）**。
pip で入る現行版は 1.2x 系で **5年差**がある。

- 開発環境で生成（netconvert / duarouter）に現行版を使った場合、**実行するのはこの 1.6.0**。
- この差が §A4（vClass 語彙）と §B2 の一部を生んでいる。
- net format version も同梱 1.9 に対し現行生成は 1.20。
- 持ち込んだ net.xml が 1.6.0 の知らない vClass を含んでいると、
  警告ではなく **exit 255（`Unknown vehicle class`）** で落ちる。
