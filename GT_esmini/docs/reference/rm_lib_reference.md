# RoadManager Python Wrapper リファレンス

このドキュメントでは、esminiRMLib および GT_esminiRMLib の Python ラッパーの全 API を記載します。

---

## 1. 概要

| モジュール | クラス名 | 対象 C ライブラリ | 役割 |
|:---|:---|:---|:---|
| `rm_lib.py` | `EsminiRMLib` | `esminiRMLib.dll` | esmini 標準の RoadManager 機能（Position 操作、道路/レーン情報取得、標識など） |
| `gt_rm_lib.py` | `GTEsminiRMLib` | `GT_esminiLib.dll` | GT 拡張機能（道路接続情報、ジャンクション情報、信号情報） |

---

## 2. EsminiRMLib (rm_lib.py)

### 2.1 構造体

#### `RM_PositionXYZ`
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `x` | `float` | X座標 |
| `y` | `float` | Y座標 |
| `z` | `float` | Z座標 |

#### `RM_PositionData`
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `x` | `float` | ワールドX座標 |
| `y` | `float` | ワールドY座標 |
| `z` | `float` | ワールドZ座標 |
| `h` | `float` | ヘディング (yaw) |
| `p` | `float` | ピッチ |
| `r` | `float` | ロール |
| `hRelative` | `float` | 道路に対する相対ヘディング |
| `roadId` | `uint32` | 道路ID |
| `junctionId` | `uint32` | ジャンクションID (-1: ジャンクション外) |
| `laneId` | `int` | レーンID |
| `laneOffset` | `float` | レーン中心からの横方向オフセット |
| `s` | `float` | 道路に沿った縦方向距離 |

#### `RM_RoadLaneInfo`
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `pos` | `RM_PositionXYZ` | グローバル座標 |
| `heading` | `float` | 道路ヘディング |
| `pitch` | `float` | 道路ピッチ |
| `roll` | `float` | 道路ロール |
| `width` | `float` | レーン幅 |
| `curvature` | `float` | 曲率 |
| `speed_limit` | `float` | 速度制限 (m/s) |
| `roadId` | `uint32` | 道路ID |
| `junctionId` | `uint32` | ジャンクションID |
| `laneId` | `int` | レーンID |
| `laneOffset` | `float` | レーンオフセット |
| `s` | `float` | 縦方向位置 |
| `t` | `float` | 横方向位置 |
| `road_type` | `int` | 道路タイプ |
| `road_rule` | `int` | 道路ルール |
| `lane_type` | `int` | レーンタイプ |

#### `RM_RoadProbeInfo`
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `road_lane_info` | `RM_RoadLaneInfo` | プローブ位置のレーン情報 |
| `relative_pos` | `RM_PositionXYZ` | 車両基準の相対位置 |
| `relative_h` | `float` | 車両基準の相対ヘディング |

#### `RM_PositionDiff`
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `ds` | `float` | 縦方向距離差 |
| `dt` | `float` | 横方向距離差 |
| `dLaneId` | `int` | レーンID差 |

#### `RM_RoadSign`
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `id` | `int` | 標識ID |
| `x` | `float` | グローバルX座標 |
| `y` | `float` | グローバルY座標 |
| `z` | `float` | グローバルZ座標 |
| `z_offset` | `float` | 道路面からのZオフセット |
| `h` | `float` | ヘディング |
| `roadId` | `uint32` | 道路ID |
| `s` | `float` | 縦方向位置 |
| `t` | `float` | 横方向位置 |
| `name` | `str` | 標識名 (3Dモデルファイル名) |
| `orientation` | `int` | 向き (1=道路方向, -1=逆方向) |
| `length` | `float` | 長さ |
| `height` | `float` | 高さ |
| `width` | `float` | 幅 |

#### `RM_RoadObjValidity`
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `fromLane` | `int` | 有効範囲の開始レーン |
| `toLane` | `int` | 有効範囲の終了レーン |

