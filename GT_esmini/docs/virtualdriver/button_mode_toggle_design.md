# 三角ボタンによる AUTO⇄MANUAL モード切替 — 設計と実装プラン

**状態**: 設計完了・実装未着手（実装は Codex 担当）
**作成**: 2026-07-31
**関連**: [`handover_control_ownership_defects.md`](handover_control_ownership_defects.md) /
[`domain_split_ownership.md`](domain_split_ownership.md) /
[`realwheel_reverse_test.md`](realwheel_reverse_test.md)

**根拠表記**: ★ = 本作業でコードを直接開いて確認 / ☆ = 未確認・実装時に要確認。
**行番号はズレうる。シンボル名で参照すること。**

---

## 0. ユーザー確定事項（2026-07-31）

| # | 決定 | 意味 |
|---|---|---|
| **D1** | 三角ボタンで「手動へ」は **VD 内オーバーライドのみ** | コントローラは切り替えない。VD が MANUAL ラッチに入り、運転者の入力を VD が素通しする。ManualDriveController は登場しない |
| **D2** | 実機のホイールは **VD にも開かせる** | 移管シナリオでも VD 側 config を `input_type=sdl2_wheel` にする。VD が自分でボタンを読む |

D1 により、**コントローラ活性（`ActivateControllerAction`）には一切触らない**。
本プランの射程は `OverrideManager` とその設定・計器・検証だけである。

---

## 1. 作るもの（1文）

**三角ボタン（既定 = SDL2 ボタン3）を押すと、AUTO のときは MANUAL に入り、MANUAL のときは
AUTO に戻る — 同じボタンで往復するトグルにする。**

現状は片道である ★:

| 現状の状態 | 三角ボタンを押すと |
|---|---|
| MANUAL（ラッチ済み） | AUTO へ復帰する（`OverrideManager::Update` の resume 立ち上がりエッジ分岐、`OverrideManager.cpp` の `resume_edge` ブロック）★ |
| AUTO | **何も起きない**（同分岐は AUTO を AUTO に設定して return するだけ）★ |

MANUAL に入る手段は現状 3 つあり、いずれも「三角ボタン」ではない ★:

1. 直接軸経路 — `|steering| > steering_threshold`（既定 0.05 axis-frac ＝ ハンドル 22.5°）。
   ただし `!ffb_sample_.active` のフレームだけ（サーボ稼働中は残差経路に譲る）
2. FFB 残差経路 — シャドウモデルとの乖離が `residual_threshold` を `sustain_time` 継続
3. `ButtonBits::OVERRIDE`（既定 = SDL2 ボタン0、`override_button: true` のとき）— **押している間だけ**の
   レベル判定。離すと（閾値を割っていれば）AUTO へ戻る

**今回追加するのは「ラッチする 3 番目のボタン」ではなく、既存の復帰ボタンに逆方向を持たせるトグル**である。

---

## 2. ボタン割り当ての設定場所 — **2か所に分かれている**（ユーザー質問への回答）

**同じ既定値 `3` が偶然一致しているだけで、MD と VD は別のファイル・別のキー名で持っている。** ★

| | ManualDrive 側 | VirtualDriver 側 |
|---|---|---|
| ファイル | `GT_esmini/config/manual_drive.json` | `GT_esmini/config/virtual_driver.json`（実機は `virtual_driver_realwheel.json`） |
| キー | `input.auto_resume_button`（ネスト構造） | `sdl2_auto_resume_button`（フラット・行パース） |
| 既定値 | `3` | `3` |
| パーサ | `ManualDriveConfig.cpp` の `parse_int("auto_resume_button", sdl2.auto_resume_button)` ★ | `VirtualDriverConfig.cpp` のキーテーブル → `ControllerVirtualDriver` 構築時に `io_config_.sdl2.auto_resume_button` へ詰め替え ★ |
| Web UI | ManualDrivePanel の button_mapping → `auto_resume` | VirtualDriverPanel → "Auto Resume (F7)" |
| 消費側 | **共通**: `SDL2WheelInput::Poll` の `read_btn(auto_resume_button_, ButtonBits::AUTO_RESUME)` ★ |

**帰結（実装時に効いてくる）**:

