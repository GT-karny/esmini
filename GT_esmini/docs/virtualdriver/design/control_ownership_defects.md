# シナリオからの手動運転移管 — 3症状の原因と対策プラン

**状態**: **修正1・修正2・修正3-a は実装済み**（`708e792c`）。§7.5 の欠陥 D1-D3 も修正済み。
以下は原因分析と、実装後も残る既知の制約の記録である。**未実施の項目は §4 修正3-c／修正4／修正5。**
**作成**: 2026-07-31
**関連**: [`scenario_control_handoff_design.md`](scenario_control_handoff_design.md) /
[`scenario_control_handoff_howto.md`](../guides/scenario_control_handoff_howto.md) /
[`domain_split_ownership.md`](domain_split_ownership.md)

**根拠の出どころ表記**: ★ = 本調査でコードを直接開いて確認 / ☆ = サブエージェント報告（実装時に再確認すること）。
行番号はズレうる。**シンボル名で参照すること。**

---

## 0. 対象の3症状（ユーザー報告）

1. **MD → VD にステアリングのボタンから復帰できない**
2. **VD からステア角・エンジン回転数などの OSI 出力がされない**
3. **VD からシナリオアクションで MD に移行したとき、ステアかペダルを操作するまで MD が動作しない**（エンジンブレーキがかからない。Default Controller に落ちている疑い）

---

## 1. 全体像 — なぜ3つが同時に起きるのか

### やさしい説明

車を運転しているのが「誰か」を管理する台帳が、いま **4つ別々にある**。
4つの内訳と、1フレームの中でそれらがどの順に効くかは
[`control_ownership_pitfalls.md`](control_ownership_pitfalls.md) §2 にある。

平常時は4つの答えが一致するので問題が出ない。**引き渡しの瞬間だけ4つがバラバラになる**。
3つの症状は、そのバラけ方の違いである。

- ①（活性ドメイン）が「降りた」と言っているのに ③（オーバーライド状態）は「まだ AUTO（人は触ってない）」と言う → **誰も運転していない**（症状3）
- ① が「降りた」＝ VD の `Step()` が呼ばれない → **復帰ボタンを見ている人がゼロ**（症状1）
- ④（出力のデータ源）は名前の優先順で決め打ちしていて ②③ を見ない → **運転していない方のコントローラの空データが OSI に出る**（症状2）

3つに共通するのは、**上書きする人が居ない瞬間に `defaultController` が等速で道なりに車を滑らせる**ことである ★。
ユーザーの「Default Controller に落ちてるかも」という直感は正しい。

---

## 2. 症状ごとの原因

### 症状3: 移行後、操作するまで MD が動かない（エンブレがかからない）

**やさしい説明**

ManualDrive には「**人がまだ触っていないなら、運転はシナリオに任せて自分は何もしない**」という
省エネ動作がある。これは「シナリオが車を走らせている最中に人が割り込む」使い方のための設計で、
**任せる先（シナリオ）が居ることが大前提**になっている。

ところが VD を降ろしたあとは、任せる先が居ない。MD は「シナリオに任せた」つもりで何もせず帰り、
残るのは前述の**デフォルトの運転手による等速移動だけ**。だからエンジンブレーキもかからず、
アクセルもブレーキも効かない。ハンドルかペダルを閾値以上動かすと MANUAL にラッチして初めて
MD が仕事を始める。

**コード根拠** ★

```cpp
// GT_esmini/src/control/manualdrive/ManualDriveCoordinator.cpp:46-50
if (!c.override_mgr_.IsAnyManual() && !split_active)
{
    c.scenarioengine::Controller::Step(dt);   // dirty ビットを立てるだけ。何も動かさない
    return;                                    // 物理も Apply() も HVD 更新も走らない
}
```

- `OverrideManager` の初期モードは **両ドメイン AUTO**（`OverrideManager.hpp:173-174`）★
- 実機の既定 config `GT_esmini/config/manual_drive.json` は **`override.enabled = true`** ☆
  → 起動直後は AUTO/AUTO → `IsAnyManual()` 偽