#### `RM_GeoReference`
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `a_` | `float` | 楕円体パラメータ a |
| `axis_` | `str` | 座標軸 |
| `b_` | `float` | 楕円体パラメータ b |
| `ellps_` | `str` | 楕円体名 |
| `k_` | `float` | スケールファクター k |
| `k_0_` | `float` | スケールファクター k_0 |
| `lat_0_` | `float` | 基準緯度 |
| `lon_0_` | `float` | 基準経度 |
| `lon_wrap_` | `float` | 経度ラップ |
| `over_` | `float` | over パラメータ |
| `pm_` | `str` | 本初子午線 |
| `proj_` | `str` | 投影法 |
| `units_` | `str` | 座標単位 |
| `vunits_` | `str` | 垂直座標単位 |
| `x_0_` | `float` | False easting |
| `y_0_` | `float` | False northing |
| `datum_` | `str` | 測地系 |
| `geo_id_grids_` | `str` | ジオイドグリッド |
| `zone_` | `float` | UTMゾーン |
| `towgs84_` | `int` | WGS84変換パラメータ |
| `original_georef_str_` | `str` | 元のGeoReference文字列 |

### 2.2 定数 (RM_PositionMode)

```python
RM_Z_SET = 1        # Z: 設定
RM_Z_DEFAULT = 1    # Z: デフォルト
RM_Z_ABS = 3        # Z: 絶対
RM_Z_REL = 7        # Z: 相対
RM_H_ABS = 48       # Heading: 絶対
RM_H_REL = 112      # Heading: 相対
RM_P_ABS = 768      # Pitch: 絶対
RM_P_REL = 1792     # Pitch: 相対
RM_R_ABS = 12288    # Roll: 絶対
RM_R_REL = 28672    # Roll: 相対
```

使用例: `RM_Z_REL | RM_H_ABS` = 相対Z + 絶対Heading

---

### 2.3 初期化・管理

#### `Init(odr_filename: str) -> int`
OpenDRIVEファイルでRoadManagerを初期化する。
- **戻り値**: 0=成功, -1=失敗

#### `InitWithString(odr_xml_string: str) -> int`
OpenDRIVE XML文字列でRoadManagerを初期化する。
- **戻り値**: 0=成功, -1=失敗

#### `Close() -> int`
RoadManagerを終了する。

#### `SetLogFilePath(log_file_path: str)`
ログファイルのパスを設定する。`Init()` の前に呼び出す必要がある。空文字でログ無効化。

#### `CreatePosition() -> int`
Positionオブジェクトを作成する。
- **戻り値**: ハンドル (>=0) または -1 (エラー)

#### `GetNrOfPositions() -> int`
作成済みPositionオブジェクトの数を取得する。

#### `DeletePosition(handle: int) -> int`
Positionオブジェクトを削除する。handle=-1 で全削除。

#### `CopyPosition(handle: int) -> int`
Positionオブジェクトをコピーする。
- **戻り値**: 新しいハンドル または -1

---

### 2.4 Position モード

#### `SetObjectPositionMode(handle: int, pos_mode_type: int, mode: int)`
Position のアライメントモードを設定する (Z, H, P, R)。
- `pos_mode_type`: 0=SET, 1=UPDATE
- `mode`: RM_PositionMode ビットマスク

#### `SetObjectPositionModeDefault(handle: int, pos_mode_type: int)`
Positionモードをデフォルトに戻す。

#### `SetSnapLaneTypes(handle: int, lane_types: int) -> int`
Positionがスナップするレーンタイプを指定する。
- `lane_types`: ビットマスク (ANY_DRIVING=1966594, ANY_ROAD=1966734, ANY=-1)

#### `SetLockOnLane(handle: int, mode: bool) -> int`
レーンIDを固定するか(True)、最近傍にスナップするか(False)を設定する。

---

### 2.5 道路情報

#### `GetNumberOfRoads() -> int`
道路の総数を取得する。

#### `GetSpeedUnit() -> int`
速度単位を取得する。
- **戻り値**: -1=エラー, 0=未定義, 1=km/h, 2=m/s, 3=mph

#### `GetIdOfRoadFromIndex(index: int) -> int`
インデックスから道路IDを取得する。

#### `GetRoadLength(road_id: int) -> float`
道路の長さ (m) を取得する。見つからない場合は 0.0。

#### `GetRoadIdString(road_id: int) -> str`
道路の文字列IDを取得する。

#### `GetRoadIdFromString(road_id_str: str) -> int`
文字列IDから整数道路IDを取得する。見つからない場合は -1。

#### `GetJunctionIdString(junction_id: int) -> str`
ジャンクションの文字列IDを取得する。

#### `GetJunctionIdFromString(junction_id_str: str) -> int`
文字列IDから整数ジャンクションIDを取得する。見つからない場合は -1。

---

### 2.6 レーン情報

