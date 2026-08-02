# VirtualDriver の停止位置を停止線基準にする設計

**状態**: **実装済み**（`b7433c7e` カタログ層追加、`1626fb09` スキャン層拡張、`27e5e703` TrafficLightAware配線、
`b7b5887c` StopYieldSignAware配線、`7477ca88` 国別切替の単体テスト、`0071dac0` 判別資産とゲート新設）。対象は
`policy:traffic_light`（`TrafficLightAware`）と `policy:stop_yield`（`StopYieldSignAware`）の停止側（STOP標識）。

**設計変更**（`policy:traffic_light` 側のみ。§3.1・§3.2）：実装完了後、信号ヘッドのアンカーの取り方に
欠陥が見つかった。前提となる`OpenDrive::Clear()`のcontrollerリーク
是正（`d58b2d66`）、信号がどの交差点を司るかを解決する`SignalJunctionResolver`の新設（`f2fd312e`）、
アンカーを交差点入口とヘッドのminへ移す本体と判別資産の作り直し（`9f01fba5`）の3コミットで、
アンカーを「対応する交差点の入口とヘッドのうち近い方」に拡張した。`policy:stop_yield`
（STOP標識側、§7）はこの変更の対象外で、アンカーは変更前と同じく標識自身の距離のままである。

§9.2の窓の実測（既定値10.0mを維持する結論を含む）は、STOP標識側は変更前のまま
`resources/xosc/verification/04_traffic_signs/stop_sign_stop_line_paired.expectations.yaml`、
信号ヘッド側は設計変更後に再実測した
`resources/xosc/verification/03_traffic_signals/red_hold_stop_line_paired.expectations.yaml` と、
新設したfar-side判別資産の
`resources/xosc/verification/03_traffic_signals/red_hold_stop_line_paired_farside.expectations.yaml`
の各`notes`ブロックにある。
**制約**: R1 Clean Core（`EnvironmentSimulator` 無改変）、既存資産の挙動は bit-identical、
新パラメータは config + Web API + フロントエンドまで露出、判定ロジックは純関数として単体テスト可能にする。

---

## 0. この文書が解決する問題

現行の `TrafficLightAware` と `StopYieldSignAware` は、停止目標を信号ヘッドや標識そのものの
s座標から一定距離（`tl_stop_margin` / `sign_stop_margin`、既定 3.0m）手前に置いている。
路面に描かれた停止線は一切読んでいない。

この方式は、ヘッドや標識の設置位置と実際の停止線の位置が一致する資産でしか正しい場所に止まらない。
`multi_intersections.xodr` road196 の実測（信号 290 が s=0.0、停止線 292 が s=4.0、
両者とも orientation `-`）では停止線がヘッドの4m手前にあり、現行のマージン基準（5.00m手前）は
たまたま停止線の1m手前に収まっている。しかしこの一致は資産固有のものであり、ヘッドが交差点の
向こう側のマストアームに載っている資産では、同じ5m手前が交差点内に落ちる。

以下、この文書は次の9点を決定する。

1. 停止線の表現をどこまで対応するか
2. 国別コードの可変機構をどこに置くか
3. 停止線とヘッド・標識の対応付け規則
4. フォールバック（停止線が無いときの挙動）
5. スキャン保持制約の満たし方
6. `JunctionStopGuard` との相互作用
7. 一時停止標識側（STOP FSM）の扱い
8. OSI出力を変えるか
9. 検証資産の用意

---

## 1. 停止線の表現をどこまで対応するか

OpenDRIVEで「停止線」を表現しうる系統は3つある。

- **signal type=294**：`multi_intersections.xodr` に実在する `dynamic="no"` の signal で、
  路面標示を signal 要素として置いたもの（`name="SgRMHoldingline-1Lane.flt"` からして
  CADツール由来の "Haltlinie"、独語で停止線の意）。
- **`<object>` + `<markings>`**：GTの `UpdateOSIRoadMarkingsODR`（`GT_esmini/src/osi/GT_OSIReporter.cpp:980`）
  が OSI `RoadMarking` として出しているが、これは任意の路面標示（車線境界、矢印、横断歩道の縞）を
  汎用的に描画するための経路であり、「この object は停止線である」と分類する仕組みを持たない。
- **`<semantics><priority @type="stopLine">`**：OpenDRIVE 1.9 XSD の `e_signals_semantics_priority`
  列挙値の一つ。ただし GT では既にこれを別の意味で使っている。

3系統目から順に検討する。

`<semantics><priority type="stopLine">` は、GTの `StopYieldSignAware.cpp` の
`ClassifyPriorityTypes` がすでに読んでおり、`stop` と同じ扱い（STOP標識としてのdwell+creep FSMを
発火させる）にマッピングされている（`GT_esmini/docs/gt_roadmanager_patches.md` §6.2、
`straight_semantic_stop_sign.xodr` で挙動フィクスチャ化・単体テスト `SemanticPriorityFallback.*`
で固定済み）。つまりこの値は「この標識はSTOP標識として機能する」という**標識自体の分類**であって、
路面に引かれた線の位置を指すものではない。この解釈は依頼者の指摘（「混同しないこと」）とも一致する。
同じXSD値を停止線位置の意味で読み替えると、既存のP4契約（catalog-FIRST・semantics-FALLBACK、
決定1・決定2）と衝突するため、この経路は**採用しない**。

`<object>` + `<markings>` は、停止線を「なぜそう分類できるか」を機械的に判定する手がかりがない。
ASAM OpenDRIVEのobject/markings機構は形状・色・幅を記述するだけで、用途（停止線か、単なる
車線境界の破線か）を示す属性を持たない。この系統で停止線を拾おうとすると、位置と形状からの
ヒューリスティック（交差点近くの、幅の広い、横断方向の実線）を自作することになり、根拠のある
出典なしに推測で判定基準を決めることになる（R3の精神に反する）。リポジトリ中にこの表現で
停止線を意図したと確認できる資産も無い。よって**対応しない**。将来この形の資産が実在すれば、
その時点で別途設計する。