- 丸ごと引き渡し後は lat も lon も MD 所有なので **`split_active` は偽**（`ManualDriveCoordinator.cpp:35-43`）★
  → 既存の分割用の例外に救われない
- `Controller::Step()`（基底）は dirty ビットを立てるだけで何も動かさない ★（`Controller.cpp:71-86`）

**この機構は既にリポジトリ内に文書化されている（未修正のまま）** ☆ —
`GT_esmini/config/manual_drive_realwheel_split.json` の `_why_override_disabled` に
「override 機構を有効にすると ManualDrive は AUTO で始まり、**オーバーライドのラッチが発火するまで
物理ホイールは何もしない**」と明記されている。分割構成では config で回避したが、
**丸ごと引き渡し構成では回避されていない**。

**なぜテストで見つからなかったか** ☆
MD を使う検証シナリオは全て `manual_drive_headless_stub.json` / `manual_drive_realwheel_split.json` を
使っており、いずれも `override.enabled = false`。これは両ドメインを恒久 MANUAL に固定するので、
**この早期 return を一度も踏まない**。実機の既定 config だけが踏む経路になっている。

---

### 症状1: ステアのボタンで VD に復帰できない

**やさしい説明**

VD を「横も縦も false」で降ろすと、esmini は **VD の `Step()` を呼ばなくなる**。
ところが AUTO_RESUME ボタンを見張っているコードは **VD の `Step()` の中にしかない**。
つまり降りた瞬間に**見張り番が居なくなる**。片道切符である。

さらに二重に塞がっている。MD 側にはボタンを共有バスに載せる仕組みがあるが、その処理は
症状3の早期 return の**後ろ**にあるので、AUTO の間はボタンがバスに載らない。

**コード根拠**

- 非活性コントローラは `Step()` を呼ばれない ★（`ScenarioEngine.cpp:260-269` の `Active()` ゲート、
  `Active()` = `active_domains_ != 0`）。設計文書の「確認済み事実D」と同じ。
- `ActivateControllerAction lateral=false longitudinal=false` は `Deactivate()` を呼ばず、
  基底 `Activate()` がビットを落とすだけ ☆（`OSCPrivateAction.cpp:849` → `Controller.cpp:115-135`）
- AUTO_RESUME 立ち上がりエッジの判定は **`OverrideManager::Update()` の中だけ** ☆
  （`OverrideManager.cpp:279-307`）。呼び出し元は `ControllerVirtualDriver::Step()` と
  `ManualDriveCoordinator::RunFrame()` の2箇所のみ。
- `PublishDeviceButtons` は `ManualDriveCoordinator.cpp:103` ★ ＝ 早期 return（`:46`）**より後ろ**
- **再活性化の手段がシナリオアクション以外に存在しない** ☆
  （`GT_esminiLib.hpp` の VD 系エクスポートは `GT_GetVirtualDriverTelemetry` のみ）

**補足**: 既存の `vd_resume_*` / `f7_reverse_split_latch_probe.py` が扱う "resume" は
`OverrideManager` のラッチ復帰であって、**`ActivateControllerAction` によるドメイン再活性とは別機構**である ☆。
分割構成では VD が Active のままなので前者だけで足りたが、丸ごと引き渡しでは後者が要る。

---

### 症状2: OSI にステア角・エンジン回転数が出ない

これは**2つの面**に分けないと話が噛み合わない。

#### 面1: OSI GroundTruth（`.osi` トレース / UDP 48198）

- **`steering_wheel_angle` フィールドは OSI 側に実在するが、誰も書いていない** ☆
  （`set_steering_wheel_angle` の呼び出しはリポジトリ全域で0件。upstream も書いていない）
  → **VD 固有ではなく、全コントローラで出ない。**
- **rpm / gear / ペダル開度は `osi3::MovingObject` に受け皿そのものが無い** ☆。OSI 仕様上 HostVehicleData の専管。
- タイヤ舵角に相当するのは `wheel_data[axle==0].orientation.yaw` のみ ☆。

