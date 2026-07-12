---
name: package
description: GT_Simの配布パッケージをフルビルドする（C++ビルド → フロントエンド+PyInstaller+Electron → ZIP）。`/package` が呼ばれたとき、またはユーザーがパッケージ作成・配布ビルド・EXE配布・ZIPアーカイブ作成について言及したときに使用する。
---

# package

GT_Simの配布パッケージをフルビルドします。C++ビルド → PyInstallerサーバー → Electronデスクトップアプリの順で実行します。

> **Single source of truth**: `scripts/build_package.ps1` がパイプライン全体の正式実装です。
> このSKILL.mdにインラインのcmake/pyinstaller/npmコマンドは一切記載しません。
> パイプラインの詳細は `scripts/build_package.ps1` 本体を参照してください。

## 使用方法

```
/package --version <VERSION>
```

`--version` は必須引数。例: `/package --version 0.7.0`

## 実行コマンド

リポジトリルートから以下を実行する:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_package.ps1 -Version <VERSION>
```

`<VERSION>` をユーザーが指定したバージョン文字列に置換する（例: `0.9.0`）。

> **長時間実行の起動作法**: このパイプラインは10分級。Claude Codeの`run_in_background`タスクは
> ホストプロセス再起動（ウィンドウリロード/拡張再起動）で子プロセスごと死ぬため、
> `Start-Process powershell -ArgumentList '-ExecutionPolicy','Bypass','-File','scripts/build_package.ps1','-Version','<VERSION>' -RedirectStandardOutput <scratchpad>/package.log -WindowStyle Hidden -PassThru`
> で**detached起動**し、ログのtailとプロセスID生存で完了検知する（v0.12リリース作業で2回中断された実績への対策）。

## 前提条件

以下が揃っていることを確認してから実行する。不足している場合はユーザーに伝える。

- `thirdparty/python-embed/python-3.12.10-embed-amd64/` — 組み込みPythonが配置済みであること
- `GT_esmini/web/.venv/Scripts/python.exe` — ビルド用venv（PyInstaller + webバックエンド依存）が存在すること。未セットアップの場合は `.\scripts\setup_web_venv.ps1` を実行する。
- Node.js がインストール済みであること（フロントエンド+Electronビルドに必要）
- **ビルド前にGT_Sim.exeを停止**: Electronやサーバーが起動中だとDLL/EXEがロックされ `PermissionError` になる。ビルド前にアプリを閉じるようユーザーに伝える。

## スクリプトが実行するパイプライン

`scripts/build_package.ps1` は以下のステップを順番に実行する:

| ステップ | 内容 |
|---|---|
| **Step 0** | CMake configure（Visual Studio 17 2022, x64, 必須オプション付き） |
| **Step 1** | C++ビルド（`GT_Sim`, `GT_esminiLib`, `esminiRMLib`, `GT_RoadGen` ターゲットのみ） |
| **Step 1b** | アーティファクトのステージング: `GT_Sim.exe`, `GT_RoadGen.exe`, DLL群を `DriverScript/bin/` へコピー。`esminiRMLib.dll` と `SDL2.dll` も `BuildRelease` と `DriverScript/bin/` にステージ |
| **Step 2** | `GT_esmini/web/pyinstaller/build_package.py` を `GT_esmini/web/.venv` で実行: フロントエンド(`npm run build`) + PyInstaller + アセンブリ（**--no-zip** でZIPを後回し） |
| **Step 3** | Electronビルド: `npm install` + `npm run build` + `@electron/packager` でパッケージ化 → `PackageDir` にマージ |
| **Step 3b** | Electronのlocalesを `en-US.pak` のみに絞る（容量削減） |
| **Step 4** | `PackageDir` をZIPアーカイブ化（`dist/GT_Sim_v<VERSION>.zip`） |

### 利用可能なパラメータ

| パラメータ | 効果 |
|---|---|
| `-Version <x.y.z>` | **必須**。バージョン文字列 |
| `-SkipCMake` | CMake configure + C++ビルドをスキップ |
| `-SkipFrontend` | フロントエンドビルド(`npm run build`)をスキップ |
| `-SkipPyInstaller` | PyInstallerビルドをスキップ |
| `-SkipElectron` | Electronビルド + パッケージ化をスキップ |
| `-NoZip` | ZIPアーカイブ作成をスキップ |

例（C++ビルド済みでフロントエンドのみ変更した場合）:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_package.ps1 -Version 0.9.0 -SkipCMake
```

## 出力

- `dist/GT_Sim_v<VERSION>/` — 展開済みパッケージディレクトリ（Electron + サーバー + リソース）
- `dist/GT_Sim_v<VERSION>.zip` — 配布用ZIPアーカイブ

**起動方法**: `dist/GT_Sim_v<VERSION>/GT_Sim.exe` をダブルクリック

## アーキテクチャ

```
GT_Sim.exe (Electron シェル)
  └→ child process: server/gt_sim_web.exe (FastAPI + uvicorn, PyInstaller製)
       ├→ React SPA を http://127.0.0.1:8000 で提供
       ├→ REST API (/api/*)
       ├→ WebSocket (/ws, OSIストリーム)
       └→ subprocess: bin/GT_Sim.exe (C++シミュレーションエンジン)
```

> **ELECTRON_RUN_AS_NODE**: VS Code内から実行する場合、この環境変数が`1`にセットされているとElectronがNode.jsモードで動作する。Electron起動時は`delete env.ELECTRON_RUN_AS_NODE`が必須（`dev.mjs`では対策済み）。

## 主要ファイル

- `scripts/build_package.ps1` — **パイプライン実装の単一正解**（このSKILL.mdが委譲する先）
- `GT_esmini/web/pyinstaller/build_package.py` — フロントエンド+PyInstaller+アセンブリ+ZIPの内部実装
- `GT_esmini/web/pyinstaller/gt_sim_web.spec` — PyInstaller spec定義
- `GT_esmini/web/pyinstaller/gt_sim_web_entry.py` — 凍結バイナリのエントリーポイント
- `GT_esmini/web/electron/` — Electronデスクトップアプリ（main process + preload）
- `GT_esmini/web/electron/src/main/server.ts` — FastAPI child process管理 + health check
- `GT_esmini/web/electron/scripts/bundle.mjs` — esbuildバンドルスクリプト

## トラブルシューティング

### DLLロックエラー (`PermissionError`)
GT_SimまたはPythonプロセスが起動中だとDLL/EXEファイルがロックされビルドが失敗する。
タスクマネージャーまたは以下で停止してからリトライ:
```powershell
Stop-Process -Name "GT_Sim" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "gt_sim_web" -Force -ErrorAction SilentlyContinue
```

### GT_RoadGen.exe が見つからない警告
Step 1b で `WARNING: GT_RoadGen.exe not found` が出た場合、大規模OpenDRIVE道路の生成が低速になる（またはハング）。
`-SkipCMake` を使わずにフルビルドするか、`GT_RoadGen` ターゲット単体をビルドしてから再実行する。

### フロントエンドビルド失敗
`GT_esmini/web/frontend/` に入り `npm install` を実行してからリトライ。

### PyInstallerビルド後にフロントエンドが古い
`-SkipPyInstaller` を使うとサーバーexe内のフロントエンドが古いままになる。
TSX/CSS変更がある場合はPyInstallerをスキップしないこと。
