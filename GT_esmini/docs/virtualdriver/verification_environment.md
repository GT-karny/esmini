# ControllerVirtualDriver — 検証環境設計

| 項目 | 内容 |
| --- | --- |
| ドキュメント種別 | プリ実装設計（要件合意） |
| 関連ドキュメント | [roadmap.md](./roadmap.md) |
| 状態 | Draft（要件合意済み、実装未着手） |
| 最終更新 | 2026-06-02 |

---

## 1. ビジョン

VirtualDriver の振る舞いを「プランニング段階から走行結果まで」観測できる**ビジュアライザ**と、**Claude Code 自身が回せる自動検証ループ**を同時並行で構築する。

検証は2系統に分ける:
- **単純系**（信号停止、標識、道路構造、車両モデル等の決定論的領域）→ 数値比較 + YAML 宣言で自動判定
- **複雑系**（交通流、対向車待ち、混合状況等の判断が要る領域）→ **シチュエーション生成 → 実行 → 人間アノテーション → ラベル付きデータセット**で評価

---

## 2. 全体構成

```
                          ┌──────────────────────┐
                          │  Scenario Library    │
                          │  (xosc + 期待値YAML  │
                          │   またはアノテーション要)│
                          └──────────┬───────────┘
                                     │
              ┌──────────────────────┼──────────────────────┐
              │                      │                      │
              ↓                      ↓                      ↓
       ┌─────────────┐        ┌─────────────┐        ┌─────────────┐
       │ Headless    │        │ GUI 実行     │        │ Baseline    │
       │ Runner CLI  │        │ (Web)       │        │ Generator   │
       │ gt_sim_test │        │             │        │ (Default)   │
       └──────┬──────┘        └──────┬──────┘        └──────┬──────┘
              │                      │                      │
              ↓                      ↓                      ↓
       ┌──────────────────────────────────────────────────────────┐
       │              Telemetry / OSI Recording                   │
       │              (.osi + 拡張ヘッダで VirtualDriver データ)    │
       └──────────────────────────────────────────────────────────┘
              │                      │                      │
              ↓                      ↓                      ↓
       ┌─────────────┐        ┌─────────────┐        ┌─────────────┐
       │ JSON変換    │        │ LiveScene + │        │ Numeric     │
       │ ツール       │        │ Plan/Policy │        │ Comparison  │
       │ (Claude読用)│        │ オーバーレイ │        │ vs Baseline │
       └──────┬──────┘        └─────────────┘        └──────┬──────┘
              │                                              │
              ↓                                              ↓
       ┌─────────────────┐                          ┌─────────────────┐
       │ Annotation UI   │ ←  人間レビュー          │ Auto Assert     │
       │ (Web)           │                          │ (YAML/Pytest)   │
       └────────┬────────┘                          └────────┬────────┘
                │                                            │
                ↓                                            ↓
       ┌─────────────────────────────────────────────────────────┐
       │           Verdict (pass/fail/needs-review)              │
       └─────────────────────────────────────────────────────────┘
```

---

## 3. ビジュアライザ要件

### 3.1 ベース方針
- **LiveSceneView 拡張で対応**（[GT_esmini/web/frontend/src/components/LiveSceneView.tsx](GT_esmini/web/frontend/src/components/LiveSceneView.tsx)）
- 単独パネルを増やすのではなく、既存 SVG シーン上にレイヤーとしてオーバーレイ
- 細部チャート（v_target(s) profile、誤差時系列）は LiveSceneView と並べる別パネル

### 3.2 LiveSceneView の前提改善（Phase 1 と同期）

| 改善項目 | 内容 |
| --- | --- |
| FPS 改善 | 描画ボトルネック特定 → SVG 要素数削減/Canvas 化検討/差分更新 |
| 信号表示 | 信号位置と現在 phase を描画（緑/黄/赤） |
| 標識表示 | STOP/YIELD 標識を停止線とセットで描画 |
| 停止線描画 | OpenDRIVE の stop line / 信号停止位置をマーキング |
| ズーム/パン操作の応答性 | 既存ズーム機構の動作確認と必要に応じた最適化 |

### 3.3 VirtualDriver 専用レイヤー（Phase 1 から段階導入）

| グループ | 要素 | 描画形式 | Phase |
| --- | --- | --- | --- |
| **自車プラン** | 短期軌道 (x,y,v,t)×N | polyline + 速度カラーグラデーション | 1 |
| **自車プラン** | 中長期 v_target(s) | 別チャート（s 軸の折れ線） | 2 |
| **経路** | Route waypoints | 半透明の polyline + 現在 waypoint ハイライト | 1 |
| **Policy・知覚** | TrafficPolicy 制約マーカー | stop@s_X / yield zone をアイコン | 3 |
| **Policy・知覚** | 検出他車 + TTC | 強調枠 + 数値ラベル | 3 |
| **Policy・知覚** | 信号・標識状態 | 信号 phase 色、標識タイプ | 3b/3c |
| **制御・リプレイ** | 内部誤差・操作量チャート | 別パネル（時系列） | 1 |
| **制御・リプレイ** | 記録再生 | 過去実行を巻き戻し可能に | 1 |

