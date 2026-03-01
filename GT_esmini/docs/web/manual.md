# GT_Sim Web UI / REST API マニュアル

GT_Sim Web は、ブラウザからシミュレーションの実行・結果確認を行うための Web アプリケーションです。
REST API を通じた外部システムからの自動実行にも対応しています。

## 技術スタック

| レイヤー | 技術 |
|:---|:---|
| バックエンド | Python FastAPI + uvicorn |
| フロントエンド | React + TypeScript + Vite |
| CSS | Tailwind CSS |
| 状態管理 | TanStack Query (React Query) |
| DB | SQLite (aiosqlite) |

すべてのライブラリは MIT / BSD / Apache 2.0 ライセンスであり、商用利用可能です。

## ディレクトリ構成

```
GT_esmini/web/
├── start_server.py          # サーバー起動スクリプト
├── pyproject.toml            # Python 依存定義
├── gt_sim.db                 # SQLite DB（自動生成）
├── settings.json             # 永続化設定（自動生成）
│
├── backend/
│   ├── main.py               # FastAPI アプリ
│   ├── config.py             # パス解決・デフォルト設定
│   ├── api/                  # API ルーター
│   │   ├── scenarios.py
│   │   ├── roads.py
│   │   ├── scripts.py
│   │   ├── controller_config.py
│   │   ├── simulations.py
│   │   ├── results.py
│   │   ├── config_api.py
│   │   └── osi_stream.py
│   ├── services/             # ビジネスロジック
│   │   ├── scenario_service.py
│   │   ├── road_service.py
│   │   ├── script_service.py
│   │   ├── simulation_runner.py
│   │   ├── result_service.py
│   │   ├── grpc_server.py
│   │   └── osi_bridge.py
│   ├── models/               # Pydantic スキーマ
│   └── db/                   # SQLite 管理
│
└── frontend/
    ├── package.json
    ├── vite.config.ts        # Vite 設定（API プロキシ含む）
    └── src/
        ├── pages/            # ページコンポーネント
        ├── components/       # 共通コンポーネント
        └── api/client.ts     # API クライアント
```

## セットアップ

### 前提条件

- Python 3.12+（`DriverScript/.venv/` を推奨）
- Node.js 20.19+
- GT_Sim.exe がビルド済み（`build/GT_esmini/Release/GT_Sim.exe`）

### 1. Python 依存パッケージのインストール

```bash
DriverScript\.venv\Scripts\pip.exe install fastapi uvicorn[standard] python-multipart aiosqlite pyyaml
```

### 2. フロントエンドのビルド

```bash
cd GT_esmini/web/frontend
npm install
npm run build
```

### 3. サーバー起動

```bash
# リポジトリルートから実行
DriverScript\.venv\Scripts\python.exe GT_esmini/web/start_server.py
```

起動すると以下のサービスが利用可能になります:

| サービス | アドレス | 説明 |
|:---|:---|:---|
| Web UI | http://127.0.0.1:8000/ | ブラウザ UI |
| Swagger UI | http://127.0.0.1:8000/docs | REST API ドキュメント |
| ReDoc | http://127.0.0.1:8000/redoc | REST API ドキュメント（別形式） |
| gRPC OSI | `0.0.0.0:50051` | OSI GroundTruth / HostVehicleData ストリーミング |

### 起動オプション

```bash
python GT_esmini/web/start_server.py --host 0.0.0.0 --port 8080 --reload
```

| オプション | デフォルト | 説明 |
|:---|:---|:---|
| `--host` | `127.0.0.1` | REST API バインドホスト |
| `--port` | `8000` | REST API バインドポート |
| `--reload` | OFF | ソース変更時に自動リロード（開発用） |

### 環境変数

| 環境変数 | デフォルト | 説明 |
|:---|:---|:---|
| `GT_SIM_GRPC_PORT` | `50051` | gRPC サーバーのポート番号 |
| `GT_SIM_CORS_ORIGINS` | *(なし)* | 追加 CORS オリジン（カンマ区切り、例: `http://localhost:3000`） |

### 開発モード（フロントエンド Hot Reload）

フロントエンドの開発時は、バックエンドとフロントエンドを別々に起動します:

```bash
# ターミナル 1: バックエンド
DriverScript\.venv\Scripts\python.exe GT_esmini/web/start_server.py --reload

# ターミナル 2: フロントエンド（Vite dev server）
cd GT_esmini/web/frontend
npm run dev
```

