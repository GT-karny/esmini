---
name: package
description: GT_Simの配布パッケージをフルビルドする（C++ビルド → フロントエンド+PyInstaller+Electron → ZIP）。`/package` が呼ばれたとき、またはユーザーがパッケージ作成・配布ビルド・EXE配布・ZIPアーカイブ作成について言及したときに使用する。
---

# package

GT_Simの配布パッケージをフルビルドします。C++ビルド → PyInstallerサーバー → Electronデスクトップアプリの順で実行します。

## 使用方法

```
/package --version <VERSION>
```

`--version` は必須引数。例: `/package --version 0.7.0`

## 前提条件

以下が揃っていることを確認してから実行する。不足している場合はユーザーに伝える。

- `thirdparty/python-embed/python-3.12.10-embed-amd64/` — 組み込みPythonが配置済みであること
- Node.js がインストール済みであること（フロントエンド+Electronビルドに必要）

## 実行手順

作業ディレクトリは常にリポジトリルート (`e:\Repository\GT_esmini\esmini`) とする。

### Step 0: CMake Configure（必須オプション確認）

パッケージビルド前に、必ず以下のオプションで configure する。
キャッシュが古い場合やオプションが OFF になっている可能性があるため、**毎回明示的に指定する**。

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DUSE_OSG=ON \
  -DUSE_OSI=ON \
  -DUSE_SUMO=ON \
  -DUSE_IMPLOT=ON \
  -DUSE_SDL2=ON
