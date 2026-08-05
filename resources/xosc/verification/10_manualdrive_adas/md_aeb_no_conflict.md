# 通常追従でのAEB誤介入抑止(手動運転中ADAS、負例)

07_aeb/normal_following.xosc のSOTIFミラー幾何(カットインも急制動も無い通常同一レーン追従)を流用し、
自車のコントローラを ManualDriveController に差し替え、定常アクセルの合成ドライバ("steady_throttle"プロファイル)を対にした負例。

## 検証の狙い

負例(REQ-AD-025 段b、slug `md-aeb-no-false-intervention`)。
AEBはENABLEDのまま(manualdrive_config: adas_aeb_enabled=true)だが、衝突コースが存在しないため、
人間の合成ペダル入力に一切介入してはならない。VD版REQ-AD-013の手動運転版。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_2lane.xodr`(直線500m、2車線) |
| 自車 | road1 lane-1、s=30、Init速度25.0 m/s(90 km/h)、コントローラ=ManualDriveController |
| 他エンティティ | 先行車、road1 lane-1(同一レーン)、s=100(バンパー間ギャップ約65 m)、速度23.0 m/s(82.8 km/h) |
| 入力プロファイル | `profiles/steady_throttle.json` — throttle=0.30を全区間保持、ブレーキ・操舵は常に0 |
| 走行時間 | StopTrigger: シミュレーション時間 13 s |

## 07_aeb版からの意図的な差分: バンパー間ギャップの拡大

VD版normal_followingはLeadStartS=85(ギャップ約50m)。本シナリオはLeadStartS=100(ギャップ約65m)へ拡大した。
理由: VirtualDriverControllerはStory上のSpeedActionで自車速度を直接固定できるが、ManualDriveControllerは
"steady_throttle"プロファイルがRealVehicleBackendの物理を介して生む速度に依存する。throttle=0.30という値は
real_vehicle_params.jsonのトルク/変速モデルに対して校正されていない**推測**であり(profiles/steady_throttle.json参照)、
実測でEgoが25.0 m/sから乖離する可能性がある。VD版のTTCマージン(実測TTC 12.0s vs ゲート閾値2.5s、4.8倍)と同じ考え方で、
ギャップを広げることでこの不確実性を吸収する設計とした。

## 進行

1. t=0: 自車はInit SpeedAction(step)で25.0 m/sに初期化。先行車も同一レーンで23.0 m/sの定速で前方に位置。
2. t>0: 運転者(合成)は0.30の定常スロットルを保持し続ける。カットインも急制動も一切発生しない。
3. AEBは候補を検知しても(同一レーンのordinary follow)、ゲート(TTC/a_req閾値)を一度も超えないはずである。
4. t>13s: StopTriggerでシミュレーション終了。

## 期待する挙動

- 窓内でAEBのgt.aeb出力が一切ACTIVEにならず、実効ペダル値が入力プロファイル(steady_throttle)と一致する(`no_intervention_in_window`)。
- AEBのHVD状態がSTANDBY(監視中・未発火)であり、UNAVAILABLE(そもそもOFF)ではないこと(`adas_state_matches`)。これにより
  「切ってあったから発火しなかった」ではなく「見張っていて撃たなかった」ことを示す(検証計画§4-2)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| no_intervention_in_window | function gt.aeb, after 0.5s, before 13.0s, min_frames 1 | AEBが窓内で一切介入しないこと |
| adas_state_matches | function gt.aeb, expect standby, mode all, after 0.5s, before 13.0s | AEBがARM状態のまま発火しなかったこと(負例の意味を担保) |

## 数値の出典・要校正の明示

- `steady_throttle` プロファイルのthrottle=0.30は**推測**。物理パラメータへの校正は未実施(profiles/steady_throttle.json参照)。
- LeadStartS=100(VD版85からの拡大)は上記の不確実性を吸収するための設計判断であり、実測ではない。
- 2つのmatcherとも数値閾値を持たない(構造/状態判定)ため、校正が必要な数値はプロファイル側にのみ存在する。

## この xosc はバッチで**2回**走る(2026-08-05 フェーズBで追加)

同じファイルが `manualdrive_adas_batch.yaml` に2行ある。差分は `adas_aeb_enabled` だけ:

| 行 | adas_aeb_enabled | 期待値ファイル | 主張 |
| :-- | :-- | :-- | :-- |
| （variant なし） | true | `md_aeb_no_conflict.expectations.yaml` | AEBは**ARMされていて撃たなかった**（STANDBY） |
| `variant: adas_off` | false | `md_aeb_no_conflict_adas_off.expectations.yaml` | AEBは**切ってあった**（UNAVAILABLE） |

これは REQ-AD-028 段a の 3値規律(slug `md-state-three-value-discipline`)で、
**1本の実行では原理的に示せない**主張である——どちらの構成でも「AEBが撃たなかった」という
同じ振る舞いになるので、区別は同一刺激・2構成で観測量だけが変わることでしか示せない。

**この xosc を編集するときは両方の行に効くことに注意**。逆に、一方の期待値ファイルだけを
消すと主張そのものが消える(片側だけでは「常にSTANDBYを返す壊れた計器」「常にUNAVAILABLEを
返す壊れた計器」のどちらも通ってしまう)。幾何を複製して2ファイルにしないのは、
「絶対に食い違ってはいけない2ファイル」をレビュー対象に増やすほうが悪い失敗様式だから。

## 関連

- バッチ: `manualdrive_adas_batch.yaml`
- 期待値: `md_aeb_no_conflict.expectations.yaml` / `md_aeb_no_conflict_adas_off.expectations.yaml`
- 入力プロファイル: `profiles/steady_throttle.json`
- 関連ID: `req-vd-ad:REQ-AD-025` 段b / `req-vd-ad:REQ-AD-028` 段a / `vd-func:FUNC-075`
- 参考(幾何の出典): `resources/xosc/verification/07_aeb/normal_following.xosc`