Vite dev server (`http://localhost:5173`) が API リクエストを `http://127.0.0.1:8000` にプロキシします。

## Web UI の使い方

### ページ一覧

| ページ | パス | 機能 |
|:---|:---|:---|
| Scenarios | `/scenarios` | シナリオ一覧・検索 |
| Run Simulation | `/simulations/new` | シミュレーション実行フォーム |
| Jobs | `/simulations` | ジョブ一覧・状態確認 |
| Job Detail | `/simulations/:id` | ジョブ詳細・メトリクス・結果DL |

### シミュレーション実行の流れ

1. **Scenarios ページ**でシナリオを選択し「Run」をクリック
   - または **Run Simulation** ページに直接移動
2. **コントローラーを選択**
   - **Default**: esmini 標準の defaultController
   - **Python Driver**: PythonDriverController（スクリプト・クラスも選択可能）
3. **実行パラメータを設定**
   - Frequency (Hz): シミュレーション周波数（デフォルト: 120 Hz）
   - Timeout (s): タイムアウト秒数（デフォルト: 60 秒）
   - Headless: 3D ビューア非表示（デフォルト: ON）
   - Record: sim.dat 記録（デフォルト: ON）
   - No Realtime: 最速実行（デフォルト: OFF = リアルタイム実行）
   - AutoLight: 自動ライト制御（デフォルト: ON）
   - OSI Output: OSI メッセージ出力（デフォルト: ON）
   - Window: 3D ビューアのウィンドウ位置・サイズ（Headless OFF 時のみ）
4. **「Run Simulation」** をクリック
5. **Job Detail ページ**に自動遷移し、実行状態をリアルタイムで確認
6. 完了後、**メトリクス**と**出力ファイル**（DAT, CSV, ログ等）を確認・ダウンロード

### コントローラー設定

| コントローラー | 用途 |
|:---|:---|
| Default | esmini 内蔵の defaultController。シナリオ定義通りの動作 |
| Python Driver | PythonDriverController。Python スクリプトによるカスタム制御 |

Python Driver 選択時に設定可能な項目:

- **Python Script**: 使用するスクリプトファイル（推奨スクリプトは星マーク付き）
- **Class Name**: スクリプト内のコントローラークラス名
- **Enable trace logging**: Python 実行トレースの出力

### 実行結果

完了したジョブの詳細ページでは以下が確認できます:

- **Metrics**: シミュレーション概要（実行時間、フレーム数、平均速度等）
- **Final State**: 最終フレームの状態（位置、速度、ヘディング等）
- **Output Files**: 出力ファイルのダウンロード
  - `sim.dat` — esmini バイナリ記録
  - `sim.csv` — CSV 形式（自動変換）
  - `stdout.txt` / `stderr.txt` — プロセス出力
  - `python_trace.jsonl` — Python トレースログ（Python Driver 使用時）

## 外部システムからの利用

REST API を使って外部システム（CI/CD、テストフレームワーク等）からシミュレーションを実行できます。

### curl によるシミュレーション実行例

```bash
# シナリオ一覧取得
curl http://127.0.0.1:8000/api/scenarios

# シミュレーション実行
curl -X POST http://127.0.0.1:8000/api/simulations \
  -H "Content-Type: application/json" \
  -d '{
    "scenario_id": "lane_change_simple",
    "controller": {
      "controller_type": "python",
      "python": {
        "script": "DriverScript/pythondriver/scenario_drive_embedded.py",
        "class": "EmbeddedController",
        "trace_enabled": true
      }
    },
    "execution": {
      "headless": true,
      "record": true,
      "hz": 120,
      "no_realtime": false,
      "timeout": 60,
      "osi": {"enabled": true, "ip": "127.0.0.1"},
      "autolight": true,
      "window": {"x": 60, "y": 60, "w": 1280, "h": 720},
      "extra_args": []
    }
  }'

# ジョブ状態確認
curl http://127.0.0.1:8000/api/simulations/{job_id}

# メトリクス取得
curl http://127.0.0.1:8000/api/results/{job_id}/metrics

# ファイルダウンロード
curl -O http://127.0.0.1:8000/api/results/{job_id}/files/sim.dat
```

### Swagger UI

`http://127.0.0.1:8000/docs` で Swagger UI にアクセスし、全 API エンドポイントをインタラクティブにテストできます。

