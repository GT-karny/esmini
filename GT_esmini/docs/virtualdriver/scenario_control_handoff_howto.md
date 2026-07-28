# シナリオから VirtualDriver を降ろして手動運転へ移す — 書き方

**読者**: シナリオ（.xosc）を書く人。
**これは使い方の文書**である。なぜそう作られているかは
[`scenario_control_handoff_design.md`](scenario_control_handoff_design.md)（設計の記録）を見ること。
**設計判断はあちら、書き方はこちら**で分けている。

---

## 0. 先に結論（3行）

* `ActivateControllerAction lateral="false" longitudinal="false"` を Story の Event で撃つと VD が降りる。
* **`controllerRef` は効かない。** どの Controller に効くかは「**最後に Assign したもの**」で決まる。
* 降りた瞬間に FFB は止まり、介入ラッチは捨てられる。**再 Activate できる。**

---

## 1. 最小の例

以下は `resources/xosc/verification/08_handoff/scenario_deactivate_vd.xosc` からの
**逐語の抜粋**である（後述のとおり回帰ゲート Step 2.8 が毎回実行しているファイル）。

### 1-1. Init で VD を有効化する

`Storyboard/Init/Actions/Private`（Ego）の中に、他の Private Action と並べて置く:

```xml
<PrivateAction>
   <ControllerAction>
      <ActivateControllerAction lateral="true" longitudinal="true"/>
   </ControllerAction>
</PrivateAction>
```

### 1-2. Story の Event で VD を降ろす

```xml
<Act name="HandoffAct">
   <ManeuverGroup maximumExecutionCount="1" name="HandoffSequence">
      <Actors selectTriggeringEntities="false">
         <EntityRef entityRef="Ego"/>
      </Actors>
      <Maneuver name="HandoffManeuver">
         <Event name="Deactivate" priority="overwrite">
            <Action name="DeactivateAction">
               <PrivateAction>
                  <ControllerAction>
                     <ActivateControllerAction lateral="false" longitudinal="false"/>
                  </ControllerAction>
               </PrivateAction>
            </Action>
            <StartTrigger>
               <ConditionGroup>
                  <Condition name="DeactivateStart" delay="0" conditionEdge="none">
                     <ByValueCondition>
                        <SimulationTimeCondition value="8.0" rule="greaterThan"/>
                     </ByValueCondition>
                  </Condition>
               </ConditionGroup>
            </StartTrigger>
         </Event>
      </Maneuver>
   </ManeuverGroup>
   <!-- Act の StartTrigger / StopTrigger は元ファイルを参照 -->
</Act>
```

### 1-3. 属性はどれを使うか

| 属性 | 使うか | 備考 |
| :--- | :--- | :--- |
| `longitudinal` | **使う** | 縦方向（アクセル・ブレーキ） |
| `lateral` | **使う** | 横方向（操舵） |
| `animation` | 使わない | VD は解釈しない |
| `lighting` | 使わない | VD は解釈しない。灯火は `LightStateAction` / AutoLight 側 |
| `controllerRef` | **書いても効かない** | §5-1 を必ず読むこと |

---

## 2. 縦と横は独立に降ろせる

`lateral` と `longitudinal` は**別々のブール**で、`false` にした側だけが降りる。
実装も per-domain である（`ControllerVirtualDriver::DeactivateDomains(unsigned int domains)` が
ドメインマスクを受ける）。

```xml
<!-- 操舵だけ人へ渡し、アクセル・ブレーキは AD のまま -->
<ActivateControllerAction lateral="false" longitudinal="true"/>
```

**片方だけ降ろしたときに何が起きるか**: 降ろした側だけがシナリオ／手動側の指令に従い、
残した側は VD が出し続ける。**VD 自体は Active のままなので Step() は回り続け、
テレメトリも出続ける**（`vd_active` は全ドメインを手放したときに false になる）。

