# シナリオからの制御の受け渡し

**読者**：シナリオ（.xosc）を書く人。
**対象バージョン**：GT_Sim v0.14.2。

シナリオの中で、AI（VirtualDriverController）と人（ManualDriveController）のあいだで運転の主導権を渡す書き方をまとめる。
なぜその設計になっているかは [`scenario_control_handoff_design.md`](../design/scenario_control_handoff_design.md)、ドメイン別分担の内部仕様は [`domain_split_ownership.md`](../design/domain_split_ownership.md) にある。

## 1. 渡し方は3種類ある

| 渡し方 | 何が起きるか | 使いどころ |
| :--- | :--- | :--- |
| **ドメイン分割** | 横（操舵）と縦（アクセル、ブレーキ）を別々のコントローラが持ち続ける | 「ハンドルは人、速度は AI」のような分担 |
| **丸ごと移管** | 一方のコントローラが全ドメインを降り、他方が全ドメインを取る | テイクオーバーの再現 |
| **ボタンでの往復** | 物理ホイールのボタンで運転者が主導権を切り替える | 実機での試乗 |

3つは排他ではない。
丸ごと移管したあと、ボタンで戻すシナリオが書ける。

## 2. 最小の例

VD を Init で起動し、8秒後に降ろす。

Init で活性化する。

```xml
<PrivateAction>
   <ControllerAction>
      <ActivateControllerAction objectControllerRef="VirtualDriverController"
                                lateral="true" longitudinal="true"/>
   </ControllerAction>
</PrivateAction>
```

Story の Event で降ろす。

```xml
<Event name="Deactivate" priority="overwrite">
   <Action name="DeactivateAction">
      <PrivateAction>
         <ControllerAction>
            <ActivateControllerAction objectControllerRef="VirtualDriverController"
                                      lateral="false" longitudinal="false"/>
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
```

降格は Story の Event でしか撃てない。
Init は活性化専用なので、Init だけ書くと VD は永久に降りない。

`lateral` と `longitudinal` の2属性だけを使う。
`animation` と `lighting` はどちらのコントローラも解釈しない（灯火は `LightStateAction` と AutoLight の担当）。

## 3. コントローラを名前で狙う

同じエンティティに複数のコントローラを載せるときは、どれを操作するかを名前で指定する。
**属性名がファイルの版で変わる**ので、`FileHeader` の `revMinor` に合わせる。

| revMinor | 使う属性 |
| :--- | :--- |
| 3 以上 | `objectControllerRef` |
| 2 以下 | `controllerRef` |

版と属性名が食い違うと、名前は空文字として扱われ、最後に割り当てたコントローラが黙って選ばれる。
`ScenarioReader` はまず `controllerRef` を読み、`revMinor >= 3` のとき `objectControllerRef` の値で無条件に上書きするためである。
このとき `ActivateControllerAction::Start` は名前なしの分岐に落ち、`controllers_.back()` を採る。

症状は「意図と別のコントローラが降りる」「降ろしたはずの VD が走り続ける」として出る。
エラーにはならず、ログに `pick most recently assigned` が出るだけなので気付きにくい。

`AssignControllerAction` の `activateLateral` と `activateLongitudinal` は使わない。
横と縦が入れ替わる（[`domain_split_ownership.md`](../design/domain_split_ownership.md) の欠陥B）。
活性化は必ず `ActivateControllerAction` で行う。

## 4. ドメインは独立している

`lateral` と `longitudinal` は別々のブール値で、`false` にした側だけが降りる。

```xml
<!-- 操舵だけ人へ渡し、アクセルとブレーキは AI のまま -->
<ActivateControllerAction objectControllerRef="VirtualDriverController"
                          lateral="false" longitudinal="true"/>
```

片方だけ降ろしても、そのコントローラは活性のままである。
残した側の指令を出し続け、テレメトリも出続ける。
テレメトリの `vd_active` が false になるのは全ドメインを手放したときだけである。

横を ManualDrive、縦を VD に分ける構成は、2つのコントローラを Init で別々のドメインへ割り当てて作る。

