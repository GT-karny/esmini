# AEB 機能要求 — NCAP/R152 逆算スキーマ ＋ 要求ファミリ（draft）

> **status: DRAFT（機能軸フェーズ0の続き）** — `vd-func:FUNC-001`(前方AEB)/`FUNC-002`(VRU-AEB) を
> テスト可能な要求（`req-vd-ad`）に落とす。設計軸は [adas_axis.md](adas_axis.md)。
> 要求文は自作せず、**Euro NCAP / UN R152 の試験プロトコルを翻案**する（シーンで OpenX 統制語彙を
> 使ったのと同じ発想）。数値は指標であり **要校正（GT_esmini側の合否閾値は別途キャリブレーション）**。

## 1. なぜ標準から逆算するか

AEB は世界で最も標準化された ADAS 機能。試験プロトコルが**定量化されたシナリオ行列**を与えるので、
要求は「発明」ではなく「セル→要求の対応づけ」でよい。要求スキーマの項目も、NCAP セルが実際に
規定している項目（target / 速度域 / 減速 / オーバーラップ / 評価基準）から**逆算**して決める。

## 2. NCAP / R152 シナリオ調査（一次ソース、要点）

出典は §6。未確認値は「(未確認)」を付す。GT_esmini で ground-truth 知覚のまま再現可能かも併記。

### 2.1 Euro NCAP AEB Car-to-Car（v4.3.1, 2024-02）

| code | 概要 | target | ego速度 | target速度/減速 | 評価 | GT再現 |
| :-- | :-- | :-- | :-- | :-- | :-- | :-- |
| CCRs | 停止先行車 | 停止 | AEB 10–50 km/h | 停止 | 相対衝突速度カラーバンド | ◎ |
| CCRm | 等速先行車 | 等速 | 30–80 km/h | 等速(格子) | 同上 | ◎ |
| **CCRb** | **制動先行車** | 制動 | 固定 50 km/h | 50→ **−2 / −6 m/s²**、車間 **12 / 40 m** | 同上 | ◎ |
| CCFtap | 交差点右左折で対向直進車を横断 | 対向 | 10/15/20 km/h | 30/45/60 km/h | 9通りの回避率 | ○(要交差点) |
| CCCscp | 交差点直進で横断車 | 横断 | 20–60 km/h | 40/50/60 km/h | 格子点合否 | ○ |
| CCFhos/hol | 対向はみ出し/追い越し正面 | 対向逸脱 | 50 / 70 km/h | 同速 | ドシエ+実車 | ○ |

**カラーバンド評価**（相対衝突速度 Vrel_impact、50km/h例）: 緑 0–5 / 黄 5–15 / 橙 15–30 / 茶 30–40 / 赤 ≥40 km/h。
＝「完全回避」だけでなく**衝突速度の低減量**を段階評価する（要求の受入基準に直結）。

### 2.2 Euro NCAP AEB VRU（v4.4, 2023-06）

歩行者: CPFA(遠側成人)/CPNA-25,75(近側成人)/CPNCO(近側子供・遮蔽)/CPLA(縦方向)/CPTA(右左折横断)/CPRA(後退)。
自転車: CBFA/CBNA(O)/CBLA/CBTA/CBDA(ドア開)。ego 10–60 km/h、横断速度 歩行 5km/h・自転車 15/20km/h。
評価: ≤40km/h は速度低減率で線形、>40km/h は **速度低減 ≥20 km/h で合否**。歩行者は **3 km/h 低速検知**要件も。
GT再現: 横断系は ◎（歩行者/自転車エンティティ+速度プロファイル）。遮蔽は静止遮蔽物を置くだけ。

### 2.3 UN R152（AEBS, 規制フロア）

| 項目 | 値 |
| :-- | :-- |
| 対象 | M1 / N1 |
| 作動速度域 | Car-to-Car **10–60 km/h** / Car-to-Ped **20–60 km/h**（最低） |
| 警報 | 緊急制動開始の **≥0.8 s 前**（時間不足なら検知時即） |
| 緊急制動 | サービスブレーキに **≥5.0 m/s²** 要求 |
| 目標(移動) | 車両 20 km/h、歩行者 5 km/h ⊥ 横断(未確認: 段落番号) |
| 性能 | 閉じ速度に応じ許容相対衝突速度が上がる表（低速=衝突ゼロ）(未確認: 数値表) |
| 誤作動 | 誤警報/誤制動の最小化を標準シナリオ群(Annex3 App2)で実証 |
| 失敗率 | カテゴリ毎 **≤10%**、失敗は原因分析必須 |

### 2.4 「カットイン→急制動」の位置づけ（本命）

**単一の NCAP/R152 コードは無い＝複合シナリオ**。
- 「制動」半分 = **CCRb**（先行車が −2/−6 m/s²、車間 12/40 m）。
- 「割り込み」半分 = **Cut-in**（Euro NCAP Assisted Driving protocol の ACC/Safety-Backup。3.5m 横移動、TTC 同期、
  VUT 50–130 km/h、target 10–70 km/h。基本AEB評価ではなく L2 支援運転評価側）。