#### 面2: OSI HostVehicleData（UDP 48199）← ステア角・rpm が出る唯一の出口

こちらは実装済みで、**VD も MD も同じ経路で同じフィールドを埋めている** ☆
（`ControllerVirtualDriver.cpp:1466-1506` と `ControllerManualDrive.cpp:256-309` は同一実装）。
つまり「VD が書き漏らしている」という仮説は**棄却**された。

**では何が壊れているか — データ源の選び方** ★

```cpp
// GT_esmini/src/core/GT_esminiLib.cpp:1481-1495（要約）
Controller* ctrl = egoObject->GetController("RealDriverController");
if (!ctrl) ctrl = egoObject->GetController("PythonDriverController");   // 条件コンパイル
if (!ctrl) ctrl = egoObject->GetController("ManualDriveController");    // ← MD が先
if (!ctrl) ctrl = egoObject->GetController("VirtualDriverController");  // ← VD は後
```

- `Object::GetController(name)` は **`Controller::GetName()`（xosc の `<Controller name="...">` 属性）と
  文字列比較する。型では引かない。活性状態も見ない** ★（`Entities.cpp:291-299`）。

ここから2通りの壊れ方が出る。**どちらに当たっているかはユーザーのシナリオ次第**:

| ケース | 何が起きるか |
|---|---|
| **(A) コントローラ名をクラス名にしている場合**（`ManualDriveController` / `VirtualDriverController`） | 引き渡しシナリオには MD と VD が両方居るので、**VD が運転している最中でも常に MD が選ばれる**。MD は症状3の早期 return で `current_hvd_` を一度も埋めないため、**ステア角は固まり rpm は 0**。rpm/torque が両方 0 だと `motor` エントリごと作られない ☆（`GT_HostVehicleReporter.cpp:357`）ので、受信側からは「フィールドが存在しない＝出てこない」に見える。 |
| **(B) コントローラ名を短縮している場合**（リポジトリの `scenario_full_handover_vd_to_md.xosc` は `VD` / `MD`）★ | 名前一致が**両方失敗**し、`HVDEstimator` の推定値にフォールバック ☆。同時に **VD テレメトリ UDP / JSONL / `GT_GetVirtualDriverTelemetry` も全部沈黙する** ☆（同じ名前引きを使っているため）。 |

**(B) はリポジトリの引き渡し検証資産そのものが踏んでいる** ★ —
`scenario_full_handover_vd_to_md.xosc:87,95` が `<Controller name="MD">` / `<Controller name="VD">`。
**この資産では計器が最初から死んでいる。**

---

## 3. Step 0 — 着手前に「1回の実行」で帰属を確定させる

**修正の前に必ずこれをやること。** 上の (A)/(B) や、症状3の2つの経路は、
1回の走行で同時に見分けられる。ここを飛ばすと「直したつもり」で終わる（[[verification_instrument_fidelity]]）。

引き渡しシナリオを `--log_level debug` ＋ `--csv_logger out.csv` で走らせ、引き渡し時刻の前後を見る。

| 見るもの | 判定 |
|---|---|
| `ManualDriveController[..]: ownership ... integrator=..` が毎フレーム出るか | 出る → MD は Active。**症状3 = 早期 return で確定**。出ない → MD が活性化できていない（upstream 欠陥A 経路） |
| csv の速度が引き渡し後**完全に一定**か | 一定 → defaultController が唯一の駆動源。惰行減速していれば MD が積分している |
| `GT_GetVirtualDriverTelemetry` / VD テレメトリ UDP が**空か** | 空 → ケース (B)（名前引き失敗）。出る → ケース (A) |
| HVD の `motor` 配列が空か、`steering_angle` が固定値か | どちらも → HVD のデータ源が休眠中の MD になっている |
| `.osi` の `vehicle_attributes.steering_wheel_angle` | 常に無い（全コントローラ共通の未実装）。**ここを期待していたなら症状2は面1の話** |

**計器の欠落（先に塞ぐと以降が楽になる）**: `ManualDriveCoordinator.cpp:46` の早期 return は**無言**である。
ここに1行ログ／テレメトリ1フィールドを出すだけで、この型の症状は今後即断できる。