### 3.4 切替 UI
- 各レイヤーは Web パネル側でトグル可能（重なって読めなくなる問題対応）
- VirtualDriver 以外のコントローラ使用時は当該レイヤーは非表示

---

## 4. テレメトリ・記録仕様

### 4.1 形式: OSI + 変換ツール

**主**: OSI 拡張領域（または OSI 隣に伴走する protobuf チャネル）で記録
- 既存 OSI 配信路（gRPC）と同居
- esmini `.dat` とは独立した記録系
- 多言語（C++ / Python / TS）から読める

**変換ツール**: Claude Code が読める形式へ変換するスクリプトをセットで配布
- `GT_esmini/scripts/verification/osi_to_jsonl.py` — OSI 記録 → JSONL（1 frame 1 行）
- `GT_esmini/scripts/verification/osi_to_csv.py` — テレメトリ要約を CSV へ（既存 `scripts/osi2csv.py` 流用可）
- `GT_esmini/scripts/verification/osi_snapshot.py` — 任意時刻の状態を JSON で抜く

> **配置確定（2026-06-02）**: 検証 Python ツールは `GT_esmini/scripts/verification/`（既存 `verify_osi_*.py` / `udp_osi_common.py` と同居）。ルート `tools/` は新設しない。venv は `DriverScript/.venv`。Step 1 成果: `generate_baseline.py`（Default 起動 → OSI GroundTruth を `.osi` トレース記録、`osi2csv.py`/`osiviewer.py` 互換）。
- Claude Code は変換後の JSONL/CSV/JSON を `Read` で直接読める

### 4.2 記録粒度

| 項目 | サンプル周期 | 備考 |
| --- | --- | --- |
| エンティティ位置・姿勢・速度 | 1 frame（10〜100Hz） | OSI 標準 |
| 自車プラン（短期軌道） | 1 frame | VirtualDriver 拡張 |
| 自車プラン（中長期 v_target(s)） | 1 frame | VirtualDriver 拡張 |
| TrafficPolicy 出力 | イベント駆動 + 1 frame snapshot | 拡張 |
| 制御内部（誤差、PIDパラメータ等） | 1 frame | 拡張 |
| 信号 phase 変化 | イベント駆動 | 拡張 |
| ユーザーイベント（手動オーバーライド等） | イベント駆動 | 拡張 |

### 4.3 アクセス API
- Live: gRPC OSI streaming（既存）
- 録画: ファイル出力（`results/<run_id>/telemetry.osi`）
- 変換: 上記 CLI ツール
- 一括取得（Live でも）: `GT_GetVirtualDriverTelemetry()` C-API（[roadmap.md](./roadmap.md) §3 Phase 1 成果物）

---

## 5. 自動検証フレームワーク

### 5.1 2 系統

#### A. 単純系（決定論的領域）

**対象**: 車両モデル動作、基本制御、信号停止、標識停止、道路構造追従。

**手法**:
1. **Default ベースライン比較**（数値）
   - 同じシナリオを Default コントローラで実行 → ベースライン軌跡保存
   - VirtualDriver で実行 → ベースラインとの RMSE 等を算出
   - 閾値以内なら pass
2. **Expected events YAML 宣言**
   - シナリオごとに `expectations.yaml` を併置
   - 例:
     ```yaml
     scenario: virtual_driver_signal_stop
     must:
       - event: speed_below
         threshold: 0.1
         within:
           road_id: 5
           s_range: [95, 110]
         reason: "赤信号で停止線手前停止"
       - event: speed_above
         threshold: 5.0
         after: { sim_time: 12.0 }
         reason: "青に変わって発進"
     ```
   - テレメトリから actual events 抽出 → 突き合わせ → pass/fail

#### B. 複雑系（判断・交通流領域）

**対象**: 対向車待ち、無信号交差点優先、混合交通、エッジケース挙動。

**手法**: **アノテーションベース評価**
1. **シチュエーション生成**
   - Claude Code に多数のシチュエーション（xosc）を生成させる
   - シードを変えた多数バリエーション
2. **実行 + 記録**
   - Headless で全件回す
   - キーシーンの PNG スクリーンショット + フルテレメトリ保存
