# ControllerVirtualDriver — 全体ロードマップ

| 項目 | 内容 |
| --- | --- |
| ドキュメント種別 | プリ実装ロードマップ（設計合意） |
| 対象モジュール | `gt_esmini::ControllerVirtualDriver` |
| 状態 | Draft（要件合意済み、Phase 0 未着手） |
| 最終更新 | 2026-06-02 |

---

## 1. ビジョン

**「フル車両物理を、人間並みのドライバーロジックで自動運転させる」**。

OpenSCENARIO Action 準拠の挙動をベースに、中長期の状況予測と法規ベースの優先判断を備え、いつでも手動オーバーライドできるコントローラを構築する。

既存コントローラ群との位置付け：

| Controller | 走行ロジック | 車両モデル |
| --- | --- | --- |
| Default（vanilla esmini） | MoveAlongS + Action | なし（運動学のみ） |
| `ControllerManualDrive` | 人間入力（SDL2/ネットワーク） | RealVehicle 物理 |
| `ControllerRouteDrive` | 経路追従 + LC（横方向強化） | 既定に委譲 |
| `ControllerKinematic` | 軌道曲率からステア生成 | 軽量 `vehicle::Vehicle` |
| **`ControllerVirtualDriver`（本ロードマップ）** | **自動生成ペダル+ステアで Default 等価挙動 + 法規判断** | **RealVehicle 物理（共通化）** |

---

## 2. 最終形アーキテクチャ

プラガブル 4 層 + 横断 1 層。各層は単方向の依存で、上位の制約が下層に流れ込む。

```
[Driver Stack]                                    [外部 / 観測]
┌─────────────────────────────────────┐         ┌─────────────┐
│ IInputSource     (手動入力)         │ ──┐     │ Web GUI     │
└─────────────────────────────────────┘   │     │ Python      │
                                          │     │ Recording   │
┌─────────────────────────────────────┐   │     └─────────────┘
│ ITrafficPolicy   (法規・優先判断)    │   │           ↑
│  ├ LeadVehicleAware                 │   │     ┌─────────────┐
│  ├ TrafficLightAware                │   ├──→  │ GT_CAPI:    │
│  ├ StopYieldSignAware               │   │     │ GT_GetVirt- │
│  ├ ConflictPointResolver            │   │     │ ualDriver-  │
│  └ JunctionPriority                 │   │     │ Telemetry() │
└─────────────────────────────────────┘   │     └─────────────┘
       ↓ "constraint: stop@s=X / yield"   │  各層が snapshot
┌─────────────────────────────────────┐   │  を集約して公開
│ IMidLongPlanner  (中長期 v_target(s))│   │
│  └ ManeuverAwareSpeedPlanner        │   │
└─────────────────────────────────────┘   │
       ↓ "v_target(s) curve"              │
┌─────────────────────────────────────┐   │
│ IShortPlanner    (短期 trajectory)   │   │
│  └ TrajectoryShortPlanner           │   │
└─────────────────────────────────────┘   │
       ↓ "(x,y,v,t) preview"              │
┌─────────────────────────────────────┐   │
│ IDriverModel     (逆制御)            │←──┘  OverrideManager
│  └ PIDPurePursuitDriver             │      (deadzone/mix/always)
│  └ StanleyMPCDriver  (差替え可)      │
└─────────────────────────────────────┘
       ↓ "throttle/brake/steer"
┌─────────────────────────────────────┐
│ IPhysicsBackend  (車両物理)          │
│  └ RealVehicleBackend (既存共通化)   │
│  └ HighFidelityBackend (将来差替え)   │
└─────────────────────────────────────┘
       ↓
[Object pose / OSI HVD / VehicleLight]

[Cross-cutting]
  IIndicatorPolicy (Auto/Manual 切替)
  OverrideManager  (横/縦独立で手動入力をマージ)
```

### 主要インターフェース要件

