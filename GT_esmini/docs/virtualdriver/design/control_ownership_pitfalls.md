# 制御の所有権を触る前に読むこと

**読者**：VirtualDriver と ManualDrive の活性、所有権、移管まわりのコードを触る実装者。

移管の実装で実際に踏んだものだけを集めてある。
経緯は各設計文書にあり、ここには「何を踏むか」と「どう避けるか」だけを置く。

コード位置はシンボル名で書く。
この領域のファイルは並行編集されることが多く、行番号は当てにならない。

## 1. 前提となる共通事実

| 事実 | 確認先 |
| :--- | :--- |
| 非活性のコントローラは `Step()` を呼ばれない。`Active()` は `active_domains_ != DOMAIN_MASK_NONE` で決まる | `ScenarioEngine` の制御ループ、`Controller.hpp` |
| `ActivateControllerAction` に `lateral="false" longitudinal="false"` を書いても `Deactivate()` は呼ばれない。基底 `Activate(mode)` が OFF ドメインのビットを落とすだけである | `ActivateControllerAction::Start`、`Controller::Activate` |
| `ControllerVirtualDriver::Activate()` と `ControllerManualDrive::Activate()` は引数 `mode[]` を見ずに初期化群（物理バックエンド、入力ソース、ライト拡張）を実行してから基底を呼ぶ。降格でも同じ経路を通る | 各 `Activate()` |
| `Activate()` の引数配列は `ControlDomains` 添字（LONG=0, LAT=1）である。`ControlDomainMasks`（LONG=1, LAT=2）と取り違えても黙ってコンパイルが通り、逆のドメインを指す | `CommonMini.hpp` |
| `Controller::Activate()` は常に 0 を返す。失敗を報告しない | `Controller.cpp` |
| `operating_domains_` に無いドメインを ON にしても、WARN が出るだけで無視される | 同上 |

最初の1行が効いてくる場面が多い。
降ろした側で動いていた処理（力覚サーボ、介入検出、ボタンの監視）は、降ろした瞬間に止まるのではなく、止める処理が実行されないまま凍る。

## 2. 誰が運転しているかを決める4つの台帳

平常時は4つの答えが一致する。
食い違うのは引き渡しの瞬間だけで、移管まわりの不具合はほぼここから出る。

| # | 台帳 | 持ち主 | 決めること |
| :-: | :--- | :--- | :--- |
| ① | 活性ドメイン（`Controller::Active()`） | upstream | そのコントローラの `Step()` を呼ぶかどうか |
| ② | 所有台帳（`DomainOwnershipLedger`） | GT | 誰が車を積分するか |
| ③ | オーバーライド状態（`OverrideManager` の AUTO/MANUAL） | GT | ManualDrive が仕事をするか、シナリオに任せるか |
| ④ | 出力のデータ源選択（`GT_esminiLib.cpp`） | GT | OSI とテレメトリにどのコントローラの値を載せるか |

① は「自分は動いてよいか」に答え、② は「このドメインを書く資格があるのは誰か」に答える。
別の問いであり、調停に使えるのは ② だけである。

1フレームの順番は次のとおりで、`defaultController` がコントローラより先に車を等速で道なりに進める。

```
storyboard アクション
  ↓
defaultController   ← 等速で MoveAlongS
  ↓
各コントローラの Step()   ← Active() が真のものだけ
  ↓
OSI GroundTruth 送信 / OSI HostVehicleData 送信
```

`defaultController` を抑止できるのは `MODE_OVERRIDE` のときだけである。
GT の VirtualDriver も ManualDrive も強制 ADDITIVE なので抑止できない。
これは SpeedAction の目標を読むための意図的な設計であり、`MODE_OVERRIDE` にするのは解にならない。
誰も上書きしない瞬間を作らないことが解である。

## 3. upstream 欠陥

いずれも R1 Clean Core により GT 側では直さない。

| | 何が起きるか | GT 側の扱い |
| :-- | :--- | :--- |
| **欠陥A** | OpenSCENARIO v1.3 以降の per-domain 解放が、探し当てた現職ではなく新参に対して呼ばれる。現職が降りず、両者が同じドメインを持っていると認識したまま残る | 降ろす Action と取る Action を別々に書く。詳細と upstream への提案文は [`domain_split_ownership.md`](domain_split_ownership.md) §2 |
| **欠陥B** | `AssignControllerAction` の brace-init で横と縦が入れ替わる。`activateLateral="true"` が Longitudinal を返す | `AssignControllerAction` の `activateLateral` と `activateLongitudinal` を使わない。活性化は必ず `ActivateControllerAction` で行う。同上 |
| **欠陥C** | `LatDistanceAction::Step` が `ControlDomains::DOMAIN_LAT`（値1）を `ControlDomainMasks` として渡しており、横アクションが縦ドメインを問い合わせている | 観測のみ。upstream issue 候補として [`control_ownership_defects.md`](control_ownership_defects.md) §4 修正5 に記録 |

## 4. 踏んではいけない罠