---

## 4. 対策プラン

### 設計方針（1文）

**「いま実際にこの車を運転しているのは誰か」を `DomainOwnershipLedger` 1か所に集約し、
出力（OSI/テレメトリ）も復帰も全部そこを参照する。名前の決め打ちと暗黙の前提をやめる。**

すべて `GT_esmini/` 内で閉じ、**R1 Clean Core（`EnvironmentSimulator/` 無改変）に抵触しない**。

---

### 修正1【症状3】MD の「シナリオに委譲」を、委譲先が居るときだけにする

`ManualDriveCoordinator.cpp:46` の早期 return が本体。**2案あり、判断が要る**（§5-1）。

**案1-a（推奨）— 引き渡しを受けた瞬間に MANUAL で始める**

`ControllerManualDrive::Activate()` に「**走行開始後に** INACTIVE→ACTIVE へ遷移し、
かつ全ドメインの所有者になった」＝ 引き渡しの検出を入れ、そのときだけ
`OverrideManager` を MANUAL で開始する（既存 `RequestAutoMode()` の対になる
`RequestManualMode()` を新設 — 現状 `OverrideManager` の公開 API に無い ★）。

- 接合点は既にある ★: `ControllerManualDrive::Activate()` は `Controller::Activate(mode)` の
  戻り後に `Claim()` し、`was_domain_integrator_` を種付けしている（`ControllerManualDrive.cpp:224-245`）。
  同じブロックで「引き渡しか否か」を判定できる。
- 物理バックエンドは同関数の `SetInitialState()` で現在位置・速度に再シード済み ★
  （`:198-203`）なので、走行中に引き継いでも飛ばない。ギアも速度から seed される ☆。
- **意味論**: 「シナリオが明示的に人へ渡した ＝ 人が握っている」。テイクオーバーの定義そのもの。
- **既存挙動への影響が小さい**: Init から MD が居るだけの通常シナリオ（シナリオが走らせて人が割り込む）は
  Init 時活性なので判定に当たらず、AUTO 開始のまま。

**案1-b — 委譲先の有無で早期 return を絞る**

`:46` の条件に「委譲先が存在する」を足す（台帳で MD が全ドメイン唯一の所有者なら委譲先なし → 委譲しない）。
AUTO のままでも物理を回す（入力ゼロ ＝ スロットル0／ブレーキ0）ので惰行減速＝エンジンブレーキが効く。

- より一般的だが、**Init から MD 単独のシナリオの既定挙動を変える**（従来はシナリオに委譲していた）。
  回帰の射程が案1-a より広い。
- `split_active` の既存例外はこの一般則の特殊形として吸収できる。

**どちらでも必須の付随作業**
- 早期 return したことを**観測可能にする**（ログ or テレメトリ1フィールド）。
- AUTO のまま物理を回す場合、`:60-63` の「lateral が scenario 制御なら `cmd.steering = 0`」を
  引き渡し後にどう扱うか決める（人のハンドル位置をそのまま使うのが正しい）。

**採らない案**: 既定 `manual_drive.json` を `override.enabled=false` にする回避策。
オーバーライド機構そのものを殺すので、走行中の割り込みができなくなる。

---

### 修正2【症状1】ボタンでの VD 復帰経路を作る

**案2-b（推奨）— MD 側で AUTO_RESUME を検知してピアの VD を再活性化する**

MD は活性なので毎フレーム `Step()` される。**症状3の早期 return より前**で
`OverrideManager::JustPressedResume()`（既存 ★ `OverrideManager.hpp:146`）を見て、
同一オブジェクトの VD を引き当てて `Activate({ON,ON,UNDEFINED,UNDEFINED})` を呼ぶ。

**実装上の必須注意**

1. **VD を上げるだけでは MD が降りない。** upstream 欠陥A（現職ではなく新参に `DeactivateDomains` を
   呼ぶ）☆ により現職は自動で降りない。**MD 側で明示的に自分のドメインを降ろし、台帳を更新する**
   （`ControllerManualDrive::DeactivateDomains()` が `Claim()` で再主張する既存経路 ★ を使う）。
