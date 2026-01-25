# RealVehicle パラメータ解説ガイド

このドキュメントでは、`real_vehicle_params.json`の各パラメータの意味と、調整による車両挙動への影響を説明します。

## 目次
- [ピッチ/ロール（車体姿勢）パラメータ](#ピッチロール車体姿勢パラメータ)
- [最大値制限パラメータ](#最大値制限パラメータ)
- [ステアリングパラメータ](#ステアリングパラメータ)
- [アンダーステアパラメータ](#アンダーステアパラメータ)
- [速度・加速度パラメータ](#速度加速度パラメータ)
- [ギア比パラメータ](#ギア比パラメータ)
- [Terrain Tracking（地形追跡）機能](#terrain-tracking地形追跡機能)
- [実行時オプション設定](#実行時オプション設定)
- [調整例](#調整例)

---

## ピッチ/ロール（車体姿勢）パラメータ

RealVehicleは**バネ-ダンパーモデル**を使用して車体の前後傾斜（ピッチ）と左右傾斜（ロール）をシミュレートします。

### 物理モデル

```
加速度 = -剛性係数 × 角度 - 減衰係数 × 角速度 + 外力
```

- **剛性（Stiffness）**: バネの硬さ。値が大きいほど素早く元の姿勢に戻ろうとする
- **減衰（Damping）**: ダンパーの強さ。値が大きいほど振動を早く抑える
- **外力（Forcing）**: 加速度やカーブによって生じる慣性力

### ピッチ/ロールの2つの成分

車両のピッチ/ロールは以下の2つの成分から構成されます：

1. **動的成分（Dynamic）**: 加速・減速・カーブによる慣性力で発生（自車のみ）
2. **地形成分（Terrain）**: OpenDRIVEの道路形状から計算（全車両）

最終的な姿勢 = 動的成分 + 地形成分

---

### `pitch_stiffness` （ピッチ剛性）
**デフォルト値**: `8.0`

**役割**: 前後傾斜（ピッチ）のバネ硬さ

**効果**:
- **大きくする** → 車体が素早く水平に戻る（硬いサスペンション）
  - 加速/ブレーキ時の沈み込みが小さい
  - より「硬い」乗り心地
- **小さくする** → 車体がゆっくり戻る（柔らかいサスペンション）
  - 加速/ブレーキ時にノーズが大きく動く
  - より「柔らかい」乗り心地

**推奨範囲**: `5.0 ~ 15.0`

**調整例**:
```json
"pitch_stiffness": 12.0  // スポーツカー風（硬い）
"pitch_stiffness": 5.0   // セダン風（柔らかい）
```

---

### `pitch_damping` （ピッチ減衰）
**デフォルト値**: `3.0`

**役割**: 前後傾斜の振動を抑える強さ

**効果**:
- **大きくする** → 振動が素早く収束（タイトな制御）
  - 加速後すぐに姿勢が安定
  - オーバーダンピング気味になる可能性
- **小さくする** → 振動が長く続く（ふわふわした動き）
  - 加速後に何度か前後に揺れる
  - アンダーダンピング気味

**推奨範囲**: `1.0 ~ 5.0`

**調整例**:
```json
"pitch_damping": 5.0   // 素早く安定（レーシングカー）
"pitch_damping": 1.5   // ゆっくり安定（クラシックカー）
```

---

### `roll_stiffness` （ロール剛性）
**デフォルト値**: `10.0`

**役割**: 左右傾斜（ロール）のバネ硬さ

**効果**:
- **大きくする** → カーブで車体が傾きにくい
  - コーナリング時のロールが小さい
  - スポーティな挙動
- **小さくする** → カーブで車体が大きく傾く
  - コーナリング時のロールが大きい
  - SUVやバン風の挙動

**推奨範囲**: `6.0 ~ 20.0`

**調整例**:
```json
"roll_stiffness": 15.0  // レースカー風（ロールが小さい）
"roll_stiffness": 6.0   // SUV風（ロールが大きい）
```

---

### `roll_damping` （ロール減衰）
**デフォルト値**: `5.0`

**役割**: 左右傾斜の振動を抑える強さ

**効果**:
- **大きくする** → カーブ後の左右揺れが素早く収まる
- **小さくする** → カーブ後に左右に何度か揺れる

**推奨範囲**: `2.0 ~ 8.0`

**調整例**:
```json
"roll_damping": 7.0   // タイトな挙動
"roll_damping": 2.5   // ゆったりした挙動
```

---

### `mass_height` （重心高さ効果係数）
**デフォルト値**: `0.02`

**役割**: 加速度がピッチ/ロールに与える影響の大きさ

**物理的意味**: 重心が高いほど、加速・カーブで車体が傾きやすい

**効果**:
- **大きくする** → ピッチ/ロールの動きが大きくなる
  - 加速時のノーズアップが顕著
  - カーブでのロールが大きい
- **小さくする** → ピッチ/ロールの動きが小さくなる
  - ほぼフラットな走行
  - レーシングカー風

**推奨範囲**: `0.01 ~ 0.1`

**調整例**:
```json
"mass_height": 0.05   // SUV/トラック（重心が高い）
"mass_height": 0.015  // スポーツカー（重心が低い）
```

**注意**:
- 実装では `pitch_forcing = -mass_height * long_acc`（符号反転）
- 加速時にノーズダウンさせたい場合は正の値、ノーズアップさせたい場合は負の値

---

### `center_of_rotation_z_offset` （回転中心のZ軸オフセット）
**デフォルト値**: `0.4`

**役割**: ピッチ/ロール回転の中心位置（車体下方）

**効果**:
- **大きくする** → 回転中心が車体の下に移動
  - ピッチ/ロール時の視覚的な動きが自然
- **小さくする** → 回転中心が車体原点に近づく
  - ピッチ/ロール時に車体が「浮く」感じ

**推奨範囲**: `0.3 ~ 0.6`（通常、車高の半分程度）

**調整例**:
```json
"center_of_rotation_z_offset": 0.5   // 背の高い車
"center_of_rotation_z_offset": 0.3   // 低い車
```

---

## 最大値制限パラメータ

### `max_pitch_deg` （最大ピッチ角度）
**デフォルト値**: `6.0` 度

**役割**: 前後傾斜の最大許容角度

**効果**:
- **大きくする** → より大きく前後に傾ける
- **小さくする** → 傾きを抑える

**推奨範囲**: `3.0 ~ 10.0` 度

**調整例**:
```json
"max_pitch_deg": 8.0   // ドラマチックな動き
"max_pitch_deg": 4.0   // 控えめな動き
```

---

### `max_roll_deg` （最大ロール角度）
**デフォルト値**: `6.0` 度

**役割**: 左右傾斜の最大許容角度

**効果**:
- **大きくする** → カーブで大きく傾く
- **小さくする** → カーブでの傾きを抑える

**推奨範囲**: `3.0 ~ 10.0` 度

**調整例**:
```json
"max_roll_deg": 8.0   // SUV風
"max_roll_deg": 4.0   // スポーツカー風
```

---

## ステアリングパラメータ

### `steer_gain` （ステアリングゲイン）
**デフォルト値**: `0.7` ラジアン

**役割**: ステアリング入力から実際のタイヤ角への変換係数

**効果**:
- **大きくする** → 少しのステアリングで大きく曲がる（敏感）
  - より素早い方向転換
  - 高速走行時に不安定になる可能性
- **小さくする** → たくさんステアリングしないと曲がらない（鈍感）
  - より安定した直進性
  - カーブで大きく操作が必要

**推奨範囲**: `0.5 ~ 1.2` ラジアン（約28～69度）

**調整例**:
```json
"steer_gain": 0.9   // 敏感（カート風）
"steer_gain": 0.5   // 鈍感（トラック風）
```

---

## アンダーステアパラメータ

高速走行時に、タイヤのグリップ限界によってステアリング効果が減少する現象（アンダーステア）をシミュレートします。

### 物理モデル

```
速度比 = 現在速度 / 臨界速度
アンダーステア係数 = understeer_factor × (速度比² - 1)
グリップ係数 = 1 / (1 + アンダーステア係数)
実効ステアリング角 = 入力ステアリング角 × グリップ係数
```

臨界速度を超えると、速度の2乗に比例してステアリング効果が減少します。

---

### `understeer_factor` （アンダーステア係数）
**デフォルト値**: `0.0015`

**役割**: 速度によるステアリング効果の減少率

**効果**:
- **大きくする** → 高速時にステアリングが効きにくくなる（強いアンダーステア）
  - 高速コーナーで曲がりにくい
  - より安全で安定した挙動
- **小さくする** → 高速時でもステアリングが効く
  - ニュートラルな挙動
  - レスポンシブな操作感
- **0.0に設定** → アンダーステア無効（常に100%ステアリング効果）

**推奨範囲**: `0.0005 ~ 0.003`

**調整例**:
```json
"understeer_factor": 0.002    // 強いアンダーステア（SUV風）
"understeer_factor": 0.001    // 軽いアンダーステア
"understeer_factor": 0.0      // 無効（理想的なグリップ）
```

---

### `critical_speed` （臨界速度）
**デフォルト値**: `30.0` m/s（約108 km/h）

**役割**: アンダーステアが発生し始める速度閾値

**効果**:
- **大きくする** → より高速になるまでアンダーステアが発生しない
  - 市街地走行には影響しない
- **小さくする** → 低速からアンダーステアが発生
  - 常にアンダーステア傾向

**推奨範囲**: `20.0 ~ 50.0` m/s（72～180 km/h）

**調整例**:
```json
"critical_speed": 40.0   // 高速道路向け（144 km/h から効く）
"critical_speed": 25.0   // 一般道向け（90 km/h から効く）
```

---

### `max_understeer_reduction` （最大アンダーステア減少率）
**デフォルト値**: `0.6`

**役割**: ステアリング効果の最大減少量（0～1の範囲）

**効果**:
- **大きくする** → 超高速時にステアリングがほぼ効かなくなる
  - 0.8 = 最大80%減少（20%しか効かない）
- **小さくする** → 超高速時でも一定のステアリング効果を維持
  - 0.3 = 最大30%減少（70%は効く）

**推奨範囲**: `0.3 ~ 0.8`

**調整例**:
```json
"max_understeer_reduction": 0.7   // 強い制限（安全重視）
"max_understeer_reduction": 0.4   // 軽い制限（スポーツ走行向け）
```

---

### アンダーステアの効果グラフ

```
ステアリング効果 (%)
100% |────────┐
     |        ╲
 70% |         ╲
     |          ╲────────── (max_understeer_reduction = 0.3)
 50% |
     |            ╲────────  (max_understeer_reduction = 0.5)
 30% |
     |              ╲──────  (max_understeer_reduction = 0.7)
     |__________________|___|______
                  臨界速度  速度 →
```

---

## 速度・加速度パラメータ

### `max_acc` （最大加速度）
**デフォルト値**: `12.0` m/s²

**役割**: エンジンの最大加速能力

**効果**:
- **大きくする** → スロットル全開時の加速が速い
  - 0-100km/h到達時間が短い
  - スポーツカー風
- **小さくする** → スロットル全開時の加速が遅い
  - ゆっくり加速
  - エコカー風

**推奨範囲**: `5.0 ~ 20.0` m/s²

**参考値**:
- 軽自動車: `6.0 ~ 8.0`
- セダン: `8.0 ~ 12.0`
- スポーツカー: `15.0 ~ 20.0`

**調整例**:
```json
"max_acc": 18.0   // 高性能スポーツカー
"max_acc": 7.0    // エコノミーカー
```

---

### `max_speed` （最大速度）
**デフォルト値**: `60.0` m/s（約216 km/h）

**役割**: 車両の理論最高速度

**効果**:
- **大きくする** → より高速まで加速できる
- **小さくする** → 低い速度で頭打ち

**推奨範囲**: `30.0 ~ 100.0` m/s（108～360 km/h）

**参考値**:
- 市街地車両: `40.0`（144 km/h）
- 高速道路: `55.0`（198 km/h）
- スーパーカー: `90.0`（324 km/h）

**調整例**:
```json
"max_speed": 80.0   // レーシングカー（288 km/h）
"max_speed": 35.0   // 商用バン（126 km/h）
```

---

## ギア比パラメータ

### `reverse_gear_ratio` （リバースギア比）
**デフォルト値**: `1.5`

**役割**: バックギア時のトルク倍率

**効果**:
- **大きくする** → バック時の加速が速い/力強い
- **小さくする** → バック時の加速が遅い/弱い

**推奨範囲**: `1.0 ~ 2.5`

**調整例**:
```json
"reverse_gear_ratio": 2.0   // 強力なバック
"reverse_gear_ratio": 1.2   // 弱いバック
```

**注意**: バック最高速は約20 m/s（72 km/h）にハードコードで制限されています（RealVehicle.cpp:234）

---

## Terrain Tracking（地形追跡）機能

### 概要

OpenDRIVEの道路形状（高さ、勾配、傾斜）を4輪位置で読み取り、車両の姿勢に反映する機能です。

**対象**: 全車両（RealDriverコントローラを使用していない車両も含む）

### 動作原理

1. 各車両の4輪位置（前後左右）をグローバル座標で計算
2. 各輪位置でOpenDRIVEの道路高さを取得
3. 4点の高さから車両のピッチとロールを幾何学的に計算
4. 車両オブジェクトの姿勢を更新

```
        前輪
      FL ─── FR
       │     │
       │     │
      RL ─── RR
        後輪

ピッチ = atan2(前輪平均高さ - 後輪平均高さ, ホイールベース)
ロール = atan2(右輪平均高さ - 左輪平均高さ, トラック幅)
```

### 車両寸法の計算

```cpp
front_axle_dist = wheelbase * 0.6    // 前輪距離（重心から）
rear_axle_dist  = wheelbase * 0.4    // 後輪距離（重心から）
track_width     = width * 0.85       // トラック幅
```

### 特徴

- **OSG Viewerに反映**: 地形に沿った車両傾斜が可視化される
- **静止車両はスキップ**: 速度0.01 m/s以下の車両は処理をスキップ（性能最適化）
- **全車両対応**: NPC車両やTrafficManager制御の車両も地形追跡される

### 自車（RealDriver）の特別処理

RealDriverコントローラを使用する自車のみ：

```
最終ピッチ = 地形ピッチ + 動的ピッチ（加速による傾き）
最終ロール = 地形ロール + 動的ロール（横Gによる傾き）
```

他車は地形成分のみが適用されます。

---

## 実行時オプション設定

### パラメータファイルの配置

`real_vehicle_params.json`は以下の順序で検索されます：

1. **実行ファイルと同じディレクトリ** （優先）
2. カレントディレクトリ

```
GT_Sim.exe
real_vehicle_params.json  ← ここに配置
```

### コマンドライン実行例

```bash
# 基本実行
./GT_Sim.exe --osc scenarios/my_scenario.xosc

# ウィンドウなしで実行（ヘッドレス）
./GT_Sim.exe --osc scenarios/my_scenario.xosc --headless

# OSI出力を有効化
./GT_Sim.exe --osc scenarios/my_scenario.xosc --osi_file output.osi

# 固定タイムステップで実行
./GT_Sim.exe --osc scenarios/my_scenario.xosc --fixed_timestep 0.01
```

### OpenSCENARIO（xosc）での設定

#### RealDriverコントローラの設定

```xml
<PrivateAction>
    <ControllerAction>
        <AssignControllerAction>
            <Controller name="RealDriver">
                <Properties>
                    <!-- UDP通信設定 -->
                    <Property name="port" value="25252"/>

                    <!-- パラメータファイルパス（オプション）-->
                    <Property name="configFile" value="my_custom_params.json"/>
                </Properties>
            </Controller>
        </AssignControllerAction>
        <ActivateControllerAction longitudinal="true" lateral="true"/>
    </ControllerAction>
</PrivateAction>
```

#### コントローラプロパティ一覧

| プロパティ | デフォルト | 説明 |
|-----------|-----------|------|
| `port` | `25252` | UDP通信ポート |
| `configFile` | `real_vehicle_params.json` | パラメータファイルパス |

### Terrain Trackingの有効/無効

コード内で `TerrainTracker::SetEnabled(false)` を呼び出すことで無効化できます。

現在、xoscやコマンドラインからの切り替えは未実装です。必要な場合はコードを修正してください。

```cpp
// TerrainTrackerを無効化する例（ControllerRealDriver.cpp内）
gt_esmini::TerrainTracker::SetEnabled(false);
```

### デバッグ用の出力確認

ログ出力で動作を確認できます：

```cpp
// RealVehicle.cpp内でパラメータ読み込み時に出力
LOG("RealVehicle: Loaded parameters from %s", config_path.c_str());
LOG("  understeer_factor: %.4f", params_.understeer_factor);
LOG("  critical_speed: %.1f m/s", params_.critical_speed);
```

---

## 調整例

### 例1: スポーツカー風セッティング

高い剛性と速い応答性を持つ、タイトで敏感な挙動

```json
{
    "pitch_stiffness": 12.0,
    "pitch_damping": 4.0,
    "roll_stiffness": 15.0,
    "roll_damping": 6.0,
    "mass_height": 0.015,
    "center_of_rotation_z_offset": 0.3,
    "max_pitch_deg": 4.0,
    "max_roll_deg": 4.0,
    "steer_gain": 0.85,
    "max_acc": 18.0,
    "max_speed": 80.0,
    "reverse_gear_ratio": 1.3,

    "understeer_factor": 0.001,
    "critical_speed": 40.0,
    "max_understeer_reduction": 0.4
}
```

**特徴**:
- ロールとピッチが小さい
- 高剛性・高減衰で素早く安定
- 高加速・高最高速
- やや敏感なステアリング
- 軽いアンダーステア（高速でもコントロール可能）

---

### 例2: SUV/トラック風セッティング

柔らかいサスペンションと大きなロール、安定したステアリング

```json
{
    "pitch_stiffness": 6.0,
    "pitch_damping": 2.0,
    "roll_stiffness": 7.0,
    "roll_damping": 3.0,
    "mass_height": 0.06,
    "center_of_rotation_z_offset": 0.5,
    "max_pitch_deg": 8.0,
    "max_roll_deg": 8.0,
    "steer_gain": 0.55,
    "max_acc": 8.0,
    "max_speed": 40.0,
    "reverse_gear_ratio": 2.0,

    "understeer_factor": 0.002,
    "critical_speed": 25.0,
    "max_understeer_reduction": 0.7
}
```

**特徴**:
- 大きなロールとピッチ
- 低剛性・低減衰でゆったりした挙動
- 控えめな加速と最高速
- 鈍感で安定したステアリング
- 強力なバック
- 強いアンダーステア（安全重視）

---

### 例3: バランス型セダン（デフォルト）

現実的でバランスの取れた乗り心地

```json
{
    "pitch_stiffness": 8.0,
    "pitch_damping": 3.0,
    "roll_stiffness": 10.0,
    "roll_damping": 5.0,
    "mass_height": 0.02,
    "center_of_rotation_z_offset": 0.4,
    "max_pitch_deg": 6.0,
    "max_roll_deg": 6.0,
    "steer_gain": 0.7,
    "max_acc": 12.0,
    "max_speed": 60.0,
    "reverse_gear_ratio": 1.5,

    "understeer_factor": 0.0015,
    "critical_speed": 30.0,
    "max_understeer_reduction": 0.6
}
```

**特徴**:
- 中庸なロールとピッチ
- バランスの取れた応答性
- 実用的な加速と最高速
- 扱いやすいステアリング
- 自然なアンダーステア

---

### 例4: クラシックカー風セッティング

柔らかく揺れやすい、ふわふわした挙動

```json
{
    "pitch_stiffness": 5.0,
    "pitch_damping": 1.5,
    "roll_stiffness": 6.0,
    "roll_damping": 2.0,
    "mass_height": 0.04,
    "center_of_rotation_z_offset": 0.45,
    "max_pitch_deg": 10.0,
    "max_roll_deg": 10.0,
    "steer_gain": 0.6,
    "max_acc": 7.0,
    "max_speed": 35.0,
    "reverse_gear_ratio": 1.8,

    "understeer_factor": 0.0025,
    "critical_speed": 20.0,
    "max_understeer_reduction": 0.8
}
```

**特徴**:
- 大きく長く揺れる
- 低減衰で振動が続く
- 控えめな性能
- クラシックな「船」のような乗り心地
- 非常に強いアンダーステア

---

### 例5: レーシングカー風セッティング（アンダーステア無効）

理想的なグリップでシャープな挙動

```json
{
    "pitch_stiffness": 15.0,
    "pitch_damping": 5.0,
    "roll_stiffness": 20.0,
    "roll_damping": 8.0,
    "mass_height": 0.01,
    "center_of_rotation_z_offset": 0.25,
    "max_pitch_deg": 3.0,
    "max_roll_deg": 3.0,
    "steer_gain": 1.0,
    "max_acc": 20.0,
    "max_speed": 100.0,
    "reverse_gear_ratio": 1.0,

    "understeer_factor": 0.0,
    "critical_speed": 50.0,
    "max_understeer_reduction": 0.0
}
```

**特徴**:
- 極めて小さいロールとピッチ
- 超高剛性でフラットな姿勢
- 最高レベルの加速と最高速
- 敏感なステアリング
- アンダーステア無効（完璧なグリップ）

---

## パラメータ調整のヒント

### 1. ピッチ/ロールを調整する時
1. まず**剛性（stiffness）**を調整して傾きの大きさを決める
2. 次に**減衰（damping）**を調整して揺れの収束速度を決める
3. **mass_height**で全体的な動きの大きさを調整
4. **max_pitch_deg/max_roll_deg**で異常な傾きを制限

### 2. 振動が激しすぎる場合
- `pitch_damping`と`roll_damping`を**増やす**
- または`pitch_stiffness`と`roll_stiffness`を**減らす**

### 3. 動きが鈍い/硬すぎる場合
- `pitch_damping`と`roll_damping`を**減らす**
- または`pitch_stiffness`と`roll_stiffness`を**増やす**

### 4. 加速性能を調整する時
- `max_acc`で加速の速さを調整
- `max_speed`で最高速を制限
- 両方のバランスで車両の性格が決まる

### 5. ステアリング感度を調整する時
- 高速走行が多い → `steer_gain`を小さく（安定）
- 狭い道や急カーブが多い → `steer_gain`を大きく（敏感）

### 6. アンダーステアを調整する時
- 安全重視 → `understeer_factor`を大きく、`critical_speed`を小さく
- スポーツ走行 → `understeer_factor`を小さく、`critical_speed`を大きく
- 完全無効 → `understeer_factor = 0.0`

---

## 技術的な注意事項

### バネ-ダンパーモデルの数式

```cpp
double pitch_forcing = -mass_height * longitudinal_acceleration;
double pitch_acc = (-pitch_stiffness * pitch_angle)
                   - (pitch_damping * pitch_rate)
                   + pitch_forcing;
```

- 臨界減衰条件: `damping ≈ 2 × sqrt(stiffness)`
- オーバーダンピング（振動なし、遅い）: `damping > 2 × sqrt(stiffness)`
- アンダーダンピング（振動あり、速い）: `damping < 2 × sqrt(stiffness)`

### アンダーステアモデルの数式

```cpp
if (speed_abs > critical_speed) {
    double speed_ratio = speed_abs / critical_speed;
    double understeer_coeff = understeer_factor * (speed_ratio * speed_ratio - 1.0);
    understeer_coeff = std::min(understeer_coeff, max_understeer_reduction);
    double grip_factor = 1.0 / (1.0 + understeer_coeff);
    target_wheel_angle *= grip_factor;
}
```

### エンジンブレーキ

`engine_brake_factor_`は別途UDP経由で調整可能（デフォルト: 0.49）

スロットルオフ時に自動的に適用されます（RealVehicle.cpp:183-185）

---

## まとめ

| パラメータ | 用途 | 増やすと | 減らすと |
|----------|------|---------|---------|
| pitch_stiffness | ピッチの硬さ | 素早く戻る | ゆっくり戻る |
| pitch_damping | ピッチの減衰 | 振動が早く収まる | 振動が続く |
| roll_stiffness | ロールの硬さ | 傾きにくい | 大きく傾く |
| roll_damping | ロールの減衰 | 振動が早く収まる | 振動が続く |
| mass_height | 傾きの大きさ | 大きく動く | 小さく動く |
| center_of_rotation_z_offset | 回転中心 | 自然な動き | 浮いた感じ |
| max_pitch_deg | ピッチ制限 | より傾ける | 傾きを抑える |
| max_roll_deg | ロール制限 | より傾ける | 傾きを抑える |
| steer_gain | ステアリング感度 | 敏感 | 鈍感 |
| **understeer_factor** | アンダーステア強度 | 高速で曲がりにくい | よく曲がる |
| **critical_speed** | アンダーステア開始速度 | 高速で発生 | 低速で発生 |
| **max_understeer_reduction** | 最大減少率 | ほぼ曲がらない | 一定量は曲がる |
| max_acc | 加速力 | 速く加速 | 遅く加速 |
| max_speed | 最高速 | 高速まで出る | 低速で頭打ち |
| reverse_gear_ratio | バック力 | 強力 | 弱い |

---

## 関連ファイル

- [GT_esmini/RealVehicle.cpp](GT_esmini/RealVehicle.cpp) - 物理シミュレーション実装
- [GT_esmini/RealVehicle.hpp](GT_esmini/RealVehicle.hpp) - クラス定義
- [GT_esmini/ControllerRealDriver.cpp](GT_esmini/ControllerRealDriver.cpp) - UDP入力とパラメータ読み込み
- [GT_esmini/TerrainTracker.hpp](GT_esmini/TerrainTracker.hpp) - 地形追跡クラス定義
- [GT_esmini/TerrainTracker.cpp](GT_esmini/TerrainTracker.cpp) - 地形追跡実装

---

## 変更履歴
- 2026-01-22: アンダーステアパラメータ、Terrain Tracking機能、実行時オプション設定を追加
- 2026-01-21: 初版作成
