# GT_Sim Web API リファレンス

ベース URL: `http://127.0.0.1:8000`

OpenAPI 仕様は `/docs`（Swagger UI）または `/redoc`（ReDoc）で閲覧できます。

---

## ヘルスチェック

### `GET /api/health`

サーバーの稼働状態を確認します。

**レスポンス:**
```json
{"status": "ok"}
```

---

## シナリオ管理 `/api/scenarios`

### `GET /api/scenarios`

利用可能な XOSC シナリオの一覧を取得します。`resources/xosc/` ディレクトリをスキャンします。

**クエリパラメータ:**

| パラメータ | 型 | 必須 | 説明 |
|:---|:---|:---|:---|
| `search` | string | No | ファイル名フィルタ（部分一致） |

**レスポンス例:**
```json
[
  {
    "id": "lane_change_simple",
    "filename": "lane_change_simple.xosc",
    "path": "e:/Repository/GT_esmini/esmini/resources/xosc/lane_change_simple.xosc",
    "size": 4523,
    "modified": "2026-02-20T15:30:00"
  }
]
```

### `GET /api/scenarios/{scenario_id}`

シナリオの詳細情報（XOSC パース結果）を取得します。

**レスポンス例:**
```json
{
  "id": "lane_change_simple",
  "filename": "lane_change_simple.xosc",
  "path": "...",
  "size": 4523,
  "modified": "2026-02-20T15:30:00",
  "entities": [
    {"name": "Ego", "model": "car_white", "controller": null},
    {"name": "Target", "model": "car_red", "controller": null}
  ],
  "road_file": "../xodr/straight_500m.xodr"
}
```

---

## Python スクリプト管理 `/api/scripts`

### `GET /api/scripts`

利用可能な Python コントローラースクリプトの一覧を取得します。

スキャン対象ディレクトリ:
- `DriverScript/pythondriver/`（推奨）
- `DriverScript/examples/`
- `DriverScript/realdriver/`（レガシー）

**レスポンス例:**
```json
{
  "scripts": [
    {
      "path": "DriverScript/pythondriver/scenario_drive_embedded.py",
      "name": "scenario_drive_embedded.py",
      "category": "pythondriver",
      "classes": ["EmbeddedController"],
      "recommended": true
    },
    {
      "path": "DriverScript/examples/acc_lkas_example.py",
      "name": "acc_lkas_example.py",
      "category": "examples",
      "classes": ["ACCLKASController"],
      "recommended": false
    }
  ]
}
```

### `GET /api/scripts/{script_path}`

特定のスクリプトの詳細情報を取得します。

**パスパラメータ:**
- `script_path`: スクリプトの相対パス（例: `DriverScript/pythondriver/scenario_drive_embedded.py`）

---

## コントローラー設定 `/api/controller-config`

### `GET /api/controller-config/presets`

プリセット一覧を取得します。

**レスポンス例:**
```json
[
  {
    "name": "Default Controller",
    "description": "esmini built-in defaultController()",
    "config": {
      "controller_type": "default",
      "python": { "script": "...", "class": "...", ... }
    }
  },
  {
    "name": "Python Driver (Recommended)",
    "description": "PythonDriverController with EmbeddedController",
    "config": {
      "controller_type": "python",
      "python": {
        "script": "DriverScript/pythondriver/scenario_drive_embedded.py",
        "class": "EmbeddedController",
        "python_home": "",
        "trace_enabled": true,
        "trace_dir": ""
      }
    }
  }
]
```

### `GET /api/controller-config/current`

現在のデフォルトコントローラー設定を取得します。

### `PUT /api/controller-config/current`

デフォルトコントローラー設定を更新します。`settings.json` に永続化されます。

**リクエストボディ:**
```json
{
  "controller_type": "python",
  "python": {
    "script": "DriverScript/pythondriver/scenario_drive_embedded.py",
    "class": "EmbeddedController",
    "python_home": "",
    "trace_enabled": true,
    "trace_dir": ""
  }
}
```

---

## シミュレーション実行 `/api/simulations`

### `POST /api/simulations`

新しいシミュレーションジョブを登録・実行します。

**リクエストボディ:**

```json
{
  "scenario_id": "lane_change_simple",
  "controller": {
    "controller_type": "python",
    "python": {
      "script": "DriverScript/pythondriver/scenario_drive_embedded.py",
      "class": "EmbeddedController",
      "python_home": "",
      "trace_enabled": true,
      "trace_dir": ""
    }
  },
  "execution": {
    "headless": true,
    "record": true,
    "hz": 120,
    "no_realtime": false,
    "timeout": 60,
    "osi": {
      "enabled": true,
      "ip": "127.0.0.1"
    },
    "autolight": true,
    "window": {
      "x": 60,
      "y": 60,
      "w": 1280,
      "h": 720
    },
    "extra_args": []
  }
}
```

