# 配布用パッケージ構成ガイド

`GT_Sim.exe` をRelease版として配布する推奨フォルダ構成を以下に示します。

## 推奨ディレクトリ構成

```
GT_esmini_Release/
├── bin/
│   ├── GT_Sim.exe            # 実行ファイル
│   ├── GT_esminiLib.dll      # 推奨: 核となるDLL (OSI機能等を含む)
│   └── (esminiLib.dll)       # ※依存関係によっては必要だが、GT_esminiLibが静的リンクしている場合は不要
├── resources/                # 必須: esminiのリソースフォルダ
│   ├── fonts/
│   ├── models/
│   ├── objects/
│   └── ... (esmini/resourcesの中身を全てコピー)
├── scenarios/                # サンプルシナリオ
│   ├── demo.xosc
│   └── ...
├── LICENSE                   # ライセンスファイル
└── README.md                 # 説明書
```

## ポイント

1.  **DLLの依存関係**:
    - `GT_Sim.exe` は非常に軽量（約16KB）であり、実体は `GT_esminiLib.dll`（約16MB）にあります。必ず同じフォルダに配置してください。
    - OSG（OpenSceneGraph）はビルドログにより `OSG_LIBRARY_STATIC` が定義されているため、静的リンクされています。別途OSGのDLLやプラグインフォルダを配布する必要はありません。

2.  **Resourcesフォルダ**:
    - esminiは実行時に `../resources` または `./resources` を探索して3Dモデルやフォントを読み込みます。
    - `bin` フォルダと同階層（`../resources`）に `resources` フォルダを置くのが標準的です。

3.  **実行方法**:
    - ユーザーは `bin/GT_Sim.exe` を実行します。
    - 引数でシナリオパスを指定する場合: `GT_Sim.exe --osc ../scenarios/demo.xosc`

## 作成手順（例）

1. 任意の作業フォルダ（例: `package`）を作成
2. `e:\Repository\GT_esmini\esmini\build\GT_esmini\Release` から `GT_Sim.exe` と `GT_esminiLib.dll` を `package/bin` にコピー
3. `e:\Repository\GT_esmini\esmini\resources` フォルダを `package/resources` としてコピー
4. 配布したいXOSCファイルを `package/scenarios` にコピー
5. 全体をZIP圧縮


## ライセンス準拠に関する注意点

esmini (MPL 2.0) および依存ライブラリ (OSGPL等) を含むバイナリを配布する場合、以下の点に注意が必要です。

1.  **ソースコードの提供義務 (MPL 2.0)**:
    - esminiのソースコード（および `GT_OSIReporter.cpp` などMPL適用ファイルへの変更分）を提供可能にする必要があります。
    - **実用的な対応**: READMEに「本ソフトウェアはesmini (MPL 2.0) を使用しています。ソースコードは [リポジトリURL] から入手可能です」と明記するのが一般的です。

2.  **ライセンスファイルの同梱**:
    - `LICENSE` ファイル (MPL 2.0) を同梱してください。
    - OSG (OpenSceneGraph) は静的リンクされていますが、OSGPL (LGPL + 例外条項) によりバイナリ配布は許可されています。念のため `3rd_party_terms_and_licenses` フォルダも同梱するか、READMEで言及することを推奨します。

3.  **無保証の免責**:
    - ユーザーに対して「現状有姿 (As-Is)」での提供であり、保証がないことを明記してください。

---

# GT_Sim Web パッケージング

GT_Sim Web（REST API + Web UI）を Python 環境不要のスタンドアロンパッケージとしてビルドし、zip で配布する手順です。

## 前提条件

| 項目 | 要件 |
|:---|:---|
| Python | 3.12+（`DriverScript/.venv/` を推奨） |
| Node.js | 20.19+ |
| PyInstaller | `pip install pyinstaller` |
| GT_Sim ビルド | `build/GT_esmini/Release/GT_Sim.exe` が存在すること |
| フロントエンド | `GT_esmini/web/frontend/dist/` がビルド済みであること |
| Embedded Python | `thirdparty/python-embed/python-3.12.10-embed-amd64/` が存在すること |