#### `GetRoadNumberOfLanes(road_id: int, s: float, type_mask: int = -1) -> int`
指定位置でのレーン数を取得する。
- `type_mask`: レーンタイプフィルタ (-1=全て, 1966594=走行可能)

#### `GetLaneIdByIndex(road_id: int, lane_index: int, s: float, type_mask: int = -1) -> Tuple[int, int]`
インデックスとタイプからレーンIDを取得する。
- **戻り値**: `(result, lane_id)` — result=0 で成功

#### `GetRoadNumberOfDrivableLanes(road_id: int, s: float) -> int`
走行可能なレーン数を取得する。

#### `GetDrivableLaneIdByIndex(road_id: int, lane_index: int, s: float) -> Tuple[int, int]`
走行可能レーンのIDをインデックスで取得する。
- **戻り値**: `(result, lane_id)`

#### `GetNumberOfRoadsOverlapping(handle: int) -> int`
指定位置で重なっている道路の数を取得する。

#### `GetOverlappingRoadId(handle: int, index: int) -> int`
重なっている道路のIDを取得する。

#### `GetLaneWidth(handle: int, lane_id: int) -> Tuple[int, float]`
現在のs位置でのレーン幅を取得する。
- **戻り値**: `(result, width)`

#### `GetLaneWidthByRoadId(road_id: int, lane_id: int, s: float) -> Tuple[int, float]`
指定道路・位置でのレーン幅を取得する。
- **戻り値**: `(result, width)`

#### `GetLaneType(handle: int, lane_id: int) -> int`
現在のs位置でのレーンタイプを取得する。

#### `GetInLaneType(handle: int) -> int`
現在 Position が存在するレーンタイプを取得する。

#### `GetLaneTypeByRoadId(road_id: int, lane_id: int, s: float) -> int`
指定道路・位置でのレーンタイプを取得する。

---

### 2.7 Position 設定

#### `SetLanePosition(handle: int, road_id: int, lane_id: int, lane_offset: float, s: float, align: bool = True) -> int`
道路座標 (レーンベース) から位置を設定する。

#### `SetRoadPosition(handle: int, road_id: int, s: float, t: float, align: bool = True) -> int`
道路 s/t 座標から位置を設定する。

#### `SetS(handle: int, s: float) -> int`
s (縦方向距離) のみを設定する。

#### `SetWorldPosition(handle: int, x: float, y: float, z: float, h: float, p: float, r: float) -> int`
ワールド座標 (x, y, z, heading, pitch, roll) から位置を設定する。
`float('nan')` を指定するとそのパラメータはスキップされる。

#### `SetWorldXYHPosition(handle: int, x: float, y: float, h: float) -> int`
ワールド X, Y, Heading から位置を設定する。

#### `SetWorldXYZHPosition(handle: int, x: float, y: float, z: float, h: float) -> int`
ワールド X, Y, Z, Heading から位置を設定する。

#### `SetWorldPositionMode(handle: int, x: float, y: float, z: float, h: float, p: float, r: float, mode: int) -> int`
ワールド座標 + PositionMode ビットマスクで位置を設定する。

#### `SetH(handle: int, h: float) -> int`
ヘディングを設定する（現在のモード設定を使用）。

#### `SetHMode(handle: int, h: float, mode: int) -> int`
ヘディングをモード指定で設定する。
- `mode`: `RM_H_ABS` (絶対) または `RM_H_REL` (相対)

#### `SetRoadId(handle: int, road_id: int) -> int`
x,y位置はそのまま、所属道路を変更する。

#### `PositionMoveForward(handle: int, dist: float, junction_selector_angle: float = -1.0) -> int`
道路に沿って前方に移動する。
- `dist`: 移動距離 (m)
- `junction_selector_angle`: ジャンクションでの方向選択 (0=直進相当, π/2=右, π=Uターン, 3π/2=左, -1=ランダム)

---

### 2.8 クエリ

#### `GetPositionData(handle: int) -> Tuple[int, RM_PositionData]`
Position の全データを取得する。
- **戻り値**: `(result, data)` — result=0 で成功

#### `GetSpeedLimit(handle: int) -> float`
現在位置の速度制限 (m/s) を取得する。

#### `GetLaneInfo(handle: int, lookahead_distance: float = 0.0, look_ahead_mode: int = 0, in_road_driving_direction: bool = True) -> Tuple[int, RM_RoadLaneInfo]`
レーン情報を取得する。
- `look_ahead_mode`: 0=LaneCenter, 1=RoadCenter, 2=CurrentOffset
- **戻り値**: `(result, data)`

