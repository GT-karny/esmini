# ControllerVirtualDriver のフェーズ定義と現況

| 項目 | 内容 |
| --- | --- |
| ドキュメント種別 | フェーズ定義と現況（知識グラフ `vd-phase` 名前空間の正典） |
| 対象モジュール | `gt_esmini::ControllerVirtualDriver` |
| 最終更新 | 2026-08-02 |

各フェーズが何を達成する単位なのかを定義し、現在どこまで実体があるかを示す。
着手時点の工数見積とマイルストーンは消化済みのため載せていない（`git log` で辿れる）。

---

## 1. 現況

| Phase | 内容 | 状態 | 実体 |
| --- | --- | --- | --- |
| Phase0 | 基盤インターフェース | 完了 | `IShortPlanner` / `IDriverModel` / `IMidLongPlanner` / `ITrafficPolicy` / `IIndicatorPolicy` |
| Phase1 | MVP（Action 準拠で物理車両が走る） | 完了 | `ControllerVirtualDriver` / `TrajectoryShortPlanner` / `PIDPurePursuitDriver` / `AutoIndicatorPolicy` |
| Phase2 | 中長期プランナー | 完了 | `ManeuverAwareSpeedPlanner` |
| Phase3 | 法規ベースの優先判断（3a から 3e の親） | 完了 | `TrafficPolicyManager` と `policies/` 配下 |
| Phase3a | 先行車追従 | 完了 | `policies/LeadVehicleAware` |
| Phase3b | 信号停止 | 完了 | `policies/TrafficLightAware`、`policies/RouteSignalScan` |
| Phase3c | 一時停止と譲れ標識 | 完了 | `policies/StopYieldSignAware` |
| Phase3d | 対向車待ち（非保護の対向横断旋回） | 完了 | `policies/ConflictPointResolver` |
| Phase3e | 無信号交差点の優先 | 完了 | `ConflictPointResolver` の junction-priority 経路（既定 OFF） |
| Phase4 | 仕上げ | 進行中 | §5 |

### ロードマップの外で後から入った層

当初の 4 層構想に無く、後の要求から追加したものを分けて記す。
フェーズ ID は振っていない。

| 追加分 | 位置 | 出自 |
| --- | --- | --- |
| `policies/CrosswalkPedestrianAware`、`policies/RouteCrosswalkScan` | `ITrafficPolicy` | 横断歩道の歩行者優先 |
| `policies/AebSafety` と tier 調停 | `ITrafficPolicy`（safety tier） | [adas_axis.md](adas_axis.md) の機能軸 |
| `AdSteeringEnvelope`、`ResumeMergeProfile`、FFB サーボ | 横断層 | 手介入と復帰（feature:F7） |
| `DomainOwnershipLedger`、ドメイン別の所有権 | 横断層 | [domain_split_ownership.md](domain_split_ownership.md) |

---

## 2. ビジョンと既存コントローラの中での位置

フル車両物理を、人間並みのドライバーロジックで自動運転させる。
OpenSCENARIO Action 準拠の挙動を土台に、中長期の状況予測と法規ベースの優先判断を備え、いつでも手動でオーバーライドできる構成にする。

| Controller | 走行ロジック | 車両モデル |
| --- | --- | --- |
| Default（vanilla esmini） | MoveAlongS + Action | なし（運動学のみ） |
| `ControllerManualDrive` | 人間入力（SDL2 とネットワーク） | RealVehicle 物理 |
| `ControllerRouteDrive` | 経路追従と車線変更（横方向強化） | 既定に委譲 |
| `ControllerKinematic` | 軌道曲率からステア生成 | 軽量 `vehicle::Vehicle` |
| `ControllerVirtualDriver` | 自動生成ペダルとステアで Default 等価挙動、および法規判断 | RealVehicle 物理（共通化） |

---

## 3. アーキテクチャ

プラガブル 4 層と横断層で構成する。
各層は単方向に依存し、上位の制約が下層へ流れ込む。

