# シナリオから VirtualDriver を降ろして手動運転へ移す — 書き方

**読者**: シナリオ（.xosc）を書く人。
**これは使い方の文書**である。なぜそう作られているかは
[`scenario_control_handoff_design.md`](scenario_control_handoff_design.md)（設計の記録）を見ること。
**設計判断はあちら、書き方はこちら**で分けている。

---

## 0. 先に結論（3行）

* `ActivateControllerAction lateral="false" longitudinal="false"` を Story の Event で撃つと VD が降りる。
* **コントローラは名前で狙える。** ただし**版で属性名が違う**:
  `revMinor >= 3` なら `objectControllerRef`、`revMinor <= 2` なら `controllerRef`。
  **どちらも書かなければ**「最後に Assign したもの」になる（§5-1）。
* 降りた瞬間に FFB は止まり、介入ラッチは捨てられる。**再 Activate できる。**

> **2026-07-30 訂正**: 本文書は当初「`controllerRef` は効かない／活性化は常に
> `controllers_.back()` を採る」と書いていた。**これは誤りだった。**
> 誤った理由と正しい仕組みは §5-1 に、当時そう読んでしまった構造は
> そこの「なぜ誤ったか」に残してある。

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
| `objectControllerRef` | **revMinor >= 3 ではこれを使う** | 名前でコントローラを狙う。§5-1 |
| `controllerRef` | **revMinor <= 2 ではこれを使う** | v1.3 以上では読まれた直後に上書きされる。§5-1 |

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

> **2026-07-30 更新**: 当初ここには「片側構成を実機／ゲートで走らせた記録は無い」と
> 書いていたが、**現在は回帰ゲートに載っている。** Step 2.8 が実行するのは 3 本:
> **両方 false**（`scenario_deactivate_vd`）、**別コントローラによるドメイン奪取**
> （`scenario_domain_takeover_vd`）、そして **2 つのコントローラが 1 ドメインずつを
> 保持し続ける片側構成**（`scenario_split_domain_md_vd`、付録A）。
> 片側構成は実機でも 1 回確認済み（付録A）。

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

### 5-1. 【最重要】版に合わない属性名で狙う → **名前が空になり `back()` に落ちる**

コントローラは**名前で狙える**。ただし**属性名が版で変わる**。

`ScenarioReader.cpp` はまず `controllerRef` を読む（:2628）。そのあと
**`revMinor >= 3` なら `objectControllerRef` で無条件に上書きする**（:2671）。
つまり v1.3 以上のファイルに `controllerRef` **だけ**書くと、直後に空文字で
上書きされて**名前が消える**。

`OSCPrivateAction.cpp` の `ActivateControllerAction::Start`（:792）はこう分岐する:

```cpp
if (ctrl_name_.empty())
{
    controller_ = object_->controllers_.back();
    LOG_WARN("... No controller name given for activation ... pick most recently assigned");
}
else
{
    controller_ = object_->GetController(ctrl_name_);   // ← 名前があれば名前で引く
}
```

`controllers_.back()` は**名前を指定しなかったときのフォールバック**であって、
「名前が効かない」という意味ではない。

**⇒ 動く書き方**（版に合わせる）:

```xml
<!-- revMinor="3" の場合 -->
<ActivateControllerAction objectControllerRef="VirtualDriverController"
                          lateral="false" longitudinal="false"/>
```

```xml
<!-- revMinor="2" 以下の場合 -->
<ActivateControllerAction controllerRef="VirtualDriverController"
                          lateral="false" longitudinal="false"/>
```

```xml
<!-- 落とし穴: revMinor="3" なのに controllerRef だけ書く
     → 名前が空になり、最後に Assign したコントローラが黙って選ばれる -->
<ActivateControllerAction controllerRef="VirtualDriverController" .../>
```

**症状**: 意図と別のコントローラが降りる／降ろしたつもりの VD が走り続ける。
ログに `pick most recently assigned` が出ていたらこれ。
**エラーにはならない**（警告のみ）ので気づきにくい。

