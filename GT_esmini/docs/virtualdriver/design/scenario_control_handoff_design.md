# シナリオからの制御終了と手動運転への移管（feature:F7）— 実装プラン

**状態**: **実装済み**（`73e4dccb`。回帰ゲート Step 2.8 と CI `gate:scenario-handoff-regression` で稼働中）。
以下は実装時の判断台帳（§4 の R-1〜R-5）と、確認済み事実（§1）の記録である。
**方式**: OpenSCENARIO 標準 `ActivateControllerAction`。独自アクションは採らない。
**制約**: R1 Clean Core（`EnvironmentSimulator` / `OSMP_FMU` 無改変）、既定挙動不変、回帰ゲート deviation=0 維持、新パラメータは config + Web GUI まで露出、検証はヘッドレス恒久テスト。

---

## 1. 確認済み事実

### 1.1 一次確認済み（本ワーカーがコードを直接読んで確認）

| # | 事実 | 根拠 |
|---|---|---|
| A | `ActivateControllerAction` に `lateral="false" longitudinal="false"` を書いても **`Controller::Deactivate()` は呼ばれない**。常に `controller_->Activate(mode)` が呼ばれ、基底が OFF ドメインのビットを落とすだけ。 | `OSCPrivateAction.cpp:849`、`Controller.cpp:115-135` |
| B | GT の `ControllerVirtualDriver::Activate()` は引数 `mode[]` を**一切見ずに** `physics_backend_->Init/SetInitialState`・`input_source_->Init(io_config_)`・`VehicleLightExtension` 登録を無条件実行してから基底を呼ぶ。 | `ControllerVirtualDriver.cpp:202-229` |
| C | `ControllerVirtualDriver::Deactivate()` は `LOG_INFO` + 基底呼び出しの4行スタブ。 | `ControllerVirtualDriver.cpp:231-235` |
| D | **非活性のコントローラは `Step()` を呼ばれない**。`Active()` は `active_domains_ != DOMAIN_MASK_NONE`。 | `ScenarioEngine.cpp:260-269`、`Controller.hpp:112-115` |
| E | `ActivateControllerAction` は `PrivateAction`。`Start()` は即 `End()`（1フレーム完結）。Story/Act/ManeuverGroup 内で使え、対象は `<Actors>` で決まる。 | `OSCPrivateAction.hpp:1365`、同 `.cpp:856-857` |
| F | GT では既に活性側で常用中（検証シナリオ約40本、`simulation_runner.py:142-154,236-250`、`vd_diag.py:48`）。非活性化の用例は GT 側に無い。 | 同上 |

> **行番号についての注意**: `ControllerVirtualDriver.cpp` は合流軌道ワーカーが並行編集中（`git status` で `M`）。本書の行番号は執筆時点のスナップショットであり、実装時にはずれている可能性がある。実際、subagent が `:434` と報告した `ffb->Update()` は再確認時点で `:568` にあった。**行番号ではなくシンボル名で参照すること。**

> **この節は本作業の確認記録である。** 移管まわりで恒久的に効く事実と罠は
> [`control_ownership_pitfalls.md`](control_ownership_pitfalls.md) に集約してある。
> これから触る人はそちらを先に読むこと。

### 1.2 一次確認済み（当初 subagent 報告 → 本ワーカーが再確認）