- 移管シナリオでは MD と VD の config が**同時にロードされる**。片方だけ変更すると
  **往路と復路でボタンが食い違う**。今は両方 3 なので露見していないだけ。
- 他に `manual_drive_realwheel_reverse.json` / `manual_drive_headless_udp_override.json` にも
  同じキーの実体があり、**計 6 ファイル**が同じ物理ボタンを別々に宣言している ★。
- → **タスク T7 で「同一オブジェクトに MD と VD が居て割り当てが食い違うとき起動時 WARN」を入れる。**
  値の自動同期はしない（どちらを正とするか決められないため。警告して人に決めさせる）。

---

## 3. 設計

### 3-1. 新しいボタンビットを 1 つ足す（Web/UDP の Resume を巻き込まないため）

物理ボタンは 1 つだが、**意味は 2 つ**（→AUTO と →MANUAL）。既存の `ButtonBits::AUTO_RESUME`
（`1u << 7`、最上位使用ビット ★）をそのままトグルとして再解釈すると、**Web パネルの Resume ボタンと
UDP の AUTO_RESUME ビットまでトグルになり、AUTO で押すと車が勝手に手動へ落ちる。**

```cpp
// GT_esmini/include/gt_esmini/control/common/VehicleCommand.hpp
constexpr uint32_t AUTO_RESUME  = 1u << 7;  // 既存: manual -> auto（意味を変えない）
constexpr uint32_t TAKE_MANUAL  = 1u << 8;  // 新規: auto -> manual を要求する
```

- **物理ボタンだけが両方のビットを立てる**。`SDL2WheelInput::Poll` で
  `read_btn(auto_resume_button_, ButtonBits::AUTO_RESUME | ButtonBits::TAKE_MANUAL)` 相当にする
  （設定キーは増やさない。三角ボタン ＝ モードトグルボタン、という 1 つの割り当て）。
- Web / UDP の Resume 経路は `AUTO_RESUME` のみを送る → **従来どおり「AUTO へ戻る」専用**のまま。
- ヘッドレス検証はビット 8 を明示的に注入できる。UDP のボタンフィールドは
  **4 バイト全幅**なので配線変更は不要 ★（`HeadlessFfbInput.cpp` の `memcpy(&latest_buttons_, buf+40, 4)`、
  `NetworkInputBridge.cpp` の 44 バイトフォーマット）。

### 3-2. 判定は `OverrideManager::Update()` の中に置く

VD の `Step()` 側に書かない。理由は 2 つ:

1. AUTO/MANUAL を決める権限を 1 か所に保つ（`lat_manual` / `lon_manual` は `Update()` の直後に
   読まれ、そのフレームのサーボ活性 `SetSteerTarget(auto_cmd.steering, !lat_manual)` まで一本で流れる ★）。
   Step 側で後から書き換えると、サーボ解放が 1 フレームずれる。
2. MD 側でも同じ機構が要る日に備える（ただし**既定は VD だけ ON**。§3-5）。

### 3-3. `Update()` 内の順序（ここを間違えると自己相殺する）

現行の resume ブロックは「エッジ → 両ドメイン AUTO → 介入状態リセット → **early return**」★。
`TAKE_MANUAL` を後ろに置くと early return で永久に到達しない。**必ず resume 分岐の直前で方向を決める。**

```
1. 遷移フラグのクリア（既存）
2. scenario ドメインの強制 AUTO（既存）
3. !enabled_ → 両ドメイン MANUAL して return（既存）        ← ★ enabled_=false ではボタンも死ぬ（§6-1）
4. was_any_manual を退避（既存）
5. 起動時軸ベースラインの記帳（既存・resume より前にある理由は既存コメント参照）
6. ★新★ 2 本の立ち上がりエッジを計算する
      resume_edge      = (buttons & AUTO_RESUME) && !prev_resume_pressed_
      take_manual_edge = (buttons & TAKE_MANUAL) && !prev_take_manual_pressed_
      prev_* の更新は **どの分岐にも入る前に無条件で**行う（二度撃ち防止）
7. ★新★ 方向の決定（同一フレームで両エッジが立つのが正常。was_any_manual で振り分ける）
      if (resume_edge && was_any_manual)            → 既存の AUTO 復帰分岐（無改変）
      else if (take_manual_edge && !was_any_manual
               && button_takeover_)                  → ★新★ MANUAL 化分岐（下記）
      else if (resume_edge)                          → 既存の AUTO 分岐（実質 no-op）
8. 以降の閾値判定・残差判定（既存・無改変）
```