## 外部エディタ連携

GT_Sim Web API は、OpenSCENARIO エディタなどの外部アプリケーションからの利用に対応しています。
シナリオ・道路の動的アップロード、リアルタイム速度制御、gRPC/WebSocket による OSI データストリーミングを組み合わせることで、
エディタ上でのシナリオ再生ワークフローを実現できます。

### 連携フロー

```
エディタ                         GT_Sim Web API                    GT_Sim.exe
  │                                   │                               │
  ├─ POST /api/roads/upload ─────────>│  .xodr 保存                   │
  │<── road_id ──────────────────────│                               │
  │                                   │                               │
  ├─ POST /api/scenarios/upload ─────>│  .xosc 保存（パス絶対化）      │
  │<── scenario_id (tmp_xxx) ────────│                               │
  │                                   │                               │
  ├─ POST /api/simulations ──────────>│  GT_Sim.exe 起動 ────────────>│
  │<── job_id ───────────────────────│                               │
  │                                   │                               │
  ├─ gRPC StreamGroundTruth ─────────>│<── OSI UDP ──────────────────│
  │<── GroundTruth stream ───────────│                               │
  │                                   │                               │
  ├─ PUT /api/simulations/{id}/speed─>│  Named Pipe ────────────────>│
  │                                   │  速度変更                      │
  │                                   │                               │
  ├─ GET /api/simulations/{id} ──────>│  状態確認                      │
  │<── completed ────────────────────│                               │
```

### CORS 設定

外部エディタ（別オリジン）からのアクセスを許可するには、環境変数 `GT_SIM_CORS_ORIGINS` を設定します:

```bash
# 単一オリジン
GT_SIM_CORS_ORIGINS=http://localhost:3000

# 複数オリジン（カンマ区切り）
GT_SIM_CORS_ORIGINS=http://localhost:3000,http://localhost:4200
```

デフォルトでは `http://localhost:5173` と `http://127.0.0.1:5173`（Vite dev server）のみ許可されています。

### シナリオアップロード API

外部エディタから XOSC XML を直接アップロードして一時シナリオを作成できます。

#### `POST /api/scenarios/upload`

XOSC XML をボディとして送信し、一時シナリオ ID を取得します。

```bash
curl -X POST http://127.0.0.1:8000/api/scenarios/upload \
  -H "Content-Type: text/xml" \
  -d @my_scenario.xosc
```

**レスポンス** (201):

```json
{
  "scenario_id": "tmp_a1b2c3d4e5f6",
  "entities": [
    {"name": "Ego", "model": "car_white"},
    {"name": "Target", "model": "car_red"}
  ],
  "road_file": "fabriksgatan.xodr",
  "expires_at": "2025-01-01T01:00:00"
}
```

- `scenario_id` は `tmp_` プレフィックス付きの一時 ID
- 既存の `POST /api/simulations` にそのまま `scenario_id` として渡せます
- XML 内の `RoadNetwork/LogicFile/@filepath` や `CatalogLocations` の相対パスは自動的に絶対パスに変換されます
- 一時シナリオは 1 時間後に自動削除されます

#### `DELETE /api/scenarios/upload/{scenario_id}`

一時シナリオを手動削除します。`tmp_` プレフィックスの ID のみ削除可能です。

```bash
curl -X DELETE http://127.0.0.1:8000/api/scenarios/upload/tmp_a1b2c3d4e5f6
```

### 道路アップロード API

カスタム道路（OpenDRIVE）をアップロードし、シナリオ内で参照できます。

#### `POST /api/roads/upload`

```bash
curl -X POST http://127.0.0.1:8000/api/roads/upload \
  -H "Content-Type: text/xml" \
  -d @my_road.xodr
```

**レスポンス** (201):

```json
{
  "road_id": "tmp_road_a1b2c3d4e5f6",
  "road_path": "/absolute/path/to/tmp_road_a1b2c3d4e5f6.xodr"
}
```

返却された `road_path` をアップロードする XOSC の `RoadNetwork/LogicFile/@filepath` に指定することで、
カスタム道路上でのシミュレーションが可能です。

#### `DELETE /api/roads/upload/{road_id}`

```bash
curl -X DELETE http://127.0.0.1:8000/api/roads/upload/tmp_road_a1b2c3d4e5f6
```

### 速度制御 API