```

**必須オプション一覧:**

| オプション | 値 | 理由 |
|---|---|---|
| `USE_OSG` | **ON** | 3D可視化（esmini viewer） |
| `USE_OSI` | **ON** | OSI GroundTruth 出力・gRPCストリーミング |
| `USE_SUMO` | **ON** | SUMO交通シミュレーション連携 |
| `USE_IMPLOT` | **ON** | リアルタイムプロット表示 |
| `USE_SDL2` | **ON** | レーシングホイール入力・フォースフィードバック |

> **注意**: `CACHE BOOL` のため、一度 OFF で configure すると `build/CMakeCache.txt` に残り、
> CMakeLists.txt のデフォルト ON が効かなくなる。明示指定で上書きする。

`build/` ディレクトリが存在しない場合もこのコマンドで新規作成される。

### Step 1: C++ビルド (GT_Sim)

```bash
cmake --build "e:/Repository/GT_esmini/esmini/build" --config Release
```

ビルド成功後、DLLとEXEをコピーする:

```bash
cp build/GT_esmini/Release/*.dll DriverScript/bin/ 2>/dev/null
cp build/GT_esmini/Release/GT_Sim.exe DriverScript/bin/ 2>/dev/null
```

DLLコピーが失敗する場合は、GT_SimやPythonプロセスが実行中でファイルがロックされている可能性がある。先にプロセスを停止するようユーザーに伝える。

### Step 2: パッケージビルド (フロントエンド + PyInstaller + アセンブリ)

`build_package.py` がフロントエンド(`npm run build`)、PyInstaller、アセンブリ、ZIP作成をすべて一括実行する。

```bash
DriverScript/.venv/Scripts/python.exe GT_esmini/web/pyinstaller/build_package.py \
    --version <VERSION> --output dist/
```

`<VERSION>` はユーザーが指定したバージョン文字列に置換する。

フロントエンドビルドが失敗した場合は、`cd GT_esmini/web/frontend && npm install` を実行してからリトライする。

**オプションフラグ:**
- `--skip-frontend` — フロントエンドビルドをスキップ（TSXに変更がない場合）
- `--skip-pyinstaller` — PyInstallerビルドをスキップ
- `--no-zip` — ZIPアーカイブ作成をスキップ

### Step 3: Electronデスクトップアプリビルド

PyInstallerで作成したサーバーをElectronシェルでラップする。

```bash
cd GT_esmini/web/electron && npm install && npm run build && cd "e:/Repository/GT_esmini/esmini"
```

次に `@electron/packager` でパッケージ化（`electron-builder` はWindows開発者モード未有効時にwinCodeSignのシンボリックリンクエラーが発生するため使わない）:

```bash
cd GT_esmini/web/electron && npx @electron/packager . GT_Sim --platform=win32 --arch=x64 --out=release --overwrite --ignore="node_modules" --ignore="src" --ignore="scripts" --ignore="tsconfig" --ignore="electron-builder.yml" && cd "e:/Repository/GT_esmini/esmini"
```

> **重要**: `--ignore` フラグで `node_modules`, `src`, `scripts` 等を除外すること。
> 除外しないと devDependencies（584MB超）がそのままバンドルされ、パッケージが肥大化する。

ビルド成果物をパッケージディレクトリに統合:

```bash
cp -r GT_esmini/web/electron/release/GT_Sim-win32-x64/* dist/GT_Sim_v<VERSION>/ 2>/dev/null
```

> **注意**: Electronはデスクトップアプリのシェル（ネイティブウィンドウ）を提供する。
> FastAPIサーバー（`server/gt_sim_web.exe`）はElectronからchild processとしてspawnされる。
> GT_Sim.exe（C++シミュレーション）はFastAPIからさらにsubprocessとして起動される。

### Step 4: 結果サマリ表示

```bash
echo ""
echo "=== Build Output ==="
ls -lh build/GT_esmini/Release/*.dll build/GT_esmini/Release/*.exe 2>/dev/null
echo ""
echo "=== Package Output ==="
ls -lh dist/GT_Sim_v<VERSION>/ 2>/dev/null | head -20
ls -lh dist/GT_Sim_v<VERSION>.zip 2>/dev/null
echo ""
echo "起動方法: dist/GT_Sim_v<VERSION>/GT_Sim.exe をダブルクリック"
```

## アーキテクチャ

```
GT_Sim.exe (Electron)
  └→ child process: server/gt_sim_web.exe (FastAPI + uvicorn, PyInstaller製)
       ├→ React SPA を http://127.0.0.1:8000 で提供
       ├→ REST API (/api/*)
       ├→ WebSocket (/ws, OSIストリーム)
       └→ subprocess: bin/GT_Sim.exe (C++シミュレーションエンジン)
```

## 主要ファイル

- `GT_esmini/web/pyinstaller/build_package.py` — メインビルドスクリプト（フロントエンド+PyInstaller+アセンブリ+ZIP）
- `GT_esmini/web/pyinstaller/gt_sim_web.spec` — PyInstaller spec定義（フロントエンドdistをdatasとしてバンドル）
- `GT_esmini/web/pyinstaller/gt_sim_web_entry.py` — 凍結バイナリのエントリーポイント
- `GT_esmini/web/electron/` — Electronデスクトップアプリ（main process + preload）
- `GT_esmini/web/electron/src/main/server.ts` — FastAPI child process管理 + health check
- `GT_esmini/web/electron/scripts/bundle.mjs` — esbuildバンドルスクリプト

## 出力

- `dist/GT_Sim_v<VERSION>/` — 展開済みパッケージディレクトリ（Electron + サーバー + リソース）
- `dist/GT_Sim_v<VERSION>.zip` — 配布用ZIPアーカイブ

## 注意事項

- **TSX変更は自動反映**: `build_package.py` がデフォルトで `npm run build` を実行するため、フロントエンドの変更は自動的にパッケージに含まれる。
- 開発用DB (`GT_esmini/web/gt_sim.db`) はパッケージに含まれない。パッケージ版は `PACKAGE_ROOT/data/gt_sim.db` を初回起動時に新規作成する。
- 各ステップは順番に実行する。前のステップが失敗した場合は次に進まない。
- エラーが発生した場合はログを確認し、原因をユーザーに報告する。
- **ELECTRON_RUN_AS_NODE**: VS Code内から実行する場合、この環境変数が`1`にセットされているとElectronがNode.jsモードで動作する。Electron起動時は`delete env.ELECTRON_RUN_AS_NODE`が必須（`dev.mjs`では対策済み）。
- **フロントエンド変更時はPyInstallerも再ビルド必須**: `--skip-pyinstaller` を使うとサーバーexe内のフロントエンドが古いままになる。TSX/CSS変更がある場合はPyInstallerをスキップしないこと。
- **パッケージビルド前にGT_Sim.exeを停止**: Electronやサーバーが起動中だとDLL/EXEがロックされ `PermissionError` になる。ビルド前にアプリを閉じるようユーザーに伝える。