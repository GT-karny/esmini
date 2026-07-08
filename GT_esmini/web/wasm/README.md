# GT_esmini esminiJS (WASM) — GT variant

This is the **GT_esmini variant of esminiJS**: the Emscripten/WASM binding of the
esmini scenario + RoadManager engine, with GT extensions wired in (GT_RoadManager
LHT/RHT swap, GT scenario extension actions, traffic-signal + vehicle-light
introspection, and the lane-change-aware route binding).

## Origin

Relocated out of the upstream core directory
`EnvironmentSimulator/Libraries/esminiJS/` to restore Clean Core separation
(audit **BND-1 / R5-U1**). The GT modifications were originally applied directly
on top of upstream esmini **v3.0.2** (fork merge-base `e504e8ae`), which was a
"Clean Core" violation. After this relocation, the core `esminiJS` directory is
**vanilla upstream** again and merges as a clean take-theirs against
upstream/master (v3.3.0).

Files here (all GT content; the upstream core copies are untouched / vanilla):

| File | Notes |
| :--- | :--- |
| `CMakeLists.txt` | Standalone emscripten project. Identical to the GT core copy except relative paths (this dir is 3 levels under the repo root). |
| `esminijs.cpp` / `esminijs.hpp` | `OpenScenario` class: XOSC sanitizer (R5-U3: rewraps bare `<PrivateAction><LightStateAction>` into the native `AppearanceAction` wrapper; light actions are now PARSED+EXECUTED by the native v3.3.0 `LightStateAction`, no longer stripped), `GT_ScenarioReader::ParseExtensionActions`, step API, vehicle-light (via `VehicleLightBridge::ReadLight`) / traffic-signal introspection. |
| `embind.cpp` | embind bindings for the step API, `StoryBoardEvent`/`ConditionEvent`, and the introspection functions. |
| `embind_rm.cpp` | `RoadManagerJS` wasm wrapper (coordinate conversion, no running scenario needed). |
| `gt_embind_route.cpp` | `GTRouteJS` — LaneIndependentRouter route binding (already lived here). |
| `build.sh` | Build helper (Git Bash + emsdk). |

## Path layout (relative to this directory)

- Core esmini modules: `../../../EnvironmentSimulator/Modules/...`
- Repo-root externals (pugixml / fmt / expr / yaml): `../../../externals/...`
- GT_esmini sources / headers: `../../src/...`, `../../include`
- GT route binding: `${CMAKE_CURRENT_SOURCE_DIR}/gt_embind_route.cpp`

## How to build

