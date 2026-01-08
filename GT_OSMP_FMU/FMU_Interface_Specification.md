# GT_OSMP_FMU (esmini) インターフェース仕様書

本ドキュメントは、esmini Co-Simulation FMU (`GT_OSMP_FMU`) の固定パラメータ (Fixed Parameters)、入力 (Inputs)、出力 (Outputs) をまとめたものです。

## 1. esmini FMU (`esmini`)
コ・シミュレーション用に、esmini シナリオプレーヤー (OSMP サポート付き) をカプセル化します。OSI TrafficUpdates (外部車両) を入力とし、OSI SensorViews (GroundTruth およびセンサーデータ) を出力します。

### パラメータ (Fixed)
| 変数名 | 型 | 単位 | 説明 |
| :--- | :--- | :--- | :--- |
| `xosc_path` | String | - | 読み込む OpenSCENARIO (.xosc) ファイルへのパス。 |
| `esmini_args` | String | - | esmini に渡す追加のコマンドライン引数 (例: `--window 800 600`, `--headless`)。 |
| `use_viewer` | Boolean | - | 可視化ウィンドウの有効化/無効化 (デフォルト: false)。 |

### 入力 (Inputs)
| 変数名 | 型 | 単位 | 説明 |
| :--- | :--- | :--- | :--- |
| `OSMPTrafficUpdateIn` | OSMP (Binary) | - | OSI `TrafficUpdate` メッセージ。esmini シミュレーション内に外部車両の状態を注入するために使用されます。 |
| `OSMPTrafficCommandUpdateIn` | OSMP (Binary) | - | OSI `TrafficCommand` メッセージ。(modelDescription で定義されていますが、現在の実装では未使用です)。 |

### 出力 (Outputs)
| 変数名 | 型 | 単位 | 説明 |
| :--- | :--- | :--- | :--- |
| `OSMPSensorViewOut` | OSMP (Binary) | - | OSI `SensorView` メッセージ。シミュレーションからの GroundTruth とセンサーデータを含みます。 |
| `OSMPTrafficCommandOut` | OSMP (Binary) | - | OSI `TrafficCommand` メッセージ。シナリオによって生成された交通コマンドを含みます。 |
| `valid` | Boolean | - | 有効性フラグ。シミュレーションステップが成功した場合に true (1) に設定されます。 |

---

## 技術詳細

### OSMP (Open Simulation Model Packaging)
バイナリ入出力は **OSMP** 標準に準拠しており、データのシリアライズには **Protocol Buffers** (protobuf) を使用します。
- **入力** は、3つの整数変数 (`base.lo`, `base.hi` (メモリアドレス), `size`) を介して提供されます。
- **出力** も同様に提供され、FMU はシリアライズされた protobuf データをバッファに書き込み、そのアドレスとサイズを公開します。

### 実装ノート
- **OSI バージョン**: ビルド構成で定義された OSI バージョン (通常は OSI 3.x) と互換性があります。
- **座標系**: OSI 参照フレームの規約に従います。
- **交通注入**: `OSMPTrafficUpdateIn` により、外部シミュレーター (Chrono::Vehicle ラッパーなど) が esmini ワールド内の車両を駆動できるようになります。
- **交通コマンド**: `OSMPTrafficCommandOut` により、esmini は (例: `TrafficCommandAction` からの) 高レベルコマンドを外部コントローラーに送信できます。
