# VirtualDriver 機能軸（安全機能 / 快適機能 / 法規遵守機能 / 譲り合い機能）— 設計ドラフト

> **status: DRAFT（フェーズ0・コード非改変）** — Issue #34（カットインAEB）を単発で実装する前に、
> VD の挙動を「なぜ介入するか（動機層）」× 「どんな主体か（AD / ADAS）」の2軸で整理し、
> 以後の ADAS/AD 機能の共通の受け皿にすることを目的とする。
> レビュー用の叩き台であり、要求（`req-vd-ad`）は seed。呼称・粒度は確定前。

## 0. 動機

現状 VD の拡張点は `ITrafficPolicy`（`TrafficPolicyManager`）という**1本の軸**しか無く、
そこに追従・信号・標識・交差点・横断歩道の全挙動をぶら下げてきた。この結果:

- `LeadVehicleAware`（IDM追従）は**実質 ACC**なのに「policy」として置かれている。
- **安全介入（AEB）を表現する層が存在しない**。`TrafficPolicyManager` の合成は
  「最も厳しい `MAX_SPEED` が勝つ」、`ManeuverAwareSpeedPlanner` は減速を**すべて
  `comfort_decel`（既定2.0 m/s²）で頭打ち**にするため、快適減速を超える緊急減速という
  概念自体を表現できない（Issue #34 の根本原因）。

そこで、機能を**動機で層別**し、層ごとに調停の硬さ（override / constraint / soft）を与える。
AEB はこの軸上の「安全機能・最初の1機能」として素直に載る。

## 1. 軸1: 動機層（機能カテゴリ）

呼称は日本語表示名「◯◯機能」、機械側（KG-ID・configキー・GUIラベル）は ascii スラッグを併用する。

| 表示名 | slug | 調停の硬さ | 役割 | 例 |
| :--- | :--- | :--- | :--- | :--- |
| **安全機能** | `safety` | **override**（快適・法規の上限を破ってよい） | 衝突回避の最後の砦。TTC / 必要減速度が臨界なら emergency_decel を許可 | AEB（#34）、緊急回避 |
| **法規遵守機能** | `compliance` | **hard-constraint**（破ってはいけない下限。安全にのみ譲る） | 交通法規の遵守 | 信号、一時停止、歩行者優先、速度制限 |
| **譲り合い機能** | `courtesy` | **soft**（重み付け最適化） | 法規を超えた協調・譲歩 | 非優先交差点での譲り、合流時の車間譲り |
| **快適機能** | `comfort` | **soft**（重み付け最適化） | 走行品質（滑らかさ・車間） | ACC定常追従、曲率減速、ジャーク制限 |

**調停順序**（フラットな4段ではなく硬さで決まる）:

```
安全(override)  >  法規遵守(hard-constraint)  >  { 譲り合い, 快適 }(soft trade-off)
```

- 安全機能は他層の上限を**破れる**（唯一 override 可能な層）。
- 法規遵守は破れない下限。安全にのみ譲る（例: 赤信号で止まるが、追突回避が優先なら安全が上書き）。
- 譲り合いと快適は互いにトレードオフ（「譲るか」「滑らかさ」を重みで解く）。

## 2. 軸2: 主体（AD / ADAS）— 共存するが役割が違う

| 主体 | 定義 | VD内の位置づけ | 例 |
| :--- | :--- | :--- | :--- |
| **AD** | 連続的な計画・交渉レイヤ。経路追従＋ルール遵守＋協調を統合して「こう走りたい」を出す | 既存ポリシー群の大半（信号/標識/交差点/横断歩道/追従）はこちら＝**AD挙動** | ACC追従、信号停止、譲り合い |
| **ADAS** | 離散的なガーディアン/アシスト機能。特定トリガで介入する保険 | 安全機能に多い。AD計画の**上に override 層として乗る** | AEB、（将来）LKA/LDW |

**共存モデル**: AD層が計画を出す → その上に **ADAS 安全ガーディアン（AEB）が override として乗る**。
AD層が何を計画していようが、TTCが臨界なら安全機能（ADAS）が勝つ。
「良く出来た AD は本来 AEB を発火させない、しかし ADAS の AEB は最後の保険として別に存在する」——
これが AD と ADAS を**区別しつつ共存**させる要点。

動機層（軸1）と主体（軸2）は**直交**する。1つの要求／機能は「層 × 主体」の交点に置かれる。

## 3. 機能カタログと既存VD挙動のマッピング

> **網羅版カタログ**: `GT_esmini/docs/knowledge/function_catalog_vd_ad.yaml`（namespace `vd-func`、
> FUNC-001..048）に4系統×AD/ADASの全候補機能を列挙。以下は既存実装のダイジェスト。
> トレーサビリティ: `policy -> realizes -> vd-func(機能) -> realizes -> req-vd-ad(要求)`。

| 既存挙動（source） | 動機層 | 主体 | 備考 |
| :--- | :--- | :--- | :--- |
| `LeadVehicleAware`（IDM追従, `policy:lead`） | 快適機能 | AD | ACC相当。安全側面（追突回避）はAEBに分離 |
| **AEB（新規, Issue #34）** | **安全機能** | **ADAS** | 安全層の初号機。emergency_decel |
| `TrafficLightAware`（`policy:traffic_light`） | 法規遵守機能 | AD | |
| `StopYieldSignAware`（`policy:stop_yield`） | 法規遵守(STOP) + 譲り合い(YIELD) | AD | 1挙動が2層にまたがる例 |
| `CrosswalkPedestrianAware`（`policy:crosswalk`） | 法規遵守(歩行者優先) + 譲り合い(待ち歩行者) | AD | |
| `ConflictPointResolver` / junction priority（`policy:conflict`） | 譲り合い + 法規(優先) | AD | |
| mid/long の曲率速度・comfort_decel・comfort_jerk | 快適機能 | AD | `ManeuverAwareSpeedPlanner` |