Proven local toolchain (from the original core build's `CMakeCache.txt`):

- Generator: **Ninja**
- Toolchain file: `E:/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`
- emsdk at `E:\emsdk`

### Git Bash

```bash
source /e/emsdk/emsdk_env.sh      # activate emsdk (PATH for emcc/emcmake/emmake)
cd GT_esmini/web/wasm
./build.sh                        # -> build/esmini.js
```

### Windows (cmd / PowerShell)

```bat
E:\emsdk\emsdk_env.bat
cd GT_esmini\web\wasm
mkdir build & cd build
emcmake cmake -G Ninja ..
emmake ninja
```

Or invoke the emscripten toolchain file directly without the `emcmake` wrapper:

```bat
cmake -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=E:/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake ^
  -S . -B build
cmake --build build
```

Output is a single-file module `build/esmini.js` (`-s SINGLE_FILE=1`,
`MODULARIZE=1`, `EXPORT_NAME="esmini"`).

## Notes

- The core `EnvironmentSimulator/Libraries/esminiJS/` is now vanilla upstream and
  must NOT carry GT edits.
- Builds clean against the current tree (upstream v3.4.0 core + GT_RoadManager
  fork + odr_side; verified P9b 2026-07-05, emscripten 5.0.2). The old note about
  being pinned to v3.0.2-era APIs is obsolete.
- `build/` is gitignored (see `.gitignore`).

## OpenSCENARIO エディタ側への申し送り(ODR 1.6-1.9 プログラム完了時点、2026-07-05)

esmini.js を同梱している下流(OpenSCENARIO エディタ等)向けのハンドオーバー。
**2026-07-02 より前のビルドを同梱している場合、差分は P9b 単体ではなく
OpenDRIVE 1.6-1.9 対応プログラム全体(P0〜P9b)**である。

### 1. esmini.js の差し替えが必須

- 旧ビルドは **odr_side 欠落のリンク切れ状態**だった(P9b で修理: CMakeLists が
  odr_side ソース一覧をコア RoadManager CMakeLists の `[GT_ODR:cmake]` APPEND
  リストから configure 時に自動抽出する — 以後 GT 側パーサが増えても再ビルド
  するだけで追随)。
- サイズ **5.4MB → 6.3MB(+17%)**(SINGLE_FILE 同梱のため初回ロードに影響)。
- 差し替え後の健全性確認: リポジトリ root を HTTP 配信して
  [`smoke/index.html`](smoke/index.html) を開く(レーンレイヤ+virtual junction の
  ロード/描画を自動 PASS/FAIL 判定)。ヘッドレスなら
  `msedge --headless --screenshot=... <URL>`。

### 2. API 互換性: 破壊的変更なし

`RoadManagerJS` / `OpenScenario` / `GTRouteJS` の embind シグネチャは不変。
`esmini()` が Promise を返す点も従来どおり(`await` 必須)。

### 3. ただし「同じファイルを読んだ結果」は変わる(挙動変更)

| 変更 | エディタで見える影響 |
|---|---|
| **`<include>` 入り xodr はロード失敗**(恒久ハードエラー仕様) | 以前は黙って通った可能性 → **`loadOpenDrive` の false を必ず UI でエラー表示すること**(最重要) |
| 合成オブジェクトの出現 | crossPath→横断歩道(id **900000000 番台**)/ bridge(910M)/ objectReference 複製(920M)。オブジェクト一覧 UI に巨大 id の新オブジェクトが増える |
| 信号の増加・分類変化 | signalReference がクローン実体化され信号数が増える。動的信号の TrafficLight 昇格条件が緩和され、1.8/1.9 地図で `getTrafficSignalStates` の件数が増え得る |
| 1.9 マルチレイヤ道路 | **常に permanent レイヤ**が選ばれる(wasm には環境変数が無く temporary マージは到達不能) |
| virtual junction | 本線途中分岐が正しく位置解決・ルーティングされる。本線スパン上の junction 所属は **false/-1** を返す仕様(v1) |
| コンソールログ | ロード時に `[GT_ODR] detected OpenDRIVE version …` info 行、未対応構造には `[ODR-UNSUPPORTED]` 警告(1.4/1.5 の既存地図は警告ゼロ保証) |

### 4. 未公開機能(欲しくなったら小規模な追加開発)

- レーンレイヤ/VJ メタデータの JSON API(`GT_RM_GetLaneLayersJson` 等)は
  **DLL 側 C API のみ**で embind 未公開。エディタで web UI 同等のメタデータ
  パネルを出す場合は `embind_rm.cpp` へ binding 追加(~半日)。
- temporary レイヤ表示も同様に `SetLaneLayerModeForTest` の binding 公開で可能。

### 5. 保守

- カバレッジは**手動スモークのみ**(CI レグなし、計画どおり)。esmini.js 更新時は
  上記スモークページ確認を習慣に(resync チェックリスト項 26 にも組込済み)。
- ビルドの罠: Git Bash 配下の cmd では `emsdk_env.bat` が効かない(bash 用
  export を出力する)— bash で `source /e/emsdk/emsdk_env.sh` を使うこと。
- 課題・保留機能の正典は
  [`../../docs/opendrive_16_19_support.md`](../../docs/opendrive_16_19_support.md) §3。