> #### なぜ当初「controllerRef は効かない」と書いてしまったか（歴史）
>
> 本文書は 2026-07-29 時点で「`controllerRef` は解釈されない。活性化は毎回
> `controllers_.back()` を採る。GT ビルドと素の esmini.exe の両方で確認済み」と
> 断定していた。**観測は正しく、結論が誤っていた。**
>
> 観測されたのは「v1.3 のシナリオに `controllerRef` を書いたら
> `pick most recently assigned` が出て `back()` が選ばれた」という事実である。
> ここから「`controllerRef` という属性は実装されていない」と一般化してしまった。
> 実際には **:2671 の上書きによって名前が空になっていた**だけで、
> `Start()` の `else` 側（名前で引く経路）は最初から存在していた。
>
> **誤読の構造**: `controllers_.back()` を採るコードパスを見て、それが
> **唯一の**経路だと読んだ。同じ関数内の `else` 側を見ていない。
> 「フォールバックを見て仕様だと結論する」は再発しやすい形なので記録しておく。
> 版によって属性名が変わる仕様（:2628 と :2671 の二段読み）が、
> この誤読を起こしやすくしている。

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
| `resources/xosc/verification/08_handoff/scenario_domain_takeover_vd.xosc` | **別コントローラによるドメイン奪取**（OSC v1.3 の per-domain 降格経路） |
| `resources/xosc/verification/08_handoff/scenario_split_domain_md_vd.xosc` | **横=ManualDrive / 縦=VD の分担**（付録A）。§5-1 の版別属性名の実例でもある |
| `resources/xosc/verification/scenario_handoff_batch.yaml` | 上記 3 本のバッチ定義 |
| `GT_esmini/test/regression_baseline/scenario_handoff_expected.yaml` | 期待値ベースライン |

回帰ゲート **Step 2.8**（`scripts/run_regression_gate.ps1`）がこのバッチを実行し、
ベースラインと突き合わせている。**読者はこの 2 ファイルを開けば動く実例が手に入る。**

> **原著時点（2026-07-29 09:05）に本文書のために実行したもの**: 無い。以降の追記（付録A、§5-1 の訂正）は実行に基づく。
> リリース前の最終ゲートが別担当により実行中で、ヘッドレス実行は
> OSI の UDP ポート（48198 ほか）で衝突しうるため、**意図的に走らせていない。**
> 代わりに (a) 断片をゲート実行対象ファイルからの逐語抜粋に限定し、
> (b) §3 の挙動は実装（`ControllerVirtualDriver.cpp`）の読解に基づき、
> (c) 確認していない構成（§2 の片側降格、§4 の網羅真理値表）は
> **「未確認」と明示した。**

---

## 7. 関連

* **ドメイン別分担（横=人/縦=AI など）の正典**:
  [`domain_split_ownership.md`](domain_split_ownership.md)
  — 所有台帳・積分器の選び方・コマンドバス・upstream 欠陥の扱い。付録A はここへ委譲する。
* 設計の記録: [`scenario_control_handoff_design.md`](scenario_control_handoff_design.md)
* 実装: `GT_esmini/src/control/ControllerVirtualDriver.cpp`
  （`Activate` / `Deactivate` / `DeactivateDomains` / `TearDownControlOutputs`）
* 実装コミット: `73e4dccb`（シナリオ側からの制御終了）、`7678bb99`（teardown 経路の統一）

---

# 付録A. 横=ManualDrive / 縦=VD — **成立する**（2026-07-30 更新）

**ユーザーからの設計質問**: 「横制御だけ ManualDriveController、縦制御だけ VD にしたら、
反力制御はバグらないか」。

## 結論: **成立する。実装済み・回帰搭載済み・実機確認済み。**

> **この付録は当初「今の実装では、この構成はそもそもシナリオから作れない」と
> 結論していた。それは誤りだった。** 誤りの根は §5-1 の
> 「`controllerRef` は効かない」という誤読で、そこから
> 「シナリオから活性化できるコントローラは実質 1 つだけ」を導いていた。
> 実際には版に合った属性名（v1.3 なら `objectControllerRef`）を使えば
> 2 つのコントローラを別ドメインへ個別に割り当てられる。
> 当時の分析（下の「仮に割り当てられたとして」以降）は、**前提が外れただけで
> 中身は今も有効**なので歴史として残す。