2. **`Activate()` の多重 `Init()` 問題。** `ControllerVirtualDriver::Activate()` も
   `ControllerManualDrive::Activate()` も `input_source_->Init()` を無条件に呼ぶ ★
   （`ControllerManualDrive.cpp:206`）。実機 SDL2 では joystick/haptic の再オープンと
   effect の孤児化が起きうる ☆。**再活性化の実装と同時に多重 Init ガードを入れること。**
   これを飛ばすと「復帰はするが FFB が壊れる」に化ける。
3. **VD 再活性時の状態**: `SetUpControlOutputs()` が `control_outputs_released_` を戻す ☆。
   物理バックエンドは `SetInitialState` で再シードされるので走行中でも飛ばない（要実測確認）。
4. **配列の添字に注意** ☆: `Activate` の引数配列は `ControlDomains` 添字（**LONG=0, LAT=1**）。
   `AssignControllerAction` の `activateLateral/Longitudinal` は upstream 欠陥B で横縦が入れ替わるので
   **使わない**。GT 側から直呼びする場合は添字を明示すること。

**代替案（採らなかった理由つき）**

- **案2-a: `GT_Step` に resume watcher を置く** — `Active()` に依存しない唯一のフレームフック
  （`TrafficSignalControllerManager` などと同格）☆。横断的で確実だが、**ボタンの供給源を別途持つ必要がある**。
  MD が既にデバイスを持っているので案2-b の方が短い。将来 MD 不在構成が要るなら再検討。
- **案2-c: VD の `operating_domains_` に ANIM を足して活性を維持する** — `Step()` は回り続けるが、
  `TearDownControlOutputs()` の発火条件（`goes_inactive`）が成立しなくなり、
  **FFB が解放されない回帰を作る危険がある** ☆。`ControllerVirtualDriver.cpp` のコメントが
  その危険を明記している。採らない。

**併せて**: `PublishDeviceButtons` / `PublishDeviceAxis` を早期 return より**前**へ移す（修正1 で
早期 return 自体が変わるなら自然に解決するが、順序として明示しておく）。

---

### 修正3【症状2】OSI/テレメトリのデータ源を「実際の運転者」にする

**3-a（必須）— 名前引きを型引きに変える**

`Object::GetController(name)` は名前一致 ★。GT 側に **型で走査する free helper** を作り
（`Entities.hpp` への追加は R1 抵触なので不可 — [[osi_assigned_lane_driving_fix]] と同じパターン）、
以下を全部置換する ☆:

- `GT_esminiLib.cpp:1481-1495`（HVD の入力収集元）
- `GT_esminiLib.cpp:1647`（VD テレメトリ UDP）
- `GT_esminiLib.cpp:1699`（テレメトリ JSONL）
- `GT_esminiLib.cpp:1944`（`GT_GetVirtualDriverTelemetry`）
- `HeadingCorrectionManager.cpp` / `VehiclePhysicsManager.cpp` のスキップ判定
  （後者は `CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME` がスキップ一覧に無い ☆ — 別バグとして要確認）

これで xosc のコントローラ名に依存しなくなる。**この修正を最初にやると、以降の検証が可能になる。**

**3-b（必須）— 優先順の決め打ちをやめる**

HVD のデータ源を「名前の優先順で最初に見つかった1つ」から
**`DomainOwnershipLedger::IntegratorOf(obj)`（＝実際に積分している人）** に変える ☆。
台帳が空なら `Object::GetControllerActiveOnDomain(DOMAIN_LONG)`（upstream の既存 helper ★
`Entities.cpp:270-273`）にフォールバック。

**3-c（任意・面1を埋める）— OSI GroundTruth に `steering_wheel_angle` を出す**

`GT_esmini/src/osi/GT_OSIReporter_Moving.cpp` は GT 側フォーク実体なので自由に書ける ☆。
値は `Object::GetWheelAngle()`（既存 getter ★、upstream への追加不要）× ステアリングギア比。
比は `host_vehicle_config.json` の `steering_input_to_wheel_ratio`（既定 12.9）を流用。

