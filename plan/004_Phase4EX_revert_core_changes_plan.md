# Phase 4 実装計画: OSIサポート (Build Replacement 戦略)

## 1. ゴール
新しいライト機能（LightStateAction, AutoLight）のサポートを OSI (Open Simulation Interface) に組み込みます。
`GT_esmini` が管理するライト状態（`VehicleLightExtension` 経由）を OSI の `MovingObject` メッセージに反映させることを目標とします。

## 2. 技術的アプローチ: 「Build-Time Component Swap（ビルド時コンポーネント置換）」

ユーザー要件に基づき、`esminiLib` のビルド構成を変更し、オリジナルの `OSIReporter.cpp` の代わりに、改修版である `GT_OSIReporter.cpp` をコンパイル・リンクさせる方式を採用します。
これにより、ヘッダファイル（`OSIReporter.hpp`）や他のソースコードを変更することなく、`OSIReporter` の挙動を差し替えることができます。

### 戦略の概要
1.  **Clone & Modify**: `esmini` の `OSIReporter.cpp` を `GT_esmini/GT_OSIReporter.cpp` にコピーし、ライト注入ロジックを追加します。
    *   **重要**: クラス名は `OSIReporter` のまま維持します（ヘッダファイルとの整合性のため）。
2.  **Build Configuration**: `CMakeLists.txt` を編集し、ビルド対象から `OSIReporter.cpp` を外し、`GT_OSIReporter.cpp` を追加します。
3.  **Traceability**: 変更内容と置換の事実を追跡可能なドキュメントとして記録します。

## 3. 実装ステップ

### 3.1. GT_OSIReporter の作成

#### [NEW] `GT_esmini/GT_OSIReporter.cpp`
`EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSIReporter.cpp` をコピーして作成します。
`UpdateOSIMovingObject` メソッド内に、ライト状態のセット処理を追加します。

```cpp
// GT_esmini/GT_OSIReporter.cpp

// ... (Includes and other methods) ...

int OSIReporter::UpdateOSIMovingObject(ObjectState* objectState)
{
    // 1. オリジナルの処理（位置、速度などのセット）
    // ... (Original logic copied from OSIReporter.cpp) ...
    
    // 2. [New] Light State Injection
    // static変数 obj_osi_internal へのアクセスはこのファイル内なら可能
    
    int id = objectState->state_.info.id;
    // Find vehicle extension (using singleton or helper)
    // Note: Need to include appropriate headers for VehicleExtensionManager
    auto* vehicle = gt_esmini::VehicleExtensionManager::Instance().GetVehicleById(id);
    
    if (vehicle) {
         auto* ext = gt_esmini::VehicleExtensionManager::Instance().GetExtension(vehicle);
         if (ext) {
             // Access the last added moving object (which corresponds to this vehicle)
             // obj_osi_internal.mobj is usually a pointer to the current object being updated
             if (obj_osi_internal.mobj) {
                // Map and set light state
                // ext->GetLightState(...) -> obj_osi_internal.mobj->mutable_vehicle_classification()->mutable_light_state()->...
             }
         }
    }
    
    // ...
    return 0;
}
```

### 3.2. ビルド設定の変更

#### [MODIFY] `CMakeLists.txt` (esmini root or relevant subdirectory)
`OSIReporter.cpp` をコンパイルリストから除外し、`GT_OSIReporter.cpp` に置き換えます。

```cmake
# Example modification logic (Conceptual)
# list(REMOVE_ITEM ESMINI_SOURCES "EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSIReporter.cpp")
# list(APPEND ESMINI_SOURCES "GT_esmini/GT_OSIReporter.cpp")
```

### 3.3. トレーサビリティの確保

#### [NEW] `plan/005_build_modifications_traceability.md`
ビルド構成の変更点と、なぜファイルを置換したかの理由、およびオリジナルファイルとの差分概要（あるいは差分確認方法）を記録します。

## 4. プロジェクト構成

### 4.1. ファイル構成
- `GT_esmini/GT_OSIReporter.cpp`: 改変された実装ファイル。
- `plan/005_build_modifications_traceability.md`: 変更追跡ドキュメント。

### 4.2. 依存関係
- `GT_OSIReporter.cpp` は `OSIReporter.hpp` および `GT_esmini` のヘッダ（`VehicleExtensionManager.hpp`等）に依存します。インクルードパスの設定が必要になる場合があります。

## 5. 検証計画
1.  **ビルド確認**: `esminiLib` (および `esmini` アプリケーション) がリンクエラーなくビルドできることを確認。
2.  **機能確認**: `auto-light` シナリオを実行し、出力されるOSIメッセージにライト状態が含まれていることを確認（UDP受信ツール等を使用）。
3.  **既存機能確認**: 通常の位置情報出力などがリグレッションしていないことを確認。
