# Racing Wheel Controller — Feasibility Report

**Date**: 2025-03-21
**Scope**: C++ ネイティブのレーシングホイールコントローラー (FFB + オーバーライド機構)
**Target Device**: Logitech G29 等のレーシングホイール
**方針**: Python不使用、C++完結

---

## 1. 現状分析

### 1.1 既存 HID コントローラー (`ControllerHID`)

**場所**: `EnvironmentSimulator/Modules/Controllers/ControllerHID.cpp`

現在のesminiには `ControllerHID` が搭載されている。調査で判明した問題点:

| 項目 | 現状 | 問題 |
|------|------|------|
| **入力API** | Win: `joyGetPosEx` (WinMM) / Linux: `/dev/input/js*` | レガシーAPI。G29のペダル個別軸の正しいマッピングが困難 |
| **車両モデル** | `vehicle::Vehicle::DrivingControlAnalog()` | 簡易的な自転車モデル。サスペンション、RPM、ギア無し |
| **ステアリング** | `steering_rate_` による角速度制限 | ダイレクト入力ではなく「入力→目標角→レート制限」のため遅延感がある |
| **スロットル/ブレーキ** | 合算して `-1〜1` の1軸に統合 | ブレーキ独立軸（G29はペダル3軸）の活用が不完全 |
| **FFB** | なし | コード上一切存在しない |
| **オーバーライド** | なし | 自動運転→手動の切り替え機構なし |
| **ギアシフト** | なし | G29のパドルシフト/Hシフターに未対応 |

### 1.2 GT_esmini 既存資産

`GT_esmini` には以下の活用可能な既存実装がある:

- **`RealVehicle`**: サスペンション動力学（ピッチ/ロール）、RPMトラッキング、ギア比、エンジンブレーキを持つ拡張車両モデル。`vehicle::Vehicle` を継承
- **`ControllerRealDriver`**: UDP入力→`RealVehicle`→シナリオ同期の完全なパイプライン。friend class パターンによるモジュール分割済み
- **`AutoLightController`**: ブレーキランプ、ウインカー等の自動制御
- **`TerrainTracker`**: 路面追従
- **`EsminiStateApplier`**: 車両状態→シナリオオブジェクトの同期

---

## 2. 提案アーキテクチャ

### 2.1 新コントローラー: `ControllerRacingWheel`

```
GT_esmini/
├── include/gt_esmini/control/
│   ├── ControllerRacingWheel.hpp     # メインコントローラー
│   ├── RacingWheelInput.hpp          # SDL2 入力 + FFB 抽象化
│   └── OverrideManager.hpp           # オーバーライド判定ロジック
├── src/control/
│   ├── ControllerRacingWheel.cpp
│   ├── RacingWheelInput.cpp
│   └── OverrideManager.cpp
```

### 2.2 コンポーネント構成

```
┌─────────────────────────────────────────────────────────┐
│                  ControllerRacingWheel                   │
│                                                         │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐ │
│  │RacingWheel   │  │ Override     │  │ FFBEngine     │ │
│  │Input (SDL2)  │──│ Manager      │──│ (SDL_Haptic)  │ │
│  │              │  │              │  │               │ │
│  │• Steering    │  │• 入力閾値判定 │  │• SelfAlign    │ │
│  │• Throttle    │  │• 状態遷移    │  │• RoadFeel     │ │
│  │• Brake       │  │• ブレンド     │  │• Collision    │ │
│  │• Clutch      │  │• タイマー    │  │• Centering    │ │
│  │• Gear (H/Pad)│  │              │  │               │ │
│  │• Buttons     │  │              │  │               │ │
│  └──────────────┘  └──────────────┘  └───────────────┘ │
│           │                                    ▲        │
│           ▼                                    │        │
│  ┌──────────────────────────────────────────────┐      │
│  │              RealVehicle (既存)                │      │
│  │  UpdatePhysics(dt, throttle, brake, steer, gear)     │
│  └──────────────────────────────────────────────┘      │
│           │                                             │
│           ▼                                             │
│  ┌──────────────────────────────────────────────┐      │
│  │         EsminiStateApplier (既存)              │      │
│  └──────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────┘
```

---

## 3. 各機能のフィジビリティ

### 3.1 入力処理 — SDL2 導入 ✅ 実現可能

**現状**: esminiは SDL を使っていない。HIDコントローラーは Win=WinMM / Linux=evdev 直接アクセス。

**提案**: SDL2 を GT_esmini のオプショナル依存として導入。

| 項目 | 詳細 |
|------|------|
| **メリット** | クロスプラットフォーム統一、FFB API (`SDL_Haptic`) 同梱、G29軸/ボタンの自動検出 |
| **リスク** | esmini本体への影響なし（GT_esminiのみにリンク）。SDL2は静的リンク可能 |
| **ビルド** | `find_package(SDL2)` を `GT_esmini/CMakeLists.txt` に追加。`GT_ENABLE_SDL2` フラグで制御 |
| **代替案** | Linux: evdev直接 / Win: DirectInput直接 → クロスプラットフォームコード2倍。非推奨 |