| インターフェース | 入力 | 出力 | 既存資産 |
| --- | --- | --- | --- |
| `IInputSource` | デバイス/ネット | `PedalSteerCommand` | ManualDrive `IInputSource` をそのまま再利用 |
| `ITrafficPolicy` | Entities / Signals / Route | `PolicyConstraint` 集合 | 一部 `ControllerACC` / `ControllerNaturalDriver` |
| `IMidLongPlanner` | Route / Constraint | `v_target(s)` curve | 新規 |
| `IShortPlanner` | Route / Action / v_target(s) | `(x,y,v,t)` preview | 新規 |
| `IDriverModel` | Preview / 自車状態 | `throttle/brake/steer` | 新規 |
| `IPhysicsBackend` | 入力 | 自車 pose / dyn | `RealVehicleBackend` を共通化 |
| `IIndicatorPolicy` | 横アクション / 経路情報 | LightMask | 部分: `IndicatorFSM`、`AutoLightController` |

---

## 3. Phase 別計画

### Phase 0 — 基盤整備（0.5 週）

**ゴール**: 全 Phase で共有する純抽象インターフェースを切り、既存資産を共通配置に移す。

**成果物**:
- `GT_esmini/include/gt_esmini/control/virtualdriver/IShortPlanner.hpp`
- `GT_esmini/include/gt_esmini/control/virtualdriver/IDriverModel.hpp`
- `IMidLongPlanner.hpp` / `ITrafficPolicy.hpp` / `IIndicatorPolicy.hpp`（骨格のみ）
- 既存 `IPhysicsBackend` を `manualdrive/` から `control/common/` 等共通配置に移動
- ヘッダ専用 telemetry struct 群（`ShortPlannerSnapshot` / `DriverModelSnapshot` / `MidLongPlannerSnapshot` / `TrafficPolicySnapshot`）

**受入基準**: ビルドが通る。既存 `ManualDriveController` の振る舞いに変化なし。

**リスク**: 低。

---

### Phase 1 — MVP（2〜3 週）

**ゴール**: シナリオ Action 準拠で物理車両が走るところまで一気通貫。

**成果物**:
- `GT_esmini/include/gt_esmini/control/ControllerVirtualDriver.hpp`
- `GT_esmini/src/control/ControllerVirtualDriver.cpp`
- `virtualdriver/TrajectoryShortPlanner.{hpp,cpp}` — Action 読取 + Route 先読みで `(x,y,v,t)×N` を出す
- `virtualdriver/PIDPurePursuitDriver.{hpp,cpp}` — Lateral=Pure Pursuit、Longitudinal=Speed PID
- `virtualdriver/AutoIndicatorPolicy.{hpp,cpp}` — `ControllerRouteDrive` 流のシンプル版
- `RealVehicleBackend` を `IPhysicsBackend` として共通参照
- `GT_esmini/config/virtual_driver.json` — プランナー/ドライバーモデル選択、ゲイン、override 設定
- GT_CAPI: `GT_GetVirtualDriverTelemetry()` 実装
- Web フロント: `ControllerSection.tsx` に `VirtualDriver` 選択肢追加
- 動作確認用 xosc: `resources/xosc/virtual_driver_basic.xosc`（直線→カーブ→LC→停止）

**受入基準**:
- `AssignRouteAction` で経路に従う
- `SpeedAction` で目標速度に物理応答
- `LaneChangeAction` でレーン変更（プランナー出力がトラジェクトリ込みで表現）
- 手動入力（オーバーライド）で介入可能
- GT_CAPI でテレメトリ取得確認（Web 側で生表示できれば可）

**リスク**: 中。`IShortPlanner` のトラジェクトリ表現と境界条件設計をしくじると後で書き直しになる。早めにレビューポイントを置く。

---

### Phase 2 — 中長期プランナー（2〜3 週）

**ゴール**: 「先に右折があるから事前に減速」「先のカーブで横 G 超えるから減速」を実現。

**成果物**:
- `virtualdriver/ManeuverAwareSpeedPlanner.{hpp,cpp}` — Route 前方を等間隔スキャンし `v_target(s)` プロファイルを出す
- スキャナ内訳:
  - 曲率 → `v_max(s) = sqrt(a_lat_max / |κ(s)|)`
  - `JunctionTurnDirection()` 流用で右左折検出 → ターン速度設定
  - `SpeedLimit` 変化点での滑らかな乗り換え
  - 勾配（`GetElevation`）からエンジン負荷補正
- `TrajectoryShortPlanner` を `v_target(s)` 境界条件で動かす改修
- 動作確認用 xosc: `resources/xosc/virtual_driver_anticipation.xosc`（信号無し交差点を右折、ヘアピンカーブ）