- **G**: `SDLFFBSink` の `Update()` は VD の `Step()` 内からしか呼ばれない（`ControllerVirtualDriver.cpp` の `ffb->Update(hvd, timeStep)`）。`ffb_sink_.Close()` の呼び出しは `SDL2WheelInput.cpp:247` の1箇所のみ（Shutdown/デストラクタ経路）。**一次確認済み。**
- **G2（新規・設計を単純化する発見）**: `IFFBSink` インタフェースに **`SetEnabled(bool)` が純粋仮想として既にある**（`IFFBSink.hpp:70`）。`SDLFFBSink::SetEnabled(false)` は `SDL_HapticStopAll(haptic_)` を実行する（`SDLFFBSink.cpp:351-358`）。さらに `SilenceDevice()`（`SDLFFBSink.cpp:500-506`）と `SetSteerTarget(target, active)`（`IFFBSink.hpp:79`）も既存。**つまり teardown に必要な原語は全て既にインタフェース上にあり、新しい仮想関数を足す必要がない。**
- **I**: `OverrideManager::RequestAutoMode()` は宣言（`OverrideManager.hpp:24`）・定義（`OverrideManager.cpp:730`）ともに存在するが、**呼び出し箇所はコードベース全体でゼロ**。**一次確認済み。**
- **H**: `input_source_->Init()` に多重呼び出しガードが無い。2回目で SDL joystick/haptic を再オープンし、旧 effect ID を上書き（破棄せず孤児化）、`LiveSinks()` に同一 sink を重複登録する。
- **I**: `OverrideManager` の全状態リセットは `Configure()` のみで、これは**コンストラクタでしか呼ばれない**（`ControllerVirtualDriver.cpp:162`）。`RequestAutoMode()` は実装済みだが**どこからも呼ばれていない**。
- **J**: `bf03fceb`「異常終了でも haptic 解放」は `scripts/ffb_spike/` の無人ソーク用 Python スーパーバイザのみで、製品コード変更なし。C++ から呼べない。別系統としてプロセス内緊急解放（`RegisterEmergencyRelease` / `SilenceDevice` = `SDL_HapticStopAll`、`SDLFFBSink.cpp:392-536`）が既存だが、**プロセスクラッシュ時のみ**発火。
- **K**: `GT_ENABLE_SDL2` は既定 OFF（`GT_esmini/CMakeLists.txt:52`）。`SDLFFBSink` は実機/配布ビルドでのみコンパイルされる。ヘッドレスは `HeadlessFfbInput`（SDL 非依存、Linux CI 可）。
- **L**: VD テレメトリには既に `override.*`、`ffb.target_active`、`ffb.gates.*` が出ている。非活性化すると `Step()` が止まるので `telemetry.sim_time` が凍結する一方、`GT_GetVirtualDriverTelemetry` は凍結値を返し続ける（`Active()` を見ていない）。
- **M**: 回帰ベースラインは (manifest, baseline) の独立3組。**新規は別マニフェスト＋別 baseline で追加でき、既存3組に触れずに済む**。

---

## 2. PM 確認項目への回答

### 確認項目① FFB サーボと介入検出は正しく止まるか → **止まらない。力が残る。**

正確には「回り続ける」のではなく「**止める処理が実行されないまま凍結する**」。事実 D により `Step()` が呼ばれなくなり、`SDLFFBSink::Update()` も止まる。その結果、**直前フレームの合成トルクを積んだ `SDL_HAPTIC_INFINITY` の CONSTANT effect がハードウェア上にその値のまま残留する**（事実 G）。`Deactivate()` は止める処理を持たない（事実 C）。target-track サーボ稼働中に非活性化されると、PID 出力を含む最後の指令値でホイールに力が固着する。

**今日ユーザーが踏んだ「異常終了時に力が残る」と同型の危険**であり、しかも今回は異常終了ですらない正常系で起きる。既存の緊急解放（事実 J）はプロセスクラッシュ時にしか発火しないため救済にならない。**これが本実装の第一の修正対象。**

介入検出も同じ理由で止まる（`OverrideManager::Update()` は `Step()` 内）。安全上は「止まる」で正しいが、**状態がリセットされずに凍結したまま次の再活性化へ持ち越される**（事実 I）。

### 確認項目② 再 Activate 時の初期化は「即オーバーライド判定」と同じ経路を踏むか → **経路は共有する。判定の成否は未確定。**

事実 A+B により、非活性化でも再活性化でも `Activate()` の初期化群が走る。よって「開始時に舵角が中立でない」状態から入る条件は**再活性化のたびに再現する**。

一方、その条件が即ラッチに至るかは**確定していない**。subagent は「残差検出のブートストラップ（初回サンプルでシャドウを実測軸にシード）が `suppress` として働くため、1点だけでは即ラッチしない」と報告している（`OverrideManager.cpp:355-385`）。しかし**ユーザーは実機で即オーバーライド判定を実際に観測している**。コード読解と実観測が食い違っている以上、コード側の説明を採ってはならない。**検出器側ワーカーの診断結果が出るまで、この項目は未決とする。**

