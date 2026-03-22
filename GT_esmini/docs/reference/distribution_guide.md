# 配布用パッケージ構成ガイド

GT_Sim を Electron デスクトップアプリとして配布するためのパッケージ構成と作成手順を説明します。

## 現行パッケージ構成 (v0.8+)

v0.8 以降、GT_Sim は **Electron デスクトップアプリ** としてパッケージ化されています。
起動はルートの `GT_Sim.exe`（Electron）をダブルクリックするだけで、内部的にWebサーバーを自動起動し、ブラウザウインドウを開きます。

```
GT_Sim_v{VERSION}/
├── GT_Sim.exe                    # Electronデスクトップアプリ（メインエントリ）
├── README.txt                    # ユーザー向けクイックスタートガイド
├── LICENSE                       # Mozilla Public License 2.0
│
├── bin/                          # エンジン本体 + Embedded Python + DLL
│   ├── GT_Sim.exe                # CLIシミュレーション実行ファイル
│   ├── GT_esminiLib.dll          # GT_esmini コアライブラリ (~17MB)
│   ├── esminiLib.dll             # esmini コアライブラリ (~16MB)
│   ├── SDL2.dll                  # ハンドルコントローラー入力用
│   ├── python312.dll             # Embedded Python 3.12
│   ├── python312.zip             # Python 標準ライブラリ
│   └── *.pyd                     # Python 拡張モジュール (asyncio, sqlite3, ssl 等)
│
├── server/                       # Web サーバー（PyInstaller 出力）
│   ├── gt_sim_web.exe            # FastAPI サーバー実行ファイル
│   ├── _internal/                # PyInstaller 内部ファイル
│   └── frontend/dist/            # React SPA（ビルド済みフロントエンド）
│
├── config/                       # 設定ファイル（ユーザー編集可能）
│   ├── manual_drive.json         # ManualDrive コントローラー設定
│   ├── real_vehicle_params.json  # 車両物理パラメータ
│   └── comparison_thresholds.yaml # テスト閾値
│
├── resources/                    # シナリオ・道路・3Dモデル
│   ├── xosc/                     # OpenSCENARIO ファイル
│   ├── xodr/                     # OpenDRIVE ファイル
│   └── models/                   # 3D モデルデータ
│
├── data/                         # ランタイムデータ（自動生成）
│   └── (シミュレーション結果等)
│
├── docs/                         # ドキュメント
│
└── [Electron/Chromium files]     # Electron ランタイム
    ├── chrome_*.pak
    ├── libEGL.dll
    ├── v8_context_snapshot.bin
    └── ...
```

## 使い方（配布先ユーザー向け）

### 起動

1. ZIP を展開する
2. `GT_Sim.exe` をダブルクリック
3. Electronアプリが起動し、Web UIが表示される

> バッチファイル（`GT_Sim.bat`）は不要です。Electronアプリが内部でWebサーバーを自動起動します。

### ManualDrive設定

`config/manual_drive.json` を編集するか、Web UI のManualDrive設定パネルから設定できます：
- ボタンマッピング（シフト、ウインカー、ヘッドライト等）
- FFBパラメータ（バネ係数、ダンパー係数等）
- ドメイン制御（横方向/縦方向の手動・シナリオ切り替え）

### 車両パラメータ

`config/real_vehicle_params.json` でピッチ・ロール・サスペンション・パワートレインのパラメータを車種別にカスタマイズできます。

---

## パッケージ作成手順

### 前提条件

| 項目 | 要件 |
|:---|:---|
| Python | 3.12+（`DriverScript/.venv/` を推奨） |
| Node.js | 20.19+ |
| PyInstaller | `pip install pyinstaller` |
| GT_Sim ビルド | `build/GT_esmini/Release/GT_Sim.exe` が存在すること |
| フロントエンド | `GT_esmini/web/frontend/dist/` がビルド済みであること |
| Embedded Python | `thirdparty/python-embed/python-3.12.10-embed-amd64/` が存在すること |

### `/package` コマンド（推奨）

Claude Code の `/package` スキルを使うと、全工程を自動実行できます：

```
/package --version 0.8.0
```

### 手動ビルド

#### 1. C++ ビルド

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

#### 2. フロントエンドビルド

```bash
cd GT_esmini/web/frontend
npm install
npm run build
```

#### 3. パッケージ作成

```bash
DriverScript\.venv\Scripts\python.exe GT_esmini/web/pyinstaller/build_package.py \
    --version 0.8.0 --output dist/ --build-frontend
```

### オプション一覧

| オプション | デフォルト | 説明 |
|:---|:---|:---|
| `--version` | `0.1.0` | バージョン文字列 |
| `--output` | `dist/` | 出力ディレクトリ |
| `--build-frontend` | OFF | `npm run build` も実行 |
| `--skip-pyinstaller` | OFF | PyInstaller をスキップ（既にビルド済みの場合） |
| `--no-zip` | OFF | zip 作成をスキップ |

### ビルドスクリプトの構成

| ファイル | 役割 |
|:---|:---|
| `GT_esmini/web/pyinstaller/build_package.py` | メインパッケージングスクリプト |
| `GT_esmini/web/pyinstaller/gt_sim_web.spec` | PyInstaller ビルド仕様 |
| `GT_esmini/web/pyinstaller/gt_sim_web_entry.py` | PyInstaller 用エントリポイント |
| `GT_esmini/web/electron/` | Electron デスクトップシェル |

---

## ポイント

1. **DLLの依存関係**:
   - `bin/GT_Sim.exe`（CLI版）は軽量（約74KB）であり、実体は `GT_esminiLib.dll`（約17MB）にあります。
   - OSG（OpenSceneGraph）は静的リンクされており、別途OSGのDLLは不要です。
   - `SDL2.dll` はManualDriveコントローラーのハンドル入力に必要です。

2. **Electronアプリ**:
   - ルートの `GT_Sim.exe` はElectronラッパー（約188MB、Chromiumランタイム含む）です。
   - 内部で `server/gt_sim_web.exe` を自動起動し、ヘルスチェック後にウインドウを表示します。
   - カスタムタイトルバー（フレームレスウインドウ）で動作します。

3. **設定ファイル**:
   - `config/` 配下の設定ファイルはユーザーが直接編集可能です。
   - Web UI からも設定変更が可能です（`settings.json` に自動保存）。

## ライセンス準拠に関する注意点

esmini (MPL 2.0) および依存ライブラリ (OSGPL等) を含むバイナリを配布する場合：

1. **ソースコードの提供義務 (MPL 2.0)**: READMEに「本ソフトウェアはesmini (MPL 2.0) を使用しています。ソースコードは [リポジトリURL] から入手可能です」と明記。
2. **ライセンスファイルの同梱**: `LICENSE` ファイル (MPL 2.0) を同梱。
3. **無保証の免責**: 「現状有姿 (As-Is)」での提供であることを明記。

## 技術メモ

- **PyInstaller モード**: `--onedir`（`--onefile` より起動が高速）
- **デュアルモード**: `config.py` が `sys.frozen` または環境変数で開発/パッケージモードを自動判定
- **pip ブートストラップ**: ビルド時に `ensurepip` を試行し、失敗時は `get-pip.py` をダウンロードしてフォールバック