**MANUAL 化分岐がやること**:

```
lat_configured_manual_  なら lat_mode_  = MANUAL
long_configured_manual_ なら long_mode_ = MANUAL
manual_explicit_ = true          // §3-4
idle_timer_ = 0.0
idle_axis_ref_valid_ = false     // アイドル基準は無意味になる
just_transitioned_to_manual_ = true
take_manual_edge_ = true         // 計器用（JustPressedTakeManual()）
return;                          // 同フレームの閾値再評価を抑止（resume 分岐と同じ流儀）
```

`ffb_*` のシャドウ状態はここでは触らない。サーボが次フレーム以降 `active=false` になると
既存の「サンプル不活性」分岐が S6 として再武装するため ★。

### 3-4. `auto_return_timeout` からの除外（`manual_explicit_`）

`auto_return_timeout > 0` のとき、MANUAL 中に「運転者がハンドルを動かしていない」状態が続くと
自動で AUTO に戻る ★。**ボタンで明示的に手動に入った運転者を、静止しているという理由で
AI に戻すのは意図に反する。**（VD の既定 config は `auto_return_timeout: 0` ＝無効なので
既定構成では顕在化しないが、Web から変更できる値である。）

→ `manual_explicit_` フラグを新設し、真の間は idle 自動復帰をスキップする。
セットするのは (a) このボタン分岐、(b) 進行中の作業で追加された `RequestManualMode()`
（シナリオ移管。同じ理由で除外が正しい ★）。クリアは `ResetInterventionStateOnReturnToAuto()` で一括。

### 3-5. 設定キー（既定値の非対称に注意）

| 側 | キー | 既定 | 理由 |
|---|---|---|---|
| VD | `override_button_takeover`（フラット、`virtual_driver*.json`） | **true** | 今回の要望そのもの |
| MD | `override.button_takeover`（ネスト、`manual_drive*.json`） | **false** | MD 側は進行中作業で `JustPressedResume()` → `ResumeVirtualDriverControl()`（VD へ制御を返す）に使っている ★。ここで同時に MANUAL 化すると 1 回の押下で 2 つのことが起きる |

C++ 既定値（`ManualDriveConfig` の struct 既定）は **false**。VD は
`ControllerVirtualDriver` の構築時に自分のフラットキーから `io_config_.override_cfg.button_takeover` を
上書きする（`sdl2_auto_resume_button` を詰め替えているのと同じ場所 ★）。

> **罠**: 既定値は C++ struct / `config/*.json` / Web の Python 既定 / TS 既定 の**4 か所**に散る。
> 食い違うと「フォールバック時だけ挙動が違う」という最悪の形で出る。4 か所すべてを揃えること。

---

## 4. 実装タスク（順序つき）

### T1. ボタンビットと入力層

- `include/gt_esmini/control/common/VehicleCommand.hpp` — `TAKE_MANUAL = 1u << 8` を追加。
  コメントに「物理トグルボタンの →MANUAL 方向。Web/UDP の Resume はこのビットを立てない」と明記。
- `src/control/manualdrive/SDL2WheelInput.cpp` — `read_btn(auto_resume_button_, ...)` が
  `AUTO_RESUME | TAKE_MANUAL` を立てるようにする。**新しい設定キーは作らない。**
  （`read_btn` が単一ビット前提なら、その 1 行だけ 2 ビット版に分ける）

### T2. `OverrideManager` 本体

- `OverrideManager.hpp` — メンバ追加: `button_takeover_`（config）、`prev_take_manual_pressed_`、
  `take_manual_edge_`、`manual_explicit_`。公開 API 追加: `JustPressedTakeManual()`。