**受入基準**:
- 右折の 300 m 手前から滑らかに減速、カーブ手前で速度低下、立ち上がりで再加速
- パラメータ（最大横 G、快適減速度）が JSON で調整可能
- テレメトリに `v_target(s)` curve が含まれる（Web で可視化可能）

**リスク**: 低〜中。プランニング自体は既存 API で完結。ゲイン調整に時間を取られる可能性あり。

---

### Phase 3 — 法規ベース優先判断（段階的）

`ITrafficPolicy` を中心に、シーンごとに独立した Policy モジュールを積み上げる。各 Policy は「現在の状況に対する制約」（`{stop_at_s, max_speed, max_speed_to_s, wait_until}` 等）を出力し、`IMidLongPlanner` がそれを境界として `v_target(s)` を修正する。

#### Phase 3a — 先行車対応（1〜2 週）

**成果物**:
- `virtualdriver/policies/LeadVehicleAware.{hpp,cpp}` — IDM (Intelligent Driver Model) ベースで先行車に追従
- `ControllerACC` / `ControllerNaturalDriver` のロジックを参考
- パラメータ: time-headway、最小車間、快適減速度

**受入基準**:
- 自レーン先行車を検出して車間維持
- 渋滞でも自然な停止・再発進

**リスク**: 低。既存パターン豊富。

#### Phase 3b — 信号停止（1 週）

**成果物**:
- `virtualdriver/policies/TrafficLightAware.{hpp,cpp}`
- 自レーン前方の信号探索 + `GT_TrafficSignalController` の phase 取得
- 黄信号判断（行ける／停まる）ロジック

**受入基準**: 赤で停止線で停止、青で発進、黄で適切判断。

**リスク**: 低。`GT_TrafficSignalController` が既に動いている。

#### Phase 3c — 一時停止・譲れ標識（1〜2 週）

**成果物**:
- `virtualdriver/policies/StopYieldSignAware.{hpp,cpp}`
- Signal 前方スキャン（`Signal::TypeEnum::TYPE_STOP` 等）
- 停止線で完全停止 → 周辺安全確認 → 発進ロジック

**受入基準**: STOP 標識で停止後発進、YIELD 標識で必要時のみ停止。

**リスク**: 中。標識の物理位置と停止線の対応付けが OpenDRIVE データ品質依存。

#### Phase 3d — 対向車待ち（非保護の対向横断旋回）★本丸（2〜3 週）

> **状態 2026-06-23: 実装・検証・目視確認 済み（コミットは保留中）。** 当初の「TTC + 交差点 s 座標（点）」モデルは非保護左折で破綻した（対向到達の瞬間に解除→衝突。隣接レーンを反平行ですれ違うため中心間距離 ~2.8m が下限で衝突判定に使えない）。ユーザーと合意のうえ、下記の**フットプリント・コリドー空間時間占有**モデルに作り直した。詳細は `tech_debt_audit_2026-06.md` の 2026-06-23 更新を参照。

**成果物（実装済み）**:
- `virtualdriver/policies/ConflictPointResolver.{hpp,cpp}`
- 各車の経路を車幅で太らせた**コリドー（多角形リボン）**にし、自車×他車の**真のポリゴン交差**（凸クアッドを Sutherland–Hodgman でクリップ）で**重なり領域（面）**を算出（純関数 `conflict_geom::ConvexClip`/`PolygonArea`、単体テスト同梱）
- 車長込みフットプリントの**空間×時間占有**で gap-acceptance（±`pet`）→ 待機/進入判定
- 他車予測: 割当 Route でコリドー形状 + 現在速度の等速
- 待機: 重なり領域進入の `standoff` **手前**に STOP_AT_S（**クロール許容**、0 停止は強制しない）。解除: 対向フットプリントが領域を `release_buffer` 越えて**物理通過**したか（位置ベース）
- 国別ルール: LHT/RHT は ego road `GetRule()` から**自動推定**（`opendrive-lht-rht.md` 参照）
- 衝突判定（検証）: 中心間距離でなく **OBB（長さ×幅）重なり**（マッチャ `min_obb_separation_above`、SAT）