再活性化時に初期化されない状態は以下（事実 I・subagent 表）: `OverrideManager` 全状態、`ad_envelope_state_`（アンカー）、SpeedAction ラッチ、`prev_steering_`、`sim_time_`、プランナ内部状態、指示器・ブレーキランプラッチ。逆に初期化される（＝**再活性化で飛ぶ**）のは物理バックエンドの位置・姿勢・速度と FFB サーボ PID 状態。

### 確認項目③ AUTO_RESUME ラッチとシナリオ由来の活性状態の優先順位 → **シナリオが構造的に上位。競合は再活性化時に出る。**

事実 D により、非活性中は `OverrideManager` が評価されない。つまり両者は同格で競合するのではなく、**シナリオ由来の活性状態がラッチ機械の動作可否そのものを握る**階層関係にある。「どちらが勝つか」を調停で決める必要はなく、次の順序を明文化すれば足りる:

1. シナリオの `ActivateControllerAction` が VD の活性ドメインを決める（最上位）。
2. 活性ドメイン上でのみ `OverrideManager` が評価され、AUTO_RESUME ラッチが進行する。
3. ラッチはシナリオの活性状態を覆せない（覆す手段が存在しない）。

**実際の危険は再活性化時**である。MANUAL ラッチを抱えたまま非活性化 → 再活性化すると、AUTO に戻す機会（アイドルタイマ）が非活性中は進まないため、**MANUAL ラッチのまま AD が再開する**。これを「持ち越す」か「AUTO に戻す」かは設計判断（§4 の R-2）。

---

## 3. 設計方針

事実 A の帰結として、**実装の中心は `Deactivate()` ではなく `Activate()` 側**になる。`Activate()` にドメイン遷移の判定を入れ、遷移種別ごとに処理を分ける。

```
Activate(mode[]) 内で:
  before = active_domains_（基底呼び出し前の現在値）
  after  = mode[] を before に適用した結果（UNDEFINED は現状維持）

  遷移 INACTIVE -> ACTIVE : 従来どおり初期化群を実行（＋任意でラッチ再アンカー）
  遷移 ACTIVE   -> ACTIVE : 初期化群をスキップ（事実 H の二重 SDL オープン/孤児 effect を防ぐ）
  遷移 ACTIVE   -> INACTIVE: 初期化群をスキップし、teardown を実行
  遷移 INACTIVE -> INACTIVE: 何もしない
```

`Deactivate()` にも同じ teardown を通す。`Deactivate()` は OSC 1.2 以下の競合解消経路（`OSCPrivateAction.cpp:843`）から到達しうるため、両経路を1つの teardown 関数に収束させる。

### teardown の内容

1. **FFB の力を落とす** — 最優先。`SDL_HapticStopAll` 相当（既存 `SilenceDevice()` と同じ強さ）まで。完全な `Close()` はしない（再活性化のたびにデバイス再オープンが必要になり、事実 H の問題を悪化させるため）。
2. **target-track サーボの目標と PID 状態をクリア**し、`ffb_target_active` を false にする。
3. **介入検出を AUTO へ戻す** — 既存の未使用関数 `RequestAutoMode()`（事実 I）をここで初めて使う。挙動は §4 R-2 の判断に従う。

### 接合点 — 新しい仮想関数は不要

事実 G2 により、teardown は**既存のインタフェースメソッドだけで書ける**:

```cpp
// VD は既に IFFBSink* を取得して Step 内で Update している。同じポインタを使う。
if (auto* ffb = <既存の取得経路>)
{
    ffb->SetSteerTarget(0.0, /*active=*/false);  // IFFBSink.hpp:79（既存）
    ffb->SetEnabled(false);                      // IFFBSink.hpp:70（純粋仮想・既存）
}
```

`SDLFFBSink::SetEnabled(false)` は `SDL_HapticStopAll` を実行する既存の実装であり、デバイスを閉じないので再活性化時に再オープンが要らない（事実 H の悪化を招かない）。`NullFFBSink` / headless の合成 sink はそれぞれ既存の実装に従う。**`IInputSource` に新しい仮想関数を足す当初案は撤回する。** 追加コードが減り、既存の実績あるコードパスを使うぶんリスクも低い。