**リクエストフィールド詳細:**

| フィールド | 型 | デフォルト | 説明 |
|:---|:---|:---|:---|
| `scenario_id` | string | (必須) | シナリオ名（拡張子なし） |
| `controller.controller_type` | string | `"default"` | `"default"` または `"python"` |
| `controller.python.script` | string | `"...scenario_drive_embedded.py"` | Python スクリプトパス |
| `controller.python.class` | string | `"EmbeddedController"` | コントローラークラス名 |
| `controller.python.python_home` | string | `""` | Python ホームパス（空欄で自動検出） |
| `controller.python.trace_enabled` | bool | `true` | Python トレースログ出力 |
| `controller.python.trace_dir` | string | `""` | トレース出力先（空欄で出力ディレクトリ） |
| `execution.headless` | bool | `true` | 3D ビューア非表示 |
| `execution.record` | bool | `true` | sim.dat 記録 |
| `execution.hz` | int | `120` | シミュレーション周波数 |
| `execution.no_realtime` | bool | `false` | 最速実行（false = リアルタイム） |
| `execution.timeout` | int | `60` | タイムアウト [秒] |
| `execution.osi.enabled` | bool | `true` | OSI メッセージ出力 |
| `execution.osi.ip` | string | `"127.0.0.1"` | OSI 送信先 IP |
| `execution.autolight` | bool | `true` | 自動ライト制御 |
| `execution.window.x` | int | `60` | ウィンドウ X 位置 |
| `execution.window.y` | int | `60` | ウィンドウ Y 位置 |
| `execution.window.w` | int | `1280` | ウィンドウ幅 |
| `execution.window.h` | int | `720` | ウィンドウ高さ |
| `execution.extra_args` | string[] | `[]` | 追加 CLI 引数 |

> **Note**: `window` は `headless: false` の場合のみ CLI 引数に反映されます。

**レスポンス:** `SimulationStatus` オブジェクト（下記参照）

**実行フロー:**
1. `controller_type == "python"` → XOSC バリアントを生成（PythonDriverController 注入）
2. `controller_type == "default"` → XOSC バリアントを生成（コントローラーなし）
3. XOSC 内の相対パスを自動的に絶対パスに変換
4. GT_Sim.exe をサブプロセスとして起動
5. stdout/stderr をキャプチャし、タイムアウトを監視

### `GET /api/simulations`

ジョブ一覧を取得します。

**クエリパラメータ:**

| パラメータ | 型 | デフォルト | 説明 |
|:---|:---|:---|:---|
| `status` | string | (なし) | ステータスでフィルタ |
| `limit` | int | `20` | 取得件数 |
| `offset` | int | `0` | スキップ件数 |

**レスポンス例:**
```json
{
  "jobs": [
    {
      "job_id": "abc12345",
      "scenario_id": "lane_change_simple",
      "status": "completed",
      "controller_type": "python",
      "progress_pct": 100,
      "pid": 12345,
      "exit_code": 0,
      "output_dir": "test_results/web/abc12345",
      "started_at": "2026-02-24T10:30:00",
      "completed_at": "2026-02-24T10:30:45",
      "error_message": null,
      "options": {}
    }
  ],
  "total": 1
}
```

### `GET /api/simulations/{job_id}`

特定のジョブの状態を取得します。

**ジョブ状態遷移:**

```
queued → running → completed
                 → failed
                 → timeout
       → cancelled
```

| ステータス | 説明 |
|:---|:---|
| `queued` | キュー待ち |
| `running` | 実行中 |
| `completed` | 正常完了 (exit_code=0) |
| `failed` | 異常終了 (exit_code!=0) |
| `timeout` | タイムアウト |
| `cancelled` | ユーザーによるキャンセル |

### `DELETE /api/simulations/{job_id}`

実行中のジョブをキャンセルします（プロセスを SIGTERM で停止）。

**レスポンス:**
```json
{"job_id": "abc12345", "status": "cancelled"}
```

---

## 結果取得 `/api/results`

### `GET /api/results/{job_id}`

結果のメタ情報とファイル一覧を取得します。

**レスポンス例:**
```json
{
  "job_id": "abc12345",
  "scenario_id": "lane_change_simple",
  "files": [
    {"name": "sim.dat", "size": 245760, "type": "dat"},
    {"name": "sim.csv", "size": 512000, "type": "csv"},
    {"name": "stdout.txt", "size": 1024, "type": "log"},
    {"name": "stderr.txt", "size": 256, "type": "log"},
    {"name": "python_trace.jsonl", "size": 8192, "type": "trace"}
  ]
}
```

### `GET /api/results/{job_id}/files/{filename}`

結果ファイルをダウンロードします。

**例:**
```
GET /api/results/abc12345/files/sim.dat
GET /api/results/abc12345/files/sim.csv
GET /api/results/abc12345/files/stdout.txt
```