3. **アノテーションUI（Web）**
   - 各実行結果を 1 件ずつ表示（LiveSceneView での再生 + 主要イベントタイムライン）
   - 人間が `止まるべきだった`/`動いてよい`/`OK`/`NG`/`コメント` をラベル付け
   - ラベル付きデータがデータセットとして蓄積される
4. **回帰判定**
   - 後続のコード変更で同じシナリオを再実行 → 過去ラベル付きの「OK 挙動」と類似度判定
   - ラベル外（未アノテーション）のケースは "needs-review" としてキューイング

### 5.2 判定の粒度

| カテゴリ | 自動 / 半自動 / 手動 | 例 |
| --- | --- | --- |
| ビルド成功 | 完全自動 | ビルドコケない |
| Default 等価軌跡 | 完全自動（数値比較） | 直線 100m 走行で位置誤差 < 0.2m |
| 信号停止 | 完全自動（YAML） | 信号 ID=5 で s=[100,120] で speed < 0.1 |
| 標識停止 | 完全自動（YAML） | STOP 標識前で 2 秒以上停止 |
| 中長期減速 | 半自動（数値 + 視覚確認） | カーブ手前で減速プロファイルが滑らか |
| 対向車待ち | **アノテーション** | ギャップ判断が妥当だったか |
| 無信号交差点 | **アノテーション** | 優先判断が妥当だったか |
| 突発挙動 | アノテーション | 説明できない挙動が出ていないか |

---

## 6. 検証ツール群

### 6.1 CLI: `gt_sim_test`

```
gt_sim_test run <scenario.xosc>
    --controller VirtualDriver
    --config virtual_driver.json
    --out results/<run_id>/
    [--headless] [--baseline-compare] [--screenshot frames=...]

gt_sim_test batch <scenarios.yaml>
    --controller VirtualDriver
    --out results/batch_<id>/

gt_sim_test assert <run_id> --expectations expectations.yaml
gt_sim_test compare <run_id> <baseline_id>
```

成果物:
- `results/<run_id>/telemetry.osi` — 生 OSI 記録
- `results/<run_id>/telemetry.jsonl` — JSON 変換版（Claude が読む）
- `results/<run_id>/snapshots/*.png` — キーフレーム PNG
- `results/<run_id>/verdict.json` — 自動判定結果

### 6.2 ベースライン生成
- `gt_sim_test baseline <scenario.xosc> --controller Default` を `GT_esmini/scripts/verification/regenerate_baselines.*` 等で一括再生成
- Default の挙動が変わった時に更新

### 6.3 アノテーションUI
- Web 側に専用ページ（仮: `/verification/annotate`）
- 1 件 = 1 実行結果
- LiveSceneView での再生 + イベントタイムライン
- ラベル: `pass` / `fail` / `needs-discussion` / 自由コメント
- データは `annotations/<scenario>/<run_id>.json` に保存
- 過去ラベルとの突き合わせは `GT_esmini/scripts/verification/annotation_match.py`

### 6.4 Claude Code の自己検証ループ
- 標準フロー:
  1. コード変更
  2. ビルド（既存 Protocol A）
  3. `gt_sim_test batch baseline_set.yaml --out results/<run>/` 実行
  4. `verdict.json` を Read
  5. fail があれば JSONL/PNG を Read して原因分析
  6. 必要に応じて修正 → 戻る
- PNG は Claude Code が `Read` で読めるので、視覚デバッグも可能

---

## 7. シナリオライブラリ構成

```
resources/xosc/verification/
  ├── 01_vehicle_model/          # 単純系: 車両物理
  │   ├── straight_constant_speed.xosc
  │   ├── straight_constant_speed.expectations.yaml
  │   └── ...
  ├── 02_basic_control/          # 単純系: 基本制御
  │   ├── lane_change_simple.xosc
  │   └── ...
  ├── 03_traffic_signals/        # 単純系: 信号
  │   ├── red_stop_green_go.xosc
  │   └── ...
  ├── 04_traffic_signs/          # 単純系: 標識
  │   ├── stop_sign.xosc
  │   └── ...
  ├── 05_anticipation/           # 中長期判断
  │   ├── decelerate_for_curve.xosc
  │   ├── decelerate_for_right_turn.xosc
  │   └── ...
  ├── 06_lead_vehicle/           # 半自動: 先行車
  │   └── ...
  ├── 07_oncoming_yield/         # アノテーション: 対向車待ち
  │   └── ...
  ├── 08_unsignalized_junction/  # アノテーション: 無信号交差点
  │   └── ...
  └── 09_crosswalk_pedestrian/   # ハイブリッド: 横断歩道の歩行者への譲り
      └── ...                    #   （discriminator 7本は expectations で自動判定、全20は注釈）
```