---

## 4. 判断事項（PM 承認済み）

| ID | 論点 | 決定 |
|---|---|---|
| R-1 | 非活性化時に FFB を落とすのを既定にするか | **決定: 既定 ON。**（PM 承認）既存シナリオ94本はこの非活性化経路を一切通らない（事実 F）ため既定挙動は変わらず、回帰ゲートの deviation=0 も維持される。力の残留は安全問題であり、opt-in にすると「設定し忘れ＝ホイールに力が残る」が既定になってしまう。**config パラメータは追加しない**（常時有効）。 |
| R-2 | 非活性化時に介入ラッチをどう扱うか | **決定: 非活性化時に AUTO へ戻す。**（PM 承認）シナリオが制御を手放した時点でラッチの意味が失われるため。既存の未使用関数 `OverrideManager::RequestAutoMode()`（事実 I）を teardown から呼ぶ。これにより再活性化時に古い MANUAL ラッチを引きずらない。**config パラメータは追加しない**（常時有効）。 |
| R-3 | 冗長な再 Init（ACTIVE→ACTIVE）の抑止を今回入れるか | (a) 入れる (b) 別件に切り出す → **(a)**。事実 H は本機能を入れると発火頻度が上がるため、同時に塞ぐのが筋。ただし挙動変更なので回帰ゲートで確認する。 |
| R-4 | `objectControllerRef` を使うか | (a) 全面的に使う（既存シナリオを OSC 1.3+ へ引き上げ） (b) **新規シナリオのみ 1.3 宣言で使い、既存は触らない** (c) 省略して `controllers_.back()` に委ねる | **(b)**。影響評価の結果は下記。 |

**R-4 の影響評価（実測済み）**: `ActivateControllerAction` を使う xosc は **94本**（当初「約40本」と報告したのは検索件数の上限による過少計上。訂正する）。`revMinor` の分布は **0 が11本、1 が68本、2 が6本、3 が9本** — すなわち **85/94（90%）が OSC 1.3 未満**で、そこでは `objectControllerRef` が黙って無視される。`objectControllerRef` / `controllerRef` を既に使っているシナリオは1本のみ。
既存85本を 1.3 へ引き上げるのはパーサ挙動が版で変わる（事実 A の競合解消分岐も 1.3 で切り替わる）ため回帰リスクが大きく、本機能の範囲を超える。**新規の移管シナリオだけを 1.3 宣言で書き、既存には一切触らない**のが妥当。VD が対象オブジェクト唯一のコントローラである限り、`objectControllerRef` 省略でも `controllers_.back()` で正しく解決される点も退避路として使える。
| R-5 | upstream の 1.3+ 競合解消が「相手ではなく自分」を落としている件（`OSCPrivateAction.cpp:833-838`）への対処 | (a) GT 側は「競合相手は自動で外れない」前提で設計 (b) upstream へ issue | **(a) を採り、(b) は別途**。R1 により修正はできない。 |

---

## 5. 検証計画

### 5.1 恒久テスト（ヘッドレス、CI 搭載）

1. **新シナリオ** `resources/xosc/verification/08_handoff/scenario_deactivate_vd.xosc`
   Init で VD 活性 → Story イベントで `ActivateControllerAction lateral="false" longitudinal="false"` を発火。
2. **新マニフェスト＋新 baseline**（事実 M により既存3組は無改変）。`scripts/check_regression_baseline.py --update` で初回生成。`run_regression_gate.ps1` と `ci.yml` に既存3本と同じ形で1ステップ追加。
3. **新 matcher**（`GT_esmini/web/backend/services/vd_metrics.py`）: 「指定時刻以降 VD が制御していない」を判定。判定材料は `telemetry.sim_time` の凍結（事実 L）と `ffb.target_active` の `true→false` 遷移。C++ 変更不要。
4. **teardown 到達の検証**: `IInputSource` の停止関数が呼ばれたことをテレメトリに1フィールド出し、ヘッドレスでアサートする。
5. **ユニット**（傘バイナリ `test_ScenarioReaderParsing`）: `OverrideManager` の `RequestAutoMode()` によるラッチ復帰と、ドメイン遷移の真理値表（4遷移 × LAT/LONG）を固定する。