signal type=294 は、`dynamic="no"` であるため `TrafficLightAware` の
`dynamic_cast<TrafficLight*>` を通らず（`[GT_ODR:tl-gate]` は `dynamic="yes"` だけを
`TrafficLight` へ昇格する）現行ループでは素通りするが、`RouteSignalScan::ScanSignalsAhead` は
型不可知なので走査結果には入っている。orientation・lane validity・invalidated の3フィルタも
通過済みであることは `multi_intersections.xodr` road196 で確認済みである。これが停止線表現として
**唯一、実資産で確認できているパターン**であり、以下の設計はこれを主対応（かつ唯一の対応）とする。

ただし294という番号自体はASAM由来ではない。Wikimedia Commonsの図版名（"Zeichen 294 -
Haltlinie, StVO 1970.svg"）から辿れるとおり、294はドイツの道路交通令（StVO）が定める
標識番号であり、`multi_intersections.xodr` はこれを本来ドイツの番号を意味する場面のはずの
signalに、`country="OpenDRIVE"`という汎用国コードを付けて出力している。`de_traffic_signals.txt`
にも `opendrive_traffic_signals.txt` にも294のエントリが無いことは grep で確認済みであり、
このsignalは今日どちらのカタログでも未分類のままである。つまり294という組み合わせは
ASAMが定めた汎用パターンではなく、この資産を生成したCADツール固有の流儀である。
本設計ではこれを「ASAM仕様上正しい書き方」としてではなく、「唯一実在する資産が使っている、
現実に対応せざるを得ないパターン」として認識する（§2・§9.1）。

---

## 2. 国別コードの可変機構

signal の `country` 属性は、ASAM OpenDRIVE Specification v1.8.1 §14.1
（"Introduction to signals"）が "Country code of the road, see ISO 3166-1, alpha-2 codes"
と明記しており、想定される値はISO 3166-1 alpha-2（`de`・`jp`のような小文字2文字）である。
同じ§14.1は、どの国の公式標識にも対応しない要素向けに `country="OpenDRIVE"` という特別値も
定義しており、その `type`/`subtype` は別冊「ASAM OpenDRIVE Signal reference」
（現行v1.0.0、2023-11-22公開）が定めるとしている。esminiの `opendrive_traffic_signals.txt`
はこの特別値に対応するファイルである。

ただし、この`opendrive_traffic_signals.txt`はASAM公式カタログの写しではない。esmini開発者が
GitHub issue #515で「そのファイルはesminiリポジトリ側で正式に埋めているものではなく、
そうした標識で道路網を作りたい任意のユーザー向けのテンプレートとして、いくつかの
OpenDRIVE標識IDを添えたものにすぎない」と明言している。実際に294のような行は無く、
`1.000.0xx`系の値の多くも空欄のままである。したがって、このファイルへ新しい行を足すことは
ASAMの定義済み内容を書き換えることにはならない。

signal の `type`（+ `subtype`）が表す意味は国ごとの標識体系に依存する。これは既に upstream の
`OpenDrive::LoadSignalsByCountry`（`EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp:7669`）が
`resources/traffic_signals/<country>_traffic_signals.txt` を読み、
`signals_types_[country + type[.subtype]] = OSI型文字列` という国別カタログを構築して解決している。
signal の `country` 属性（`Signal::GetCountry()`）を鍵に、資産ごとに自動でカタログが切り替わる。
現状 `de`（397行）・`opendrive`（汎用、ほぼ空欄）・`se`（11行）・`cn`（2行）があり、`jp` は無い。

停止線判定にもこの仕組みをそのまま転用したいところだが、転用できない理由がある。
upstream のカタログが対応づける先は OSI `TrafficSign` の分類列挙（`Signal::OSIType`、
`RoadManager.hpp:1561` 以下）であり、この列挙には「路面標示としての停止線」という区分が
そもそも存在しない。type=294 のような信号は、カタログに載っていない type として扱われ、
`osi_type_` は「未分類」を表すセンチネル値のまま残る（実際 `opendrive_traffic_signals.txt` に
294のエントリは無い）。つまりこれは日本のデータが欠けているという問題ではなく、
**upstream のOSI型カタログ機構そのものに「停止線」という区分の置き場所が無い**という問題であり、
`jp_traffic_signals.txt` を足しても解決しない。

したがって、以下の2つを分けて用意する。

**(a) STOP・YIELD標識自体の国別コード**：既存の `LoadSignalsByCountry` 機構をそのまま使う。
新しい国が要るときは `resources/traffic_signals/<country>_traffic_signals.txt` を1本
追加するだけでよく（内容＝具体的な標識番号は必要になった時点で埋める。§9.1）、
`EnvironmentSimulator` のコードは一切変更しない。`de_traffic_signals.txt` の
`205=TYPE_GIVE_WAY` / `206=TYPE_STOP` と同じ形式を踏襲すればよいので、
upstream の `LoadSignalsByCountry` は無改変のまま新しい国の資産を解釈できるようになる。

**(b) 停止線マーキングの国別コード**：GT独自の、upstreamのカタログとは独立した小さな分類機構を
新設する。ファイルは `resources/traffic_signals/stop_line/<country>_stop_line.txt`
（既存カタログと同じ `key=value` 形式、例 `294=stop_line`）とし、新しい国は
ファイルを1本足すだけで対応できる。ロードは GT 側の新規コード（後述§10）が担い、
upstream の `signals_types_` には触れない。初期に用意するのは実資産で確認済みの
`opendrive_stop_line.txt`（`294=stop_line`）で、他の国は必要になった時点で追加する（§9.1）。

**国選択はGUIではなく資産の `country` 属性で自動決定する**。signalは既に自分自身の
`country` 属性を持っており（`GetCountry()`）、STOP・YIELD標識のOSI分類も停止線分類も
同じ鍵（signal自身のcountry）で引く。ユーザーがシナリオ実行前に「今回は日本仕様」と
選び直す操作は不要で、xodrに `country="jp"` と書けば自動的にその国のカタログが使われる。
これは既存の `LoadSignalsByCountry` の呼び出し規約（signalごとにcountryを見て必要なら
カタログを読み替える）と同じ考え方であり、新しいGUI操作系を増やさずに済む。

---

## 3. 停止線とヘッド・標識の対応付け規則