#### `GetProbeInfo(handle: int, lookahead_distance: float = 0.0, look_ahead_mode: int = 0, in_road_driving_direction: bool = True) -> Tuple[int, RM_RoadProbeInfo]`
ロードプローブ情報（レーン情報＋相対位置）を取得する。
- **戻り値**: `(result, data)`

#### `SubtractAFromB(handle_a: int, handle_b: int) -> Tuple[int, RM_PositionDiff]`
2つの Position 間の差分 (Δs, Δt, ΔlaneId) を取得する。
- **戻り値**: `(result, pos_diff)` — result=0 で成功, -2 で経路が見つからない

---

### 2.9 道路標識

#### `GetNumberOfRoadSigns(road_id: int) -> int`
指定道路の標識数を取得する。

#### `GetRoadSign(road_id: int, index: int) -> Tuple[int, RM_RoadSign]`
標識情報をインデックスで取得する。
- **戻り値**: `(result, road_sign)`

#### `GetNumberOfRoadSignValidityRecords(road_id: int, index: int) -> int`
標識のレーン有効範囲レコード数を取得する。

#### `GetRoadSignValidityRecord(road_id: int, sign_index: int, validity_index: int) -> Tuple[int, RM_RoadObjValidity]`
標識の有効範囲レコードを取得する。
- **戻り値**: `(result, validity)`

---

### 2.10 GeoReference

#### `GetOpenDriveGeoReference() -> Tuple[int, RM_GeoReference]`
OpenDRIVE の GeoReference 情報を取得する。
- **戻り値**: `(result, geo_ref)`

---

### 2.11 オプション

#### `SetOption(name: str) -> int`
オプションを設定する（次のシナリオ実行でリセット）。

#### `UnsetOption(name: str) -> int`
オプションを解除する。

#### `SetOptionValue(name: str, value: str) -> int`
オプション値を設定する（次のシナリオ実行でリセット）。

#### `SetOptionPersistent(name: str) -> int`
オプションを永続的に設定する（ライブラリ再読込まで維持）。

#### `SetOptionValuePersistent(name: str, value: str) -> int`
オプション値を永続的に設定する。

#### `GetOptionValue(name: str) -> str`
オプション値を取得する。

#### `GetOptionSet(name: str) -> bool`
オプションが設定されているか確認する。

---

## 3. GTEsminiRMLib (gt_rm_lib.py)

### 3.1 構造体 / データクラス

#### `RoadLinkInfo` (dataclass)
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `element_id` | `int` | 接続先の道路またはジャンクションID |
| `element_type` | `int` | 要素タイプ (0=unknown, 1=road, 2=junction) |
| `contact_point` | `int` | 接触点 (0=unknown, 1=start, 2=end) |

プロパティ: `is_road`, `is_junction`, `contact_point_name`

#### `JunctionConnection` (dataclass)
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `incoming_road_id` | `int` | 流入道路ID |
| `connecting_road_id` | `int` | 接続道路ID |
| `contact_point` | `int` | 接触点タイプ |

#### `RoadSignalInfo` (dataclass)
| フィールド | 型 | 説明 |
|:---|:---|:---|
| `id` | `int` | 信号ID |
| `s` | `float` | 道路に沿った縦方向位置 |
| `t` | `float` | 道路参照線からの横方向位置 |
| `x` | `float` | グローバルX座標 |
| `y` | `float` | グローバルY座標 |
| `z` | `float` | グローバルZ座標 |
| `h` | `float` | ヘディング |
| `p` | `float` | ピッチ |
| `r` | `float` | ロール |
| `type` | `str` | 信号タイプ |
| `subtype` | `str` | サブタイプ |
| `country` | `str` | 国コード |
| `value` | `float` | 値 |
| `unit` | `str` | 単位 |
| `text` | `str` | テキスト |
| `is_dynamic` | `bool` | 動的かどうか |
| `height` | `float` | 高さ |
| `width` | `float` | 幅 |

### 3.2 定数

```python
GT_RM_LINK_TYPE_PREDECESSOR = 0
GT_RM_LINK_TYPE_SUCCESSOR = 1

GT_RM_ELEMENT_TYPE_UNKNOWN = 0
GT_RM_ELEMENT_TYPE_ROAD = 1
GT_RM_ELEMENT_TYPE_JUNCTION = 2

GT_RM_CONTACT_POINT_UNKNOWN = 0
GT_RM_CONTACT_POINT_START = 1
GT_RM_CONTACT_POINT_END = 2
```