## パッケージング手順

### 1. GT_Sim 本体のビルド

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### 2. フロントエンドのビルド

```bash
cd GT_esmini/web/frontend
npm install
npm run build
```

### 3. パッケージ作成

```bash
DriverScript\.venv\Scripts\python.exe GT_esmini/web/pyinstaller/build_package.py \
    --version 0.1.0 --output dist/
```

完了すると `dist/GT_Sim_Web_v0.1.0/` と `dist/GT_Sim_Web_v0.1.0.zip` が生成されます。

### ワンライナー（フロントエンドビルドも含む）

```bash
DriverScript\.venv\Scripts\python.exe GT_esmini/web/pyinstaller/build_package.py \
    --version 0.1.0 --output dist/ --build-frontend
```

### オプション一覧

| オプション | デフォルト | 説明 |
|:---|:---|:---|
| `--version` | `0.1.0` | バージョン文字列（フォルダ名・README に反映） |
| `--output` | `dist/` | 出力ディレクトリ |
| `--build-frontend` | OFF | `npm run build` も実行 |
| `--skip-pyinstaller` | OFF | PyInstaller をスキップ（既にビルド済みの場合） |
| `--no-zip` | OFF | zip 作成をスキップ |

## 生成されるパッケージ構成

```
GT_Sim_Web_v{VERSION}/
├── GT_Sim_Web.bat          # ダブルクリック起動ランチャー
├── pip.bat                 # pip ラッパー（パッケージ追加用）
├── README.txt              # ユーザー向け説明
│
├── bin/                    # GT_Sim 本体 + Embedded Python + DLL
│   ├── GT_Sim.exe
│   ├── GT_esminiLib.dll
│   ├── python.exe          # pip 実行用
│   ├── python312._pth
│   ├── *.dll, *.pyd
│   └── Lib/site-packages/  # pip パッケージ格納先
│
├── server/                 # Web サーバー（PyInstaller 出力）
│   ├── gt_sim_web.exe
│   ├── _internal/
│   └── frontend/dist/      # React SPA
│
├── scripts/                # ユーティリティスクリプト
├── DriverScript/           # Python コントローラー（編集可能）
├── resources/              # xosc, xodr, models
├── config/                 # 設定ファイル（編集可能）
└── data/                   # ランタイムデータ（自動生成）
```

## 使い方（配布先ユーザー向け）

### サーバー起動

`GT_Sim_Web.bat` をダブルクリック → ブラウザが `http://127.0.0.1:8000` を自動オープン。

### Python パッケージの追加

`pip.bat` は Embedded Python の pip ラッパーです。全サブコマンドが使えます:

```bash
pip.bat install numpy pandas requests
pip.bat install -r requirements.txt
pip.bat install --proxy http://proxy:8080 numpy
pip.bat list
pip.bat uninstall numpy
```

追加パッケージは `bin/Lib/site-packages/` にインストールされ、PythonDriverController スクリプトから即座に `import` 可能です。

## ビルドスクリプトの構成

| ファイル | 役割 |
|:---|:---|
| `GT_esmini/web/pyinstaller/build_package.py` | メインパッケージングスクリプト |
| `GT_esmini/web/pyinstaller/gt_sim_web.spec` | PyInstaller ビルド仕様 |
| `GT_esmini/web/pyinstaller/gt_sim_web_entry.py` | PyInstaller 用エントリポイント |

## 技術メモ

- **PyInstaller モード**: `--onedir`（`--onefile` より起動が高速）
- **外部スクリプト配置**: `scripts/` と `DriverScript/` は PyInstaller にバンドルせず外部配置。ユーザーが編集可能。
- **デュアルモード**: `config.py` が `sys.frozen` または環境変数 `GT_SIM_WEB_PACKAGE_ROOT` で開発/パッケージモードを自動判定
- **pip ブートストラップ**: ビルド時に `ensurepip` を試行し、失敗時は `get-pip.py` をダウンロードしてフォールバック