実行中のシミュレーションの再生速度をリアルタイムに変更できます（Windows のみ）。
Named Pipe を介して GT_Sim プロセスの壁時計ペーシングを制御します。
シミュレーションの物理 dt には影響せず、再生速度のみが変化します。

#### `PUT /api/simulations/{job_id}/speed`

```bash
curl -X PUT http://127.0.0.1:8000/api/simulations/{job_id}/speed \
  -H "Content-Type: application/json" \
  -d '{"speed_factor": 2.0}'
```

| パラメータ | 範囲 | 説明 |
|:---|:---|:---|
| `speed_factor` | 0.1 〜 100.0 | 再生速度倍率（1.0 = リアルタイム、2.0 = 2倍速） |

**レスポンス** (200):

```json
{
  "job_id": "abc123",
  "speed_factor": 2.0
}
```

**注意**: `no_realtime` モードで起動されたシミュレーションには速度制御は効きません（すでに最速実行のため）。

### WebSocket OSI ストリーミング

`/ws/osi/{job_id}` WebSocket エンドポイントで、実行中のシミュレーションの OSI データをリアルタイムに受信できます。

各オブジェクトの JSON には `name` フィールドが含まれ、OpenSCENARIO の Entity 名が取得できます:

```json
{
  "timestamp": 1.5,
  "objects": [
    {
      "id": 0,
      "name": "Ego",
      "x": 100.0,
      "y": 50.0,
      "heading": 1.57,
      "speed": 15.0
    }
  ]
}
```

`name` は OSI GroundTruth の `source_reference[type="net.asam.openscenario"].identifier[]` から
`entity_name:` プレフィックスのエントリをパースして取得しています。

### gRPC OSI ストリーミング

gRPC サーバー（デフォルト: `0.0.0.0:50051`）で以下の 2 つのストリーミング RPC を提供します:

| サービス | RPC | 説明 |
|:---|:---|:---|
| `GroundTruthService` | `StreamGroundTruth` | OSI GroundTruth メッセージのサーバーストリーミング |
| `HostVehicleDataService` | `StreamHostVehicleData` | OSI HostVehicleData メッセージのサーバーストリーミング |

ポートは環境変数 `GT_SIM_GRPC_PORT` で変更可能です（デフォルト: `50051`）。

### エディタ連携の完全な curl 例

```bash
# 1. 道路アップロード（カスタム道路を使用する場合）
ROAD_RESP=$(curl -s -X POST http://127.0.0.1:8000/api/roads/upload \
  -H "Content-Type: text/xml" -d @my_road.xodr)
ROAD_PATH=$(echo $ROAD_RESP | jq -r '.road_path')

# 2. シナリオアップロード
SCENARIO_RESP=$(curl -s -X POST http://127.0.0.1:8000/api/scenarios/upload \
  -H "Content-Type: text/xml" -d @my_scenario.xosc)
SCENARIO_ID=$(echo $SCENARIO_RESP | jq -r '.scenario_id')

# 3. シミュレーション実行
JOB_RESP=$(curl -s -X POST http://127.0.0.1:8000/api/simulations \
  -H "Content-Type: application/json" \
  -d "{
    \"scenario_id\": \"$SCENARIO_ID\",
    \"controller\": {\"controller_type\": \"default\"},
    \"execution\": {
      \"headless\": true,
      \"record\": true,
      \"hz\": 120,
      \"no_realtime\": false,
      \"timeout\": 60,
      \"osi\": {\"enabled\": true, \"ip\": \"127.0.0.1\"}
    }
  }")
JOB_ID=$(echo $JOB_RESP | jq -r '.job_id')

# 4. 速度変更（2倍速）
curl -X PUT http://127.0.0.1:8000/api/simulations/$JOB_ID/speed \
  -H "Content-Type: application/json" -d '{"speed_factor": 2.0}'

# 5. 状態確認
curl http://127.0.0.1:8000/api/simulations/$JOB_ID

# 6. クリーンアップ（任意、1時間後に自動削除）
curl -X DELETE http://127.0.0.1:8000/api/scenarios/upload/$SCENARIO_ID
```

---

## 設定の永続化

Web UI で変更した実行パラメータやコントローラー設定は `GT_esmini/web/settings.json` に自動保存されます。
このファイルを削除すると、`config.py` で定義されたデフォルト値にリセットされます。

## 出力ディレクトリ