> ⚠ **未確認**: 「lateral だけ false・longitudinal は true」という**片側構成そのものを
> 実機／ゲートで走らせた記録は無い**。回帰ゲートが実行しているのは
> **両方 false**（`scenario_deactivate_vd`）と、**別コントローラによるドメイン奪取**
> （`scenario_domain_takeover_vd`）の 2 通りである。実装上は可能だが、
> **片側構成を使うなら自分のシナリオで確認すること。**

---

## 3. 降りた瞬間に何が起きるか

`ControllerVirtualDriver::TearDownControlOutputs()`（`GT_esmini/src/control/ControllerVirtualDriver.cpp`）が
以下を**この順で**行う。

| # | 起きること | 意味 |
| :-- | :--- | :--- |
| 1 | `telemetry_.vd_active = false` | **最初に**立てる。以降どこで早期 return しても「制御を手放した」記録は残る |
| 2 | `ffb->SetSteerTarget(0.0, false)` / `ffb->SetEnabled(false)` | **FFB サーボは止まる。** 力の指令は 0 になる |
| 3 | `override_mgr_.RequestAutoMode()` | **介入ラッチは捨てられる。** 手動ラッチ状態を次の活性へ持ち越さない |

**なぜ 2 が必須か**（コード内の注記より）: ScenarioEngine は**アクティブなコントローラしか
Step しない**ので、VD が降りた瞬間に `SDLFFBSink::Update()` も呼ばれなくなる。
ここで解放しないと、**デバイスは最後の指令を保持したままホイールを引き続ける。**

**haptic デバイスの解放（＝ `SDL_HapticClose`）はここでは行わない。**
デバイスは入力ソース（`SDL2WheelInput`）が所有しており、その `Shutdown()` で
**haptic → joystick の順**に閉じる。降格しただけではデバイスは開いたままで、
**力が 0 になるだけ**である。これは正しい: シナリオが再び Activate する可能性があるのに
デバイスを手放すと、開き直しに失敗しうる。

**teardown は冪等**である（`control_outputs_released_` ガード）。入口が複数あり
（`Deactivate()` / ACTIVE→INACTIVE の `Activate()` / `DeactivateDomains()`）、
上流の `Deactivate()` は `DeactivateDomains(ALL)` を呼ぶので**ここへ二重に来る**。

---

## 4. 降ろしたあと再び Activate できるか

**できる。** 同じ `ActivateControllerAction` を `lateral="true" longitudinal="true"` で撃つ。

**初期化されるもの**: 介入ラッチ（teardown 時に `RequestAutoMode()` 済み）、
FFB の目標値（0 から再開）、`control_outputs_released_` フラグ（再活性で解除）。

**優先順位**: **シナリオ指令が上位、AUTO_RESUME ラッチは従属**である。
シナリオが降ろせばラッチは捨てられ、シナリオが上げれば AUTO から始まる。
運転者のラッチがシナリオ指令を上書きすることはない。

**ドメイン遷移の真理値表はユニットテストに固定されている。**
設計文書 §5.1-5 が「`OverrideManager` の `RequestAutoMode()` によるラッチ復帰と、
ドメイン遷移の真理値表（**4 遷移 × LAT/LONG**）を固定する」と定めており、
実体は傘バイナリ `test_ScenarioReaderParsing` の
`GT_esmini/test/unit/manualdrive/test_OverrideManager.cpp` にある。
**表が要る読者はそこを見ること**（散文で二重に書くと必ず食い違うので、本文書では複製しない）。

> ⚠ **未確認**: 上記ユニットテストが固定しているのは `OverrideManager` の遷移である。
> 「シナリオ活性 × AUTO_RESUME ラッチ × ドメイン」の**全組み合わせを実走で通した記録は無い**。
> 私が実装読解で確認したのは §4 冒頭の 3 点のみ。

---

## 5. よくある間違い

### 5-1. 【最重要】`controllerRef` を書けば効く、と思う → **効かない**

このバージョンの esmini は `ActivateControllerAction` の `controllerRef` を**解釈しない**。
`parseActivateControllerAction()` は属性を読むが、活性化は毎回
`No controller name given for activation ... pick most recently assigned` をログに出し、
`object_->controllers_.back()` を採る。**GT ビルドと素の esmini.exe の両方で確認済み**
（GT のシナリオ sanitizer のせいではない）。