- `OverrideManager.cpp`
  - `Configure()` — `button_takeover_` を読み、新メンバを全部リセット（`prev_*` と `manual_explicit_` の
    リセット漏れは「再構成後の 1 フレーム目に誤エッジ」を生む）。
  - `Update()` — §3-3 の順序で分岐を挿入。既存 resume ブロックは**触らない**。
  - `ResetInterventionStateOnReturnToAuto()` — `manual_explicit_ = false` を追加。
  - `RequestManualMode()`（進行中作業で追加済み ★）— `manual_explicit_ = true` を追加。
  - idle 自動復帰ブロック — `if (auto_return_timeout_ > 0.0 && !manual_explicit_)` に変更。

### T3. 設定の配管（4 か所を揃える）

- `include/gt_esmini/control/manualdrive/ManualDriveConfig.hpp` — `override_cfg.button_takeover = false`。
- `src/control/manualdrive/ManualDriveConfig.cpp` — `override` セクションのパースに追加。
- `include/gt_esmini/control/virtualdriver/VirtualDriverConfig.hpp` + `VirtualDriverConfig.cpp` —
  フラットキー `override_button_takeover`（既定 true）をキーテーブルに追加。ログ出力行にも追加。
- `src/control/ControllerVirtualDriver.cpp` — `io_config_.override_cfg.button_takeover` へ詰め替え
  （`sdl2_auto_resume_button` を詰めている箇所のすぐ隣 ★）。
- `config/virtual_driver.json` / `config/virtual_driver_realwheel.json` — キーと `_comment` を追加。
- `config/manual_drive.json` ほか MD 系 — `override.button_takeover: false` を明示（既定に頼らない）。

### T4. 計器（これを省くと「押したのに効かない」を切り分けられない）

- `VirtualDriverTypes.hpp` の `VirtualDriverTelemetry` — `takeover_pressed`（`resume_pressed` の対）を追加。
- `VirtualDriverTelemetryJson.cpp` — `override` オブジェクトに出力。
- `ControllerVirtualDriver.cpp` の telemetry 充填箇所 — `override_mgr_.JustPressedTakeManual()` を代入。
  **非積分側の早期 return ブランチにも書くこと** — 逆構成では本体ブロックへ一度も到達せず、
  フィールドが既定値のまま凍る（同型の事故が `ffb.*` で 1 回起きている。
  [[domain_split_md_vd]]）。`manual_transition` は既存のまま発火する。
- ボタン由来の MANUAL 化を `LOG_INFO` で 1 行出す（「なぜ MANUAL になったか」の帰属が
  残差ラッチと区別できるように）。

### T5. VD が実機デバイスを開く構成（D2）

- 移管シナリオ用に VD の config を `input_type: sdl2_wheel` にする。
  **`virtual_driver_realwheel.json` をそのまま使うとサーボ（`ffb_target_track_enabled: true`）も
  一緒に入る**ので、力を出したくないなら派生 config を 1 本作る
  （`virtual_driver_realwheel_input_only.json` 等 = sdl2_wheel + `ffb_target_track_enabled: false`）。
  → **新 config を足したら `GT_esmini/web/pyinstaller/build_package.py` の `CONFIG_FILES` に登録する。
    網羅アサーションがあり、漏れるとパッケージビルドが停止する** ★。
- `resources/xosc/verification/08_handoff/scenario_realwheel_handover_vd_md_vd.xosc` の
  VD 側 `ConfigFile` プロパティを差し替え、冒頭コメントの「VD はデバイスを開かない／
  起動直後にホイールが動いたら異常」という**安全記述を実態に合わせて書き直す**（サーボ ON にするなら
  「動くのが正常」に反転する）。ここを直さないと手順書が嘘になる。
- **VD の多重 `Init()` ガード**: `ControllerVirtualDriver::SetUpControlOutputs()` は
  INACTIVE→ACTIVE のたびに `input_source_->Init()` を無条件で呼ぶ ★。MD→VD 復帰を繰り返すと
  SDL の joystick/haptic を再オープンし effect が孤児化する。MD 側は進行中作業で
  `input_source_initialized_` ガードが入った ★ ので、**同じ形の対称ガードを VD にも入れる**。
- **二重オープンは未検証**: VD と MD が同じ G29 を同時に開く構成になる。SDL2 は参照カウント方式と
  考えられるが実機記録は無い ☆。§7 の実機確認で最初に見るのはここ。

### T6. `enabled_ = false` のときの扱い（決めてから実装する）