シミュレーション結果は `test_results/web/{job_id}/` に保存されます。

## トラブルシューティング

### ポートが使用中

```
[Errno 10048] error while attempting to bind on address ('127.0.0.1', 8000): Only one usage of each socket address is normally permitted
```

別のプロセスがポート 8000 を使用しています。以下で特定して停止:

```bash
netstat -ano | findstr :8000
taskkill /PID <PID> /F
```

### GT_Sim.exe が見つからない

`build/GT_esmini/Release/GT_Sim.exe` が存在することを確認してください。
ビルド方法は [ビルド・インストール](../getting-started/build_install.md) を参照。

### XOSC パスエラー（exit_code=-1）

シミュレーションが即座に失敗する場合、XOSC 内の相対パス解決に問題がある可能性があります。
バックエンドが自動的にパスを絶対化しますが、カスタム XOSC では問題が発生することがあります。

---

## 配布パッケージの作成

`GT_esmini/web/pyinstaller/build_package.py` を使用して、スタンドアロンの配布パッケージを作成できます。

### 前提条件

パッケージ化を実行する前に、以下が揃っていることを確認してください。

| 必要なもの | パス | 準備方法 |
|:---|:---|:---|
| GT_Sim.exe | `build/GT_esmini/Release/GT_Sim.exe` | CMake ビルド（Release） |
| GT_esminiLib.dll | `build/GT_esmini/Release/GT_esminiLib.dll` | CMake ビルド（Release） |
| フロントエンド | `GT_esmini/web/frontend/dist/index.html` | `npm run build`（`--build-frontend` で自動化可） |
| Embedded Python | `thirdparty/python-embed/python-3.12.10-embed-amd64/` | リポジトリに同梱 |
| Python 仮想環境 | `DriverScript/.venv/` | `pip install -r DriverScript/requirements.txt` |
| PyInstaller | `DriverScript/.venv` 内 | `pip install pyinstaller` |

### パッケージ作成コマンド

```powershell
# リポジトリルートから実行
# --build-frontend: npm run build を自動実行してから処理（推奨）
DriverScript\.venv\Scripts\python.exe GT_esmini/web/pyinstaller/build_package.py `
    --version 0.5.0 `
    --output dist/ `
    --build-frontend
```

| オプション | デフォルト | 説明 |
|:---|:---|:---|
| `--version` | `0.1.0` | バージョン文字列（例: `0.5.0`） |
| `--output` | `dist/` | 出力先ディレクトリ |
| `--build-frontend` | OFF | `npm run build` を先に実行する |
| `--skip-pyinstaller` | OFF | PyInstaller ステップをスキップ（再アセンブル時） |
| `--no-zip` | OFF | ZIP アーカイブの作成をスキップ |

### ビルドのステップ

1. **前提条件チェック** — 必要ファイルの存在確認
2. **フロントエンドビルド** — `npm run build`（`--build-frontend` 指定時）
3. **PyInstaller** — Web サーバーを `gt_sim_web.exe` としてフリーズ
4. **パッケージレイアウトの構築** — 各ファイルを出力ディレクトリに配置
5. **pip のブートストラップ** — Embedded Python に pip をセットアップ
6. **ZIP アーカイブ生成** — 配布用 zip を作成

### 出力内容

```
dist/
├── GT_Sim_v0.5.0/        # 展開済みパッケージ
│   ├── GT_Sim.bat         # 起動スクリプト（ダブルクリックで起動）
│   ├── pip.bat            # pip ラッパー
│   ├── README.txt         # 使い方
│   ├── bin/               # GT_Sim.exe、DLL、Embedded Python
│   ├── server/            # PyInstaller 出力（gt_sim_web.exe）
│   ├── scripts/           # ユーティリティスクリプト
│   ├── DriverScript/      # Python コントローラースクリプト
│   ├── resources/         # シナリオ・道路・3D モデル
│   ├── config/            # 設定ファイル（編集可能）
│   ├── docs/              # ドキュメント（マニュアル・API リファレンス等）
│   └── data/              # 実行時データ・シミュレーション結果
└── GT_Sim_v0.5.0.zip      # 配布用アーカイブ
```

### エンドユーザーの使い方

1. ZIP を展開する
2. `GT_Sim.bat` をダブルクリック
3. ブラウザが `http://127.0.0.1:8000` で自動的に開く
4. Ctrl+C でサーバー停止