`ScanSignalsAhead` は既にorientation・lane validity・invalidatedでフィルタ済みの信号列を
ego からの距離昇順で返す。停止線候補もこの同じフィルタを通過した上で列に混ざっているので、
**新しい経路探索は不要**であり、この列の中から停止線を探せばよい。

対応付けの規則は次の1つに絞る。**「アンカーの距離以下で、最も近い停止線分類の信号」**を対とする。
アンカーは、STOP標識（`policy:stop_yield`）については標識自身の距離である。
信号ヘッド（`policy:traffic_light`）については、実装当初はヘッド自身の距離だったが、実装完了後に
見つかった欠陥を受けて交差点入口を加味した距離に変わっている（§3.1・§3.2）。`ScanSignalsAhead` の
出力は距離昇順なので、アンカーより手前（小さいdistance_ahead）を後ろ向きに走査し、最初に見つかった
停止線分類のエントリを採用する。探索範囲は新しい設定値（窓、§11）で打ち切る。

実資産（road196）で確認できる配置は、停止線がヘッドより常に手前（小さいdistance_ahead）にある
という前提を裏付けている。「アンカーの距離以下」という条件はこの前提をそのままコードにしたもので、
停止線がアンカーより奥にある（＝停止線を通り過ぎてからアンカーに着く）という物理的にありえない
配置を最初から候補から除く。

同一road限定、同一country限定は、いずれも採用しない。ScanSignalsAhead自体が経路距離ベースで
road境界をまたいで歩く設計であり、停止線が停止対象と別roadに乗っている資産（アプローチ路の
末端に停止線、接続路の始端にヘッド）を制約なしに扱える。停止線と対象のcountry属性が異なる
資産（想定はしにくいが禁止する理由もない）も、orientation・lane validityが既に整合を
保証しているので、country一致を追加条件にする必要はない。

複数の停止線分類信号が同一距離に並ぶ資産（タイブレークが未定義な既知の穴、
`f7_override_detector_findings` 系ではなく `tl_head_type_and_junction_clearance` メモの
「同一距離の信号のタイブレークが未定義」）は、この対応付けでは実害が出ない。
「アンカー以下で最も近い」という条件は同着のどちらを選んでも同じ距離を返すため、
`std::sort` が非安定であることの影響を受けない。

信号ヘッドについてのアンカーの具体的な定義と、それが変わった経緯は次の2節で扱う。

### 3.1 なぜ交差点入口を基準に加えるか

実装当初、信号ヘッドのアンカーも標識と同じくヘッド自身の距離だった。
この場合、窓（既定10.0m、§11）が実質主張していたのは「停止線はヘッドから10m以内にある」という、
信号機の取り付け方についての条件である。

ヘッドが交差点の手前に立つ配置（near-side）では、この条件で問題ない。
停止線とヘッドは同じ入口側にあり、両者の間隔は数メートルに収まる。
リポジトリ中で唯一実測できる資産（`multi_intersections.xodr` road196、§0）もこの配置であり、
4mという間隔は10mの窓に余裕を持って収まっていた。

しかし、ヘッドが交差点の向こう側のマストアームに載る配置（far-side）では、同じ条件が成立しない。
停止線は近づいてくる側の路面にあるのに対し、ヘッドは交差点を渡った先にあるため、両者は交差点の幅
（30〜40m程度）だけ離れる。
ヘッド基準の窓をどれだけ広げても、この間隔を10mのオーダーで埋めることはできない。
far-side配置では、ヘッド基準のアンカーは原理的に停止線を拾えない。

この問題は、「停止線はヘッドの近くにある」という信号機の取り付け方についての主張を、窓という
1つの数値に埋め込んでしまったことに起因する。
本来求めたいのは「停止線は交差点の入口の手前にある」という、路面標示の引き方についての主張であり、
これは信号機がどこに取り付けられているかに依存しない。
そこでアンカーの候補に、信号ヘッドの距離だけでなく、その信号が司る交差点の入口
（`RouteJunctionSpan::entry_ahead`、§6）の距離を加えた。

信号ヘッドがどの交差点を司るかは、OpenDRIVEの記述だけからは1通りに決まらない。
新設した`SignalJunctionResolver`（`ResolveSignalJunction`）は、次の3経路を(a)→(c)→(b)の順に試す。

- **(a) controller連鎖**：signalを参照する`<controller>`が、その`<controller>`を参照する
  `<junction>`へつながる経路。
- **(b) 接続路**：signal自身が乗っているroadが、junctionの接続路そのものである経路。
- **(c) 道路リンク**：signalの乗っているroadを、信号が司る進行方向へたどった先の道路リンクが、
  junctionそのものである経路。

(a)を最優先にするのは、これだけが信号機の物理的な取り付け位置に依存しない経路だからである。
(a)は資産の作者が「この信号はこのjunctionを司る」と明示した意味的な対応であり、ヘッドがnear-side
にあろうとfar-sideにあろうと関係なく成立する。
(b)・(c)は道路網の形状から機械的に導く経路であり、near-sideのヘッドでは大抵正しいjunctionに
行き着くが、far-sideのヘッドでは自分の乗っているroadの先にあるjunction（司っているjunctionとは
別の、地理的に近いだけのjunction）を指してしまう。
§3.2の判別資産はこの失敗を実際に踏んでおり、(a)のcontroller連鎖だけが正しいjunctionへ解決する。

(a)には2つの穴がある。
どのjunctionからも参照されないcontrollerと、複数のjunctionから参照されるcontrollerである。
前者は単に対応が無いだけだが、後者で機械的に最初の1件を選ぶと、資産の書き方次第で黙って別の
junctionを指しかねない。
`ResolveControllerChainJunctions`はどちらの場合も解決を諦め、その信号については(c)・(b)へ処理を
渡す。

(a)の経路が効く資産は、リポジトリ全体で`multi_intersections.xodr`だけであり、どの検証シナリオ
からも使われていない。
最優先の経路を未検証のまま出荷しないため、この資産を実際に読み込む単体テスト
（`GT_esmini/test/unit/virtualdriver/test_SignalJunctionResolver.cpp`）で固定した。