```
[Driver Stack]                                    [外部 / 観測]
┌─────────────────────────────────────┐         ┌─────────────┐
│ IInputSource     (手動入力)         │ ──┐     │ Web GUI     │
└─────────────────────────────────────┘   │     │ Recording   │
                                          │     └─────────────┘
┌─────────────────────────────────────┐   │           ↑
│ ITrafficPolicy   (法規・優先判断)    │   │     ┌─────────────┐
│  ├ LeadVehicleAware                 │   ├──→  │ GT_CAPI:    │
│  ├ TrafficLightAware                │   │     │ GT_GetVirt- │
│  ├ StopYieldSignAware               │   │     │ ualDriver-  │
│  ├ ConflictPointResolver            │   │     │ Telemetry() │
│  ├ CrosswalkPedestrianAware         │   │     └─────────────┘
│  └ AebSafety           (safety tier)│   │
└─────────────────────────────────────┘   │  各層が snapshot
       ↓ "constraint: stop@s=X / yield"   │  を集約して公開
┌─────────────────────────────────────┐   │
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
└─────────────────────────────────────┘
       ↓ "throttle/brake/steer"
┌─────────────────────────────────────┐
│ IPhysicsBackend  (車両物理)          │
│  └ RealVehicleBackend               │
└─────────────────────────────────────┘
       ↓
[Object pose / OSI HVD / VehicleLight]

[Cross-cutting]
  IIndicatorPolicy   (Auto/Manual 切替)
  OverrideManager    (横/縦独立で手動入力をマージ)
  AdSteeringEnvelope (AD 指令の安全包絡線)
  ResumeMergeProfile (AUTO_RESUME 後の合流軌道)
  DomainOwnershipLedger (どのドメインを誰が書くか)
```

| インターフェース | 入力 | 出力 | 由来 |
| --- | --- | --- | --- |
| `IInputSource` | デバイスとネットワーク | `PedalSteerCommand` | ManualDrive から再利用 |
| `ITrafficPolicy` | Entities / Signals / Route | `PolicyConstraint` 集合 | 新規（一部 `ControllerACC` を参考） |
| `IMidLongPlanner` | Route と Constraint | `v_target(s)` curve | 新規 |
| `IShortPlanner` | Route / Action / `v_target(s)` | `(x,y,v,t)` preview | 新規 |
| `IDriverModel` | Preview と自車状態 | `throttle/brake/steer` | 新規 |
| `IPhysicsBackend` | 入力 | 自車 pose と dyn | ManualDrive の `RealVehicleBackend` を共通化 |
| `IIndicatorPolicy` | 横アクションと経路情報 | LightMask | `IndicatorFSM` と `AutoLightController` を部分流用 |

---

## 4. フェーズ定義

各フェーズのゴールと受入基準を残す。
どの実装がどれに対応するかは §1 の表にある。

### Phase0：基盤整備

全フェーズで共有する純抽象インターフェースを切り、既存資産を共通配置へ移す。

受入基準：ビルドが通り、既存 `ManualDriveController` の振る舞いが変わらないこと。

### Phase1：MVP

シナリオ Action 準拠で物理車両が走るところまでを一気通貫させる。

受入基準は次の5つである。

- `AssignRouteAction` で経路に従う
- `SpeedAction` で目標速度に物理応答する
- `LaneChangeAction` で車線変更する（プランナー出力がトラジェクトリ込みで表現される）
- 手動入力で介入できる
- GT_CAPI でテレメトリを取得できる

動作確認は `resources/xosc/virtual_driver_basic.xosc`（直線からカーブ、車線変更、停止）で行う。

### Phase2：中長期プランナー

「先に右折があるから事前に減速」「先のカーブで横 G を超えるから減速」を実現する。
Route 前方を等間隔にスキャンして `v_target(s)` プロファイルを出す。
スキャンの内訳は曲率（`v_max(s) = sqrt(a_lat_max / |κ(s)|)`）、右左折検出によるターン速度、`SpeedLimit` 変化点の乗り換え、勾配によるエンジン負荷補正である。

受入基準：右折の 300 m 手前から滑らかに減速し、カーブ手前で速度が落ち、立ち上がりで再加速すること。
パラメータ（最大横 G、快適減速度）が JSON で調整でき、テレメトリに `v_target(s)` が含まれること。

動作確認は `resources/xosc/virtual_driver_anticipation.xosc` で行う。

### Phase3：法規ベースの優先判断

`ITrafficPolicy` を中心に、シーンごとに独立した Policy を積み上げる。
各 Policy は現在の状況に対する制約（`{stop_at_s, max_speed, max_speed_to_s, wait_until}` など）を出力し、`IMidLongPlanner` がそれを境界として `v_target(s)` を修正する。

#### Phase3a：先行車対応

IDM（Intelligent Driver Model）ベースで先行車に追従する。
パラメータは time-headway、最小車間、快適減速度である。

受入基準：自レーンの先行車を検出して車間を維持し、渋滞でも自然に停止して再発進すること。

#### Phase3b：信号停止

自レーン前方の信号を探索し、`GT_TrafficSignalController` の phase を取得して、黄信号で行くか停まるかを判断する。

受入基準：赤で停止線に停止し、青で発進し、黄で適切に判断すること。

#### Phase3c：一時停止と譲れ標識

Signal を前方スキャンし（`Signal::TypeEnum::TYPE_STOP` など）、停止線で完全停止したのち周辺の安全を確認して発進する。

受入基準：STOP 標識で停止してから発進し、YIELD 標識では必要なときだけ停止すること。

