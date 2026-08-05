# feature:F8 — ハンコン軸割り当て設定（Wheel Axis Mapping）

> **状態**: 実装済（2026-08-06）。既定値は G29 レイアウト＝F8 以前のハードコードと同一なので、既存設定ファイルの挙動は不変。
> **関連**: [feature:F7](../../docs/tech_debt_audit_2026-06.md)（FFB / 介入検出）, `GT_esmini/config/manual_drive.json`, `GT_esmini/config/virtual_driver.json`

## 1. なぜ必要だったか

F8 以前、`SDL2WheelInput` は次の2つを**コード内に固定**していた。

- 軸の並び: `0=ステア, 1=スロットル, 2=ブレーキ, 3=クラッチ`
- ペダルの生値規約: G29 の「解放 = +32767 / 全踏み = -32768」

どちらも**デバイス側の事実**であってシミュレータの性質ではない。Logitech G923 は軸の並びが異なることが観測され、設定ファイルにそれを表現する手段が一つも無かったため、「ブレーキを踏むとクラッチが動く」状態を回避できなかった。

## 2. 設定キー

`manual_drive.json` の `input` ブロック（ボタン割り当てと同じ場所・同じ流儀）。

| キー | 既定 | 意味 |
| :--- | :--- | :--- |
| `steer_axis` | 0 | ステア軸 index。`-1` = 未割り当て |
| `steer_invert` | false | 正規化後に符号反転。**FFB の力の向きも同時に反転する**（§4） |
| `steer_raw_center` / `steer_raw_full` | 0 / 32767 | 中央 / 右フルロックの生値 |
| `throttle_axis` / `brake_axis` / `clutch_axis` | 1 / 2 / 3 | ペダル軸 index |
| `<pedal>_raw_released` / `<pedal>_raw_full` | 32767 / -32768 | 解放 / 全踏みの生値 |

VirtualDriver 側は `virtual_driver.json` の `sdl2_*` プレフィクス付き同名キー（VD も同じ `SDL2WheelInput` を使うため、片方だけ直すと VD 走行で誤読する）。

**ペダルに invert フラグは無い。** `raw_released > raw_full` が反転を意味する（G29 がこれ）。極性の表現を一つに限定してあるので、「二重に反転指定して自分と矛盾する」状態が作れない。

## 3. 軸番号の調べ方

```powershell
build\GT_esmini\Release\GT_WheelProbe.exe --list
build\GT_esmini\Release\GT_WheelProbe.exe --device 0 --hz 30
```

`GT_WheelProbe` は読み取り専用の SDL2 プローブで、JSON 行を stdout に流す。Web UI の「Axis Mapping」節（ManualDrive Settings パネル）はこれを裏で起動してライブバーを描き、「Detect」ボタンで動かした軸を割り当てつつ解放値／全踏み値も同時に校正する。

- **ブラウザの Gamepad API は軸には使わない。** ブラウザは独自の index 空間を持つため、「動かした軸を割り当てる」UI がシミュレータの読む番号とは違う番号を自信満々に書き込む。probe は実行時と同じ SDL・同じ index 空間で読む。
- probe の正規化値は**本番と同じ C++ 正規化器**（`WheelAxisMapping`）を通す。マッピングは CLI 引数で渡されるので、**保存前の編集中の値**もそのままプレビューできる。
- `GT_ENABLE_SDL2=ON` のビルドでのみ生成される（既定 OFF）。無い場合 API は 503 とその理由を返し、パネルは手打ち編集だけを提供する。

## 4. FFB の符号（安全上の注意）

`SDLFFBSink` の力の符号は軸の極性と結びついている（正の力 = ホイールを左へ = 生値が負方向）。軸が反転した機種で**読み戻しだけ直して力の符号を直さないと、F7 のターゲット追従サーボが目標から遠ざかる方向に押す＝正のフィードバックになる**。

そのため `steer_invert` は `SteerAxisSpec::SignFactor()` 1か所からのみ読み、