```xml
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

2つの `ObjectController` は両方保持される。
宣言の並び順は結果を変えない。

## 5. 丸ごと移管する

VD から ManualDrive へ全ドメインを渡すには、**降ろす Action と取る Action の2つ**を書く。

```xml
<Event name="ReleaseVD" priority="override">
   <Action name="ReleaseVDAction">
      <PrivateAction>
         <ControllerAction>
            <ActivateControllerAction objectControllerRef="VirtualDriverController"
                                      lateral="false" longitudinal="false"/>
         </ControllerAction>
      </PrivateAction>
   </Action>
   <!-- StartTrigger: SimulationTimeCondition value="15.0" -->
</Event>
<Event name="TakeMD" priority="override">
   <Action name="TakeMDAction">
      <PrivateAction>
         <ControllerAction>
            <ActivateControllerAction objectControllerRef="ManualDriveController"
                                      lateral="true" longitudinal="true"/>
         </ControllerAction>
      </PrivateAction>
   </Action>
   <!-- StartTrigger: 同じ時刻 -->
</Event>
```

新参を活性化するだけの1アクション形では現職が降りない。
OpenSCENARIO v1.3 以降のドメイン別解放は、探し当てた現職ではなく新参に対して解放を呼ぶため、現職が居座る。
宣言の順序に意味があり、先に現職を解放する。

移管を受けた ManualDrive は、走行開始後に初めて活性化された場合、**手動モードで運転を始める**。
起動時から居る通常の ManualDrive シナリオはこれまでどおり自動モードで始まり、運転者がハンドルかペダルを動かした時点で手動に入る。
この区別があるのは、移管後には委譲先のシナリオ制御が居ないためである。
移管を受けても自動モードのままだと、誰も車を動かさない状態になり、エンジンブレーキもかからない。

## 6. ボタンで主導権を切り替える

物理ホイールのボタン（G29 既定はボタン3、三角）で運転者が主導権を切り替えられる。

| 今の状態 | ボタンを押すと |
| :--- | :--- |
| ManualDrive が運転中 | VirtualDriver に制御が戻る |
| VirtualDriver が運転中（AUTO） | VirtualDriver の中で手動モードに入る |

2番目は ManualDriveController への切り替えではない。
VirtualDriver が運転者の入力をそのまま車両に流す状態になり、デバイスも VirtualDriver が握ったままである。

Web パネルと UDP から送る Resume は「自動に戻す」だけで、手動には入らない。
物理ボタンだけが両方向の意味を持つ。

ボタンの割り当ては ManualDrive と VirtualDriver で別々の設定である。

| コントローラ | ファイル | キー |
| :--- | :--- | :--- |
| ManualDrive | `config/manual_drive*.json` | `input.auto_resume_button` |
| VirtualDriver | `config/virtual_driver*.json` | `sdl2_auto_resume_button` |

どちらも既定は `3` である。
移管シナリオでは両方が読まれるので、片方だけ変えると往路と復路でボタンが食い違う。

VirtualDriver 側で手動へ入る動作を止めたい場合は `override_button_takeover` を `false` にする。

## 7. コントローラごとの config を選ぶ

`ConfigFile` は Action ではなく `<Controller>` の `<Properties>` に書く。
`ActivateControllerAction` は名前でコントローラを参照するだけで、config とは無関係である。

```xml
<ObjectController>
   <Controller name="ManualDriveController">
      <Properties>
         <Property name="esminiController" value="ManualDriveController"/>
         <Property name="ConfigFile" value="manual_drive.json"/>
      </Properties>
   </Controller>