**受入基準（達成）**:
- 信号無し交差点で対向直進を横断する旋回時、対向車を待つ（p017: OBB 重なりなしで通過 → 旋回完了）✓
- ギャップが十分ならスムーズに通過（p007: 待たず進行）✓
- 何台か通過後に余裕タイミングで進入（位置ベース解除）✓
- ビューワー目視確認済み（2026-06-23）

**リスク**: 高（残）。24 バリアント全域の閾値（`standoff`/`release_buffer`/`pet`）チューニング、p023 型の境界、複数台ストリーム。

#### Phase 3e — 無信号交差点優先（2〜3 週）

**成果物**:
- OpenDRIVE `<priority>` レコード抽出 → `roadmanager` 拡張
- `virtualdriver/policies/JunctionPriority.{hpp,cpp}` — 優先道路/非優先道路の判定 + 待機ロジック

**受入基準**:
- 優先道路から非優先道路の車を見て、優先側として通過
- 非優先側として、他車を待つ

**リスク**: 高。OpenDRIVE データに priority が入っていない場合、何らかの heuristic 必要（道路幅・lane 数で代用等）。

---

### Phase 4 — 仕上げ（1〜2 週）

**ゴール**: プロダクトとしての完成度を上げる。

**成果物**:
- Web GUI: テレメトリのリアルタイム可視化パネル（プランナー出力 / 制御誤差 / TrafficPolicy 判定）
- FFB 対応（オプション）: ManualDrive の FFB 機構を流用、または外部 Python に投げ出し
- OSI 拡張領域への一部テレメトリ反映（互換性目的）
- ドキュメント: VirtualDriver 概論、設定リファレンス、トラブルシュート
- E2E xosc: `resources/xosc/virtual_driver_e2e.xosc`（信号付き市街路を経路追従 + 他車混在）

**リスク**: 低。

---

## 4. 依存グラフ

```
Phase 0 ──┬─→ Phase 1 ──┬─→ Phase 2 ──┬─→ Phase 3a ─┐
          │             │             │             │
          │             │             ├─→ Phase 3b ─┤
          │             │             │             ├─→ Phase 3d ──→ Phase 4
          │             │             ├─→ Phase 3c ─┤
          │             │             │             │
          │             │             └─→ Phase 3e ─┘
          │             │
          │             └─ (この時点で MVP として価値あり、外部利用可)
          │
          └─ (インターフェース確定、他開発と並列化可能)
```

**並列化のチャンス**:
- Phase 3a / 3b / 3c はそれぞれ独立した Policy なので並列に進められる
- Phase 4 の GUI 部分は Phase 1 完了後すぐに着手可能（フロントエンド人員がいれば）

---

## 5. 工数まとめ

| Phase | 工数 | 累積 | プロダクト価値 |
| --- | --- | --- | --- |
| 0  | 0.5 週 | 0.5 週 | 基盤のみ |
| 1  | 2〜3 週 | 2.5〜3.5 週 | **MVP・外部公開可** |
| 2  | 2〜3 週 | 4.5〜6.5 週 | 「考えて走る」体験 |
| 3a | 1〜2 週 | 5.5〜8.5 週 | 渋滞・追従 |
| 3b | 1 週   | 6.5〜9.5 週 | 信号対応 |
| 3c | 1〜2 週 | 7.5〜11.5 週 | 標識対応 |
| 3d | 2〜3 週 | 9.5〜14.5 週 | **本丸完了** |
| 3e | 2〜3 週 | 11.5〜17.5 週 | 無信号交差点 |
| 4  | 1〜2 週 | 12.5〜19.5 週 | 製品仕上げ |

並列化を入れれば **8〜12 週** で 3d 本丸まで届く想定。

---

## 6. マイルストーン

| マイルストーン | 完了時期目安 | 内容 |
| --- | --- | --- |
| **M1** | 〜3 週 | VirtualDriver MVP（Phase 1）→ 内部レビュー、設計の妥当性検証 |
| **M2** | 〜6 週 | 中長期プランナー込みでデモ可能（〜Phase 2）→ 自動運転シミュ用途として価値が確立 |
| **M3** | 〜9 週 | 先行車・信号・標識対応（〜Phase 3c）→ 一般道シナリオ実用域 |
| **M4** | 〜12 週 | 対向車待ち対応（〜Phase 3d）→ 「交差点で待てる」自動運転として完成度 MAX |
| **M5** | 〜15 週 | 無信号交差点優先 + 仕上げ（〜Phase 4）→ リリース候補 |

