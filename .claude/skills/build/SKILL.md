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
   ホストプロセス再起動（ウィンドウリロード/拡張再起動）で子プロセスごと死ぬ。
   **必ず次の形を使う**（PowerShellを噛ませずcmakeを直接起動する）:
   ```powershell
   $env:VSLANG = "1033"          # MSBuildを英語出力に。文字化けした日本語はgrepできない
   $p = Start-Process -FilePath "cmake" `
        -ArgumentList "--build","build","--config","Release" `
        -RedirectStandardOutput "<scratchpad>/build.log" `
        -RedirectStandardError  "<scratchpad>/build.err" `
        -WindowStyle Hidden -PassThru
   # 完了検知 = $p.HasExited / 判定 = $p.ExitCode（ログの文言で判定しない）
   ```

   > **この形でなければならない理由（2回踏んだ罠。次も踏む）**
   >
   > | やりがちな書き方 | 何が起きるか |
   > | :--- | :--- |
   > | `powershell -Command '... *> build.log'` | **ログが UTF-16LE で書かれる**。`grep "BUILD_EXIT="` も `grep "error C"` も**永久に一致しない**（`B` と `U` の間に `\x00` が入るため）。ビルドは正常なのに待機ループが延々空回りする |
   > | `Write-Host "BUILD_EXIT=$LASTEXITCODE" >> build.log` | **1バイトも書かれない**。`>>` は成功ストリーム(1)を捨てるが `Write-Host` は情報ストリーム(6)に書くため。完了マーカーが永遠に現れない |
   > | 完了を「ログ末尾の文言」で判定 | MSBuildは最終サマリを出さずに終わることがある。**プロセスの終了とExitCodeだけが信頼できる** |
   >
   > `Start-Process -RedirectStandardOutput` は子プロセスの生バイトをそのまま書くので
   > PowerShellのエンコーディングが介在しない。どうしても `*>` を使うなら、読む側で
   > UTF-16 をデコードすること（`python -c "open(p,'rb').read().decode('utf-16')"`）。
4. **ビルド後の「入ったか」判定はDLLのタイムスタンプとシンボル探索でやらない**。
   - `GT_Sim.exe` の日付が古いのは**stale ではない**（DLLを動的ロードする薄い殻なので再リンクされない）
   - 非エクスポート関数はDLLの文字列に出ないので、シグネチャ変更の確認に文字列grepは使えない
   - **決定的なのはユニットテストの実行**。`build/GT_esmini/test/Release/test_ScenarioReaderParsing.exe
     --gtest_filter=*<対象>*` を叩き、新規テストが実在して緑になることで確認する

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
