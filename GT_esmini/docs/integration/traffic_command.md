# Traffic Command (OSI) サポート状況

GT_esmini (およびベースとなるesmini) における `osi3::TrafficCommand` の対応状況について調査した結果をまとめます。
これは、`MovingObject.future_trajectory` フィールドの代替手段として `TrafficCommand` を使用可能かどうかの判断材料となります。

## Traffic Command とは

`BasicOSI` において、`TrafficCommand` はシミュレーター（シナリオエンジン）から外部コントローラー（自動運転スタックなど）に対して、「何をすべきか」という**指令（Command）**を送るためのメッセージです。
一方、`MovingObject.future_trajectory` は、「物体が物理的にどの経路を辿る予定か」という**予測（Prediction）**または**真値（Ground Truth）**を表します。

## esminiの実装状況

`OSITrafficCommand.cpp` の実装に基づき、以下のOpenSCENARIOアクションが `osi3::TrafficCommand` に変換され、OSI経由で出力されます。

| OpenSCENARIO Action | OSI Action Field | 対応状況 | 備考 |
|---------------------|------------------|----------|------|
| `LaneChangeAction` | `lane_change_action` | ✅ サポート済 | 距離/時間指定、ダイナミクス形状（Cubic/Linear等）も反映されます。 |
| `SpeedAction` | `speed_action` | ✅ サポート済 | 絶対速度/相対速度、距離/時間指定が反映されます。 |
| `TeleportAction` | `teleport_action` | ✅ サポート済 | 位置・姿勢が反映されます。 |
| `AssignRouteAction` | `follow_path_action` | ✅ サポート済 | ルートのウェイポイント列 (`path_point`) として出力されます。 |
| `AcquirePositionAction`| `acquire_global_position_action` | ✅ サポート済 | 目標座標への到達指令として出力されます。 |
| **`FollowTrajectoryAction`** | - | **❌ 未サポート** | OpenSCENARIOには定義がありますが、OSI Traffic Commandへの変換ロジックが実装されていません。 |

## 結論：デュアル軌道ロジックにおける選択

今回の要件（GhostおよびEgoの将来軌道・リカバリ軌道の通知）に対しては、以下の理由から **`MovingObject.future_trajectory` の使用が適切** です。

1.  **`FollowTrajectoryAction` がOSI出力未対応:** `TrafficCommand` で任意の詳細な軌道（Trajectory）を送るための標準的なアクションがesminiでサポートされていません。
2.  **データの粒度:** `AssignRouteAction` (`follow_path_action`) はウェイポイント（通過点）のリストを送るものであり、0.1秒ごとの密な位置・姿勢制御点（Trajectory）を送る用途には適していません。
3.  **意味論:** Ghostの軌道は「指令」ではなく「確定した未来の動き（Ground Truth）」であるため、`SensorView` (MovingObject) に含めるのがOSIの設計思想に合致します。

### 将来的な拡張性
もし外部コントローラーに対して「このリカバリ軌道に従って制御せよ」という**強制的な指令**を出したい場合は、`TrafficCommand` の `follow_path_action` を活用する（または `FollowTrajectoryAction` のサポートを追加実装する）方法も検討できますが、現時点では `future_trajectory` が最も確実な手段です。
