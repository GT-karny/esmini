# GT_Sim API リファレンス — 外部クライアント連携用

## 概要

GT_Sim Web API は OpenSCENARIO エディタ等の外部クライアントから
シミュレーションの実行・監視・制御を行うための REST + WebSocket + gRPC インターフェースを提供する。

- **ベース URL**: `http://127.0.0.1:8000`
- **自動生成ドキュメント**: `/docs` (Swagger UI), `/redoc` (ReDoc)
- **gRPC**: `0.0.0.0:50051`

---

## シミュレーション実行 API

### POST /api/simulations — シミュレーション開始

新しいシミュレーションジョブを作成し、GT_Sim.exe を起動する。
**同時に実行できるジョブは 1 つのみ**。既に running/queued のジョブがある場合は `409 Conflict` を返す。

```
POST /api/simulations
Content-Type: application/json
```

**Request Body**:
```json
{
  "scenario_id": "scenarios/basic.xosc",
  "project_id": "my_project",
  "controller": {
    "controller_type": "default"
  },
  "execution": {
    "headless": false,
    "record": false,
    "hz": 100,
    "no_realtime": true,
    "timeout": 60,
    "osi": { "enabled": true, "ip": "127.0.0.1" },
    "autolight": false,
    "vehicle_physics": true,
    "threads": false,
    "window": { "x": 60, "y": 60, "w": 1280, "h": 720 },
    "extra_args": []
  },
  "param_overrides": {}
}
```

| フィールド | 型 | デフォルト | 説明 |
|---|---|---|---|
| `scenario_id` | string | (必須) | シナリオファイルの相対パス、またはアップロード時の `tmp_*` ID |
| `project_id` | string \| null | null | プロジェクト ID（指定時はプロジェクト root からの相対パスで解決） |
| `controller.controller_type` | string | `"default"` | `"default"` / `"python"` / `"manual"` |
| `execution.headless` | bool | false | ウィンドウなし実行 |
| `execution.record` | bool | false | .dat ファイル記録 |
| `execution.hz` | int | 100 | シミュレーション周波数 |
| `execution.no_realtime` | bool | true | 非リアルタイム実行 |
| `execution.timeout` | int | 60 | タイムアウト (秒) |
| `execution.osi.enabled` | bool | false | OSI 出力有効化（gRPC/WebSocket ストリームの前提条件） |
| `execution.osi.ip` | string | `"127.0.0.1"` | OSI UDP 送信先 IP |
| `execution.autolight` | bool | false | 交通車両の自動ライト制御 |
| `execution.vehicle_physics` | bool | true | 車両物理モデル有効化 |
| `execution.threads` | bool | false | マルチスレッド実行 |
| `execution.window` | object | `{x:60, y:60, w:1280, h:720}` | ウィンドウ位置・サイズ |
| `execution.extra_args` | string[] | [] | GT_Sim への追加コマンドライン引数 |
| `param_overrides` | object \| null | null | OpenSCENARIO パラメータの上書き `{"param_name": "value"}` |

**Response 200**:
```json
{
  "job_id": "abc123def456",
  "scenario_id": "scenarios/basic.xosc",
  "project_id": "my_project",
  "status": "running",
  "controller_type": "default",
  "progress_pct": 0,
  "pid": 12345,
  "exit_code": null,
  "output_dir": "results/sim_abc123def456",
  "started_at": "2026-03-29T10:00:00Z",
  "completed_at": null,
  "error_message": null,
  "options": {}
}
```

**Response 409** (既に実行中):
```json
{
  "detail": "A simulation is already running: existing_job_id"
}
```

---

### GET /api/simulations — ジョブ一覧

```
GET /api/simulations?status=running&project_id=xxx&scenario_id=xxx&limit=20&offset=0
```

全パラメータ任意。`status` フィルタで実行中ジョブの検出に使える。

**Response 200**:
```json
{
  "jobs": [SimulationStatus, ...],
  "total": 1
}
```

---

### GET /api/simulations/{job_id} — ジョブ状態取得

```
GET /api/simulations/{job_id}
```

**Response 200**: `SimulationStatus` オブジェクト（上記と同じ形式）

**status の遷移**: `running` → `completed` | `failed` | `timeout` | `cancelled`

---

### DELETE /api/simulations/{job_id} — シミュレーション停止

実行中のシミュレーションを停止する。

```
DELETE /api/simulations/{job_id}
```

内部動作:
1. 制御パイプ経由で `QUIT` 送信 (graceful、1秒猶予)
2. `proc.kill()` + `taskkill /F /T /PID` (強制終了)
3. DB ステータスを `cancelled` に更新

**Response 200**:
```json
{
  "job_id": "abc123def456",
  "status": "cancelled"
}
```

---

### PUT /api/simulations/{job_id}/speed — 実行速度変更

実行中のシミュレーションの速度を変更する。(Windows のみ、名前付きパイプ経由)

```
PUT /api/simulations/{job_id}/speed
Content-Type: application/json

{
  "speed_factor": 2.0
}
```

| フィールド | 型 | 範囲 | 説明 |
|---|---|---|---|
| `speed_factor` | float | 0.1 〜 100.0 | 1.0 = リアルタイム, 2.0 = 2倍速, 0.5 = 半速 |

**Response 200**:
```json
{
  "job_id": "abc123def456",
  "speed_factor": 2.0
}
```

---

## シナリオアップロード API

