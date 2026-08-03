# signal分類カタログの国別切替

**読者**：signal分類カタログの国別切替を初めて触る開発者。VirtualDriver の内部には詳しくない前提とする。
**対象バージョン**：GT_Sim v0.14.2。

VirtualDriver の `TrafficLightAware`（信号）と `StopYieldSignAware`（STOP/YIELD標識）は、どちらも「このsignalは何の種類か」を判定してから停止目標を決める。
この判定に使う辞書が、xodrのsignalが持つ`country`属性1つで国ごとに自動的に切り替わる。
この文書は、その切り替えを支える2つのカタログがなぜ別系統なのか、新しい国を1つ追加するには何をどこに足せばよいかをまとめる。
判断の経緯と却下案は [`design/stop_line_stop_target.md`](../design/stop_line_stop_target.md) にある。

## 1. カタログが2系統に分かれている理由

VirtualDriver があるsignalをSTOP標識、YIELD標識、停止線のどれとして扱うかは、xodrのsignalが持つ`type`（+`subtype`）という番号だけでは決まらない。
同じ番号でも意味は国の標識体系によって変わるため、番号を実際の分類へ変換する辞書が要る。

この辞書は、多言語対応の翻訳辞書に近い構造を持つ。
どの言語の見出し語を追加しても、対応づけられる**訳語の一覧**そのものは辞書側の語彙に閉じている。
訳語に無い概念は、見出し語をいくら追加しても訳しようがない。

実際、esmini upstreamの`LoadSignalsByCountry`（`RoadManager.cpp`）は`resources/traffic_signals/<country>_traffic_signals.txt`を国ごとに読み、「type番号 → `Signal::OSIType`の列挙子名」という表を組み立てる。
この訳語の一覧にあたる`Signal::OSIType`（`RoadManager.hpp`）は`TYPE_STOP`や`TYPE_GIVE_WAY`のような標識そのものの分類を持つが、「路面標示としての停止線」という区分を持たない。
`de_traffic_signals.txt`を開くと`206=TYPE_STOP`のように、値の欄は必ずこの列挙子名になっている。
停止線に対応する列挙子はどこにも無い。

したがって、日本の標識番号を集めた`jp_traffic_signals.txt`を仮に用意しても、書けるのは「番号 → 既存の訳語」の対応だけであり、訳語の一覧そのものを増やす手段にはならない。
これは日本のデータが足りないという問題ではなく、upstreamの型システムに停止線の置き場所がそもそも無いという問題である。
`EnvironmentSimulator`はR1（Clean Core）により無改変が前提でもあり、`Signal::OSIType`へ列挙子を1つ足すという直し方も採れない。

GTはこのため、upstreamの辞書とは別に、停止線だけを分類する小さな辞書をもう1つ持つ（`StopLineSignalCatalog.hpp`の`ClassifyStopLineType` / `LoadStopLineCatalog`）。
`resources/traffic_signals/stop_line/<country>_stop_line.txt`を国ごとに読み、訳語はGT自身が定義するので`stop_line`という1語の語彙で足りる。
upstreamが組み立てる表には一切書き込まない。
2つの辞書はキーの形（`country + type[.subtype]`）こそ同じ流儀を踏襲しているが、中身も置き場所も完全に独立している。

## 2. 国はどこで選ばれるか

国の選択にGUIは無い。
signal自身が持つ`country`属性を、`ScanSignalsAhead`が読んだそのつど、標識分類にも停止線分類にも使う。
xodrの該当signalに`country="jp"`と書けば、そのsignalだけが`jp`のカタログで解決される。
シナリオ実行前に「今回は日本仕様」を選び直す操作は無く、増やす計画も無い。
`country`属性はsignalごとに付くので、1つの道路網の中に複数国のsignalが混在していても構わない。

Webパネルにある`tl_stop_line_aware_enabled` / `sign_stop_line_aware_enabled`は、これとは役割が違う。
こちらは「対応する停止線が見つかったときにそれを使うかどうか」のキルスイッチであり、OFFにすると停止線分類が何であっても現行の`head_s - margin` / `sign_s - margin`に固定される。
どの国のカタログを使うかを選ぶ設定ではない。

`country`属性の値はISO 3166-1 alpha-2（`de`や`jp`のような小文字2文字）を基本とする。
`OpenDRIVE`は、esminiが`opendrive_traffic_signals.txt`を別枠として用意しているとおり、どの国の公式標識にも対応しない要素向けの特別な国コードである。
XMLロード時に値は小文字化されるため、`Signal::GetCountry()`はすでに小文字を返す。

## 3. 新しい国を1つ追加する

ファイルを1本足すだけでよい。
コードは触らない。
足す場所は標識側と停止線側で別々である。

### 3.1 STOP/YIELD標識側

`resources/traffic_signals/<country>_traffic_signals.txt`を追加する。
`<country>`はISO 3166-1 alpha-2の小文字2文字（例`jp`）。
中身は`type=OSIType名`の1行1エントリで（`subtype`があれば`type.subtype`の形にする）、`country`部分は書かない（ファイル名がすでに国を表しており、読み込み時に内部で連結される）。
`de_traffic_signals.txt`に実在する2行を挙げる。

