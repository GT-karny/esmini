# 設計メモ: VD テレメトリの OSI 拡張フィールド出力(F5 ストレッチ・調査/設計のみ)

> ステータス: **調査・設計のみ。実装しない。** F5 の他項目(パス解決修正 / front-bumper
> テレメトリ / HVD velocity 移行 / dedup / ポート定数)は実装済み。本メモは監査 doc §5 F5
> 「OSI 拡張フィールドへのテレメトリ」の着手前判断材料。

## 1. 動機

現状 VD の内部診断(short/midlong プランナ、ドライバモデル逆制御量、traffic policy
constraints、front-bumper road 座標)は **独自 JSON**(`VirtualDriverTelemetryJson.cpp`
`ToJson()`)としてのみ出力され、C-API pull(`GT_GetVirtualDriverTelemetry`)と live UDP で
Web オーバレイが消費している。一方 GT は OSI を 2 系統(GroundTruth over UDP、HostVehicleData
over UDP)で標準配信しており、**OSI だけを読む外部 ADAS/ツール**からは VD の内部判断が一切
見えない。VD の「なぜ減速したか(policy constraint)」「front がどの road/lane に居るか」を
OSI ストリームに載せられれば、既存 OSI コンシューマが追加の GT 独自チャネル無しに VD を観測
できる。

## 2. OSI 側の受け皿候補と評価

OSI は proto3 で **proto2 拡張(extend)を持たない**。したがって「拡張フィールド」は以下の
いずれかの既存メッセージ面に載せることになる。

| 候補 | 概要 | 評価 |
| :--- | :--- | :--- |
| **A. `HostVehicleData.vehicle_automated_driving_function`** | ADAS 機能状態の repeated。GT は既に `GT_HostVehicleReporter` で HVD を配信済み。 | ◎ 意味的に最も近い(VD = 自動運転機能)。ただし schema は name(enum)+state(enum)+custom_name のみで、**数値(s/速度/constraint 値)を素直に運べない**。 |
| **B. `MovingObject.model_reference` / `source_reference`** | 文字列参照フィールド。 | △ 文字列に JSON を詰めれば運べるが「参照」の意味を逸脱。相互運用性が低い。 |
| **C. proto3 unknown fields(タグ番号直書き)** | 未定義タグにペイロードを書く。 | ✕ OSI バージョン更新でタグ衝突リスク。パーサ依存で消える。不採用。 |
| **D. `HostVehicleData` に GT ローカル proto 拡張メッセージを追加** | GT フォークで osi_hostvehicledata.proto に新 optional message を足す。 | ✕ **R1 Clean Core / externals READ-ONLY 違反**。externals/osi は upstream 生成物。GT では採らない。 |
| **E. サイドカー `SensorData` / `TrafficCommandUpdate`** | 別 OSI メッセージを別ポートで配信。 | △ 「OSI 拡張フィールド」ではなく別チャネル。実質は現行 GT 独自 JSON と同じ立ち位置で、OSI である利点が薄い。 |

## 3. 推奨設計(実装する場合)

**候補 A を主、E を補**とする二段構え:

1. **離散状態は A に載せる**: VD が有効な間、`HostVehicleData.vehicle_automated_driving_function`
   に 1 エントリを追加し、
   - `name = NAME_OTHER`、`custom_name = "GT_VirtualDriver"`
   - `state` に「介入中/自動/オーバライド」を enum マップ(既存 override_lateral/longitudinal を反映)
   これは **HVD を既に読んでいる外部 ADAS がゼロコストで VD の作動有無を検知**できる最小価値。
   `GT_HostVehicleReporter` の adas_func 経路(既存)を流用でき、追加コストが小さい。

2. **数値診断(constraint の s/速度、front-bumper road 座標、midlong プロファイル)は
   OSI に載せない**。理由:
   - A の schema が数値を運べず、B/C/D はいずれも規約違反 or 脆弱。
   - これらは既に安定した GT 独自 JSON 契約(3 言語同期・フロント/CLI 消費)がある。**同じ
     数値を 2 経路で二重管理すると F5 の dedup と真逆の負債**になる。
   - 数値が本当に OSI 経路で要るなら、候補 E(別 proto メッセージのサイドカー配信)を
     **独立機能として別途起案**する(本メモの範囲外)。

## 4. 実装しない判断の根拠(現時点)

- **費用対効果**: 離散状態(A)の価値は「VD 作動フラグ」に留まり、既存 Web オーバレイは既に
  リッチな JSON を持つ。外部 OSI-only コンシューマの具体要求がまだ無い(pull型需要が未確認)。
- **リスク**: 数値を OSI に載せる筋の良い受け皿が無く、無理に載せると Clean Core 違反 or
  二重管理。front-bumper/HVD velocity の今回の正攻法修正と整合しない。
- **前提未確定**: 「どの外部ツールが OSI 経由で VD を読むか」のユースケースが固まってから
  schema を決めるべき(A の state enum マッピングは需要駆動で決める)。

→ **結論: 現時点は実装保留**。着手するなら候補 A(HVD adas_func への作動状態 1 エントリ)
のみを最小スコープで先行実装し、数値診断は独立起案。実装前に「OSI-only コンシューマの
実在ユースケース」を 1 件以上確認することを着手条件とする。

## 5. 参考

- `GT_esmini/src/osi/GT_HostVehicleReporter.cpp`(adas_func 経路 = 候補 A の足場、HVD velocity は F5 で vehicle_localization/vehicle_motion へ移行済み)
- `GT_esmini/src/control/virtualdriver/VirtualDriverTelemetryJson.cpp`(現行 JSON 契約 = 数値診断の単一ソース)
- `GT_esmini/include/gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp`(テレメトリ構造体)
- `externals/osi/v11/include/osi_hostvehicledata.pb.h`(受け皿 schema。READ-ONLY)