### `GET /api/results/{job_id}/metrics`

シミュレーションメトリクスを計算して返します（DAT → CSV 変換を含む）。

**レスポンス例:**
```json
{
  "summary": {
    "duration": 30.01,
    "total_frames": 3602,
    "avg_speed": 15.23,
    "max_speed": 20.05,
    "min_speed": 0.0,
    "total_distance": 456.8
  },
  "final_state": {
    "x": 450.2,
    "y": -3.5,
    "speed": 15.01,
    "heading": 0.001,
    "road_id": 1,
    "lane_id": -1,
    "s": 450.2,
    "t": -0.02
  }
}
```

### `GET /api/results/{job_id}/timeseries`

時系列データを JSON で取得します（チャート表示用）。

**クエリパラメータ:**

| パラメータ | 型 | デフォルト | 説明 |
|:---|:---|:---|:---|
| `fields` | string | (全フィールド) | カンマ区切りのフィールド名 |
| `entity` | string | `"Ego"` | エンティティ名 |

**例:**
```
GET /api/results/abc12345/timeseries?fields=time,x,y,speed&entity=Ego
```

**レスポンス例:**
```json
{
  "data": [
    {"time": 0.0, "x": 0.0, "y": 0.0, "speed": 0.0},
    {"time": 0.01, "x": 0.15, "y": 0.0, "speed": 15.0},
    ...
  ],
  "entity": "Ego",
  "fields": ["time", "x", "y", "speed"]
}
```

---

## 設定管理 `/api/config`

### `GET /api/config/execution-defaults`

デフォルト実行パラメータを取得します。

**レスポンス例:**
```json
{
  "hz": 120,
  "headless": true,
  "record": true,
  "no_realtime": false,
  "timeout": 60,
  "osi": {"enabled": true, "ip": "127.0.0.1"},
  "autolight": true,
  "window": {"x": 60, "y": 60, "w": 1280, "h": 720}
}
```

### `PUT /api/config/execution-defaults`

デフォルト実行パラメータを更新します。`settings.json` に永続化されます。

### `GET /api/config/vehicle-params`

車両パラメータ（`GT_esmini/config/real_vehicle_params.json`）を取得します。

### `PUT /api/config/vehicle-params`

車両パラメータを更新します。

### `GET /api/config/thresholds`

比較テスト閾値（`GT_esmini/test/comparison_thresholds.yaml`）を取得します。

### `PUT /api/config/thresholds`

比較テスト閾値を更新します。

### `GET /api/config/system`

システム情報を取得します。

**レスポンス例:**
```json
{
  "gt_sim_path": "e:\\Repository\\GT_esmini\\esmini\\build\\GT_esmini\\Release\\GT_Sim.exe",
  "gt_sim_exists": true,
  "repo_root": "e:\\Repository\\GT_esmini\\esmini",
  "scenarios_dir": "e:\\Repository\\GT_esmini\\esmini\\resources\\xosc",
  "scenarios_count": 42
}
```

---

## データモデル

### SimulationStatus

| フィールド | 型 | 説明 |
|:---|:---|:---|
| `job_id` | string | ジョブ ID |
| `scenario_id` | string | シナリオ名 |
| `status` | string | `queued` / `running` / `completed` / `failed` / `cancelled` / `timeout` |
| `controller_type` | string | `default` / `python` |
| `progress_pct` | int | 進捗率 (0-100) |
| `pid` | int? | GT_Sim プロセス ID |
| `exit_code` | int? | 終了コード |
| `output_dir` | string? | 出力ディレクトリパス |
| `started_at` | string? | 開始時刻 (ISO 8601) |
| `completed_at` | string? | 完了時刻 (ISO 8601) |
| `error_message` | string? | エラーメッセージ |

### WindowConfig

| フィールド | 型 | デフォルト | 説明 |
|:---|:---|:---|:---|
| `x` | int | `60` | ウィンドウ X 位置 [px] |
| `y` | int | `60` | ウィンドウ Y 位置 [px] |
| `w` | int | `1280` | ウィンドウ幅 [px] |
| `h` | int | `720` | ウィンドウ高さ [px] |

### OsiConfig

| フィールド | 型 | デフォルト | 説明 |
|:---|:---|:---|:---|
| `enabled` | bool | `true` | OSI 出力有効 |
| `ip` | string | `"127.0.0.1"` | 送信先 IP アドレス |

---

## エラーレスポンス

エラー時は HTTP ステータスコードと `detail` フィールドを含む JSON が返されます。

```json
{"detail": "Scenario 'nonexistent' not found"}
```

| ステータス | 意味 |
|:---|:---|
| `404` | リソースが見つからない |
| `400` | 無効なリクエスト（キャンセル不可等） |
| `422` | バリデーションエラー（不正なリクエストボディ） |
| `500` | サーバー内部エラー |
