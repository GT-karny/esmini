# GT_Sim Web UI / Electron デスクトップアプリ マニュアル

GT_Sim は、Electronデスクトップアプリまたはブラウザからシミュレーションの実行・管理・結果確認を行うためのアプリケーションです。
REST API を通じた外部システムからの自動実行にも対応しています。

## 技術スタック

| レイヤー | 技術 |
|:---|:---|
| デスクトップシェル | Electron (カスタムタイトルバー) |
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
├── frontend/
│   ├── package.json
│   ├── vite.config.ts        # Vite 設定（API プロキシ含む）
│   └── src/
│       ├── pages/            # ページコンポーネント
│       ├── components/       # 共通コンポーネント
│       └── api/client.ts     # API クライアント
│
└── electron/
    ├── package.json          # Electron 依存・ビルド設定
    └── src/
        ├── main/
        │   ├── index.ts      # メインプロセス（ウインドウ管理）
        │   └── server.ts     # サーバーブリッジ（自動起動・ヘルスチェック）
        └── preload/
            └── index.ts      # プリロードスクリプト（IPC）
```

## 起動方法

### 配布パッケージ（Electronアプリ）

配布パッケージでは `GT_Sim.exe`（Electronアプリ）をダブルクリックするだけで起動します。
内部で自動的にWebサーバー（`server/gt_sim_web.exe`）を起動し、ヘルスチェック後にUIを表示します。

### 開発環境

#### 前提条件

- Python 3.12+（`DriverScript/.venv/` を推奨）
- Node.js 20.19+
- GT_Sim.exe がビルド済み（`build/GT_esmini/Release/GT_Sim.exe`）

#### 1. Python 依存パッケージのインストール

```bash
DriverScript\.venv\Scripts\pip.exe install fastapi uvicorn[standard] python-multipart aiosqlite pyyaml
```

#### 2. フロントエンドのビルド

```bash
cd GT_esmini/web/frontend
npm install
npm run build
```

#### 3. サーバー起動

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

#### 起動オプション

```bash
python GT_esmini/web/start_server.py --host 0.0.0.0 --port 8080 --reload
```

| オプション | デフォルト | 説明 |
|:---|:---|:---|
| `--host` | `127.0.0.1` | REST API バインドホスト |
| `--port` | `8000` | REST API バインドポート |
| `--reload` | OFF | ソース変更時に自動リロード（開発用） |

#### 環境変数

| 環境変数 | デフォルト | 説明 |
|:---|:---|:---|
| `GT_SIM_GRPC_PORT` | `50051` | gRPC サーバーのポート番号 |
| `GT_SIM_CORS_ORIGINS` | *(なし)* | 追加 CORS オリジン（カンマ区切り） |

#### 開発モード（フロントエンド Hot Reload）

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
| Projects | `/projects` | プロジェクト一覧・管理 |
| Project Detail | `/projects/:id` | プロジェクト詳細・シナリオ選択・実行パネル・ライブモニター |
| Scenarios | `/scenarios` | シナリオ一覧・検索 |
| New Simulation | `/simulations/new` | シミュレーション実行フォーム |
| Simulations | `/simulations` | ジョブ一覧・状態確認 |
| Simulation Detail | `/simulations/:id` | ジョブ詳細・メトリクス・KPI・結果DL |

### シミュレーション実行の流れ

1. **Projects ページ**でプロジェクトを選択、または **Scenarios ページ**でシナリオを選択
2. **コントローラーを選択**
   - **Default**: esmini 標準の defaultController
   - **Python Driver**: PythonDriverController（スクリプト・クラスも選択可能）
   - **ManualDrive**: ハンドルコントローラー/ゲームパッドによるリアルタイム操作
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
5. **Simulation Detail ページ**に自動遷移し、実行状態をリアルタイムで確認
6. 完了後、**メトリクス**（xy_rmse, speed_rmse, lane_id_match 等）と**出力ファイル**を確認・ダウンロード

### ManualDrive 設定パネル

ManualDriveコントローラー選択時に表示される専用設定パネルです。

| 設定項目 | 説明 |
|:---|:---|
| **ボタンマッピング** | シフトアップ/ダウン、ウインカー左/右、ヘッドライト、ハイビーム、フォグ、ハザード、オーバーライドの各ボタンを割り当て。ゲームパッドのボタンキャプチャ機能付き。 |
| **FFBチューニング** | バネ係数、ダンパー係数、コンスタントゲイン、最大力の4パラメータを調整 |
| **ドメイン制御** | 横方向（ステアリング）と縦方向（アクセル/ブレーキ）を独立して「手動」/「シナリオ」に割り当て |
| **プリセット** | 設定をプリセットとして保存・読み込み |

設定は `config/manual_drive.json` に保存されます。

### コントローラー設定

| コントローラー | 用途 |
|:---|:---|
| Default | esmini 内蔵の defaultController。シナリオ定義通りの動作 |
| Python Driver | PythonDriverController。Python スクリプトによるカスタム制御（開発凍結中） |
| ManualDrive | ハンドルコントローラー/ゲームパッドによるリアルタイム操作 |

### 実行結果

完了したジョブの詳細ページでは以下が確認できます:

- **Metrics**: シミュレーション概要（実行時間、フレーム数、平均速度等）
- **KPI**: xy_rmse, speed_rmse, lane_id_match 等の定量評価指標
- **Final State**: 最終フレームの状態（位置、速度、ヘディング等）
- **Output Files**: 出力ファイルのダウンロード
  - `sim.dat` — esmini バイナリ記録
  - `sim.csv` — CSV 形式（自動変換）
  - `stdout.txt` / `stderr.txt` — プロセス出力

### ライブモニター

Project Detail ページでは、実行中のシミュレーションをリアルタイムに監視できます:

- **HVD Gauge Panel**: HostVehicleData（ステアリング角、ペダル開度等）をゲージ表示
- **OSI Live Panel**: OSIデータストリームをリアルタイム表示
- **Live Scene View**: 3Dシーンの表示

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

```bash
GT_SIM_CORS_ORIGINS=http://localhost:3000,http://localhost:4200
```

デフォルトでは `http://localhost:5173` と `http://127.0.0.1:5173`（Vite dev server）のみ許可されています。

