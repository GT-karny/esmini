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

---

# 付録A. 【次バージョンの宿題】横=ManualDrive / 縦=VD は成立するか

**ユーザーからの設計質問**: 「横制御だけ ManualDriveController、縦制御だけ VD にしたら、
反力制御はバグらないか」。

## 結論を先に: **今の実装では、この構成はそもそもシナリオから作れない**

反力がバグるかどうか以前の問題がある。**2 つのコントローラを別ドメインへ割り当てる
指示が書けない。**

§5-1 のとおり、この esmini は `ActivateControllerAction` の `controllerRef` を
**解釈しない**。活性化は毎回 `object_->controllers_.back()`（＝最後に Assign した 1 つ）を
採る。したがって:

* 「ManualDrive を lateral に、VD を longitudinal に」と 2 回撃っても、
  **両方とも同じ 1 つのコントローラ**（最後に Assign したもの）に当たる。
* シナリオから活性化できるコントローラは**実質 1 つだけ**である。

⇒ **この構成は現状「成立しない」。** 動くように書かない。

> ⚠ **未確認（重要な例外可能性）**: 上は**シナリオ経路**の話である。
> ManualDrive は Web GUI / config からも選ばれる。**シナリオ以外の経路で
> ManualDrive が活性化され、同時に VD がシナリオ活性化される**組み合わせが
> 成立しうるかは**確認していない**。成立するなら以下の (2) 以降が現実の問題になる。

## 仮に割り当てられたとして、何が起きるか

### (1) ドメイン部分解除そのものは**正しく設計されている**

`ControllerVirtualDriver::DeactivateDomains()` は
`losing_lateral || goes_inactive` のときだけ `TearDownControlOutputs()` を呼ぶ。
コード注記が理由を明記している——**サーボは横方向の出力なので、横を渡したときだけ
解放する。縦だけ取られた場合はまだ操舵しているので解放しない。**

⇒ 横を ManualDrive に渡した VD は、**サーボを解放し、縦の制御を続ける**。ここは正しい。

### (2) 問題はデバイスの所有権 — ただし SDL2 は参照カウントする

`ControllerManualDrive` も `ControllerVirtualDriver` も**それぞれ独立に**
`new SDL2WheelInput()` し、各自が `SDL_Init(JOYSTICK|HAPTIC)` →
`SDL_JoystickOpen()` → `SDL_HapticOpenFromJoystick()` を行う。

**SDL2 の該当 API は 3 つとも参照カウント方式である**（`SDL_InitSubSystem` /
`SDL_JoystickOpen` / `SDL_HapticOpenFromJoystick` はいずれも既存インスタンスがあれば
ref_count を増やして同じハンドルを返す）。`SDL2WheelInput::Shutdown()` は
`SDL_HapticClose` → `SDL_JoystickClose` → `SDL_QuitSubSystem` と**対称に**減らす。

⇒ **二重オープンで即座に壊れる、とは考えにくい。** 片方が Shutdown しても
参照が残るのでデバイスは生き続ける。

> ⚠ **未確認**: これは SDL2 の実装仕様に基づく**コード読解の推論**である。
> **実機で試していない**（リリース作業中のため意図的に検証していない）。
> しかも昨夜判明したとおり、**この G29 個体は haptic として 3 つに列挙され
> 1 つは誰にも open できない**。参照カウントが素直に効くかは個体依存でありうる。

### (3) 本当の危険は API ではなく**意味論**

参照カウントが効いたとしても、以下は設計上の破綻である。

| # | 何が二重になるか | 症状 |
| :-- | :--- | :--- |
| 1 | **2 つの `SDLFFBSink`** が同じ物理ホイールに独立して CONSTANT エフェクトを張る | どちらの力が出るか不定。VD 側はデッドマンで ≤250ms に失効するが、**エフェクトのスロットは残る** |
| 2 | **2 つの `OverrideManager`** が同じ軸・同じペダルを読む | 介入検出が二重に走り、片方だけがラッチする状態が作れる |
| 3 | **2 つの入力ソース**が同じ joystick を Poll | 入力の解釈は一致するはずだが、**軸プライミング（Init 時の初回 HID 待ち）が二重に走る** |

**1 が最も悪い。** VD が横を失えばサーボは解放されるので実害は縮むが、
**「両方が同時に力を出す窓」がゼロだという保証はどこにもない。**

### (4) 順序の問題（両方向とも未確認）

* **VD が先に開いていて、後から ManualDrive が開く**: 参照カウント上は開けるはず。
  ただし VD の teardown は**デバイスを閉じない**（§3 のとおり意図的）ので、
  VD は開いたまま。ManualDrive の open は「2 人目」になる。
* **ManualDrive が先で、後から VD が Activate される**: 同上。

どちらも**未確認**。特に「昨夜の個体で open が繊細だった」ことを踏まえると、
**2 人目の open が失敗する可能性は排除できない**。

## 成立させるなら — 設計の選択肢

| 案 | 中身 | 評価 |
| :--- | :--- | :--- |
| **A. 入力ソースを共有する** | `SDL2WheelInput` を両コントローラの外に出し、1 インスタンスを共有（所有はプロセス側） | **本命。** デバイスも FFB シンクも 1 つになり、上の 1〜3 が原理的に消える。ただし所有権とライフタイムの再設計が要る |
| **B. 所有権を明示的に移譲する** | 横のハンドオーバー時に、VD が持つ入力ソースを ManualDrive へ渡す | 変更は小さいが、**移譲の瞬間**に誰が FFB を持つかの取り決めが要る。移譲漏れが即「力が残る」になる |
| **C. FFB シンクだけ共有する** | 入力は各自、FFB シンクのみ単一化 | 1 は消えるが 2（検出の二重化）が残る |
| **D. 併存を禁止する** | 同一エンティティに VD と ManualDrive を同時 active にできないよう明示的に弾く | **最も安全で最も安い。** 成立しないものを成立しないと宣言する。上の質問への当面の答えとしてはこれで足りる |

**推奨**: 次バージョンで本当に「横=人・縦=AD」を提供したいなら **A**。
そうでないなら **D** を入れて、今の「書けないが弾かれもしない」曖昧な状態を解消する。

## この付録で確かめたこと / 確かめていないこと

* **確かめた（コード読解）**: ドメイン部分解除の分岐、teardown がデバイスを閉じないこと、
  両コントローラが独立に入力ソースを生成すること、`controllerRef` が無視されること。
* **確かめていない**: 実機での二重オープンの挙動、順序依存、
  シナリオ以外の経路（GUI/config）で ManualDrive と VD が同時 active になれるか。
  **いずれも実機デバイスを掴む検証が要るため、リリース作業中は実施していない。**