> ⚠ **単位・意味論の罠**: OSI の当該フィールドは**ハンドル角**の定義。`wheel_angle_` は**タイヤ角（radian）**。
> そのまま入れると 12.9 倍の誤りになる（過去に同型の誤診がある — [[verification_semantics_lesson]]）。
> **どちらの角か**をコード上のコメントで固定し、テストで比を検査すること。

**3-d — rpm/gear は HVD 面が正**

`osi3::MovingObject` に受け皿が無く ☆、proto の拡張は R1 抵触で
[`osi_telemetry_extension_decision.md`](osi_telemetry_extension_decision.md) が既に棄却済み ☆。
**コンシューマ側を HVD 面（UDP 48199）に向けるのが正攻法。** 面を跨いだ要求が来ている場合は
「どちらの面を見ているか」をユーザーと合意してから着手すること。

**3-e — 沈黙する計器を塞ぐ**

MD が早期 return している間 `SetBaseHostVehicleData()` が呼ばれず ★、
HVD は**古い値を送り続ける（送信自体は成功する）** ☆。修正1 で解消するが、
「更新されていない」ことが受信側から判るようにするか、少なくともログに出すこと
（[[silent_instrument_and_tolerance_creep]] と同型）。

---

### 修正4【検証】症状を検出できるゲートを作る

**現状のカバレッジ穴** ☆:
- 回帰ゲート Step 2.8 が保証しているのは「VD が手放し、手放したままか」（`vd_control_relinquished`）**1ブールだけ** ★。
  **誰が引き継いだか / 制御を返せるか / 出力が遷移を生き延びたか** は一切見ていない。
- `scenario_full_handover_vd_to_md.xosc` は expectations もバッチ所属も無い ☆。
  しかもコントローラ名が `VD`/`MD` なので**計器が死んでいる** ★。
- MD を使う検証資産は全て `override.enabled=false` ☆ ＝ **症状3の経路を一度も踏まない**。

**やること**

1. `scenario_full_handover_vd_to_md.xosc` のコントローラ名を**クラス名に揃える**
   （`VD` → `VirtualDriverController`、`MD` → `ManualDriveController`）。
   `objectControllerRef` の値も同時に直す（`:140,:159,:177` ★）。
   ※ 修正3-a を入れれば名前依存は消えるが、**資産側も規約に揃えておく**（howto §付録A の指示どおり）。
2. **`override.enabled = true` の MD config を使う引き渡しシナリオを1本追加**。
   これが無い限り症状3は永久にゲートを通過する。
3. expectations を付けて `resources/xosc/verification/scenario_handoff_batch.yaml` に載せる
   → 回帰ゲート Step 2.8 と CI `gate:scenario-handoff-regression` で回る ☆。
4. **新 matcher**（`GT_esmini/web/backend/services/vd_metrics.py`）:
   - `control_taken_over` — 引き渡し後、速度が**等速でない**こと（＝ defaultController に落ちていない）。
     惰行減速 or 入力への応答を検査。既存 `domain_split_holds` の「travelled/reported 比」と同じ流儀。
   - `control_returned_to_vd` — `vd_active` が false → **true に戻る**遷移が AUTO_RESUME 後に起きること。
     **既存の `vd_control_relinquished` は「戻ったら fail」なので併用不可** ★
     （`vd_metrics.py:956-964`）。復帰シナリオには別 matcher を当てること。
5. **ヘッドレスプローブ**: AUTO_RESUME を UDP で注入して往復を確認する。
   `GT_esmini/test/headless/f7_reverse_split_latch_probe.py` の合成入力パターンを流用できる ☆。
   ただし**そのプローブが検証しているのはラッチ復帰であってドメイン再活性ではない**ので、
   `vd_active` の false→true を直接見ること。
6. **OSI を見る検証が現状ゼロ** ☆（`scenario_handoff_batch.yaml` は `osi: false`、
   headless プローブは OSI を一切読まない）。症状2 を恒久ゲートに載せるなら OSI 面の観測を
   1本足す必要がある。**スコープ判断が要る**（§5-3）。