- ⇒ `acc-test.xosc` は **Cut-in 幾何 + CCRb 減速** の合成。両プロトコルを出典に**独自複合要求**として定義するのが妥当。

## 3. 逆算した要求スキーマ

NCAP セルが規定している項目を、テスト可能な要求の項目に対応させる:

| スキーマ項目 | NCAP由来 | 意味 |
| :-- | :-- | :-- |
| `kind` | — | positive(介入必須) / negative(誤作動抑止) / arbitration(調停) / regulatory(規制フロア) |
| `statement` | — | **EARS**構文。トリガ時=WHEN / 状態時=WHILE / 異常時=IF + 「〜すること(shall)」 |
| `scenario` | 概要・幾何 | 前提シーン（target種別・接近形態） |
| `trigger` | — | 作動条件（TTC/必要減速度が閾値超過 等） |
| `parameters` | ego速度域・target速度/減速・overlap | 試験行列（回すパラメータ） |
| `response` | 緊急制動 ≥5m/s²・警報≥0.8s | 要求される応答（定量、emergency_decel 等） |
| `acceptance` | カラーバンド・速度低減量 | 合否（低速=完全回避 / 高速=衝突速度低減 ≥Δv）＋測定量 |
| `verification` | — | matcher（既存/要新規）＋シナリオ資産 |
| `source` | プロトコル名/版 | 出典標準（翻案元。OpenX相当の統制） |

**両面原則（SOTIF）**: positive（ぶつかりそうなら止まる）と negative（ぶつからない時に誤って急制動しない）を
**対で**置く。R152 も誤作動最小化を明文で要求。

## 4. AEB 要求ファミリ（req-vd-ad へ起票）

`REQ-AD-001`(複合・本命) を核に、NCAP軸で展開。全文と定量値は `requirements_vd_ad.yaml` に格納。

| id | kind | 要求(要約) | 出典 | 検証matcher |
| :-- | :-- | :-- | :-- | :-- |
| REQ-AD-001 | positive | **カットイン+急制動で追突しない**（複合・acc-test.xosc） | Cut-in + CCRb | min_obb_separation_above（既存）＋衝突速度低減(要新規) |
| REQ-AD-010 | positive | 停止先行車に追突しない | CCRs | min_obb_separation_above |
| REQ-AD-011 | positive | 等速/制動先行車に追突しない | CCRm/CCRb | min_obb_separation_above＋衝突速度低減(要新規) |
| REQ-AD-012 | positive | 横断歩行者に衝突しない | CPNA/CPFA | min_obb_separation_above |
| REQ-AD-013 | negative | 衝突コース不在時に緊急制動を出さない | R152 誤作動 | 誤作動ゼロ(要新規) |
| REQ-AD-014 | arbitration | 快適減速で回避可能なら emergency_decel を使わない | — | deceleration_profile_smooth＋発火監視 |
| REQ-AD-015 | regulatory | AEB作動包絡線と応答フロア（10–60km/h・警報≥0.8s・≥5m/s²） | R152 | 作動域/警報タイミング(要新規) |

## 5. 必要な新規 matcher（verification ギャップ）

既存 matcher は `min_obb_separation_above`(衝突ゼロ) / `deceleration_profile_smooth`(快適) 程度。以下は未整備で、
`proposal:P11`(必要減速度/TTCメトリクス)・`P12`(衝突検出) に依存（graph.yaml で depends-on 済み）:

- **impact_speed_reduction** — 回避不能時の衝突速度低減量（カラーバンド評価用）
- **ttc_min_above** / 必要減速度 a_req — 緊急介入トリガの妥当性
- **no_emergency_without_conflict** — 誤作動ゼロ（negative要求用）
- **aeb_activation_envelope** — 作動速度域・警報リード時間の適合

## 6. 出典（版と caveat）

- Euro NCAP AEB Car-to-Car Test Protocol **v4.3.1 (2024-02)** — 全文確認
- Euro NCAP AEB/LSS VRU Test Protocol **v4.4 (2023-06)**（v4.5.1 あり・未差分）
- Euro NCAP Assessment Protocol – Safety Assist **v9.1 (2021-11)** / VRU **v10.0.3 (2020-06)**（新版あり・配点は変動しうる）
- Euro NCAP Assisted Driving Test & Assessment Protocol **v1.1**（cut-in/cut-out・新版v2.1あり未差分）
- **UN Regulation No.152 Rev.2 (2024-04)** — UNECE直リンクは環境から取得不可、EU Reg 2020/1597 経由で内容確認。
  数値表・一部段落番号・自転車目標追加時期は **(未確認)**。

> **caveat**: 上記の速度/減速/閾値は**要求文の骨格**として使い、GT_esmini の合否閾値は
> `acc-test.xosc` 実測でキャリブレーションしてから確定する。NCAP の配点は評価用であり、
> ここでは「どのシナリオ・どの応答・どの合否量」を要求として借りるだけ。