**設計の詳細は
[`domain_split_ownership.md`](domain_split_ownership.md) が正典である。**
本付録は経緯と使い方だけを扱う。

### 動く書き方（`resources/xosc/verification/08_handoff/scenario_split_domain_md_vd.xosc` からの逐語抜粋）

```xml
<!-- 2 つの ObjectController は両方保持される。並び順は結果を変えない。 -->
<ObjectController>
   <Controller name="ManualDriveController">
      <Properties>
         <Property name="esminiController" value="ManualDriveController"/>
         <Property name="ConfigFile" value="manual_drive_headless_stub.json"/>
      </Properties>
   </Controller>
</ObjectController>
<ObjectController>
   <Controller name="VirtualDriverController">
      <Properties>
         <Property name="esminiController" value="VirtualDriverController"/>
      </Properties>
   </Controller>
</ObjectController>
```

```xml
<!-- Init の中で、名前を指定して別々のドメインへ割り当てる（revMinor="3"） -->
<PrivateAction>
   <ControllerAction>
      <ActivateControllerAction objectControllerRef="ManualDriveController"
                                lateral="true" longitudinal="false"/>
   </ControllerAction>
</PrivateAction>
<PrivateAction>
   <ControllerAction>
      <ActivateControllerAction objectControllerRef="VirtualDriverController"
                                lateral="false" longitudinal="true"/>
   </ControllerAction>
</PrivateAction>
```

**コントローラ名をクラス名そのものにしてあるのは必須**である。
`GT_GetVirtualDriverTelemetry` は `Object::GetController()` を**型ではなく名前**の
一致で引くので、`"VD"` のような別名を付けるとテレメトリが黙ってゼロ件になる。

> ⚠ **`AssignControllerAction` 側の `activateLateral` / `activateLongitudinal` は
> 使ってはならない。** 横縦が入れ替わる。活性化は必ず `ActivateControllerAction` で行う。
> 理由は `domain_split_ownership.md` の欠陥B。

### 確認済みの範囲

* **回帰ゲート Step 2.8** が `scenario_split_domain_md_vd.xosc` を毎回実行し、
  `domain_split_holds` matcher が「縦=VD（ゼロスロットルでは不可能な再加速）」
  「横≠VD（車線逸脱）」「単一積分器（実移動/報告速度 比 0.98-1.02）」を検査している。
* **実機（G29）で確認済み**: 2026-07-29 23:40、横=ManualDrive / 縦=VirtualDriver。
  ハンドルを切れば曲がり、手を離せば車線を外れ、その間も速度は AI が保った。
  ログ `test_results/realwheel/realwheel_split_md_vd_20260729_2340.log`。
  **ただし 1 シナリオ・G29 1 個体・31.6 秒の 1 走行**である。

> ⚠ **未検証**: **逆構成（横=VD / 縦=ManualDrive）は実機で試していない。**
> ヘッドレスでは成立を確認済み（`vd_reverse_split_probe.py`）だが、実機は未了。
> 逆構成では VD が横を持つため、**停止時の舵の挙動がこの構成に当たる**
> （その不具合自体は修正済みで、ヘッドレスでは保持を確認している）。

---

## 【歴史】以下は「割り当てられない」と考えていた時点の分析

**前提（割り当てられない）は外れたが、中身の危険の指摘は今も有効である。**
実際にどう解決したかは各節の末尾に追記した。

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

## 【歴史】当時挙げた設計の選択肢 — と、実際に採った道