信号がどのjunctionを司るかの解決は、STOP標識・信号ヘッドを問わず走査対象の全信号について
`ScanSignalsAhead`が行い、`ScannedSignal::junction_id`に持たせる。
解決自体を走査側で行うのは、経路(c)が要求する進行方向（信号がどちら向きの交通を司るか）を
持っているのが走査側だけだからである。
ただし、この結果を実際にアンカーへ使うのは`TrafficLightAware`だけである。
`StopYieldSignAware`（STOP標識側、§7）は変更していない。
STOP標識は自分の立つ場所そのものが法的な停止位置であり、ヘッドのように別の場所へ取り付けられる
余地がそもそも無いため、この問題を抱えていない。

`SignalJunctionResolver`を実装しテストする過程で、別の欠陥が見つかった。
同一プロセス内で複数のxodrを切り替えて実行する構成（バッチ実行、Webバックエンド）では、
`OpenDrive::Clear()`が`road_`や`junction_`はクリアする一方、トップレベルの`<controller>`一覧
（`controller_`）を見落としており、後にロードしたxodrに前のxodrのcontrollerが残り続けていた。
これはフォーク限定の実装漏れであり、経路(a)の信頼性がプロセスの実行順序に左右されかねなかった。
`SignalJunctionResolver`本体とは独立に是正し（`d58b2d66`、`[GT_ODR:ctrl-clear]`、
`GT_esmini/docs/gt_roadmanager_patches.md`）、その上で経路(a)を実装した。

### 3.2 交差点入口だけでは足りない理由

交差点入口をそのままアンカーにすると、新しい欠陥が生まれる。
入口だけを見た場合、ヘッドより奥（egoから見て遠い側）に停止線が来る配置がありうる。
この配置で対応付けが成立すると、停止目標はヘッドより奥に置かれる。
egoはその目標へ向けて減速するが、停止線の手前で止まる前に自分の原点がヘッドのs座標を越えて
しまうことがあり、越えた瞬間に`RouteSignalScan::ScanSignalsAhead`の前方限定の走査からヘッドが
外れる。
ヘッドがスキャンから消えるとRED制約も消え、赤信号のまま再発進する。

これは§5が明示している「停止中もヘッドをスキャン内に残す」という保証そのものであり、入口単独の
アンカーはこの保証を静かに破っていた。
この欠陥は実測で確認されている。
再現の記録は
`resources/xosc/verification/03_traffic_signals/red_hold_stop_line_paired.expectations.yaml`
の`notes`ブロック（"N=3 hazard"の項）にあり、egoが停止線の手前でほぼ止まった状態から、
シナリオ側の停止判定が働くt=25の時点でもなお前方へ這うように動き続けていたことを記録している。

そこで、アンカーは**「交差点入口とヘッドのうち、egoに近い方」**とした。
`RouteSignalScan::ResolveStopLineAnchor`が`min(junction_entry_ahead, head_dist_ahead)`を返す。
minを取ると、停止線は定義上必ずアンカー以下の距離にしか対応付けられないため、アンカー自体が
ヘッド以下になることも保証される。
§5の保証はこうして無条件に戻る。

near-side配置ではヘッドの方が入口より近いため、minはヘッドを選ぶ。
この場合の対応付けは実装当初と同じ関数（`FindPairedStopLine`、ヘッド自身のインデックスを
アンカーとする）を経由し、実測でも変更前とビット一致する結果になることを確認している
（同notesブロック）。
far-side配置ではヘッドの方が遠いため、minは入口を選ぶ。
この場合は新設した`FindPairedStopLineByDistance`を経由する。
アンカーが裸の距離であり、ヘッドや標識のように「自分自身」として候補から除くべきインデックスが
無いための別関数である。
near-sideとfar-sideは同じ1つの規則（min）から、資産の作り方に応じて自動的に振り分けられる。
振り分けを選ぶ設定項目は無く、どちらが選ばれたかは診断KV`gt.traffic_light.stop_line_anchor`
（値は`"junction_entry"`または`"head"`）で観測できる。

判別資産も作り直した。
near-side側は生成器（`resources/scenario_authoring/road_catalog/gen_signalized_short_block.py`）の
パラメータを、ヘッドからの後退量ではなく交差点入口からの後退量に改めた。
far-side側は、ヘッドをjunctionの先の道路に置き、controller連鎖（経路(a)）で司るjunctionへ結ぶ
資産を新設した
（`resources/xosc/verification/03_traffic_signals/red_hold_stop_line_paired_farside.xosc`）。
この資産は経路(c)・(b)がヘッド自身の道路リンクから誤って別のjunctionを指してしまう配置であり、
これを正しく解けることが経路(a)を最優先にする理由（§3.1）の実地の裏付けになっている。
near-side・far-sideそれぞれの実測値は、前段落および上記2つのexpectations.yamlの`notes`
ブロックにある。

多段階の横断のように、1つの交差点に停止線が2本以上ある配置（中央分離帯の手前と奥、など）でも、
奥側の停止線が誤って対応付けられる心配はない。
`FindPairedStopLine`・`FindPairedStopLineByDistance`はいずれも、アンカーより奥にある候補を、
窓の値を見るより前の時点で無条件に除外する。
奥側の停止線は交差点の内側にあり、near-sideのヘッドから見ても交差点入口から見てもアンカーより
遠いため、この除外規則だけで窓の設定に関係なく候補から落ちる。

---

## 4. フォールバック

対応する停止線分類の信号が窓の中に見つからない場合は、**現行の `head_s - margin` /
`sign_s - margin` をそのまま使う**。これは実装上は「新しい経路が何も見つけられなかった時の
分岐」ではなく、「対象の距離をそのまま使う既存コードパスに、停止線が見つかった時だけ値を
差し替える」という構成にする（§10）。差し替えが起きなければ計算は今日と完全に同じになる。

既存の検証資産（`car_following_traffic_control_batch.yaml` が使う fabriksgatan系、
`04_traffic_signs` のSTOP標識フィクスチャ）には停止線分類される信号が1つも無いことを確認済みである
（`type="294"` はリポジトリ全体で `multi_intersections.xodr` にしか出現せず、この資産はどの
検証シナリオからも使われていない）。したがって既定でこの機能を有効にしても、既存のベースラインは
変わらない。フォールバックであることを診断KVに出すかどうかは、他ポリシーの診断慣習
（`gt.traffic_light.*`、`gt.stop_yield.*`）に倣い、停止線が見つかったかどうかを示す
真偽値のKVを1つ追加する形で対応する（§10）。

