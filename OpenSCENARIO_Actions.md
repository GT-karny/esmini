# OpenSCENARIO v1.2.0 走行関連アクション詳細

OpenSCENARIO v1.2.0 の仕様書（XSDスキーマ）に基づき、走行（Driving）に関連する主要な `PrivateAction` を分類し、そのパラメータと動作の詳細をまとめました。

## 1. LongitudinalAction (縦方向・速度制御)

車両の縦方向の運動（加速、減速、速度維持、車間距離維持）を制御します。

### 1.1 `SpeedAction`
目標速度に到達するように制御します。

*   **概要**: 指定された遷移ダイナミクスに従って、絶対速度または相対速度を目標として速度を変更します。
*   **子要素**:
    *   **`SpeedActionDynamics`** (必須): 遷移の挙動を指定します。
        *   `dynamicsShape`: `linear`, `cubic`, `sinusoidal`, `step` (遷移の形状)
        *   `dynamicsDimension`: `rate` (変化率), `time` (所要時間), `distance` (所要距離)
        *   `value`: 上記 `dimension` に対応する値
    *   **`SpeedActionTarget`** (必須): 目標速度を指定します。
        *   `AbsoluteTargetSpeed`: 絶対速度 (m/s)
            *   `value`: 速度値 (Double)
        *   `RelativeTargetSpeed`: 参照エンティティに対する相対速度
            *   `entityRef`: 参照するエンティティ名
            *   `value`: 相対速度値 (Double)
            *   `speedTargetValueType`: `delta` (差分), `factor` (倍率)
            *   `continuous`: `true` の場合、参照エンティティの速度変化に追従し続ける

### 1.2 `LongitudinalDistanceAction`
先行車等の対象との車間距離を調整します。

*   **概要**: 指定された距離または時間的距離（Time Gap）を維持して走行します。
*   **属性**:
    *   `entityRef` (必須): 対象となるエンティティ名
    *   `distance`: 目標とする距離 (m)
    *   `timeGap`: 目標とする時間的車間距離 (s) - `distance` と排他利用が一般的
    *   `freespace` (必須): `true` の場合、車両のバウンディングボックス間の距離（隙間）、`false` の場合、中心点間の距離
    *   `continuous` (必須): `true` の場合、継続的に距離を維持する
*   **子要素**:
    *   `DynamicConstraints`: 最大加速度・減速度などの制約条件

### 1.3 `SpeedProfileAction`
時系列や距離に応じた速度プロファイルに従って走行します。

*   **概要**: `Time` または `Distance` と `Speed` のペアで定義されたリストに従って速度を変化させます。
*   **属性**:
    *   `entityRef`: (オプション) 特定のエンティティに関連付ける場合
    *   `followingMode`: `position` (位置追従) または `follow` (追従)
*   **子要素**:
    *   `SpeedProfileEntry`: プロファイルの各点
        *   `speed`: 速度 (m/s)
        *   `time`: 時間 (s) または 距離 ... ※XSD上は `time` 属性のみ定義されているように見えますが、仕様としては距離ベースも想定される場合があります（要確認: v1.2.0 XSDでは `time` 属性のみ）
    *   `DynamicConstraints`: 制約条件

---

## 2. LateralAction (横方向・操舵制御)

車両の横方向の運動（車線変更、オフセット走行）を制御します。

### 2.1 `LaneChangeAction`
車線変更を行います。

*   **概要**: 指定された遷移ダイナミクスに従って、目標とする車線へ移動します。
*   **属性**:
    *   `targetLaneOffset`: 目標車線の中心からのオフセット (m)
*   **子要素**:
    *   **`LaneChangeActionDynamics`** (必須): 遷移の挙動
        *   `dynamicsShape`: `linear`, `cubic`, `sinusoidal`
        *   `dynamicsDimension`: `rate` (横移動速度), `time` (所要時間), `distance` (所要距離)
        *   `value`: 上記 `dimension` に対応する値
    *   **`LaneChangeTarget`** (必須): 目標車線
        *   `AbsoluteTargetLane`: 絶対的な車線ID
            *   `value`: Lane ID (String)
        *   `RelativeTargetLane`: 現在の車線からの相対的な車線数
            *   `entityRef`: 参照エンティティ (通常は自車)
            *   `value`: 車線数 (+1: 左, -1: 右 など、OpenDRIVEの定義に準拠)