---

### 3.3 初期化・終了

#### `init(odr_path: str) -> int`
OpenDRIVEファイルで GT_esminiRMLib を初期化する。
- **戻り値**: 0=成功, -1=失敗

#### `close()`
GT_esminiRMLib を終了しリソースを解放する。

---

### 3.4 道路接続情報

#### `get_road_successor(road_id: int) -> Optional[RoadLinkInfo]`
道路の後続リンクを取得する。後続がなければ `None`。

#### `get_road_predecessor(road_id: int) -> Optional[RoadLinkInfo]`
道路の先行リンクを取得する。先行がなければ `None`。

#### `get_connected_roads(road_id: int, direction: str = 'both') -> List[Tuple[int, str, int]]`
接続道路を取得する。ジャンクションも自動解決する。
- `direction`: `'successor'`, `'predecessor'`, `'both'`
- **戻り値**: `(connected_road_id, connection_type, contact_point)` のリスト
  - `connection_type`: `'successor'`, `'predecessor'`, `'junction_successor'`, `'junction_predecessor'`

---

### 3.5 ジャンクション情報

#### `get_junction_connection_count(junction_id: int) -> int`
ジャンクションの接続数を取得する。

#### `get_junction_connection(junction_id: int, index: int) -> Optional[JunctionConnection]`
インデックスでジャンクション接続を取得する。

#### `get_junction_connections(junction_id: int) -> List[JunctionConnection]`
ジャンクションの全接続を取得する。

#### `get_junction_connections_from_road(junction_id: int, incoming_road_id: int) -> List[int]`
特定流入道路からの接続道路ID一覧を取得する。

#### `get_junction_connections_from_road_with_contact(junction_id: int, incoming_road_id: int) -> List[Tuple[int, int]]`
特定流入道路からの接続道路IDと接触点を取得する。
- **戻り値**: `(connecting_road_id, contact_point)` のリスト

---

### 3.6 道路情報

#### `get_num_roads() -> int`
道路数を取得する。

#### `get_road_id_by_index(index: int) -> Optional[int]`
インデックスから道路IDを取得する。

#### `get_all_road_ids() -> List[int]`
全道路IDを取得する。

#### `get_road_length(road_id: int) -> float`
道路の長さ (m) を取得する。

---

### 3.7 信号情報

#### `get_road_signal_count(road_id: int) -> int`
道路上の信号数を取得する。見つからない場合は -1。

#### `get_road_signal(road_id: int, index: int) -> Optional[RoadSignalInfo]`
信号情報をインデックスで取得する。

#### `get_all_road_signals(road_id: int) -> List[RoadSignalInfo]`
道路上の全信号を取得する。

---

## 4. C API カバレッジ対照表

### 4.1 esminiRMLib.hpp → rm_lib.py (59/59 = 100%)