---

## 5. スキャン保持制約の満たし方

`tl_stop_margin` は単なる前端オフセットではなく、**停止中もヘッドをスキャン内に残す**ための
値である（`TrafficLightAware.hpp` のコメントに明記。egoの原点がヘッドのsを越えると
RED制約が消え、赤信号無視に読める挙動になる）。

§3の対応付け規則は「停止線はヘッド以下の距離にしかない」という前提で候補を絞っているので、
停止線を基準に停止しても、ヘッドまでの残距離は margin 基準で止まった場合と同じか、それ以上に
大きくなる。停止線がヘッドの直前にある通常の配置では、停止線基準の目標はヘッド基準の目標より
ego に近い側に来るため、ヘッドとの間の余白は縮まりこそすれヘッドがスキャン外に出ることはない。
停止線とヘッドが同一sの資産（信号の設置台座に停止線が重なっている想定）でも、対応付け規則は
「アンカーの距離**以下**」を許すので停止線基準の目標はヘッド基準の目標と一致し、現行と同じ結果になる。
このため、スキャン保持のために追加の特別処理は要らない。

この保証が自動的に成り立つのは、アンカーがヘッド自身の距離だけだった間の話である。
後日の設計変更（§3.1・§3.2）でアンカーの候補に交差点入口が加わったときは、この前提が無条件には
成り立たなくなった。
交差点入口だけをアンカーにする案では、停止線が「交差点入口以下・ヘッドより奥」という配置で
対応付けられてしまい、この節が守ろうとしていた保証そのものが破れ、赤信号のまま再発進する欠陥が
実測された（§3.2）。
最終的に採用した`min(交差点入口, ヘッド)`は、「停止線は必ずアンカー以下」という対応付け規則を
経由して「アンカーは必ずヘッド以下」も同時に満たすため、この節の結論（追加の特別処理は要らない）
自体は信号ヘッド側でも変わらず成り立つ。
ただしそれは§3の対応付け規則から自動的に導かれるのではなく、minという構成で明示的に満たしている。

---

## 6. `JunctionStopGuard` との相互作用

`ResolveJunctionSafeStop`（`JunctionStopGuard.hpp`）は、blocking判定を
`span.entry_ahead < s_stop_wanted && s_stop_wanted < span.exit_ahead + exit_clearance` という
純粋な入力（`s_stop_wanted`）の関数として持っている。この関数自体は変更しない。

変わるのは `TrafficLightAware::Evaluate` がこの関数に渡す `s_stop_wanted`（現行では
`head_s - margin`）であり、停止線が見つかった場合はそれが `line_s - margin` に置き換わる。
停止線が交差点の入口より手前にあれば、blocking判定はより成立しにくくなる方向に動く。これは
劣化ではなく、実際の法定停止位置に近い値でblocking判定を行うようになるという意味で正しい挙動である。
なぜなら、現行の `head_s - margin` は「ヘッドの取り付け位置からの一定オフセット」という
代理指標に過ぎず、ヘッドが交差点の向こう側にある資産ではその代理指標自体が既に不正確
（§0で述べた5m手前が交差点内に落ちるケース）だからである。停止線という一次情報が得られる場面では、
その一次情報を使う方が代理指標より信頼できる。

`PULL_BACK` のターゲット（`entry_ahead - stop_margin`）は変更しない。この値は交差点の
トポロジ（junction span の入口位置）だけから合成される点であり、どの信号にも紐づいていないため、
停止線カタログを参照する対象が存在しない。

§3.1で導入したアンカーの交差点入口側の候補は、この節の`span.entry_ahead`と同じ`RouteJunctionSpan`
から読む同じ値である。
停止線対応付けのアンカー計算とPULL_BACKのblocking判定は、別々に較正された2つの数値ではなく、
同じ「交差点入口はどこか」という1つの値を、それぞれの目的で参照しているだけである。

---

## 7. 一時停止標識側（STOP FSM）の扱い

`stop_fsm::Update`（`StopYieldSignAware.hpp`）自体は変更しない。この関数が受け取る `dist`
引数の意味だけが変わる。現行は `s.distance_ahead - cfg_.stop_margin`（STOP標識からの
オフセット）だが、対応する停止線が見つかった場合は `line.distance_ahead - cfg_.stop_margin`
に置き換える。`stop_line_tol`（「線に十分近いか」の判定値）は、この置き換えによって
**その名前が指すとおりの量**（実際の停止線までの残距離）に対して働くようになる。現行では
名前に反してSTOP標識からの距離に対して働いていた。

この変更は `StopYieldSignAware` のうちSTOP分類（`ClassifyPriorityTypes` またはカタログが
`TYPE_STOP` と判定したもの）にのみ適用し、YIELD分類には適用しない。YIELDの制約
（`MAX_SPEED_TO_S`、`c.s = s.distance_ahead` そのまま、マージンの減算すら無い）は
「標識の手前まで減速するだけで停止しない」という設計であり、停止線という「そこで止まる場所」の
概念とは別軸である。YIELD側への拡張が必要になれば独立した設計判断とする。

既存ベースライン `stopped_at_stop_sign`（`stop_sign_full_stop`）と
`semantic_stop_sign_full_stop` は、いずれも対応する停止線分類信号を持たない資産
（`straight_semantic_stop_sign.xodr` を含む `04_traffic_signs` 配下のフィクスチャに
type=294相当の信号は無い）なので、§4のフォールバックにより変化しない。

§3.1・§3.2で導入した、交差点入口を加味したアンカーは`TrafficLightAware`（信号ヘッド側）だけに
適用されている。
この節で述べたSTOP標識側の対応付けは変更しておらず、STOP標識のアンカーは今日も標識自身の距離の
ままである。

---

## 8. OSI出力を変えるか

**変えない**。`GT_OSIReporter::UpdateStaticTrafficSignals`
（`GT_esmini/src/osi/GT_OSIReporter_Traffic.cpp:56`）は、カタログ未分類のsignalも
`osi_type_` をそのまま（未分類センチネルは `TYPE_OTHER` にクランプして）OSIへ出しており、
これは今回の変更と無関係にtype=294の信号に対して既に起きている挙動である。今回追加する
停止線カタログは `Signal::osi_type_` を一切書き換えない、`TrafficLightAware` /
`StopYieldSignAware` 内部だけで完結する分類なので、OSI GroundTruthのシリアライズは
現行のまま変わらない。