---

### 修正5【並行して直すべき隣接バグ】

調査中に見つかった、今回の症状に直接は効かないが同じ構造の問題 ☆。**別コミットに分けること。**

- `VehiclePhysicsManager` のスキップ一覧に `CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME` が無い
  → VD 制御車のピッチ/ロールが毎フレーム上書きされている可能性。
- upstream 欠陥C（新規発見）: `OSCPrivateAction.cpp:2104` の `LatDistanceAction::Step` が
  `ControlDomains::DOMAIN_LAT`（値1）を `ControlDomainMasks` として渡しており、
  **横アクションが縦ドメインを問い合わせている**。R1 により GT では直せない → upstream issue 候補。
- `Entities.cpp:187-205` の `IsControllerModeOnDomains` は先頭一致で打ち切り、
  型フィルタが `GetType() == GetType()` で常に真＝死んでいる。R1 により観測のみ。

---

## 5. 判断が要る点（実装前に決めること）

### 5-1. 修正1 は案 a（引き渡し時 MANUAL 開始）か案 b（委譲先の有無で判定）か

- **推奨は案 a**。既存挙動への影響が局所で、意味論（テイクオーバー＝人が握っている）が明快。
- 案 b は一般性で勝るが、Init から MD 単独のシナリオの既定挙動を変えるため回帰の射程が広い。
- **両方入れる選択もある**（案 a を既定、案 b は `split_active` の一般化として）。その場合は
  回帰ゲートで deviation=0 を必ず確認する。

### 5-2. 復帰後の「握り」の扱い

VD へ復帰した直後、運転者がまだハンドルを握っている（軸が中立でない）と
**即座に再ラッチして MANUAL へ戻る**。これは F7 で仕様として確定済みの正当挙動 ☆
（[[f7_override_detector_findings]] §1: 直接軸経路のしきい値 0.05 axis-frac ＝ ハンドル 22.5°、無デバウンス）。
Web の Resume ボタンは押下時に入力ゼロ化を同時送信して回避している ☆。
**実機ボタン復帰でも同じ配慮が要るかを決めること**（押下フレームの抑制だけで足りるか、
数フレームの猶予が要るか）。ここを決めずに実装すると「押しても戻らない」に見える。

### 5-3. 症状2 の「OSI 出力」がどちらの面か

- **GroundTruth 面**（`.osi` / 48198）を見ているなら → ステア角は修正3-c が必要、rpm は面1に受け皿が無い。
- **HostVehicleData 面**（48199 / Web の HVD ゲージ）を見ているなら → 修正3-a/3-b で解決する。

**Step 0 の観測で1回で確定する。** 面が違うと作業量が数倍変わるので、着手前に確定させること。

---

## 6. 実施順序（依存関係つき）

| # | 作業 | 理由 |
|---|---|---|
| **0** | Step 0 の観測（§3）。ビルド不要な部分から | 帰属を確定させないと「直したつもり」になる |
| **1** | **修正3-a（型引き）＋ 早期 return の可観測化** | **計器を先に直す。** これが無いと以降の修正を検証できない |
| 2 | 修正1（MD が動かない） | 症状として最も明確。単独で完結する |
| 3 | 修正2（復帰経路）＋ 多重 Init ガード | 修正1 の後でないと「戻った後どうなるか」が観測できない |
| 4 | 修正3-b（データ源＝積分器）／3-c（OSI ステア角） | §5-3 の確定後 |
| 5 | 修正4（検証資産・matcher・ゲート搭載） | 実装と同格。**これを省くと同じ穴が再発する** |
| 6 | 修正5（隣接バグ・別コミット） | 独立 |
| 7 | `/gates -FailOnBehavioral` で deviation=0 確認 | ビルドを要する。**着手前にビルド排他を確保すること** |
| 8 | 実機 G29 で1回確認 | ヘッドレスは FFB の実力抜けを担保できない（SDLFFBSink 非コンパイル）☆ |