`Update()` は `!enabled_` で**エッジ計算より手前に return する** ★。つまり
`override.enabled: false`（両ドメイン恒久 MANUAL）の構成では**トグルは死ぬ**。
検証用 config（`manual_drive_headless_stub.json` / `manual_drive_realwheel_split.json`）が
まさにこれ ☆。VD 実機 config は `override_enabled: true` なので実害は無い。
**「恒久 MANUAL 構成ではトグル無効」を仕様として明記する**（対処せず文書化で閉じる）。

### T7. 割り当て不一致の検出

同一オブジェクトに MD と VD が両方居て、`auto_resume_button` の値が食い違うときに
起動時 `LOG_WARN` を 1 行。**同期はしない**（どちらを正とするか自動判断できない）。
置き場所は `ControllerVirtualDriver::Activate()` の ownership ログの隣が自然 ★。

### T8. Web への露出

- `web/backend/api/virtual_driver_api.py` — 許可キー一覧・既定値・説明文に `override_button_takeover` を追加。
- `web/frontend/src/api/client.ts` / `components/simulation/VirtualDriverPanel.tsx` — 同上（トグル UI）。
- **config 保存は既存ファイルへの merge。全置換は禁止**（過去に 59 キーが恒久消失している）。

---

## 5. 既存挙動への影響（明示的に受け入れる差分）

1. **順構成（VD 単独＋実機ホイール）で、AUTO 中の三角ボタンが no-op から MANUAL 化に変わる。**
   これが要望そのもの。キルスイッチは `override_button_takeover: false`。
2. `input_type: stub` の全ヘッドレス／バッチ構成ではボタンが常に 0 なのでエッジが立たず、
   **回帰の deviation は 0 のはず**。ここが 0 でなければ配線を間違えている。
3. Web / UDP の Resume は無改変（§3-1 でビットを分けた理由）。

---

## 6. この機能固有の罠

### 6-1. サーボ OFF で MANUAL 化すると操舵が飛ぶ

MANUAL 中の VD は `cmd.steering = m.steering`（運転者の生軸）を使う ★。
**サーボが動いていれば**物理ホイールは既に AD 指令角に居るので連続だが、
**サーボ OFF だと**ホイールが中立のまま AD が 20° 切っている状況で押した瞬間に操舵が 0 へ飛ぶ。
`AdSteeringEnvelope` は **AD 指令にしか効かない**ので保護にならない ★。

- 実機（`virtual_driver_realwheel.json` 系）はサーボ ON なので実害は小さい。
- T5 で「sdl2_wheel ＋サーボ OFF」の config を作る場合は**この飛びが出る**。
  最低限、押下フレームの `|m.steering − auto_cmd.steering|` をログ／テレメトリに出すこと。
  （ゲートを付けて「ずれていたら切替を拒否する」設計も可能だが、運転者が永久に奪えなくなる
   失敗モードを生むので**推奨しない**。）

### 6-2. MANUAL→AUTO 復帰直後の即時再ラッチ

サーボ OFF のとき直接軸経路が生きているので、ハンドルを 22.5° 以上切ったまま三角ボタンで
AUTO に戻すと、**次フレームに再ラッチして MANUAL に戻る** ★（resume 分岐は同フレームだけ抑止する）。
これは F7 で確定済みの仕様であり ☆、`auto_return_timeout` 経路が使っている**軸リベースライン**を
resume 側は意図的に**やっていない**（既存コメントに理由が書かれている ★）。
トグル化すると「押しても戻らない」に見える頻度が上がるので、**手順書に明記する**。
サーボ ON では残差経路が担当し直接軸経路が抑止されるため、実機既定構成での露出は小さい。

### 6-3. MANUAL に入った瞬間から惰行が始まる

`lon_manual` になると縦も運転者入力（`cmd.throttle = m.throttle` ★）。ペダルから足を離していれば
スロットル 0 ＝ 即エンジンブレーキ。「AI の速度を保ったまま横だけ手動」ではない。
**片方のドメインだけ渡したい場合は config の `override_lateral` / `override_longitudinal` で
`scenario` を指定する**（トグルは configured-manual ドメインにしか効かない）。

### 6-4. 同一フレームで両エッジが立つのは正常