---

## 7. 決定が必要な分岐点

| 時期 | 決定事項 | 影響 |
| --- | --- | --- |
| Phase 0 開始時 | `IPhysicsBackend` を `manualdrive/` から `common/` へ動かすか、`virtualdriver/` 内で再定義するか | ManualDrive と物理を共有するか分離するか |
| Phase 1 中盤 | Trajectory 表現: 等時間刻み / 等距離刻み | 後段プランナーの設計に影響 |
| Phase 1 終盤 | `IDriverModel` デフォルト: PID + PP か Stanley か | チューニング工数 |
| Phase 2 開始 | 「快適減速度」「最大横 G」のデフォルト値 | 走行フィーリング |
| Phase 3d 開始 | LHT/RHT 切替方法（JSON / xosc property / 自動推定） | 国際対応 → **決定: ego road `GetRule()` から自動推定（2026-06-23）** |
| Phase 3e 開始 | OpenDRIVE `priority` 不在時の fallback heuristic | データ品質依存 |

---

## 8. 既存資産との関係

### 再利用するもの

| 既存 | 再利用先 |
| --- | --- |
| `ControllerManualDrive` の `IInputSource` 階層 | `IInputSource` をそのまま接続 |
| `ControllerManualDrive` の `OverrideManager` | 手動オーバーライドの横/縦独立制御 |
| `ControllerManualDrive` の `RealVehicleBackend` | `IPhysicsBackend` 共通化 |
| `ControllerRouteDrive` の `JunctionTurnDirection()` ロジック | Phase 2 の右左折検出 |
| `ControllerRouteDrive` の経路計算（`LaneIndependentRouter` 呼出し） | `IShortPlanner` 内で参考 |
| `ControllerACC` / `ControllerNaturalDriver` の先行車追従 | Phase 3a の IDM 実装参考 |
| `GT_TrafficSignalController` | Phase 3b の信号 phase 取得 |
| `IndicatorFSM` / `VehicleLightExtension` | `IIndicatorPolicy::Manual` 実装 |

### 新規実装するもの

- `ControllerVirtualDriver` 本体
- 4 つの新規インターフェース（`IShortPlanner`, `IDriverModel`, `IMidLongPlanner`, `ITrafficPolicy`）と各々の具体実装
- 5 つの Policy モジュール（Phase 3）
- OpenDRIVE `<priority>` 抽出の roadmanager 拡張（Phase 3e）
- GT_CAPI `GT_GetVirtualDriverTelemetry()` と Web 側可視化

---

## 9. 推奨スタンス

1. **Phase 0〜1 をしっかり**: ここのインターフェース設計が後工程すべてを支配する。レビューに時間をかけて損はない。
2. **Phase 2 までで一旦リリース判断**: 「考えて走る車」までで市場価値が大きい。Phase 3 以降は需要を見ながら段階的に進められる構造にしてある。
3. **Phase 3d を作る前に 3a / 3b / 3c で経験を積む**: 各 Policy 共通の構造（前方スキャン → 制約出力）を 3 回繰り返すと、3d の設計勘ができる。
4. **テレメトリは早期から**: Phase 1 から `GT_GetVirtualDriverTelemetry()` を入れておけば、各 Phase の追加分が自然に乗る。デバッグが指数関数的に楽。

---

## 10. 関連ドキュメント

- 検証環境設計: [verification_environment.md](./verification_environment.md) — ビジュアライザ・自動検証・アノテーションUIを並列構築する設計。VirtualDriver の各 Phase と同期して進める前提。

---

## 11. 次の一手

- Phase 0〜1 の実装プラン（ファイル単位の手順と API 契約まで詰めたもの）を作成
- インターフェース 4 種のヘッダ（純抽象 + telemetry struct）を先に切り、レビューに回す
- Phase 1 の動作確認 xosc を先に書き、受入基準を明文化する
- 検証環境 V0/V1 のスコープ確定（[verification_environment.md](./verification_environment.md) §12）