OSIの `TrafficSign` 分類列挙自体に「停止線」の区分が無いという§2で述べた制約は、
停止線をOSIへ出したいという要望が将来出た場合に upstream への提案が要る話であり、
本設計のスコープ外として扱う。

---

## 9. 未解決事項と、それを潰すための調査手順

### 9.1 日本の実コードは本設計のスコープ外

依頼者の判断により、日本の法定標識番号そのものの確定は本設計では扱わない。本設計が
確立するのは**国別コードを差し替えられる機構**（§2・§10）であり、`jp_traffic_signals.txt` /
`stop_line/jp_stop_line.txt` の中身は、必要になった時点で埋めればよい単なるデータである。

参考までに、簡易調査で一時停止標識＝330、停止線＝203という手がかりを得ている（Wikipedia
日本語版「一時停止」「日本の路面標示」記事が引用する『図解道路交通法』・全国道路標識・標示業協会
（2018）・交通工学研究会（2012）の孫引きで、道路標識令原文での裏取りはしていない）。
実装時にこの数値をそのまま使うか、あらためて一次資料で確認するかは、その時点の判断に委ねる。
いずれにせよ機構の設計・実装（§10・§12）はこの数値の確定を前提にしない。

機構が実際に動くことの検証（§13）も、実在する国のコードを待つ必要はない。存在しない
プレースホルダの国コード（例 `zz`）で1本カタログファイルを用意し、それを読んで停止線判定が
差し替わることを確認すれば、国別切替機構そのものの検証としては十分である。

### 9.2 対応付け窓（`*_stop_line_window`）の既定値

既定値は10.0m（§11）のままだが、その根拠は§3.1・§3.2の設計変更で変わった。

STOP標識側と、信号ヘッドのnear-side配置（アンカーがヘッド自身になる場合、§3.2）については、
窓は元どおり「ヘッド（または標識）からの距離」を測る数値である。
実測できているのは`multi_intersections.xodr` road196の1例（停止線がヘッドの4m手前）だけだが、
この配置での窓の境界（停止線とアンカーの距離を振り、窓内なら採用・窓外ならフォールバックに
なるか）は設計変更後に判別資産で再実測されており、4mという実測値には従来どおり余裕を持って
収まっている
（`resources/xosc/verification/03_traffic_signals/red_hold_stop_line_paired.expectations.yaml`
の`notes`ブロック）。
この半分の根拠は、実測1件だけという弱さを含め、当初の判断と変わらない。

信号ヘッドのfar-side配置（アンカーが交差点入口になる場合）については、窓は「交差点入口からの
距離」を測る数値に変わる。
ここは実測に基づく確定値ではない。
OpenDRIVEのjunction入口（接続路の始端）は、塗装された交差点の縁と必ずしも一致しない。
同じ物理的な停止線でも、資産の作り方次第で入口からの距離は変わりうる。
この経路を実際に検証しているのはfar-side判別資産
（`resources/xosc/verification/03_traffic_signals/red_hold_stop_line_paired_farside.expectations.yaml`）
1本だけであり、これは判別が機能することを確認したものであって、実在するfar-side資産の入口と
縁の関係を実測したものではない。

つまり10.0mという1つの既定値は、根拠の強さが異なる2つの問いを束ねている。
「ヘッドから10mあれば足りるか」は実測1件で支持されており、minの採用によっても変わらない。
「交差点入口から10mあれば足りるか」は支持する実測が無い。
minはアンカーを縮める方向にしか働かないため、どちらの問いが有効かは資産ごとに自動的に決まり、
設定で選ぶものではない（§3.2）。
「1本の合成道路の実測だけでは既定値を確定できない」という結論の性質そのものは、設計変更の
前後で変わっていない。

### 9.3 KG登録の要否

新しい観測量（KV `gt.traffic_light.stop_line_used` 相当）やmatcherを追加するかどうかは
実装時に `check_knowledge_graph.py --brief policy:traffic_light` を再実行して判断する。
本設計では既存の `policy:traffic_light` / `policy:stop_yield` の枠内での拡張として
記述しており、新しい名前空間IDの追加は想定していない。

---

## 10. モジュール分割

新規ファイル `GT_esmini/include/gt_esmini/control/virtualdriver/policies/StopLineSignalCatalog.hpp`
（+ 対応する `.cpp`）。upstreamの `LoadSignalsByCountry` と対になるがGT独自実装であり、
upstreamの `signals_types_` には触れない。

```cpp
namespace gt_esmini
{

// 挙動を持つ停止線分類。将来の拡張(停止線以外の路面標示の分類)に備えて enum にしておく。
enum class StopLineKind
{
    NONE,
    STOP_LINE
};

// 純関数: 国コード(小文字化想定) + OpenDRIVEのtype(+subtype)からの分類。catalogはファイルIOを
// 持たないテーブルなので、ハンドロールしたmapで単体テストできる(ClassifyPriorityTypesと同じ形)。
StopLineKind ClassifyStopLineType(const std::unordered_map<std::string, StopLineKind>& catalog,
                                  const std::string&                                  country,
                                  const std::string&                                  type,
                                  const std::string&                                  subtype);

// resources/traffic_signals/stop_line/<country>_stop_line.txt を読み、国ごとにプロセス内
// キャッシュへ格納する。LoadSignalsByCountryと対の役割を持つが独立実装(R1)。
bool LoadStopLineCatalog(const std::string& country);

}  // namespace gt_esmini
```

`RouteSignalScan.hpp` の `ScannedSignal` に1フィールド追加する。既存フィールドの意味・
既存フィルタ（orientation / lane validity / invalidated）は変更しない。