| 案 | 中身 | 当時の評価 |
| :--- | :--- | :--- |
| **A. 入力ソースを共有する** | `SDL2WheelInput` を両コントローラの外に出し、1 インスタンスを共有 | 本命。デバイスも FFB シンクも 1 つになる。所有権とライフタイムの再設計が要る |
| **B. 所有権を明示的に移譲する** | 横のハンドオーバー時に入力ソースを渡す | 変更は小さいが移譲漏れが即「力が残る」になる |
| **C. FFB シンクだけ共有する** | 入力は各自、FFB シンクのみ単一化 | 1 は消えるが 2 が残る |
| **D. 併存を禁止する** | 同一エンティティに VD と ManualDrive を同時 active にできないよう弾く | 最も安全で最も安い |

### 実際に採ったのは A〜D のどれでもない — **別解**

実装は **(i) GT 側のドメイン所有台帳** + **(ii) 出力ゲートによる単一積分器** +
**(iii) コマンド段での合流バス** という構成になった（S1〜S4、`domain_split_ownership.md`）。

**なぜ A（入力ソース共有）に行かなかったか**: 危険の本体は「デバイスの二重オープン」
ではなく「**2 つの独立した物理積分器が同じ車体を奪い合うこと**」だと分かったため。
入力を共有しても、各コントローラが自前の `RealVehicleBackend` で姿勢を書き戻す限り
「最後に Step した方が全ドメインを総取りする」構造は残る。
積分器を 1 つに絞る方が根が深いところで効く。

**D（併存禁止）を採らなくてよかった理由**: 併存は成立する。禁止していたら
今回の機能そのものが作れなかった。

**上の (1)〜(3) で挙げた危険がどうなったか**:

| 当時の指摘 | 現在 |
| :--- | :--- |
| (1) 2 つの `SDLFFBSink` が同じホイールに力を出す窓 | **構成で消えている。** 実機構成では VD が `input_type=stub`（`GetFFBSink()` が nullptr）でデバイスに触れず、開くのは ManualDrive の 1 つだけ。さらに非積分側は S2 のフォーリングエッジで sim_time 0.000 に `SetEnabled(false)` される。**ただし担保できるのは制御フロー到達までで、実機トルクが物理的にゼロになったことは測っていない**（`SDLFFBSink::Update()` は周期ログを出さないので「ログが無い＝力が無い」は証拠にならない） |
| (2) 2 つの `OverrideManager` が同じ軸を読む | 実機構成では VD 側が stub 入力なので二重に読まない。**両方が実デバイスを読む構成は未検証** |
| (3) 軸プライミングの二重実行 | 同上。実機構成では発生しない |

## この付録で確かめたこと / 確かめていないこと

* **確かめた（実行）**: per-domain 分担の成立（回帰 Step 2.8 が毎回検査）、
  実機 1 走行での成立、逆構成のヘッドレス成立、停止時の舵保持（ヘッドレス）。
* **確かめた（コード読解）**: ドメイン部分解除の分岐、teardown がデバイスを閉じないこと、
  両コントローラが独立に入力ソースを生成すること、名前によるコントローラ解決（§5-1）。
* **確かめていない**: **逆構成の実機**。**両方が実デバイスを掴む構成**（実機構成では
  VD が stub なので発生しない）。シナリオ以外の経路（GUI/config）で
  ManualDrive と VD が同時 active になれるか。実機トルクが物理的にゼロであること。

## upstream の per-domain 解放について（未検証・断定しない）

`OSCPrivateAction.cpp` の `>= osc v1.3` 分岐は、現職コントローラ `ctrl` を
`GetControllerActiveOnDomainMask()` で**探し当てておきながら**、
`DeactivateDomains()` は新参側の `controller_` に対して呼んでいるように読める。
`ctrl` はその後どこでも読まれていない。

**そう読めるが、GT 側でこの分岐の効果を実測してはいない。**
今回の分担シナリオは 2 つのコントローラが別ドメインを取るため競合が起きず、
この分岐を通らない。「upstream のバグである」と断定するには、
この分岐を確実に通す構成での実測が要る（別途調査中）。

GT 側は**この分岐の挙動に依存しない**設計になっている
（所有台帳が last claimer wins で調停するため、現職が降りようが降りまいが結論は同じ）。
したがって仮にこの読みが誤っていても、分担の成立には影響しない。