#### Phase3d：対向車待ち（非保護の対向横断旋回）

各車の経路を車幅で太らせたコリドー（多角形リボン）にし、自車と他車の真のポリゴン交差から重なり領域を算出する。
車長込みフットプリントの空間かつ時間の占有で gap-acceptance を判定する。
待機は重なり領域へ入る `standoff` 手前に `STOP_AT_S` を置き、クロールを許容する。
解除は対向のフットプリントが領域を `release_buffer` 越えて物理的に通過したかで判定する（位置ベース）。
LHT と RHT は ego road の `GetRule()` から自動推定する。

受入基準：信号無し交差点で対向直進を横断する旋回時に対向車を待ち、ギャップが十分ならスムーズに通過すること。
衝突判定は中心間距離ではなく OBB の重なりで行う（マッチャ `min_obb_separation_above`）。

> 当初は「TTC と交差点 s 座標（点）」モデルで作り、非保護左折で破綻した。
> 経緯と再設計の実測は [tech_debt_audit_2026-06.md](../../tech_debt_audit_2026-06.md) の 2026-06-23 更新にある。
> 実装の詳細は `ConflictPointResolver.hpp` のヘッダコメントが正典である。

#### Phase3e：無信号交差点の優先

OpenDRIVE の `<junction><priority high low>` から ego と他車の優先関係を解決する。
自車が優先側なら支配的なコンフリクトに譲らず、非優先側と不明な場合は Phase3d の待機挙動に落ちる。
既定は OFF（`policy_junction_priority_enabled`）で、OpenDRIVE 側に `<priority>` が入っていることを前提とする。

受入基準：優先道路側として通過し、非優先側として他車を待つこと。

### Phase4：仕上げ

プロダクトとしての完成度を上げる。
残りは §5 にある。

---

## 5. Phase4 の残り

| 項目 | 状態 |
| --- | --- |
| Web GUI のテレメトリ可視化 | 完了（`LiveVdPanel` / `PolicyTimelinePanel` / `VTargetProfileChart` / `ActivePolicyPanel` / `FfbMarginPanel`） |
| FFB 対応 | 完了（ManualDrive の FFB 機構を流用し、目標角追従サーボを追加） |
| OSI 拡張領域へのテレメトリ反映 | 実装しない判断（[osi_telemetry_extension_decision.md](osi_telemetry_extension_decision.md)） |
| ドキュメント | 進行中（本ディレクトリ。設定リファレンスとトラブルシュートは未着手） |
| E2E シナリオ `virtual_driver_e2e.xosc` | 未着手。信号付き市街路の経路追従と他車混在を1本で通す |

---

## 6. 決定済みの設計分岐

| 論点 | 決定 |
| --- | --- |
| `IPhysicsBackend` を共通配置へ移すか | 移す。`control/common/` に置き ManualDrive と物理を共有する |
| Trajectory の刻み方 | preview は等時間刻み |
| `IDriverModel` の既定 | PID と Pure Pursuit（`PIDPurePursuitDriver`）。以後は凍結扱い |
| LHT と RHT の切替方法 | ego road の `GetRule()` から自動推定する |
| `<priority>` 不在時の扱い | heuristic で代用せず、Phase3d の待機挙動へ落とす |

---

## 7. 設定を追加するときの要件（VD-GUI-PARITY）

VD に追加する「設定で On/Off できる項目」と「調整可能なパラメータ」は、対応する GUI フォーム項目（`VirtualDriverPanel.tsx`）と API スキーマ（`virtual_driver_api.py` の `_BOOL_KEYS` / `_NUMBER_KEYS` / `_STRING_ENUM_KEYS`）の更新をセットで行うことを実装完了の要件とする（issue #33）。
C++ 側は `config/virtual_driver.json` をフラットに行パースするだけなので、GUI と API のキー追加だけで反映される。

**GUI とシナリオ `<Property name="policies">` の優先順位**：共有 config（`config/virtual_driver.json`、GUI の編集対象）がベースラインである。
シナリオの `<Property name="policies">` は、そのシナリオの実行時にかぎり追加でポリシーを有効化する（union）。
シナリオ側から無効化はできない。
`_write_virtual_driver_config()`（`services/simulation_runner.py`）が共有ファイルを基点に加算的に ON にするだけの実装のためである。

---

## 8. 関連文書

- 検証環境の設計：[verification_environment.md](verification_environment.md)
- 検証シナリオの量産基盤：[scenario_authoring_foundation.md](scenario_authoring_foundation.md)
- 機能を動機層と主体で層別する軸：[adas_axis.md](adas_axis.md)
- 手介入と移管まわり：[scenario_control_handoff_design.md](scenario_control_handoff_design.md)、[domain_split_ownership.md](domain_split_ownership.md)