```cpp
struct ScannedSignal
{
    roadmanager::Signal* signal         = nullptr;
    double               distance_ahead = 0.0;
    bool                 is_stop_line   = false;  // 追加: StopLineSignalCatalogによる分類
};

// 純関数: signalsは距離昇順(ScanSignalsAheadの出力)。anchor_indexの信号(ヘッド/STOP標識)より
// 手前かつwindow以内で最も近いis_stop_lineなエントリのインデックスを返す。無ければnullopt。
std::optional<size_t> FindPairedStopLine(const std::vector<ScannedSignal>& signals,
                                          std::size_t                       anchor_index,
                                          double                             window);
```

呼び出し側の変更点は次の1箇所ずつになる。

- `TrafficLightAware::Evaluate`：governingヘッドのインデックスが確定した時点で
  `FindPairedStopLine` を呼び、見つかった場合はそのエントリの `distance_ahead` を
  `TrafficLightShouldStop` の距離引数と `wanted` の計算の両方に使う（既存コードの
  `s.distance_ahead` という1つの変数を、停止線が見つかった時だけ差し替える形にする）。
- `StopYieldSignAware::Evaluate`：STOP分類と判定したエントリについて同様に
  `FindPairedStopLine` を呼び、見つかった場合はそのエントリの `distance_ahead` を
  `stop_fsm::Update` に渡す `dist_adj` の計算に使う。YIELD分類には適用しない（§7）。

`JunctionStopGuard.hpp` / `JunctionStopGuard.cpp` は変更しない（§6）。

後日の設計変更（§3.1・§3.2）で、この構成に2つのモジュールが加わった。
新規ファイル`GT_esmini/include/gt_esmini/control/virtualdriver/policies/SignalJunctionResolver.hpp`
（+ 対応する`.cpp`）が信号→交差点の解決（`ResolveSignalJunction`、純関数
`ResolveControllerChainJunctions` / `ResolveAheadLinkEnd`）を担う。
`RouteSignalScan.hpp`の`ScannedSignal`にはこの解決結果を持たせる`junction_id`フィールドを追加し、
既存の`FindPairedStopLine`はそのまま残した上で、裸の距離をアンカーに取る
`FindPairedStopLineByDistance`と、`min(交差点入口, ヘッド)`を計算する`ResolveStopLineAnchor`を
新設した。
呼び出し側は`TrafficLightAware::Evaluate`のみを変更しており、governingヘッドの`junction_id`が
このルートで到達するjunction spanと一致する場合は`ResolveStopLineAnchor`経由で
`FindPairedStopLineByDistance`を呼び、一致しない場合は変更前と同じ`FindPairedStopLine`を呼ぶ
（§3.1・§3.2）。
`StopYieldSignAware::Evaluate`は変更していない。

---

## 11. 新規configキー

`GT_esmini/config/virtual_driver.json` と `virtual_driver_realwheel.json` の両方に追加する。

| キー | 型 | 既定値 | 意味 |
| :--- | :--- | :--- | :--- |
| `tl_stop_line_aware_enabled` | bool | `true` | ONで信号ヘッドの停止目標を対応する停止線に差し替える。OFFで現行の`head_s - margin`のみに戻るキルスイッチ |
| `tl_stop_line_window` | number | `10.0` | アンカー（信号が司る交差点を解決できればその入口とヘッドのうち近い方、解決できなければヘッド、§3.1・§3.2）より手前・この距離以内の停止線のみ対とする（§9.2） |
| `sign_stop_line_aware_enabled` | bool | `true` | ONでSTOP標識の停止目標を対応する停止線に差し替える。OFFで現行の`sign_s - margin`のみに戻るキルスイッチ |
| `sign_stop_line_window` | number | `10.0` | STOP標識より手前・この距離以内の停止線のみ対とする |

既定値を `true` にする根拠は、§4で確認したとおり既存の全ゲート資産に停止線分類される信号が
存在せず、ONにしても既存挙動が変わらないためである。これは `tl_junction_guard_enabled` や
`ad_steering_envelope_enabled`、`resume_merge_enabled` が「安全側・高品質側の機能で、
無効化時の挙動が明確な代理」として既定ONになっている前例と同じ考え方である。

反映箇所は既存の `tl_stop_margin` 等と同じ4か所（`control_ownership_pitfalls.md` §4の教訓どおり
1か所直しで終わらせない）。

- `GT_esmini/config/virtual_driver.json` / `virtual_driver_realwheel.json`（既定値）
- `GT_esmini/web/backend/api/virtual_driver_api.py`（`_BOOL_KEYS` / `_NUMBER_KEYS` /
  `KNOWN_KEYS` への追加、defaultsディクショナリへの追加）
- `GT_esmini/web/frontend/src/api/client.ts`（`VirtualDriverConfig` 型への追加）
- `GT_esmini/web/frontend/src/components/simulation/VirtualDriverPanel.tsx`
  （`EDITABLE_KEYS` とTraffic Light / Stop-Yieldセクションへの入力追加）

`resources/traffic_signals/stop_line/*.txt` と `resources/traffic_signals/jp_traffic_signals.txt`
は `resources/` 配下の新規データ追加であり、`GT_esmini/config/*.json` のような
`CONFIG_FILES` 網羅アサーション（`web/pyinstaller/build_package.py`）の対象ではない
（`resources/` はパッケージングでは既存の慣習どおり丸ごとコピーされる想定。パッケージビルド時に
実際にそうなっているかは `/package` 実行時に確認する）。

---

## 12. 実装計画

各段階でどのゲートを緑にするかを明記する。

1. **カタログ層の追加**（配線なし）：`StopLineSignalCatalog.hpp/.cpp` + 単体テスト
   （`ClassifyStopLineType` に対して、`opendrive_stop_line.txt` 相当のハンドロールmapで）。
   ゲート: ユニットゲートのみ。どのポリシーもまだ呼び出さないので回帰ゲートは無変化。
2. **スキャン層の拡張**：`ScannedSignal::is_stop_line` + `FindPairedStopLine` + 単体テスト。
   ゲート: ユニットゲート。`TrafficLightAware` / `StopYieldSignAware` はまだ新フィールドを
   読まないので回帰ゲートは無変化（これ自体が「既存挙動bit-identical」の機械的な裏付けになる）。
3. **`TrafficLightAware` への配線** + config 4か所反映。
   ゲート: 回帰ゲート `-FailOnBehavioral` で既存3マニフェストの逸脱ゼロを確認
   （停止線分類される信号が既存資産に無いことの実測的裏付け）。