### シナリオアップロード API

#### `POST /api/scenarios/upload`

XOSC XML を直接アップロードして一時シナリオを作成できます。一時シナリオは 1 時間後に自動削除されます。

```bash
curl -X POST http://127.0.0.1:8000/api/scenarios/upload \
  -H "Content-Type: text/xml" \
  -d @my_scenario.xosc
```

#### `POST /api/roads/upload`

カスタム道路（OpenDRIVE）をアップロードし、シナリオ内で参照できます。

```bash
curl -X POST http://127.0.0.1:8000/api/roads/upload \
  -H "Content-Type: text/xml" \
  -d @my_road.xodr
```

### 速度制御 API

#### `PUT /api/simulations/{job_id}/speed`

実行中のシミュレーションの再生速度をリアルタイムに変更できます（Windows のみ）。

```bash
curl -X PUT http://127.0.0.1:8000/api/simulations/{job_id}/speed \
  -H "Content-Type: application/json" \
  -d '{"speed_factor": 2.0}'
```

### WebSocket OSI ストリーミング

`/ws/osi/{job_id}` で実行中のシミュレーションの OSI データをリアルタイムに受信できます。

### gRPC OSI ストリーミング

| サービス | RPC | 説明 |
|:---|:---|:---|
| `GroundTruthService` | `StreamGroundTruth` | OSI GroundTruth のサーバーストリーミング |
| `HostVehicleDataService` | `StreamHostVehicleData` | OSI HostVehicleData のサーバーストリーミング |

ポートは環境変数 `GT_SIM_GRPC_PORT` で変更可能です（デフォルト: `50051`）。

---

## 設定の永続化

Web UI で変更した設定は `GT_esmini/web/settings.json` に自動保存されます。
このファイルを削除するとデフォルト値にリセットされます。

## 出力ディレクトリ

シミュレーション結果は `test_results/web/{job_id}/` （開発環境）または `data/` 配下（配布パッケージ）に保存されます。

## トラブルシューティング

### ポートが使用中

```
[Errno 10048] error while attempting to bind on address ('127.0.0.1', 8000)
```

別のプロセスがポート 8000 を使用しています:

```bash
netstat -ano | findstr :8000
taskkill /PID <PID> /F
```

### GT_Sim.exe が見つからない

`build/GT_esmini/Release/GT_Sim.exe` が存在することを確認してください。
ビルド方法は [ビルド・インストール](../getting-started/build_install.md) を参照。

### Electronアプリが起動しない

- `server/gt_sim_web.exe` が存在するか確認
- ウイルス対策ソフトがブロックしていないか確認
- ログは Electron の DevTools（Ctrl+Shift+I）で確認可能

### XOSC パスエラー（exit_code=-1）

シミュレーションが即座に失敗する場合、XOSC 内の相対パス解決に問題がある可能性があります。
バックエンドが自動的にパスを絶対化しますが、カスタム XOSC では問題が発生することがあります。