**G29 軸マッピング（SDL2経由）**:
```
Axis 0: Steering (-32768 ~ 32767, 900°回転)
Axis 1: Throttle (32767=離す ~ -32768=全踏み) ※反転注意
Axis 2: Brake    (同上)
Axis 3: Clutch   (同上)
```

### 3.2 Force Feedback (FFB) — ⚠️ 条件付き実現可能

**SDL2 Haptic APIで使える主要エフェクト**:

| エフェクト | 用途 | G29対応 |
|-----------|------|---------|
| `SDL_HAPTIC_CONSTANT` | セルフアライニングトルク | ✅ |
| `SDL_HAPTIC_SPRING` | ステアリングセンタリング | ✅ (Linux: `new-lg4ff` 推奨) |
| `SDL_HAPTIC_DAMPER` | ステアリングの抵抗感 | ✅ (同上) |
| `SDL_HAPTIC_FRICTION` | 路面摩擦感 | ⚠️ ドライバー依存 |
| `SDL_HAPTIC_SINE` | 振動フィードバック（縁石等） | ✅ |

**リスクと対策**:

| リスク | 影響 | 対策 |
|--------|------|------|
| Linux標準ドライバ(`lg4ff`)のエフェクト制限 | Spring/Damperが効かない場合あり | `SDL_HapticQuery()`で実行時にcapability検出、Constantエフェクトでフォールバック |
| SDL2/SDL3間のAPI差異 | SDL3で Haptic API が変更される可能性 | 当面 SDL2 を使用。抽象層を挟む |
| Windows DirectInputの方向指定 | `SDL_HAPTIC_POLAR`が1軸デバイスで失敗 | `SDL_HAPTIC_CARTESIAN`を使用 |

**FFBの物理パラメータソース（既存RealVehicleから取得可能）**:
- **セルフアライニングトルク**: `RealVehicle::wheelAngle_` + `speed_` から計算
- **路面フィードバック**: `TerrainTracker` のピッチ/ロール変化率
- **衝突フィードバック**: esminiの衝突イベント
- **エンジン振動**: `RealVehicle::GetRPM()` からの周波数変換

### 3.3 オーバーライド機構 — ✅ 実現可能

**コンセプト**: 自動運転（シナリオ駆動）↔ 手動運転（ホイール入力）の切り替え

```
[AUTO mode]  ── ドライバー入力検知 ──→  [OVERRIDE mode]
     ▲                                        │
     └──── タイムアウト or ボタン ──────────────┘
```

**判定ロジック**:
```cpp
struct OverrideCondition {
    double steering_threshold = 0.05;  // ステアリング入力 > 5% でオーバーライド
    double throttle_threshold = 0.1;   // アクセル入力 > 10% でオーバーライド
    double brake_threshold    = 0.1;   // ブレーキ入力 > 10% でオーバーライド
    double return_timeout_sec = 3.0;   // 入力なし3秒で自動復帰
    bool   button_override    = true;  // ボタン押下で即時切り替え
};
```

**esminiコントローラーシステムとの統合**:
- `Controller::Activate()` / `Controller::Deactivate()` は LAT/LONG ドメイン別に制御可能
- オーバーライド時: `DeactivateDomains(DOMAIN_MASK_ALL)` で既存アクションを停止 → ホイール入力に切り替え
- 復帰時: 現在位置/速度を基準にシナリオ制御を再開

**課題と対策**:
| 課題 | 対策 |
|------|------|
| オーバーライド中のシナリオイベント進行 | `ControlDecisionEngine` パターンを流用。シナリオ側はイベント発火のみ、制御は無視 |
| 復帰時のジャンプ防止 | `EsminiStateApplier` の位置同期ロジックを活用。復帰前に現在位置でシナリオオブジェクトを再同期 |
| 部分オーバーライド（横のみ等） | esminiのドメインマスク (`DOMAIN_MASK_LAT` / `DOMAIN_MASK_LONG`) で自然に実現可能 |

### 3.4 車両モデル統合 — ✅ 既存資産で実現可能

`RealVehicle::UpdatePhysics(dt, throttle, brake, steering, gear)` がそのまま使える。