**⇒ 動く書き方**: 狙いたいコントローラを **`ObjectController` の並びで最後**に置く。

```xml
<!-- 動く: VD を最後に Assign する -->
<ObjectController>
   <Controller name="TakeoverController"> ... </Controller>
</ObjectController>
<ObjectController>
   <Controller name="VirtualDriverController"> ... </Controller>   <!-- ← 最後 -->
</ObjectController>
```

```xml
<!-- 動かない: ref で狙ったつもりになる -->
<ActivateControllerAction controllerRef="VirtualDriverController"
                          lateral="false" longitudinal="false"/>
```

**症状**: 意図と別のコントローラが降りる／降ろしたつもりの VD が走り続ける。
ログに `pick most recently assigned` が出ていたらこれ。

### 5-2. Init だけ書いて Story に Event を書かない

**症状**: VD は起動するが永久に降りない。
降格は**必ず Story の Event**（StartTrigger 付き）で撃つ。Init は活性化専用。

### 5-3. `priority` を `parallel` にする

例は `priority="overwrite"` である。降格は「今の制御を置き換える」意味なので
`overwrite` が素直。`parallel` にすると他 Event と同時実行になり、
**降格と同時に別の Private Action が走って挙動が読めなくなる**。

### 5-4. 降ろせば FFB デバイスも解放される、と思う

**されない**（§3）。力が 0 になるだけで、デバイスは入力ソースが持ったままである。
デバイスを完全に手放すのはシナリオ終了時（`SDL2WheelInput::Shutdown()`）。

### 5-5. `animation` / `lighting` を false にすれば灯火が止まる、と思う

VD はこの 2 属性を解釈しない。灯火は `LightStateAction` と AutoLight 側の話である。

---

## 6. 動作確認の根拠

**この文書の XML 断片は、私が組んだものではなく、回帰ゲートが毎回実行している
シナリオからの逐語の抜粋である。**

| ファイル | 何を通しているか |
| :--- | :--- |
| `resources/xosc/verification/08_handoff/scenario_deactivate_vd.xosc` | `lateral=false longitudinal=false` による**両ドメイン降格**。§1 の断片の出典 |
| `resources/xosc/verification/08_handoff/scenario_domain_takeover_vd.xosc` | **別コントローラによるドメイン奪取**（OSC v1.3 の per-domain 降格経路）。§5-1 の `controllerRef` 注記の出典 |
| `resources/xosc/verification/scenario_handoff_batch.yaml` | 上記 2 本のバッチ定義 |
| `GT_esmini/test/regression_baseline/scenario_handoff_expected.yaml` | 期待値ベースライン |

回帰ゲート **Step 2.8**（`scripts/run_regression_gate.ps1`）がこのバッチを実行し、
ベースラインと突き合わせている。**読者はこの 2 ファイルを開けば動く実例が手に入る。**

> **私が本文書のために自分で実行したもの**: **無い。**
> リリース前の最終ゲートが別担当により実行中で、ヘッドレス実行は
> OSI の UDP ポート（48198 ほか）で衝突しうるため、**意図的に走らせていない。**
> 代わりに (a) 断片をゲート実行対象ファイルからの逐語抜粋に限定し、
> (b) §3 の挙動は実装（`ControllerVirtualDriver.cpp`）の読解に基づき、
> (c) 確認していない構成（§2 の片側降格、§4 の網羅真理値表）は
> **「未確認」と明示した。**

---

## 7. 関連

* 設計の記録: [`scenario_control_handoff_design.md`](scenario_control_handoff_design.md)
* 実装: `GT_esmini/src/control/ControllerVirtualDriver.cpp`
  （`Activate` / `Deactivate` / `DeactivateDomains` / `TearDownControlOutputs`）
* 実装コミット: `73e4dccb`（シナリオ側からの制御終了）、`7678bb99`（teardown 経路の統一）
