# package

EXE配布パッケージをビルドします（フロントエンド → PyInstaller → ZIP）。

## 使用方法

```
/package --version <VERSION>
```

## 前提条件

- `build/GT_esmini/Release/GT_Sim.exe` (C++ビルド済み)
- `thirdparty/python-embed/python-3.12.10-embed-amd64/` (組み込みPython)

## 実行内容

1. フロントエンドビルド (`GT_esmini/web/frontend/`)
2. PyInstallerパッケージビルド
3. 配布用ZIPアーカイブ作成
4. 結果サマリ表示

## 実装詳細

```bash
cd e:\Repository\GT_esmini\esmini

# Step 1: フロントエンドビルド
cd GT_esmini/web/frontend && npm run build && cd ../../..

# Step 2: パッケージビルド
DriverScript/.venv/Scripts/python.exe GT_esmini/web/pyinstaller/build_package.py \
    --version ${1:?--version required} --output dist/

# Step 3: 結果表示
echo ""
echo "=== Package Output ==="
ls -lh dist/GT_Sim_v${1}/ 2>/dev/null | head -20
ls -lh dist/GT_Sim_v${1}.zip 2>/dev/null
echo ""
echo "起動方法: dist/GT_Sim_v${1}/GT_Sim.bat → http://127.0.0.1:8000"
```

## 主要ファイル

- `GT_esmini/web/pyinstaller/build_package.py` — ビルドスクリプト
- `GT_esmini/web/pyinstaller/gt_sim_web.spec` — PyInstaller spec
- `GT_esmini/web/pyinstaller/gt_sim_web_entry.py` — 凍結エントリーポイント

## 注意事項

- 開発DB (`GT_esmini/web/gt_sim.db`) はパッケージに含まれない
- パッケージ版は `PACKAGE_ROOT/data/gt_sim.db` を初回起動時に空で作成
- C++ビルドが未完了の場合は先に `/build` を実行すること

## 出力

- `dist/GT_Sim_v<VERSION>/` — 展開済みパッケージ
- `dist/GT_Sim_v<VERSION>.zip` — 配布用アーカイブ