```cpp
// ControllerRacingWheel::Step(dt) の想定フロー
void ControllerRacingWheel::Step(double dt) {
    // 1. SDL2から入力取得
    auto input = wheel_input_.Poll();

    // 2. オーバーライド判定
    override_mgr_.Update(input, dt);

    if (override_mgr_.IsManualMode()) {
        // 3. RealVehicle 物理更新
        real_vehicle_.UpdatePhysics(dt,
            input.throttle, input.brake,
            input.steering, input.gear);

        // 4. FFB更新
        ffb_engine_.Update(real_vehicle_, terrain_tracker_, dt);

        // 5. シナリオ同期
        state_applier_.Apply(/*blockSpeedUpdate=*/false);
    }
    // else: シナリオ制御に委譲

    Controller::Step(dt);
}
```

---

## 4. 実装コスト見積もり

| フェーズ | 内容 | 規模感 |
|---------|------|--------|
| **Phase 1** | SDL2 統合 + 基本入力 | 新規ファイル3-4個、CMake変更 |
| **Phase 2** | `ControllerRacingWheel` + `RealVehicle` 統合 | 既存パターン流用、中規模 |
| **Phase 3** | FFBエンジン（Constant + Spring） | 新規ファイル1-2個、デバイス依存テスト必要 |
| **Phase 4** | オーバーライド機構 | 既存ドメインシステム活用、小規模 |
| **Phase 5** | チューニング + 追加FFBエフェクト | 継続的 |

---

## 5. 依存関係・前提条件

| 依存 | 詳細 |
|------|------|
| **SDL2** | `libSDL2-dev` (Linux) / SDL2 開発ライブラリ (Windows)。esmini本体には影響しない |
| **Linux FFB** | カーネルモジュール `lg4ff` (標準) または `new-lg4ff` (拡張エフェクト用) |
| **Windows FFB** | SDL2が DirectInput 経由で処理。Logitech G HUB インストール推奨 |
| **RealVehicle** | 既存。変更不要 |
| **ControllerRealDriver** | 参照実装として活用。コード流用可能 |

---

## 6. 結論

| 項目 | 判定 | 備考 |
|------|------|------|
| **レーシングホイール入力** | ✅ Go | SDL2 導入でクロスプラットフォーム統一 |
| **リアルな車両挙動** | ✅ Go | `RealVehicle` がそのまま使える |
| **FFB (基本)** | ✅ Go | Constant + Spring で十分なフィードバック |
| **FFB (高品質)** | ⚠️ 条件付き | デバイス/ドライバー依存。フォールバック必須 |
| **オーバーライド** | ✅ Go | esminiのドメインマスクシステムと自然に統合 |
| **esmini本体への影響** | ✅ None | Extension First Policy 準拠。GT_esminiのみ |

**総合判定: 実現可能。Phase 1 (SDL2 + 基本入力) から段階的に開発推奨。**

---

## 7. 設計判断

### 7.1 SDL2 導入方式 → `thirdparty/` に同梱

esmini 自体が `thirdparty/` に OSG, OSI, SUMO 等を同梱するパターンを踏襲済み。`find_package` だとビルド環境ごとに SDL2 を入れる手間が生じる。Windows は特に SDL2 のパス設定が面倒で、同梱すれば `add_subdirectory` で済む。SDL2 は `SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC` のサブセットだけ使うため、フルビルドは不要。

### 7.2 FFB パラメータ設定 → XOSC プロパティ

esmini のコントローラーは全て XOSC の `<Properties>` で設定するパターン。`ControllerRealDriver` も `port`, `SendWaypoints` 等を XOSC プロパティで受けている。シナリオごとに FFB 特性を変えられる利点がある（高速道路は Damper 強め、等）。別途 JSON を読む仕組みを作る必要がない。

```xml
<Controller name="RacingWheel">
  <Properties>
    <Property name="ffbSpringGain" value="0.7"/>
    <Property name="ffbDamperGain" value="0.3"/>
    <Property name="overrideSteerThreshold" value="0.05"/>
    <Property name="deviceIndex" value="0"/>
  </Properties>
</Controller>
```

### 7.3 ControllerRealDriver との関係 → 独立コントローラー

入力経路が根本的に異なる（RealDriver=UDP, RacingWheel=SDL2）。混ぜると条件分岐が肥大化する。RealDriver は Python ドライバーとのペア前提であり、FFB は C++ 完結。責務が違う。

`RealVehicle`, `EsminiStateApplier` は所有ではなく共通部品として利用する。将来 RealDriver 側にも FFB を追加したくなった場合は、`FFBOutput` クラスを RealDriver にも持たせればよい。

```
ControllerRealDriver  ─── UDP入力 ─── RealVehicle ─── EsminiStateApplier
                                          ↑ 共有
ControllerRacingWheel ─── SDL2入力 ── RealVehicle ─── EsminiStateApplier
                      └── FFBOutput
                      └── OverrideManager
```

### 7.4 ビルドフラグ → `GT_ENABLE_SDL2` デフォルト OFF