</ObjectController>
```

省略すると既定（ManualDrive は `manual_drive.json`、VirtualDriver は `virtual_driver.json`）が読まれる。

**実機で走らせるシナリオに、ヘッドレス用の config を書いてはいけない。**
ヘッドレス用は入力が常にゼロで力覚も持たないため、**ハンドルもペダルも効かず、反力も出ない**。
検証資産からコピーして書くときに最も踏みやすい間違いである。

### ManualDrive 側

| ファイル | 入力 | 力覚 | 用途 |
| :--- | :--- | :--- | :--- |
| `manual_drive.json` | 実機ホイール | あり（路面感） | **実機で人が運転する既定の構成** |
| `manual_drive_realwheel_split.json` | 実機ホイール | なし | 横を人、縦を AI に分ける構成 |
| `manual_drive_realwheel_reverse.json` | 実機ホイール | あり（サーボ） | 横を AI、縦を人に分ける構成 |
| `manual_drive_headless_stub.json` | なし（常にゼロ） | なし | ヘッドレス検証、CI |
| `manual_drive_headless_udp.json` | UDP | なし | ヘッドレス検証 |

### VirtualDriver 側

| ファイル | 入力 | サーボ | 用途 |
| :--- | :--- | :--- | :--- |
| `virtual_driver.json` | なし | なし | **既定。デバイスを開かない** |
| `virtual_driver_realwheel.json` | 実機ホイール | あり | AI の操舵をホイールで感じる構成 |

`virtual_driver_realwheel.json` を指定すると、VirtualDriver 自身がホイールを開き、サーボが AI の指令舵角までホイールを駆動する。
起動直後からホイールが動くのが正常な状態である。
この構成で ManualDrive へ移管すると、同じデバイスを2つのコントローラが開くことになるが、SDL2 が参照カウントで同じハンドルを返すため両方とも力覚を使える。

## 8. 降ろした瞬間に起きること

VirtualDriver が全ドメインを失うと、次の3つがこの順で起きる。

1. テレメトリの `vd_active` が false になる。
2. 力覚サーボが止まる。力の指令が 0 になる。
3. 介入ラッチが捨てられ、次の活性化は自動モードから始まる。

サーボを止めるのは、活性なコントローラしか毎フレームの処理を呼ばれないためである。
ここで止めないと、デバイスが最後の指令を保持したままホイールを引き続ける。

デバイス自体は閉じない。
ホイールは入力ソースが所有していて、シナリオ終了時にまとめて閉じる。
降格しても力が 0 になるだけなので、同じシナリオの中で再び活性化できる。

降ろしたあと `lateral="true" longitudinal="true"` で撃てば再び運転を始める。
シナリオの指令が上位で、運転者のラッチはそれを上書きしない。

## 9. よくある間違い

**Init だけ書いて Story に Event を書かない**
VD は起動するが永久に降りない。降格は Story の Event で撃つ。

**版に合わない属性名で狙う**
名前が空になり、最後に割り当てたコントローラが選ばれる（§3）。

**新参を活性化するだけで移管したつもりになる**
現職が降りない。降ろす Action と取る Action の2つを書く（§5）。

**実機シナリオにヘッドレス用の config を書く**
ハンドルもペダルも効かず、反力も出ない（§7）。

**降ろせばデバイスも解放されると思う**
解放されない。力が 0 になるだけである（§8）。

## 10. 動く実例

すべて回帰ゲートの Step 2.8 が毎回実行しているファイルである。

| ファイル | 何を通しているか |
| :--- | :--- |
| `resources/xosc/verification/08_handoff/scenario_deactivate_vd.xosc` | 両ドメインの降格 |
| `resources/xosc/verification/08_handoff/scenario_domain_takeover_vd.xosc` | 別コントローラによるドメイン奪取 |
| `resources/xosc/verification/08_handoff/scenario_split_domain_md_vd.xosc` | 横を ManualDrive、縦を VD に分ける構成 |
| `resources/xosc/verification/scenario_handoff_batch.yaml` | 上記3本のバッチ定義 |

実機ホイールで手順つきで試すシナリオは `08_handoff/` に別途ある。
`scenario_realwheel_handover_vd_md_vd.xosc` が丸ごと移管とボタンでの復帰を、`scenario_realwheel_vd_ffb.xosc` が VD 単体でのサーボ動作を扱う。
どちらも冒頭のコメントに手順と安全上の注意がある。

## 11. 関連文書

- ドメイン別分担の内部仕様：[`domain_split_ownership.md`](../design/domain_split_ownership.md)
- 設計の記録：[`scenario_control_handoff_design.md`](../design/scenario_control_handoff_design.md)
- ボタンによるモード切り替えの設計：[`button_mode_toggle_design.md`](../design/button_mode_toggle_design.md)
- 実装：`GT_esmini/src/control/ControllerVirtualDriver.cpp`、`GT_esmini/src/control/ControllerManualDrive.cpp`
