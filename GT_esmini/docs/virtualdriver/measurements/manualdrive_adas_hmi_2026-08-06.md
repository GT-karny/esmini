# 手動運転中 ADAS の運転席表示（REQ-AD-029 段a/b の目視確認）

**確認日**: 2026-08-06 · **状態**: 凍結（一次記録・後から書き換えない）
**対象**: `HvdGaugePanel` の Driver Assistance 節
**要求**: `req-vd-ad:REQ-AD-029` 段a（状態・設定値の表示）／段b（警報の提示）
**区分**: 検証計画 §6 の「目視＋フロントテスト」——描画はフロントの領分で自動 matcher の
対象外、供給側（HVD emit → web バックエンド射影）だけが自動判定の対象。

## どう撮ったか（合成データではない）

実アプリを実際に走らせて撮った。

1. 回帰バッチが生成した ManualDrive シナリオ（`test_results/regression/manualdrive_adas/
   <scenario>/*.manualdriverun.xosc`、ADAS 有効の注入 config 付き）を
   `POST /api/scenarios/upload` → `POST /api/simulations`（`headless`、`osi.enabled`、
   `no_realtime: false`）で web バックエンドから起動。
2. 走行中に `ws://127.0.0.1:8000/ws/osi/<job>` を直接購読し、`host_vehicle_data`
   メッセージが `adas_functions[]` を実際に運んでいることを確認（下記ログ）。
3. 同じ job の `/simulations/<job>` ページを headless Edge（CDP）で開き、
   走行中のフレームをスクリーンショット。

つまり画面に出ている数字は、**製品の C++ が OSI HVD（UDP 48199）へ出し、
osi_bridge が受け、`_hvd_to_json` が射影し、WebSocket で届いたもの**である。
段c（表示専用の面2直結経路を新設しない）が守られていることは、この経路以外に
供給元が無いという事実そのものが示している。

## 撮れたもの

### ACC の状態と設定値（段a）

![ACC](images/manualdrive_adas_hmi_acc_2026-08-06.png)

`md_acc_setting_changes`（空いた道路で設定速度を走行中に4回変える）の t≈10 s。

- `ACC` の点が点灯（ACTIVE）、`AEB` / `FCW` は暗点（UNAVAILABLE＝この構成では切ってある）
- 設定値が `set 82 km/h · gap 1.6 s`——運転者が操作する量（設定速度・車間段階）を
  運転者の単位で出す。実配線は `gt.acc.set_speed_mps` = 22.8 m/s、
  `gt.acc.thw_setting_s` = 1.600。
- **3値規律が画面上でも3つに見える**ことが要点である。ACTIVE だけがアクセント色を持ち、
  STANDBY（見張っていて撃たなかった）と UNAVAILABLE（切ってある／所有していない）は
  別々の淡さで出る。ここを2値に潰すと、切ってある機能と静かな機能が同じ絵になる。

### LDW 警報（段b）

![LDW](images/manualdrive_adas_hmi_ldw_warning_2026-08-06.png)

`md_lka_human_steer`（運転者が意図的に操舵して車線を横切る）の t≈16 s。

- 警報バンドに `Lane departure`、`LKA` / `LDW` が ACTIVE。
- **警報の名前は「警報の内容」であって「フラグを載せている行」ではない**。
  設計 §8-4 は警報フラグを介入側の行に置く（FCW は `gt.aeb.warning`、
  LDW は `gt.lka.warning`）ので、素直に行名を出すと画面には `AEB` / `LKA` と出る。
  それは「どのモジュールが上げたか」であって「何が危ないか」ではない。
  最初の実装がまさにそう出ており、実機の画面を見て直した——
  **合成データを眺めていては気づけない種類の間違いで、目視確認をこの区分に置いている理由そのもの**である。

## WebSocket ペイロードの実測（供給側の一次証拠）

`md_acc_setting_changes` 走行中の 1 フレーム（抜粋）:

```json
{ "key": "gt.acc", "name": 10, "state": 6, "state_name": "active",
  "detail": { "gt.acc.set_speed_mps": "17.336", "gt.acc.effective_cap_mps": "17.336",
              "gt.acc.thw_setting_s": "1.600", "gt.acc.thw_stage": "1",
              "gt.acc.engaged": "true", ... },
  "driver_override": { "present": true, "active": false, "reasons": [] },
  "custom_state": "" }
```

`driver_override.present=true, active=false`（評価して上書き無しと測った）が
present 無し（誰も書いていない）と区別できる形で届いている。
この区別を潰さないことは pytest 側（`GT_esmini/web/backend/tests/
test_osi_stream_adas_functions.py`）で恒久化した。

## 一緒に見つかった既存の欠陥（本作業の射程外・未修正）

**`Vehicle Telemetry` の時刻表示が `t = 18446744073.71s`** になっている。
`HvdMessage.sim_time` の由来は `hvd.timestamp`（秒＋ナノ秒）で、この桁は
64bit 符号なしの折り返し（2^64 ns ≒ 1.845e10 s）であり、負値が unsigned として
読まれたときの値と一致する。本フェーズの変更（`adas_functions` の追加）とは
独立で、変更前から同じ表示になっている——同じ画面の `Live OSI Data` 側の
`t = 10.35s`（GroundTruth 由来）は正しいので、HVD 側のタイムスタンプ書き込みが
疑わしい。**記録のみ。是正は別作業**（HMI の描画拡張と一緒に直すと、
どちらの変更が何を動かしたか分からなくなる）。