物理ボタン 1 つが 2 ビットを立てるので、押下フレームは `resume_edge` と `take_manual_edge` が
**必ず両方真**になる。`was_any_manual` だけが方向を決める。ここを「どちらか一方しか来ない」と
仮定した実装にすると、片方向だけ動く不可解なバグになる。

### 6-5. `prev_*` の更新を分岐の中に書かない

早期 return が複数ある関数なので、`prev_take_manual_pressed_` の更新を分岐内に置くと
**押しっぱなしで毎フレーム再発火**する。resume 側と同じく、分岐に入る前に無条件で更新する ★。

---

## 7. 検証

### 7-1. ユニット（`GT_esmini/test/unit/manualdrive/test_OverrideManager.cpp`）

必須ケース:

1. `AUTO + TAKE_MANUAL 立ち上がり → 両ドメイン MANUAL・JustTransitionedToManual() 真`
2. `MANUAL + AUTO_RESUME 立ち上がり → AUTO`（既存の回帰。トグル追加で壊れていないこと）
3. **押しっぱなし 100 フレームで遷移はちょうど 1 回**（`prev_*` 更新位置の検査）
4. **離してから再押下で 2 回目のトグルが起きる**
5. `button_takeover_ = false` のとき TAKE_MANUAL を無視する（キルスイッチ）
6. `override_lateral = "scenario"` のとき横は AUTO のまま縦だけ MANUAL になる
7. `manual_explicit_` 中は `auto_return_timeout` で AUTO へ戻らない／
   閾値ラッチで入った MANUAL は従来どおり戻る（除外が広がりすぎていないこと）
8. `enabled_ = false` ではトグルが効かない（T6 の仕様固定）

### 7-2. ヘッドレスプローブ（新規 `GT_esmini/test/headless/vd_button_takeover_probe.py`）

`f7_reverse_split_latch_probe.py` の合成入力パターンを流用し、UDP のボタンフィールドに
**ビット 8** を注入して往復を見る:

- `override.lateral` が false→true→false→true と **2 往復**すること
- `ffb.target_active` が MANUAL 化で false、AUTO 復帰で **true に戻る**こと
  （サーボの解放と再武装。ここが動かないなら計器かサーボ配線が死んでいる）
- `override.takeover_pressed` が押下フレームだけ true（単発性）
- `driver.steer` が MANUAL 中は運転者軸を追従すること

### 7-3. ゲート

- `/gates -FailOnBehavioral` — 挙動バッチは全て stub 入力なので **deviation 0 が期待値**。
  ゼロでなければ配線ミス。
- Web バックエンドの config ラウンドトリップ試験（`test_manual_drive_wire_shape_roundtrip.py` /
  `test_per_run_manual_drive_config.py` と同じ流儀）に新キーを 1 件足す。

### 7-4. 実機 G29（ヘッドレスでは代替不可）

`SDLFFBSink` は `GT_ENABLE_SDL2` ビルドでしかコンパイルされないため、力の実挙動は実機でしか取れない ☆。
見る順:

1. **二重オープン**（T5）— VD と MD が同じデバイスを開いて暴れないか。起動直後に手を離して観察
2. 三角ボタンで MANUAL に入った瞬間、**サーボの力が抜けるか**（路面感が戻るか）
3. もう一度押して AUTO に戻り、**サーボが再武装するか**
4. 2〜3 を 3 往復。往復のたびに `input_source_->Init()` が走っていないこと（T5 のガード）

---

## 8. 未確認事項（実装時に潰すこと）

- `SDL2WheelInput::read_btn` が単一ビット前提かどうか（2 ビット同時に立てる書き換えの形）☆
- VD と MD が同じ G29 を同時に開いたときの SDL2 の実挙動 ☆ — 参照カウントと考えているが記録が無い
- 進行中（未コミット）の MD 側移管作業と本作業の**同一ファイル衝突**:
  `OverrideManager.hpp/.cpp`、`ControllerManualDrive.cpp`、`ManualDriveCoordinator.cpp` は
  現在ワーキングツリーに未コミット変更がある ★。**着手前に先行作業をコミットするか、
  ブランチを分けること。**
- 本プランは全てソース読解に基づく。プローブもゲートも 1 本も実行していない。