### 2.2 `LaneOffsetAction`
車線内での横位置（オフセット）を変更します。

*   **概要**: 車線中心から左右にずれた位置を走行するように制御します。
*   **属性**:
    *   `continuous` (必須): `true` の場合、道路の曲率に合わせてオフセットを維持し続ける
*   **子要素**:
    *   `LaneOffsetActionDynamics` (必須): 遷移挙動
        *   `maxLateralAcc`: 最大横加速度制限
    *   `LaneOffsetTarget` (必須): 目標オフセット
        *   `AbsoluteTargetLaneOffset`: 絶対値 (m)
        *   `RelativeTargetLaneOffset`: 参照エンティティからの相対値 (m)

### 2.3 `LateralDistanceAction`
他車との横方向距離を調整します。

*   **概要**: 並走する車両などとの横間隔を維持します。
*   **属性**:
    *   `entityRef` (必須): 対象エンティティ
    *   `distance`: 目標距離 (m)
    *   `freespace` (必須): バウンディングボックス考慮の有無
    *   `continuous` (必須): 継続性の有無

---

## 3. RoutingAction (経路制御)

車両の進行ルートを決定します。

### 3.1 `AssignRouteAction`
*   **概要**: 事前に定義された `Route` (OpenDRIVEのWaypoint列など) を車両に割り当てます。
*   **子要素**:
    *   `Route`: 経路定義 (Waypointのリスト)
    *   `CatalogReference`: カタログからの参照

### 3.2 `FollowTrajectoryAction`
*   **概要**: 幾何学的な軌跡 (`Trajectory`) に厳密に従って走行します。
*   **子要素**:
    *   `Trajectory`: 軌跡定義 (Polyline, Clothoid, Nurbs)
    *   `TimeReference`: 時間軸の同期方法 (`Timing` または `None`)
    *   `TrajectoryFollowingMode`: 追従モード (`position` または `follow`)

### 3.3 `AcquirePositionAction`
*   **概要**: 車両を指定された位置に到達させます。物理的な移動というよりは、シナリオ開始時やリセット時の初期化、または論理的な位置あわせに使われます。
*   **子要素**:
    *   `Position`: 到達目標位置 (World, Lane, Road, Relative 等の座標系)

---

## 4. その他 重要なアクション

### 4.1 `SynchronizeAction`
*   **概要**: 「マスター」となるエンティティの動きに合わせて、自車の位置や速度を調整します。合流地点でのタイミング調整や、隊列走行の制御に使用されます。
*   **属性**:
    *   `masterEntityRef` (必須): マスター車両
    *   `targetTolerance`: 許容誤差
*   **子要素**:
    *   `TargetPositionMaster`: マスター車両上の基準位置
    *   `TargetPosition`: 自車の目標位置
    *   `FinalSpeed`: 同期完了時の速度挙動

### 4.2 `TeleportAction`
*   **概要**: 車両を瞬時に指定位置へ移動させます。物理演算を無視したワープです。
*   **子要素**:
    *   `Position`: 移動先座標

---

## 用語補足

*   **DynamicsShape**: 値の変化カーブ。
    *   `step`: 瞬時に変化 (ジャンプ)
    *   `linear`: 直線的に変化 (一定の加速度/変化率)
    *   `cubic`: 3次関数的に変化 (滑らかな加減速)
    *   `sinusoidal`: 正弦波的に変化 (最も滑らか)
*   **DynamicsDimension**: 変化の基準。
    *   `time`: 指定された「時間」で完了する
    *   `distance`: 指定された「距離」で完了する
    *   `rate`: 指定された「変化率」(例:加速度) で変化する