```
205=TYPE_GIVE_WAY
206=TYPE_STOP
```

値の欄は`Signal::OSIType`（`RoadManager.hpp`）の列挙子名そのものを書く。
存在しない名前や空欄を書くと、そのsignalは`TYPE_UNKNOWN`のまま静かに未分類になる（エラーにはならない）。
読み込みはupstreamの`LoadSignalsByCountry`がそのまま行うので、このファイルを追加するだけで新しい国のSTOP/YIELD判定が動くようになる。

### 3.2 停止線側

`resources/traffic_signals/stop_line/<country>_stop_line.txt`を追加する。
ディレクトリが1段深く、ファイル名の末尾も`_traffic_signals.txt`ではなく`_stop_line.txt`になる。
中身は`type[.subtype]=stop_line`の1行1エントリで、認識される値は文字列`stop_line`だけである（それ以外は`StopLineKind::NONE`、つまり未分類として扱われる）。
実際に存在するファイルは2本だけで、どちらも1行しかない。
`opendrive_stop_line.txt`。

```
294=stop_line
```

単体テスト用のプレースホルダ国を示す`zz_stop_line.txt`。

```
999999=stop_line
```

読み込みはGT独自の`LoadStopLineCatalog`（`StopLineSignalCatalog.cpp`）が行い、upstreamの標識カタログには触れない。
該当国のファイルが無くてもエラーにはならず、その国のsignalは常に「停止線なし」として扱われ、`tl_stop_margin` / `sign_stop_margin`（既定3.0m）を使う既存の計算にフォールバックする。

## 4. signal type="294"の罠

`multi_intersections.xodr`には、`dynamic="no"`で`type="294"`のsignalが多数実在し、name属性の多くが`SgRMHoldingline-1Lane.flt`のように「Holdingline」（停止線）を含む。
これが今日、停止線分類を持つ唯一の実資産パターンであり、`opendrive_stop_line.txt`の`294=stop_line`はこの資産に合わせて用意した初期データである。

ここには見た目上の罠が1つある。
`country="OpenDRIVE"`は、esminiが`opendrive_traffic_signals.txt`を別枠として用意しているとおり、どの国の公式標識にも対応しない要素向けの特別な国コードである。
294という番号自体は、この特別枠の下でASAMが定めたものではない。
294はドイツの道路交通令（StVO）が定める標識番号（Zeichen 294、通称Haltlinie）であり、本来ならドイツの番号のはずのものに汎用国コード`OpenDRIVE`を付けて出力しているのは、この資産を生成したCADツール固有の流儀である。
根拠は単純で、`de_traffic_signals.txt`にも`opendrive_traffic_signals.txt`にも294のエントリが無く、このsignalはどちらのカタログで引いても`TYPE_UNKNOWN`のままである。

つまり`country="OpenDRIVE"`に任意の国の標識番号を載せるという書き方は、ASAM仕様が定めた正規パターンではなく、特定のツールが出力した結果にすぎない。
新規資産、とりわけ日本仕様の道路でこの組み合わせをそのまま踏襲すると、294が国を問わず通用する汎用の停止線番号だという誤解を持ち込むことになる。
新しい国を追加するときは、3節のとおり`country="jp"`のような正規のISO 3166-1 alpha-2コードを使う。

## 5. 日本のカタログはまだ無い

`resources/traffic_signals/`には`cn` / `de` / `opendrive` / `se`の4か国分があり、`jp`は無い。
`resources/traffic_signals/stop_line/`には`opendrive`と、単体テスト用の`zz`の2本しかなく、これも`jp`は無い。

3節で示した**機構**はすでに成立しており、ファイルを1本追加すれば新しい国が動く。
中身の数値がまだ埋まっていないだけである。
日本の法定標識番号そのものの確定はこの機構の設計・実装のスコープに含めておらず、検討過程は [`design/stop_line_stop_target.md`](../design/stop_line_stop_target.md) §9.1にある。

## 6. 動作を確認する

`TrafficLightAware` / `StopYieldSignAware`は、停止線が使われたかどうかを診断KVとして出す。
`gt.traffic_light.stop_line_used`と`gt.stop_yield.stop_line_used`が真になっていれば、その走行では停止線基準の目標に置き換わっている。
窓（`tl_stop_line_window` / `sign_stop_line_window`）の外にある、あるいは対応する停止線分類のsignalが無ければ偽のままで、目標は3節のフォールバックどおりになる。

国別切替の機構そのものは`GT_esmini/test/unit/virtualdriver/test_StopLineSignalCatalog.cpp`の単体テストで固定されている。
実在しない国コード`zz`を使い、`zz_stop_line.txt`を読ませたときと読ませていないときで分類結果が変わることを確認しているので、新しい国を追加したときの振る舞いを予想する参考になる。

## 7. 関連文書

- 判断の経緯と却下案：[`design/stop_line_stop_target.md`](../design/stop_line_stop_target.md)
- 実装：`GT_esmini/include/gt_esmini/control/virtualdriver/policies/StopLineSignalCatalog.hpp`、`GT_esmini/include/gt_esmini/control/virtualdriver/policies/RouteSignalScan.hpp`