見えてくるのは、**安全機能(override)層だけが丸ごと空白**で、そこに ADAS の AEB が最初に入る、という絵。

## 4. アーキテクチャへの含意（設計方針・実装はフェーズ1）

`ITrafficPolicy` の「最厳勝ち」を、**層タグ付き調停（arbitration by tier）**へ置き換える:

1. 各 `PolicyConstraint` が `tier`（safety / compliance / courtesy / comfort）を宣言。
2. **安全tierの制約のみ `comfort_decel` 上限を外せる**（`emergency_decel`、例 8.0 m/s²）。
   `ManeuverAwareSpeedPlanner` の STOP ランプ・後退実行可能性・ジャーク制限を、tierに応じて
   comfort / emergency の減速上限で解く。
3. 調停は「安全=override → 法規=hard下限 → 譲り合い/快適=softトレードオフ」の順。
4. 新しい制約種別の候補: `EMERGENCY_STOP` / `HARD_DECEL`（あるいは STOP_AT_S に緊急フラグ）。
5. AEB の発火判定は **TTC / 必要減速度 a_req** ベース（車間だけでなく閉じ速度を見る）。
   算出コードは `proposal:P11`（TTC/PET/必要減速度メトリクス）と共有できる。

> **設定は #33 の VD-GUI-PARITY に従う**: 追加する `emergency_decel` / TTC閾値等は
> `config/virtual_driver.json` + `VirtualDriverPanel`（GUI）にセットで露出する。

> **フェーズ1実装知見（2026-07-17・実装で判明、上記フレーミングを補正）**:
> ① §0の根本原因「快適減速を超える緊急減速という概念自体を表現できない」は **不正確** だった。
> `MAX_SPEED` 制約は既に `comfort_decel` 天井を迂回しており（`LeadVehicleAware` の IDM 経路が
> 実測 ~10.5 m/s² ＝車両物理天井まで到達）、真の欠落は **カットインの遅い検知**（同一レーン
> `dLaneId==0` のみ）だった。② よって `emergency_decel`/tier の役割は「到達不能な減速の解放」
> ではなく **(a) STOP_AT_S/減速プロファイル経路で緊急制動を正直に表現、(b) SAFETY-tier タグに
> よる調停（安全が法規を上書き）**。実装は別policy `AebSafety`（早期横侵入検知＋TTC/a_req ゲート
> ＋SAFETY-tier `STOP_AT_S` → `ManeuverAwareSpeedPlanner` が `emergency_decel` で解く）。数値ノイズ
> 誤検知を避けるため侵入キューは3フレームのデバウンス付き（REQ-AD-013 SOTIF）。③ 高速カットイン
> （Ego 108km/h・48m前で先行車が 8 m/s² 制動）は **完全回避が物理的に不能**（必要 ~13.5 m/s²≈1.38g
> ＞ Civicクラス天井 ~10 m/s²≈1.02g、乾燥路ABS実測 0.87–1.08g・13.5g級はGT2 RS/ZR1級スーパー
> カー限定、Web実測裏取り済）→ 受入は mitigation（`impact_speed_below`、07_aeb直進で閉じ速度
> 18→~9 m/s に低減）。完全回避側はカーブ変種（曲率速度制限で回避可能域）が担う。

## 5. 検証へのマッピング（トレーサビリティ）

動機層はそのまま検証手段に対応する:

| 動機層 | 検証手段 | 例 |
| :--- | :--- | :--- |
| 安全機能 | **不変条件**（衝突ゼロ, `proposal:P12`）+ 必要減速度（`proposal:P11`） | `matcher:min_obb_separation_above` |
| 法規遵守機能 | ルール matcher | `matcher:stopped_at_signal` / `stopped_at_stop_sign` |
| 快適機能 | KPI閾値 matcher | `matcher:maintained_following_distance` |
| 譲り合い機能 | 挙動期待 matcher | （交差点譲りの専用matcherは未整備・TODO） |

要求（`req-vd-ad:REQ-AD-nnn`）を**層タグ付き**で起こし、
`policy → realizes → 要求`、`matcher → verifies → 要求` で知識グラフに接続する
（`GT_esmini/docs/knowledge/requirements_vd_ad.yaml`、seed起票済み）。

## 6. ロードマップ

- **フェーズ0（本ドラフト）**: 2軸フレーム定義 / `req-vd-ad` activate / seed要求 / トレーサビリティ辺。**コード非改変**。
- **フェーズ1**: 層タグ付き調停レイヤ + `emergency_decel` を実装し、**AEB（#34）を安全機能の初号機**として載せる。
  `acc-test.xosc`（+カットイン派生）で collision-free 回帰固化（P11/P12連携）。
- **フェーズ2以降**: 同じ型で LKA/LDW 等（安全/快適の横方向機能）を追加。

## 7. 未決事項（レビューで詰める）

- 呼称の最終確定（日本語「◯◯機能」表示 × ascii slug の対応）。本ドラフトは
  安全機能=safety / 快適機能=comfort / 法規遵守機能=compliance / 譲り合い機能=courtesy で暫定。
- 「譲り合い」と「快適」のsoftトレードオフの重み付けモデル（効用関数 or ルール）。
- 動機層を KG の**名前空間に昇格**するか、要求の `tier` タグに留めるか（現状はタグ）。
- 要求→OpenX概念（`concerns`）の割当（ODD/ISO 34503軸との整合、`concept_vocabulary.yaml` 追補が前提）。