4. **`StopYieldSignAware` への配線**（STOP分類のみ）+ config 4か所反映。
   ゲート: 同上。
5. **新規判別資産の追加**：停止線を明示的に持つ信号交差点・STOP標識のジェネレータ拡張と
   xosc/expectations、専用マニフェスト + ベースライン（§13）。キルスイッチOFFで現行相当の
   fail、ONでpassを実測してから確定する（§13の「両腕測定」）。
   ゲート: 新規マニフェストが `-FailOnBehavioral` で緑。
6. **国別切替機構の検証**：プレースホルダの国コード（例 `zz`）でカタログファイルを1本用意し、
   同じ停止線 type 番号でも国が変われば分類結果が変わる（あるいは国ファイルが無ければ
   分類されない）ことを単体テストで確認する（§9.1）。実在する国のコード確定を待たない。
   ゲート: ユニットゲート。
7. **文書更新**：`GT_esmini/docs/virtualdriver/README.md` の「実装する」表に本文書を追加。
   関連するKG IDがあればコミットに引用する（§9.3）。

---

## 13. 検証資産

### 13.1 判別資産の設計

既存のジェネレータ `resources/scenario_authoring/road_catalog/gen_signalized_short_block.py`
（`36a5d727`で追加）にオプションの停止線signal（type=294）を配置するパラメータを足す。
停止線をヘッド基準マージンの位置とは異なる距離に置くことで、
キルスイッチON/OFFの2本の実行が異なる場所に停止するようにする。これは
`tl_junction_guard_enabled` を使った「同一バイナリの両腕を測る」やり方
（`tl_head_type_and_junction_clearance` メモに記録済み）と同じ構造である。

STOP標識側は `straight_semantic_stop_sign.xodr` と同様の単純な直線路に、
停止線分類されるsignalをSTOP標識から数メートル離して1本追加した資産を新規に作る。

いずれも新規資産の追加であり（R3）、既存の `car_following_traffic_control_expected.yaml` や
`04_traffic_signs` の既存ベースラインは変更しない。

後日の設計変更（§3.2）で、信号ヘッド側の判別資産はさらに作り直した。
near-side側の生成パラメータは、ヘッドからの後退量ではなく交差点入口からの後退量に改めた。
controller連鎖（経路(a)、§3.1）で交差点入口へ明示的に結んだfar-side資産（信号ヘッドをjunctionの
先の道路に置く配置）も新設した。
これも新規資産の追加であり、既存ベースラインは変更していない。

### 13.2 マニフェストとベースライン

新しいマニフェスト（例 `resources/xosc/verification/03_traffic_signals/` と
`04_traffic_signs/` にそれぞれ1本ずつ追加した上で束ねる専用manifest）と、対応する
ベースラインを新設する。既存3組（`car_following_traffic_control` /
`aeb_safety` / 他）には触れない（`realwheel_handover_results_2026-07.md` 系のメモに
記録されている「回帰ベースラインは独立3組」という前例と同じやり方）。

実装後、これは`resources/xosc/verification/stop_line_pairing_batch.yaml`と
`GT_esmini/test/regression_baseline/stop_line_pairing_expected.yaml`という組で確定した。
後日の設計変更（§3.2）では、このマニフェストへfar-side判別資産を追加しただけで、独立した
4組目は作っていない。

### 13.3 測定順序

1. キルスイッチOFF（`tl_stop_line_aware_enabled: false` 等）で新規資産を実行し、
   停止位置が現行のマージン基準の位置になることを確認する（= 修正前の挙動を実測でfailさせる、
   停止線基準で見た停止位置に対する残距離が非ゼロであることを確認する）。
2. キルスイッチONで同じ資産を実行し、停止位置が停止線基準（許容誤差内）になることを確認する。
3. 1と2の差分が判別として有効な大きさ（設置した停止線とマージン基準位置の差より十分大きい
   許容誤差設定）であることを確認してから、ベースラインとmatcherのしきい値を確定する。

---

## 14. 採らなかった案

- **`<object>` + `<markings>` を停止線として解釈する**：§1で述べたとおり、分類の手がかりが
  無くヒューリスティックに頼ることになるため採らない。
- **`<semantics><priority type="stopLine">` を停止線位置として読み替える**：既に別の意味
  （標識自体をSTOP扱いにする）で配線済みであり、意味の二重化になるため採らない。
- **停止線カタログの鍵に対象（ヘッド・標識）とのcountry一致を要求する**：orientation・
  lane validityが既に整合を保証しており、追加の制約が正当な資産を拒否するリスクの方が
  大きいため採らない（§3）。
- **停止線カタログの鍵に同一road限定を課す**：`ScanSignalsAhead`がroad境界をまたいで
  経路距離で歩く設計を無駄にするため採らない（§3）。
- **`JunctionStopGuard`のPULL_BACK目標にも停止線を反映する**：紐づく信号が無い純粋な
  トポロジ由来の点であり、参照すべきカタログエントリが存在しないため対象外とする（§6）。
- **YIELD分類にも停止線対応を広げる**：現行のYIELD制約は「標識手前まで減速するだけ」で
  停止そのものをしないため、停止線という概念とかみ合わない。独立した設計判断が必要になった
  時点で別途行う（§7）。
- **`country="OpenDRIVE"` + 任意の国の標識番号、という組み合わせを一般パターンとして
  推奨する**：`multi_intersections.xodr` の294はドイツStVOの番号を汎用国コードに載せた
  生成ツール固有の流儀であり、ASAM仕様が定めた書き方ではない（§1）。本設計はこの資産を
  実在するデータとして認識するにとどめ、新規資産（特に日本仕様）には正規のISO 3166-1
  国コード（`country="jp"`）を使うことを推奨する（§9.1）。
- **信号ヘッドのアンカーを交差点入口だけにする（minを取らない、後日の設計変更で実際に踏んだ案）**：
  入口だけでは停止線がヘッドより奥に来る配置を許してしまい、§5が守ろうとしていた「停止中も
  ヘッドをスキャン内に残す」保証が破れ、赤信号のまま再発進する欠陥を実測した。
  `min(交差点入口, ヘッド)`を採用して保証を無条件に戻した（§3.2）。