### 5.2 計器の限界（正直な記載）

**ヘッドレスでは「ホイールから力が抜けたこと」は検証できない。** 事実 K のとおり `SDLFFBSink` は `GT_ENABLE_SDL2` ビルドでしかコンパイルされず、ヘッドレス経路は `HeadlessFfbInput` を通る。したがって恒久テストが担保するのは「**teardown の制御フローがそこまで到達したこと**」までで、「SDL デバイス上の力が実際に落ちたこと」ではない。

- 恒久テストの主張: 非活性化で Step が止まり、teardown が呼ばれ、テレメトリ上のサーボが非活性になる。
- 恒久テストが**主張できないこと**: 実機ホイールのトルクがゼロになった。
- 補完: 実機での**一度きりの**確認を明示的に手順化して残す（無人ソークハーネス `scripts/ffb_spike/` の既存装置を使う）。これは恒久テストの代替ではなく、計器の射程外を埋める一次証拠として1回取る。

この区別を曖昧にしないこと。「ヘッドレスで緑だから力が抜けている」とは書かない。

### 5.3 deviation=0 の維持

既存シナリオは全て Init での活性化のみ（事実 F）で、`Activate()` は1回しか呼ばれない。よって INACTIVE→ACTIVE 分岐だけを通り、従来と同一の初期化群が走る。R-3 の再 Init 抑止も、2回目の Activate が存在しない以上、既存シナリオには影響しない。**この推論はゲート実行で確認するまで仮説とする。**

---

## 6. 新パラメータと露出

R-1/R-2/R-3 の判断に応じて 0〜3 個。追加する場合の経路は既存規約に従う（`VirtualDriverConfig.hpp` → `VirtualDriverConfig.cpp` の反射テーブル → `ControllerVirtualDriver.cpp` の転記 → `config/virtual_driver.json` → `virtual_driver_api.py` の型別許可リスト **と** `DEFAULT_VIRTUAL_DRIVER_CONFIG` の両方 → `client.ts` の型 → `VirtualDriverPanel.tsx` の `EDITABLE_KEYS` とウィジェット）。

既定値の散在と config 保存の罠は [`control_ownership_pitfalls.md`](control_ownership_pitfalls.md) §4 にある。

---

## 7. 実装順序

1. `ControllerVirtualDriver` に teardown 関数を切り出す（中身は既存の `IFFBSink::SetSteerTarget(0,false)` + `SetEnabled(false)`。新規仮想関数なし＝事実 G2）。
2. `ControllerVirtualDriver::Activate()` にドメイン遷移判定を導入し、`ACTIVE→INACTIVE` で teardown を呼ぶ。`Deactivate()` からも同じ teardown を呼び、両経路を収束させる。
3. `OverrideManager::RequestAutoMode()` を teardown から呼ぶ（R-2 の判断に従う）。
4. テレメトリに teardown 到達フィールドを1つ追加。
5. ユニットテスト（真理値表 + ラッチ復帰）。
6. 新シナリオ・新マニフェスト・新 baseline・新 matcher。
7. `run_regression_gate.ps1` / `ci.yml` にステップ追加。
8. config + Web GUI 露出（R-1〜R-3 の結論に応じて）。
9. ゲート実行（`/gates`）で deviation=0 を確認。**ビルドを要するのは 9 と、2 以降の動作確認。着手前に PM へビルド排他を要求する。**
10. 実機での一度きりの力抜け確認（§5.2）。

---

## 8. 残る未確認事項

- 確認項目②の結論（検出器側ワーカーの診断待ち）。
- §1.2 の G〜M は subagent 報告であり、実装時に該当箇所を開いて再確認する。
- `objectControllerRef` の OSC 1.3 依存（R-4）と、GT 既存40本のバージョン宣言への影響。
- upstream `OSCPrivateAction.cpp:833-838` が意図かバグか。