| C API | Python メソッド |
|:---|:---|
| `RM_Init` | `Init` |
| `RM_InitWithString` | `InitWithString` |
| `RM_Close` | `Close` |
| `RM_SetLogFilePath` | `SetLogFilePath` |
| `RM_CreatePosition` | `CreatePosition` |
| `RM_GetNrOfPositions` | `GetNrOfPositions` |
| `RM_DeletePosition` | `DeletePosition` |
| `RM_CopyPosition` | `CopyPosition` |
| `RM_SetObjectPositionMode` | `SetObjectPositionMode` |
| `RM_SetObjectPositionModeDefault` | `SetObjectPositionModeDefault` |
| `RM_SetSnapLaneTypes` | `SetSnapLaneTypes` |
| `RM_SetLockOnLane` | `SetLockOnLane` |
| `RM_GetNumberOfRoads` | `GetNumberOfRoads` |
| `RM_GetSpeedUnit` | `GetSpeedUnit` |
| `RM_GetIdOfRoadFromIndex` | `GetIdOfRoadFromIndex` |
| `RM_GetRoadLength` | `GetRoadLength` |
| `RM_GetRoadIdString` | `GetRoadIdString` |
| `RM_GetRoadIdFromString` | `GetRoadIdFromString` |
| `RM_GetJunctionIdString` | `GetJunctionIdString` |
| `RM_GetJunctionIdFromString` | `GetJunctionIdFromString` |
| `RM_GetRoadNumberOfLanes` | `GetRoadNumberOfLanes` |
| `RM_GetLaneIdByIndex` | `GetLaneIdByIndex` |
| `RM_GetRoadNumberOfDrivableLanes` | `GetRoadNumberOfDrivableLanes` |
| `RM_GetDrivableLaneIdByIndex` | `GetDrivableLaneIdByIndex` |
| `RM_GetNumberOfRoadsOverlapping` | `GetNumberOfRoadsOverlapping` |
| `RM_GetOverlappingRoadId` | `GetOverlappingRoadId` |
| `RM_SetLanePosition` | `SetLanePosition` |
| `RM_SetRoadPosition` | `SetRoadPosition` |
| `RM_SetS` | `SetS` |
| `RM_SetWorldPosition` | `SetWorldPosition` |
| `RM_SetWorldXYHPosition` | `SetWorldXYHPosition` |
| `RM_SetWorldXYZHPosition` | `SetWorldXYZHPosition` |
| `RM_SetWorldPositionMode` | `SetWorldPositionMode` |
| `RM_SetH` | `SetH` |
| `RM_SetHMode` | `SetHMode` |
| `RM_SetRoadId` | `SetRoadId` |
| `RM_PositionMoveForward` | `PositionMoveForward` |
| `RM_GetPositionData` | `GetPositionData` |
| `RM_GetSpeedLimit` | `GetSpeedLimit` |
| `RM_GetLaneInfo` | `GetLaneInfo` |
| `RM_GetProbeInfo` | `GetProbeInfo` |
| `RM_GetLaneWidth` | `GetLaneWidth` |
| `RM_GetLaneWidthByRoadId` | `GetLaneWidthByRoadId` |
| `RM_GetLaneType` | `GetLaneType` |
| `RM_GetInLaneType` | `GetInLaneType` |
| `RM_GetLaneTypeByRoadId` | `GetLaneTypeByRoadId` |
| `RM_SubtractAFromB` | `SubtractAFromB` |
| `RM_GetNumberOfRoadSigns` | `GetNumberOfRoadSigns` |
| `RM_GetRoadSign` | `GetRoadSign` |
| `RM_GetNumberOfRoadSignValidityRecords` | `GetNumberOfRoadSignValidityRecords` |
| `RM_GetRoadSignValidityRecord` | `GetRoadSignValidityRecord` |
| `RM_GetOpenDriveGeoReference` | `GetOpenDriveGeoReference` |
| `RM_SetOption` | `SetOption` |
| `RM_UnsetOption` | `UnsetOption` |
| `RM_SetOptionValue` | `SetOptionValue` |
| `RM_SetOptionPersistent` | `SetOptionPersistent` |
| `RM_SetOptionValuePersistent` | `SetOptionValuePersistent` |
| `RM_GetOptionValue` | `GetOptionValue` |
| `RM_GetOptionSet` | `GetOptionSet` |

### 4.2 GT_esminiRMLib.hpp → gt_rm_lib.py (13/13 = 100%)

| C API | Python メソッド |
|:---|:---|
| `GT_RM_Init` | `init` |
| `GT_RM_Close` | `close` |
| `GT_RM_GetRoadSuccessor` | `get_road_successor` |
| `GT_RM_GetRoadPredecessor` | `get_road_predecessor` |
| `GT_RM_GetJunctionConnectionCount` | `get_junction_connection_count` |
| `GT_RM_GetJunctionConnection` | `get_junction_connection` |
| `GT_RM_GetJunctionConnectionsFromRoad` | `get_junction_connections_from_road` |
| `GT_RM_GetJunctionConnectionFromRoadByIndex` | `get_junction_connections_from_road` (内部使用) |
| `GT_RM_GetNumRoads` | `get_num_roads` |
| `GT_RM_GetRoadIdByIndex` | `get_road_id_by_index` |
| `GT_RM_GetRoadLength` | `get_road_length` |
| `GT_RM_GetRoadSignalCount` | `get_road_signal_count` |
| `GT_RM_GetRoadSignal` | `get_road_signal` |

**追加の便利メソッド (Python のみ):**
- `get_junction_connections(junction_id)` — 全接続を一括取得
- `get_junction_connections_from_road_with_contact(junction_id, incoming_road_id)` — 接続先と接触点を取得
- `get_all_road_ids()` — 全道路IDを一括取得
- `get_all_road_signals(road_id)` — 全信号を一括取得
- `get_connected_roads(road_id, direction)` — ジャンクション解決付き接続道路取得