エディタ等がブラウザ上で構築した XML をサーバーに一時保存する。

### POST /api/scenarios/upload — XOSC アップロード

```
POST /api/scenarios/upload
Content-Type: text/xml

Body: .xosc XML 文字列
```

**Response 201**:
```json
{
  "scenario_id": "tmp_abc12345",
  "entities": [
    { "name": "Ego", "model": "car_white" },
    { "name": "Target", "model": "car_red" }
  ],
  "road_file": "../xodr/straight_500m.xodr",
  "expires_at": "2026-03-29T13:00:00Z"
}
```

返された `scenario_id` は `POST /api/simulations` の `scenario_id` フィールドでそのまま使用可能。
一時ファイルは TTL ベースで自動クリーンアップされる。

### DELETE /api/scenarios/upload/{scenario_id} — 一時シナリオ削除

```
DELETE /api/scenarios/upload/{scenario_id}
```

`tmp_` プレフィックスの ID のみ削除可能。

---

## 道路データアップロード API

### POST /api/roads/upload — XODR アップロード

```
POST /api/roads/upload
Content-Type: text/xml

Body: .xodr XML 文字列
```

**Response 201**:
```json
{
  "road_id": "tmp_road_abc",
  "road_path": "resources/xodr/tmp_road_abc.xodr"
}
```

### DELETE /api/roads/upload/{road_id} — 一時道路ファイル削除

```
DELETE /api/roads/upload/{road_id}
```

`tmp_road_` プレフィックスの ID のみ削除可能。

---

## リアルタイムストリーミング

シミュレーション実行時に `execution.osi.enabled = true` を指定すると、
以下のストリーミングチャネルが利用可能になる。

### WebSocket /ws/osi/{job_id} — OSI GroundTruth + HostVehicleData

ブラウザ向け。OSI protobuf を軽量 JSON に変換して配信する。

**接続**: `ws://127.0.0.1:8000/ws/osi/{job_id}`

**受信フレーム (GroundTruth)**:
```json
{
  "type": "ground_truth",
  "sim_time": 1.234,
  "object_count": 3,
  "objects": [
    {
      "id": 0,
      "name": "Ego",
      "x": 100.0, "y": 50.0, "z": 0.0,
      "h": 1.5708,
      "speed": 13.889,
      "obj_type": "vehicle",
      "vehicle_class": "medium_car",
      "length": 4.5, "width": 1.8,
      "head_light": "on",
      "indicator": "left",
      "brake_light": "off"
    }
  ]
}
```

**受信フレーム (HostVehicleData)**:
```json
{
  "type": "host_vehicle_data",
  "sim_time": 1.234,
  "throttle": 0.5,
  "brake": 0.0,
  "steering_angle": 0.1,
  "gear": 3,
  "rpm": 3500.0,
  "torque": 200.0,
  "speed": 13.889
}
```

**終了フレーム**:
```json
{
  "type": "end",
  "reason": "simulation_ended"
}
```

### WebSocket /ws/sv/{job_id} — シナリオ変数

**接続**: `ws://127.0.0.1:8000/ws/sv/{job_id}`

**受信フレーム**:
```json
{
  "type": "scenario_variables",
  ...
}
```

### gRPC — OSI protobuf ストリーム

ネイティブクライアント向け。生の protobuf メッセージを配信する。

| サービス | メソッド | レスポンス型 |
|---|---|---|
| `GroundTruthService` | `StreamGroundTruth()` | `stream osi3.GroundTruth` |
| `HostVehicleDataService` | `StreamHostVehicleData()` | `stream osi3.HostVehicleData` |

**ポート**: `50051`

gRPC はアクティブな OSI ブリッジから自動的にデータを取得する（job_id 指定不要）。
シミュレーションが未実行の場合は `UNAVAILABLE` ステータスを返す。

---

## 典型的な外部クライアントフロー

### パターン A: ディスク上のシナリオを実行

```
1. POST /api/simulations
   { "scenario_id": "scenarios/cut_in.xosc", "execution": { "osi": { "enabled": true } } }
   → { "job_id": "abc123", "status": "running" }

2. WebSocket ws://127.0.0.1:8000/ws/osi/abc123
   → リアルタイムで GroundTruth/HVD フレームを受信

3. GET /api/simulations/abc123  (ポーリング、任意)
   → status: "running" / "completed" / "failed"

4. DELETE /api/simulations/abc123  (停止、任意)
```

### パターン B: エディタからの XML アップロード実行

```
1. POST /api/scenarios/upload  (XML body)
   → { "scenario_id": "tmp_abc12345", "entities": [...] }

2. POST /api/simulations
   { "scenario_id": "tmp_abc12345", "execution": { "osi": { "enabled": true } } }
   → { "job_id": "def456", "status": "running" }

3. gRPC StreamGroundTruth() で osi3.GroundTruth をリアルタイム受信
   → エディタ側で SimulationFrame に変換して 3D 表示

4. PUT /api/simulations/def456/speed { "speed_factor": 2.0 }  (速度変更、任意)

5. GET /api/simulations?status=running  (実行中ジョブ確認、任意)
```

### パターン C: 実行中ジョブの検出と停止（外部クライアント）

```
1. GET /api/simulations?status=running&limit=1
   → { "jobs": [{ "job_id": "abc123", ... }], "total": 1 }

2. DELETE /api/simulations/abc123
   → { "job_id": "abc123", "status": "cancelled" }
```