| 罠 | どう現れるか | 回避 |
| :--- | :--- | :--- |
| 再活性化のたびに `input_source_->Init()` を無条件に呼ぶ | SDL の joystick と haptic を再オープンし、旧 effect ID が孤児化する。軸整定ループ（最大 500ms）がフレーム内で再実行され、復帰の瞬間にシミュレーションが止まる | 多重 Init ガードを対で入れる。VirtualDriver と ManualDrive の両方に `input_source_initialized_` がある（`708e792c`）ので、新しい経路を足すときに外さない |
| config の既定値を1か所だけ直す | 既定値は C++ struct、`config/*.json`、backend の `DEFAULT_*`、frontend の TS 型の4か所に散る。食い違うとフォールバック時だけ露見する（`9d0b2e8b`） | 4か所そろえる |
| config を全置換で保存する | 保存側に無いキーが恒久的に消える。過去に 59 キーを失っている（`5ee8857c`） | 既存ファイルへの merge にする |
| 新しい config ファイルを足して登録を忘れる | `web/pyinstaller/build_package.py` の `CONFIG_FILES` に網羅アサーションがあり、パッケージビルドが止まる | 追加と同時に `CONFIG_FILES` へ登録する |
| ヘッドレスが緑なら実機の力も抜けていると考える | `SDLFFBSink` は `GT_ENABLE_SDL2` ビルドでしかコンパイルされない。ヘッドレスが担保するのは制御フローがそこへ到達したことまでで、デバイス上の力については何も言えない | 力の実挙動は実機で1回取る。ヘッドレスの結果にその主張を書かない |
| テレメトリのフィールドを本体ブロックにだけ書く | 非積分側は早期 return して本体へ到達しないため、逆構成でフィールドが既定値のまま凍る。直っていても直っていなくても同じ値が出て、検証が無意味になる | 早期 return のブランチにも書く（`ffb.*` で1度起きている） |
| 復帰を含むシナリオの matcher を時刻窓で書く | VirtualDriver の `sim_time` は内部積算で非活性中は進まないため、復帰後はシナリオ時刻とずれる（§5） | `vd_active` のエッジで判定する |
| 出力のデータ源を `Object::GetController(name)` で選ぶ | 名前一致で引き、活性状態を見ない。移管シナリオでは運転していない側が選ばれ、空データが OSI に出る | 台帳（②）の積分者から引く |
| MD と VD のボタン割り当てを片方だけ変える | 同じ物理ボタンが `manual_drive*.json` の `input.auto_resume_button` と `virtual_driver*.json` の `sdl2_auto_resume_button` に別々にある。移管シナリオは両方を読むので、往路と復路で食い違う | 両方直す。実体のあるファイルの一覧は [`button_mode_toggle_design.md`](button_mode_toggle_design.md) §2 |

## 5. 未修正のまま運用している制約

| 制約 | 運用 |
| :--- | :--- |
| 復帰の1フレーム目だけ直接軸経路が生きている。ハンドルを 22.5°（0.05 axis-frac）以上切ったまま AUTO_RESUME を押すと、その場で MANUAL に再ラッチする | 手順書に「中立へ戻してから押す」を書く。Web の Resume は押下時に入力ゼロ化を同時送信して回避している |
| VirtualDriver テレメトリの `sim_time` は非活性中に進まないため、復帰後は非活性だった時間だけ遅れた時計になる | 時刻窓で判定する matcher を復帰シナリオに当てない |
| `GT_GetVirtualDriverTelemetry` は型ではなく名前でコントローラを引くため、シナリオが別名を付けるとテレメトリが黙ってゼロ件になる | 検証資産のコントローラ名をクラス名に揃える。本筋の修正は型で引くこと（未実施、[`domain_split_ownership.md`](domain_split_ownership.md) §4.1） |
| VirtualDriver と ManualDrive が同じ物理デバイスを同時に開く構成の実機記録がまだ無い | 起動ログで `SDL2WheelInput: Opened` が2行出ること、`SDL_HapticNewEffect` の失敗が無いことを見る |
| `ffb.disable_non_realtime` はパースされるだけで消費箇所が無い | 「ヘッドレスなら力は出ない」は成立しない。ヘッドレス検証は UDP 入力構成で行う |
| `override.enabled: false`（両ドメイン恒久 MANUAL）の構成では、`OverrideManager::Update()` がエッジ計算より手前で return するため三角ボタンのトグルが効かない | 仕様として運用する |

## 6. 関連文書

- ドメイン別分担の内部仕様と upstream 欠陥 A/B：[`domain_split_ownership.md`](domain_split_ownership.md)
- 移管を `ActivateControllerAction` で作ったときの判断：[`scenario_control_handoff_design.md`](scenario_control_handoff_design.md)
- 移管で出た3症状の原因と対策：[`control_ownership_defects.md`](control_ownership_defects.md)
- 三角ボタンの AUTO⇄MANUAL トグル：[`button_mode_toggle_design.md`](button_mode_toggle_design.md)
- シナリオの書き方（利用者向け）：[`../guides/scenario_control_handoff_howto.md`](../guides/scenario_control_handoff_howto.md)
