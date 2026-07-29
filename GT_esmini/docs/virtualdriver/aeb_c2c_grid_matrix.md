# AEB Car-to-Car グリッド採点結果

## バンド判定規則（固定・変更時はこの表を直す）

- **avoid**: 走行中一度も接触なし（OBB分離が常に > 0）。
  `avoid(aeb)` = AEB発火あり（gt.aeb.triggered=true または policy.constraints に
  source=="aeb"）／ `avoid(no_aeb)` = AEB非発火で無接触。**快適回避とは限らない**
  （AD層のIDM経路は快適天井を迂回して強制動できる）— 実際の制動強度は
  生値テーブルの max_ego_decel を見ること。
- **mitigate**: 接触したが、下記のいずれかを満たす。
  - 速度低減 >= 5.56 m/s (20 km/h) （公称閉じ速度 - 衝突速度）
  - 衝突速度 <= 公称閉じ速度の 50%
- **fail**: 接触し、上記のいずれも満たさない。
- **crash**: 採点不能（telemetry欠落／フレーム数0／batch_verdictのerrorステータス）。
  挙動 fail とは厳密に区別する（混ぜると行列が汚染される）。単独再実行で要確認。

## CCRs / CCRm マトリクス（行=ファミリ、列=ego速度 km/h）

| family | 10 | 20 | 30 | 40 | 50 | 60 | 70 |
| :-- | :-- | :-- | :-- | :-- | :-- | :-- | :-- |
| ccrs | avoid(no_aeb) | avoid(no_aeb) | avoid(no_aeb) | avoid(no_aeb) | avoid(no_aeb) | avoid(no_aeb) | avoid(no_aeb) |
| ccrm_lead20 | — | — | avoid(no_aeb) | avoid(no_aeb) | avoid(no_aeb) | avoid(no_aeb) | avoid(no_aeb) |

## CCRb マトリクス

| hw \ d | 2.0 | 6.0 |
| :-- | :-- | :-- |
| 12.0 | avoid(no_aeb) | avoid(no_aeb) |
| 40.0 | avoid(no_aeb) | avoid(no_aeb) |

## 生値テーブル（全セル）

| cell | band | min_sep_m | impact_v_mps | min_ttc_s | triggered | max_ego_decel_mps2 | speed_reduction_mps | note |
| :-- | :-- | :-- | :-- | :-- | :-- | :-- | :-- | :-- |
| ccrb_hw12_d2 | avoid | 3.68m | - | 2.83s | no | 8.60m/s2 | - |  |
| ccrb_hw12_d6 | avoid | 3.67m | - | 2.88s | no | 8.60m/s2 | - |  |
| ccrb_hw40_d2 | avoid | 5.81m | - | 3.04s | no | 2.22m/s2 | - |  |
| ccrb_hw40_d6 | avoid | 8.24m | - | 3.02s | no | 4.54m/s2 | - |  |
| ccrm_ego30_lead20 | avoid | 10.61m | - | 7.61s | no | 0.82m/s2 | - |  |
| ccrm_ego40_lead20 | avoid | 11.43m | - | 6.62s | no | 0.82m/s2 | - |  |
| ccrm_ego50_lead20 | avoid | 12.31m | - | 5.95s | no | 1.84m/s2 | - |  |
| ccrm_ego60_lead20 | avoid | 13.18m | - | 5.95s | no | 2.78m/s2 | - |  |
| ccrm_ego70_lead20 | avoid | 14.02m | - | 5.95s | no | 3.90m/s2 | - |  |
| ccrs_ego10 | avoid | 2.96m | - | 2.79s | no | 3.92m/s2 | - |  |
| ccrs_ego20 | avoid | 2.97m | - | 2.78s | no | 3.90m/s2 | - |  |
| ccrs_ego30 | avoid | 2.99m | - | 2.88s | no | 3.92m/s2 | - |  |
| ccrs_ego40 | avoid | 2.97m | - | 2.92s | no | 3.86m/s2 | - |  |
| ccrs_ego50 | avoid | 3.20m | - | 2.94s | no | 1.72m/s2 | - |  |
| ccrs_ego60 | avoid | 3.51m | - | 2.94s | no | 1.72m/s2 | - |  |
| ccrs_ego70 | avoid | 3.78m | - | 2.94s | no | 2.86m/s2 | - |  |

## crash セル一覧（単独再実行で要確認）

_crash セルなし_
