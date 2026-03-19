# build

C++プロジェクトをビルドし、DLLをDriverScript/binにコピーします。

## 使用方法

```
/build [--target GT_Sim|GT_esminiLib|all]
```

ターゲットを指定しない場合はプロジェクト全体をビルドします。

## 実行内容

1. C++ビルド実行
2. DLLコピー: `build/GT_esmini/Release/*.dll` → `DriverScript/bin/`
3. ビルド結果サマリ表示

## 実装詳細

```bash
# ビルド実行
cmake --build "e:\Repository\GT_esmini\esmini\build" --config Release ${1:+--target $1}

# ビルド成功時のみDLLコピー
if [ $? -eq 0 ]; then
  cp build/GT_esmini/Release/*.dll DriverScript/bin/ 2>/dev/null
  cp build/GT_esmini/Release/GT_Sim.exe DriverScript/bin/ 2>/dev/null
  echo ""
  echo "=== Build Output ==="
  ls -lh build/GT_esmini/Release/*.dll build/GT_esmini/Release/*.exe 2>/dev/null
  echo ""
  echo "=== Copied to DriverScript/bin/ ==="
  ls -lh DriverScript/bin/*.dll DriverScript/bin/*.exe 2>/dev/null
fi
```

## トラブルシューティング

### DLLコピー失敗
GT_SimまたはPythonプロセスが実行中の場合、DLLがロックされてコピーに失敗します。
先にプロセスを停止してください。

### CMake構成が古い
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```
で再構成してからビルドしてください。