---

## 7. 踏んではいけない罠（既知の事故から）

本調査で洗い出した罠は [`control_ownership_pitfalls.md`](control_ownership_pitfalls.md) §1 と §4 に集約した。

---

## 7.5 追記（2026-07-31）— 往復（VD→MD→VD）を実機で成立させる過程で見つかった欠陥3件

**症状①の修正が入ったあと、「奪い返せるようにする・VD 側も FFB あり」を実際に組もうとして
判明した。いずれもコード読解で確定、ヘッドレスでは原理的に検出できない**
（`SDLFFBSink` は `GT_ENABLE_SDL2` ビルドでしかコンパイルされない）。**修正済み。**

| # | 欠陥 | 症状 | 修正 |
|---|---|---|---|
| **D1** | `SDLFFBSink::SetEnabled(true)` の呼び出しが**コードベース全体で0件**だった。teardown の `SetEnabled(false)` は `enabled_` を落とし、`Update()` は `if (!haptic_ \|\| !enabled_) return;` で早期 return する。**復元する経路が無い。** | 一度でも非活性化されたコントローラは、再活性化しても**力が二度と出ない**（プロセス終了まで）。「復帰はするがハンドルが死んだまま」 | `ControllerVirtualDriver::SetUpControlOutputs()` と `ControllerManualDrive::Activate()` で `SetEnabled(true)` を実行（teardown と対にした） |
| **D2** | `ControllerVirtualDriver::SetUpControlOutputs()` が `input_source_->Init()` を**無条件**に呼ぶ。多重呼び出しガードが無い（設計文書の事実H）。Codex の修正は MD 側にだけ入っていた | 復帰のたびに joystick/haptic を再オープン。旧 effect ID が孤児化し、`SDL2WheelInput::Init` の**軸整定ループ（最大500ms）がフレーム内で再実行される**＝復帰の瞬間にシミュレーションが止まる | VD 側にも `input_source_initialized_` ガードを追加（MD と同型） |
| **D3** | `ControllerManualDrive::DeactivateDomains()` が FFB を解放していなかった。解放は `Deactivate()` にしか無いが、**per-domain 解放はそこを通らない** | AUTO_RESUME で MD が降りた瞬間、MD の spring/damper effect が**無限持続のままデバイスに残留**し、更新も停止もされない。そこへ VD のサーボが再武装して**残留力の上に重なる** | `ReleaseFfbOutputs()` に切り出し、`losing_lateral \|\| goes_inactive` で解放（`ControllerVirtualDriver::DeactivateDomains` と対称） |

**D3 は VD 側が既に解いていた問題の鏡像である。** VD の `DeactivateDomains` には
その危険を長文コメントで警告した override があるのに、**MD 側には無かった。**
片側だけ直した対称バグは、構成が反転するまで露出しない。

### 併せて確定した既知の制約（未修正・仕様として運用する）

4件を確定させた。復帰1フレーム目の再ラッチ、VD と MD の二重オープンが未検証であること、
`ffb.disable_non_realtime` が死にキーであること、VD テレメトリの `sim_time` が復帰後にずれることである。
内容と運用は [`control_ownership_pitfalls.md`](control_ownership_pitfalls.md) §5 にまとめた。

`sim_time` のずれは実測値を持っている。
移管 t=15.05 で凍結し、t=22.35 の復帰後、最初のフレームが **15.10** を報告した。
VD は内部で `sim_time_ += dt` を積算しており、非活性中は進まないためである。
修正するなら VD 側でシナリオ時刻を受け取る形にする。

## 8. 未確認事項

- 本プランは**全てソース読解**に基づく。プローブもゲートも1本も実行していない。
- ユーザーの実シナリオのコントローラ命名・`override.enabled` の値は未確認（Step 0 で確定する）。
- VD 再活性時の `input_source_->Init()` 二重呼び出しが実デバイスで何を起こすかはコード根拠のみ・実行未確認。
- `OSIReporter::UpdateOSIGroundTruth` の内部（`pos_` を再読するかキャッシュか）は未追跡。
