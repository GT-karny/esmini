# 実機ホイールで制御移管を測った一次記録（2026-07-29、07-30）

**この文書は測定の記録である。凍結して扱い、後から書き換えない。**
手順は [`field-test/realwheel_split_test.md`](../field-test/realwheel_split_test.md) と
[`field-test/realwheel_reverse_test.md`](../field-test/realwheel_reverse_test.md) にある。

| 項目 | 内容 |
| :--- | :--- |
| 対象 | 横=人/縦=AI（順構成）と 横=AI/縦=人（逆構成）の2構成 |
| ハードウェア | Logitech G29（ユーザー GT-karny の実機） |
| ビルド | 順構成 `GT_Sim_v0.14.1-dev-split01` / 逆構成 `GT_Sim_v0.14.1`（2026-07-30 未明、HEAD `e78bde41` 相当） |
| 実施者 | ユーザー本人（目視・操作）＋走行ログ |

---

## 1. 順構成（横は人、縦は AI）の記録

シナリオ `08_handoff/scenario_realwheel_split_md_vd.xosc`、config `manual_drive_realwheel_split.json`。

### 力覚の解放（G29 接続状態で測定）

- ManualDrive はデバイスを開くが、**開始 0.000 秒の時点で力の出力を止めている**。
  ログの `no longer integrating` の行がその瞬間。
- VirtualDriver は `input_type=stub` で動くのでデバイスに触れる経路を持たない。
- 終了時に `ManualDriveController: Deactivated — FFB released` が出る。

→ **デバイスを開くのは1つだけ、その力は開始直後に落ちる。両方が同時に力を出す窓は無い。**

### この測定が主張できないこと

**「実機のトルクが物理的にゼロになったこと」は測っていない。**
確認したのは制御の流れがそこへ到達したことまでである。
`SDLFFBSink::Update()` は周期ログを出さないので、「ログが無い＝力が無い」は証拠にならない。

---

## 2. 逆構成（横は AI、縦は人）の記録

シナリオ `08_handoff/scenario_realwheel_reverse_split.xosc`、config `manual_drive_realwheel_reverse.json`
（`ffb.target_track_enabled: true`、`auto_resume_button: 3`）。

### 2.1 安全ハーネスの発火

飽和ガード `ffb.safety_max_saturation_seconds` が実機で発火することを確認した。
発火条件は実効力 **0.570 以上が 2.0 秒継続**（`ffb.safety_saturation_ratio: 0.95` × 到達可能上限 0.6）。

### 2.2 オーバーライドとボタン復帰

ユーザー本人がボタンを押して、次の往復を確認した。

| 段階 | 確認内容 |
| :--- | :--- |
| 発火 | ハンドルを強く回すと MANUAL へラッチし、そこから先は運転者の操作どおりに車が曲がる |
| 復帰 | Triangle（△、G29 ボタン3）の押下で AUTO へ復帰し、**サーボが再アクティブ化する** |
| 自動復帰 | `auto_return_timeout` を無効にしてあるため、放置では戻らない。ボタンが唯一の手段 |

### 2.3 停止時の舵保持

カーブ途中で停止したときに AI の舵が保持されるかを見た。

| 系統 | 修正前 | 修正後 |
| :--- | :--- | :--- |
| ヘッドレス（順構成側の計測） | 停止で舵が反対向きの全舵角へ振れる（旋回中の **5.7 倍**） | 旋回中とほぼ同じ角度で保持（**0.98 倍**） |
| ヘッドレス（逆構成） | — | **-0.065 rad → -0.105 rad**（符号保持） |
| **実機（目視）** | — | **2026-07-30、前輪の向きが保たれることを確認** |

### 2.4 テレメトリ復路 driver.steer

**2026-07-30 の実機ログで `driver.steer` が -0.083〜0.578 の範囲で運転者の軸を追従していた。**
以前は分割構成だと常に `0.0` のまま凍っていた（`1fa408b9` / `74814b61` で複製経路を追加）。

### 2.5 曲がりきれる速度の上限（ヘッドレス実測）

道路は半径 49.1 m のカーブ。AI が車線を保てる速度は `max_lateral_accel = 2.0 m/s²` から
√(2.0 × 49.1) ≒ 9.9 m/s ＝ **約 36 km/h** と決まる。

| 速度 | 横加速度 | 結果 |
| :--- | :--- | :--- |
| 30 km/h (8.6 m/s) | 1.5 m/s² | 車線内（lane_offset 0.33 m） |
| 37 km/h (10.3 m/s) | 2.2 m/s² | 車線内 |
| 53 km/h (14.6 m/s) | 4.4 m/s² | 膨らんで車線を外れる |

逆構成では速度が運転者の担当なので、AI は「曲がりきれない速度で進入した」ときに減速する手段を持たない。
車線を外れるのは操舵の不具合ではなく入りすぎである。

---

## 3. 計器の射程と既知の欠落

### 3.1 非積分側で凍るテレメトリ

`override.resume_pressed` と `ffb.gates.*` は、**逆構成のように VirtualDriver が非積分側のとき、
常に初期値のまま**（`resume_pressed=false`、`gates.*=0` / `"none"`）で凍る。

原因は `ControllerVirtualDriver::Step()` の非積分側 early-return
（`GT_esmini/src/control/ControllerVirtualDriver.cpp` の `if (!is_integrator) { ...; return; }`）である。
この分岐は `driver.*` と `override.lateral/longitudinal/manual_transition/auto_transition` と
`ffb.target_active/commanded_force/position_error/target_norm/sample_effective_force` を複製して書く一方、
`override.resume_pressed` と `ffb.gates.*` は複製していない。**未修正。**

**機能そのものは動く**（§2.2 のとおり実機で確認済み）。死んでいるのは
「なぜ発火した／しなかったかを後から診断する」ためのテレメトリだけである。

### 3.2 この欠落で実際に誤診しかけた（2026-07-30）

`override.resume_pressed` が `false` のまま、`ffb.gates.effective_force` / `actual_norm` / `residual` が
`0` のままというログを見て、**「ボタンが効いていない」「検出器が動いていない」と読みかけた。**
実際は正常なセッションだった。帰属は `override.auto_transition` / `override.lateral` の遷移と
`ffb.commanded_force` の値で見ること。

### 3.3 存在しないキーを読むと無警告で 0 が返る

生 JSON を `dict.get(key, 0)` で読むと、キーが無い場合に無警告で `0` が返り「力が出ていない」ように見える。
`ffb.gates.target_track` や `ffb.gates.total` のようなキーは**存在しない**。
実在するキーは `VirtualDriverTelemetryJson.cpp` が唯一の正典であり、一覧は
[`field-test/realwheel_reverse_test.md`](../field-test/realwheel_reverse_test.md) §6 にある。

### 3.4 `ffb.gates.*` は1フレーム遅れ

`ffb.gates` はこのフレームの `ffb.*` を書く**前**のサンプルに対する診断である
（根拠は `VirtualDriverTelemetryJson.cpp` 冒頭のコメント）。
素朴に同じフレーム番号で突き合わせると1フレームずれる。

---

## 4. 関連

- しきい値と計器の検定規律：[`measurement_discipline.md`](measurement_discipline.md)
- 検出しきい値の調整指針（利用者向け）：[`../guides/ffb_override_tuning.md`](../guides/ffb_override_tuning.md)
- ドメイン別分担の設計：[`../design/domain_split_ownership.md`](../design/domain_split_ownership.md)