SDL2 なし環境（CI、FFB 不要ユーザー）でもビルドが通る必要がある。`ControllerRacingWheel` 全体を `#ifdef GT_ENABLE_SDL2` で囲み、コントローラー登録も条件付きとする。SDL2 がなければコントローラーが存在しないだけで、他に影響しない。

```cmake
option(GT_ENABLE_SDL2 "Enable SDL2 for racing wheel FFB support" OFF)
if(GT_ENABLE_SDL2)
    add_subdirectory(thirdparty/SDL2)
    target_compile_definitions(GT_esminiLib PRIVATE GT_ENABLE_SDL2)
endif()
```

### 7.5 設計判断サマリー

| 判断 | 推奨 | 理由 |
|------|------|------|
| SDL2 導入 | thirdparty 同梱 | 既存パターン踏襲、環境依存排除 |
| パラメータ | XOSC プロパティ | esmini 標準方式、シナリオ別設定可能 |
| コントローラー | 独立 | 入力経路が違う、責務分離 |
| ビルドフラグ | `GT_ENABLE_SDL2` デフォルト OFF | FFB 不要環境でのビルド保証 |

---

## 8. FFB 機能分類

### 8.1 FFB エフェクト優先度

| エフェクト | 物理的意味 | データソース（既存） | 優先度 |
|-----------|----------|-------------------|--------|
| **Spring** | ステアリングセンタリング | `wheelAngle_` | **必須** |
| **Constant** | セルフアライニングトルク | `wheelAngle_` × `speed_` | **必須** |
| **Damper** | ステアリング抵抗 | `speed_` | 中 |
| **Sine** | 路面振動（縁石等） | `TerrainTracker` pitch/roll変化率 | 低 |
| **Friction** | 路面摩擦 | ドライバー依存、フォールバック要 | 低 |

### 8.2 オーバーライド状態と FFB の関係

| 状態 | ステアリング制御 | FFB 挙動 | RealVehicle 入力 |
|------|----------------|---------|-----------------|
| **AUTO** | シナリオ駆動 | Spring → シナリオ目標角に追従 | シナリオ入力 |
| **OVERRIDE** | ドライバー入力 | Constant + Damper（路面感） | ホイール入力 |
| **BLEND** (遷移中) | 線形補間 | Spring 強度漸減 | 混合入力 |

オーバーライド判定トリガー: ステアリング入力 > 閾値、ブレーキ > 閾値、ボタン押下。

### 8.3 制御ループ内の FFB 挿入位置

```
ControllerRacingWheel::Step(dt)
  1. SDL2 入力取得 (RacingWheelInput::Poll)
  2. オーバーライド判定 (OverrideManager::Update)
  3. RealVehicle::UpdatePhysics()
  ──→ wheelAngle_, speed_, rpm_ が確定
  4. ★ FFB 計算・出力 (FFBOutput::Update)  ← ここ
  5. EsminiStateApplier::Apply()
  6. Controller::Step(dt)
```

### 8.4 既存資産の流用度

| コンポーネント | 流用 | 備考 |
|--------------|------|------|
| `RealVehicle` | そのまま | `UpdatePhysics()` は入力ソースに依存しない |
| `EsminiStateApplier` | そのまま | Gateway 同期はそのまま |
| `ControlDecisionEngine` | パターン流用 | AUTO/OVERRIDE 状態管理に応用 |
| `DriverOutputPort` | 参考 | UDP 出力パターン（FFB は別経路だが設計思想は同じ） |
| `AutoLightController` | そのまま | ブレーキランプ等は入力ソースに無関係 |
| `TerrainTracker` | そのまま | 路面追従 + FFB 路面振動のデータソース |

### 8.5 新規ファイル構成

```
GT_esmini/
├── include/gt_esmini/control/
│   ├── ControllerRacingWheel.hpp    # メインコントローラー
│   ├── RacingWheelInput.hpp         # SDL2 入力抽象化
│   ├── FFBOutput.hpp                # SDL_Haptic FFB 出力
│   └── OverrideManager.hpp          # AUTO↔OVERRIDE 状態機械
├── src/control/
│   ├── ControllerRacingWheel.cpp
│   ├── RacingWheelInput.cpp
│   ├── FFBOutput.cpp
│   └── OverrideManager.cpp
```

---

## 9. 参考リンク

- [SDL2 Haptic API](https://wiki.libsdl.org/SDL2/SDL_HapticEffect)
- [new-lg4ff (拡張Logitechドライバー)](https://github.com/berarma/new-lg4ff)
- [SDL3 G29 HIDAPI FFB PR #11598](https://github.com/libsdl-org/SDL/pull/11598)
- [Lazy Foo' SDL2 Force Feedback Tutorial](https://www.lazyfoo.net/tutorials/SDL/20_force_feedback/index.php)
- [SDL G29 Force Feedback Issue #6081](https://github.com/libsdl-org/SDL/issues/6081)