- 軸の読み戻し（`ReadPhysicalWheelNorm`）
- CONSTANT エフェクトの出力（`UpdateConstantEffect`）

の両方に同じ係数を掛ける。ユニットテスト `SteerInvertFlipsSignAndSignFactorAgrees` がこの一致を恒等式として固定している。方向を持つのは CONSTANT だけで、SPRING/DAMPER は大きさの係数なので反転不要。

反転設定を実機で初めて試すときは `ffb.safety_max_saturation_seconds` を有効にしてから走らせること。

## 5. 非自明な事実（実測）

### 5-1. デバイスは「列挙されるのに何も報告しない」ことがある

2026-08-06、接続済み G29 で `GT_WheelProbe` が全4軸 raw=0 を返し続けた。独立計器（PySDL2、`scripts/ffb_spike/.venv`）でも同一結果なので**コード側の問題ではなくデバイス状態**。名前・軸数・ボタン数は正しく取れるので、列挙の成功は「値が来ている」ことを何も保証しない。

G29 のペダル規約では raw=0 は正規化 0.4999…（≒半踏み）になる。したがって**正規化値だけを描くパネルは、何も言っていないホイールに対して「ペダルが半分踏まれている」と表示する**。これを防ぐため probe は各軸について「開始以降に一度でも非ゼロを報告したか」(`reported`) を返し、UI は未報告軸を `no report` と表示する。ランタイム側の同じ問題は F7 以前から「解放センチネル」で対処されている（§5-2）。

### 5-2. 解放センチネルは `raw_released != 0` の機種でしか成立しない

Windows/DirectInput は JoystickOpen 後しばらく raw=0 を返す。G29 ではこれが「半踏み」に見え、`OverrideManager` の throttle 閾値を踏んで縦方向が MANUAL にラッチしてしまう（F7 で対処済）。F8 でこのセンチネルは軸ごとの `raw_released` に一般化したが、**`raw_released == 0` の機種ではガードを無効化しなければならない**。その機種では raw=0 が正しい「解放」であり、センチネルを効かせると raw=0 が永久に「未報告」と判定されてペダルが死ぬ。

### 5-3. 既定値の回帰は「0.5」ではない

旧 `NormalizePedal` は `(32767 - raw) / 65535` なので raw=0 は **0.49999237**。ユニットテストで 0.5 を期待すると、実装が旧挙動を正しく再現していても落ちる（実際に一度落ちた）。「挙動不変」の主張は旧式そのものに対して書くこと。

### 5-4. `GT_WheelProbe` は SDL2main を使わない

Windows では `SDL.h` が `main` を `SDL_main` に置換するため、素の console ツールは `SDL_MAIN_HANDLED` + `SDL_SetMainReady()` を使う。SDL2main をリンクすると WinMain 経路を引き込む。

### 5-5. probe が CommonMini を引かない理由

esmini のロガーは既定で stdout に書く。probe の stdout は web backend が読む JSON ストリームなので、ログ 1 行で壊れる（加えて起動場所に log.txt を作る）。だから probe は `CommonMini` も `ManualDriveConfig` もリンクせず、`WheelAxisMapping.cpp` だけを直接コンパイルする。

## 6. 検証

| 層 | 内容 |
| :--- | :--- |
| ユニット | `test_WheelAxisMapping.cpp` 16 件（G29 既定値の回帰錨、非反転/部分レンジ/非対称校正、invert 両極性、センチネル両極性、範囲外 index の報告と負の対照、config パース、keyboard キーとのエイリアス回帰） |
| backend | `test_wheel_axis_mapping_api.py`（wire↔flat 往復、出荷 config の全キー網羅、per-run writer への到達、probe 不在時の 503、`_mapping_args` の全キー変換） |
| 実機 | **未完**: 手元の G29 が §5-1 の状態のため、非 G29 レイアウトでの実走行確認と FFB 反転の確認は G923 接続時に実施する |

ユニット層は `GT_ENABLE_SDL2` に依存しない（既定 OFF・CI も OFF なので、条件付きにすると検査が黙って消える）。