各シナリオは:
- `*.xosc` 本体
- `*.expectations.yaml`（単純系のみ）
- `*.annotation_required.yaml`（複雑系のみ。何にラベル付けが必要かを宣言）
- `*.notes.md`（任意：背景・既知の課題）

---

## 8. Phase 別計画（VirtualDriver Phase との対応）

| VirtualDriver Phase | 検証環境 Phase | 内容 |
| --- | --- | --- |
| Phase 0 | **V0**: テレメトリ基盤 | `GT_GetVirtualDriverTelemetry()` API、OSI 拡張記録、`osi_to_jsonl.py` |
| Phase 1（MVP） | **V1**: 検証MVP | `gt_sim_test` CLI、Default ベースライン比較、LiveSceneView 拡張（短期軌道・経路・誤差・FPS改善・信号/標識描画）、シナリオ 01〜02 |
| Phase 2（中長期） | **V2**: 中長期判定 | v_target(s) チャート、シナリオ 05、減速プロファイル比較 |
| Phase 3a（先行車） | **V3a**: 半自動 | LeadVehicle 計測、シナリオ 06、半自動アノテーション |
| Phase 3b/3c（信号・標識） | **V3bc**: 単純自動 | シナリオ 03、04、Expected events YAML 拡張 |
| Phase 3d（対向車待ち） | **V3d**: アノテーション主体 | アノテーション UI 完成、シナリオ 07、ラベルデータベース運用 |
| Phase 3e（無信号交差点） | **V3e**: アノテーション運用 | シナリオ 08、過去ラベルとの自動マッチング |
| Phase 4（仕上げ） | **V4**: 統合 | CI 上での自動回帰、レポート生成 |

### 並列性
- **V0 / V1 のうち FPS改善・信号/標識描画は VirtualDriver の Phase 1 と同期して進める**（重複工程の最小化）
- アノテーションUI（V3d 主体）は Phase 1 完了後すぐに着手可能

---

## 9. 既存資産マッピング

| 既存 | 流用先 |
| --- | --- |
| [LiveSceneView.tsx](GT_esmini/web/frontend/src/components/LiveSceneView.tsx) | VirtualDriver レイヤーの土台 |
| `OsiLivePanel.tsx` | テレメトリ取得のフロント側パターン |
| `gRPC OSI streaming`（[services/grpc_server.py](GT_esmini/web/backend/services/grpc_server.py) 等） | ライブテレメトリ転送 |
| `osi3::HostVehicleData` | 自車状態の OSI 表現 |
| esmini `.dat` / replayer | 多車視聴・別経路の補助 |
| `GT_Sim.exe` headless 起動 | `gt_sim_test` の実行エンジン |
| `EnvironmentSimulator/Unittest/` | 単体テスト基盤（C++層） |
| `scripts/compare_python_vs_default.py`（凍結） | ベースライン比較ツールの過去事例 |

---

## 10. 工数見積（VirtualDriver 工数に追加）

| 検証 Phase | 工数 | 内容 |
| --- | --- | --- |
| V0 | 0.5〜1 週 | テレメトリ基盤、OSI 拡張、JSON 変換ツール |
| V1 | 2 週 | gt_sim_test CLI、LiveSceneView FPS改善+信号/標識、ベースライン比較 |
| V2 | 1 週 | v_target(s) チャート、シナリオ追加 |
| V3a/b/c | 1〜2 週 | Expected events YAML 拡張、シナリオ追加 |
| V3d | 2〜3 週 | アノテーション UI、ラベルマッチング |
| V3e/V4 | 1〜2 週 | CI 統合、レポート |

**合計**: 7〜11 週。VirtualDriver 本体（12〜19 週）と並列実施可能。並列化により全体工数の増加は 30〜40% 程度に抑えられる見込み。

---

## 11. 推奨スタンス

1. **V0 + V1 は VirtualDriver Phase 0/1 と完全同期**。テレメトリ基盤と最低限の自動検証が揃わないと Phase 1 のレビューができない。
2. **アノテーション UI は Phase 3d 直前に**。Phase 3a〜3c で機能要求が見えてから作る方が後悔しない。
3. **「単純系で済むケースを増やす努力」を継続**。本来アノテーション必要かもしれない判定が、Expected events YAML の表現力拡張で自動化できる場面は多いはず。
4. **シナリオ生成を Claude Code に任せる**。決まったテンプレからバリエーションを大量生成 → ユーザーは「これは止まるべき場面？」だけ判定。

---

## 12. 次の一手

- V0 のスコープ詳細化（テレメトリスキーマ仕様、JSON 変換ツールの I/F 仕様）
- LiveSceneView の FPS 計測（実機ベンチ）と改善方針の前調査
- ベースライン管理戦略（更新ポリシー、変更検知）の確定
- アノテーション UI のワイヤフレーム
