---
name: build
description: GT_esminiのC++ビルド（Protocol A）とDLLステージングを正しい手順で実行する。`/build` が呼ばれたとき、またはユーザーがビルド・リビルド・cmake・DLL更新・ビルドエラー・リンクエラーに言及したとき、そしてテストゲート実行前のリビルドが必要なときに必ず使用する。
---

# build — Protocol A ビルド & DLLステージング

## 基本コマンド

```powershell
# 構成（初回 or CMakeLists変更時のみ）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# ビルド（全体）
cmake --build build --config Release

# ビルド（ターゲット指定）
cmake --build build --config Release --target GT_Sim
# 主要ターゲット: GT_Sim / GT_esminiLib / GT_esminiLib_static / esminiRMLib / GT_RoadGen
```

出力先: `build/GT_esmini/Release/`（GT_Sim.exe, GT_esminiLib.dll, GT_RoadGen.exe 等）

## DLLステージング（ビルド成功後）

検証ハーネス（gt_sim_test / web backend）は `DriverScript/bin/` のDLLも参照する:

```powershell
Copy-Item build/GT_esmini/Release/*.dll DriverScript/bin/ -Force
Copy-Item build/GT_esmini/Release/GT_Sim.exe, build/GT_esmini/Release/GT_RoadGen.exe DriverScript/bin/ -Force
```

## 鉄則

1. **ゲート実行前は必ず全リビルド**。フォーク（GT_RoadManager.cpp等）はビルド実体なので、
   「pristineをstashして無関係証明」は成立しない。編集後の未リンクDLLは偽FAIL/偽PASSの温床
   （ODRプログラムで実害2回: stale-DLL幽霊、偽陰性プローブ）。
2. **MSBuildインクリメンタルは並行編集直後のcppをスキップすることがある**（obj時刻>src時刻）。
   文言変更がDLLに反映されない疑いがあれば: 対象cppをtouch→再ビルド→
   `Select-String -Path <dll> -Pattern '<新文言>' -Encoding ascii` で機械確認。
3. **10分級の長時間ビルドはdetached起動**。Claude Codeの`run_in_background`タスクは
   ホストプロセス再起動（ウィンドウリロード/拡張再起動）で子プロセスごと死ぬ:
   ```powershell
   $p = Start-Process powershell -ArgumentList '-NoProfile','-Command',
        'cmake --build build --config Release *> <scratchpad>/build.log' `
        -WindowStyle Hidden -PassThru
   # 完了検知はログのtail + プロセスID生存チェック（Get-Process -Id $p.Id）
   ```

## トラブルシューティング

| 症状 | 対処 |
| :--- | :--- |
| DLLコピー失敗 / `PermissionError` | GT_Sim/Pythonプロセスがロック中。`Stop-Process -Name GT_Sim,gt_sim_web -Force -ErrorAction SilentlyContinue` |
| GT_OSMP_FMU (`esmini_fmu`) リンクエラー | **既知の既存破損**（Protocol B、CLAUDE.md §3）。自分の変更とは無関係。GT_Sim/GT_esminiLib/testsが通れば成功扱い |
| `gt_core` で imgui.h 未検出 | ViewerBaseリンク欠落の既知パターン（`if(USE_OSG)` で `GT_CORE_TARGET` にもViewerBaseをリンク） |
| `far`/`near` で C2059 構文エラー | MSVC予約マクロ（windows.h）。識別子に使わない |

## ビルド後のヘッドレス実行スモーク

- **PowerShellで実行**（MSYS/git-bash経由は `GT_esminiLib.dll: cannot open shared object` で失敗）
- 組込Python依存のため、事前に `thirdparty/python-embed/python-3.12.10-embed-amd64` と
  Releaseディレクトリを `$env:PATH` に追加（無いと `0xC0000135 DLL_NOT_FOUND`）
- VSCode配下のシェルは `ELECTRON_RUN_AS_NODE=1` を継承する。GT_Sim.exe（Electron同梱版）を
  起動する場合はこの環境変数を除去してから起動

```powershell
$env:PATH = "thirdparty/python-embed/python-3.12.10-embed-amd64;build/GT_esmini/Release;$env:PATH"
build/GT_esmini/Release/GT_Sim.exe --osc resources/xosc/lane_change.xosc --headless --fixed_timestep 0.05
# EXIT 0 で完走すればOK
```

ビルド後の検証は `/gates` を使う。配布パッケージは `/package`。
