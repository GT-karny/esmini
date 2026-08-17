# 意味論のみで分類するSTOP標識での完全停止

stop_sign_full_stopの双子シナリオ。
道路上のSTOP標識はOpenDRIVEの@type=9001で、カタログ(de)に存在しない値のためOSI分類上は未分類(UNCLASSIFIED)になる。
`<semantics><priority type="stopLine"/></semantics>`をL2フォールバックとして読み取って初めてSTOPと判定できるかどうかを検証する。

## 検証の狙い

カタログベースの分類(OpenDRIVE @type → OSI type)だけでは、この標識をSTOPと認識できない。
StopYieldSignAwareのP4 L2意味論フォールバック(カタログ未分類の標識について`<priority type="stopLine"/>`を読み、STOPとして扱う)が機能しているかどうかを、カタログ分類で素直にSTOPと判定できる正例のstop_sign_full_stopと対比して切り分ける。
L2フォールバック導入前(カタログ判定のみ)は自車が標識を素通りし、`stopped_at_stop_sign`が不成立(FAIL)になる。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_semantic_stop_sign.xodr`(500 m直線2車線。標識 id=10, type=9001(deカタログ未分類), s=200, t=-3.57, lane -1のみ有効, `<semantics><priority type="stopLine"/></semantics>`付与, @value属性なし) |
| 自車 | road 1 / lane -1 / s=30 にテレポート。Cruiseイベントで目標速度13.889 m/sへ3.0 s(linear)で加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間 40 s 超過 |

## 進行

1. sim_time > 0でCruiseイベントが起動し、目標速度13.889 m/sへ3.0 s(linear)で加速する。
2. カタログ上は未分類の標識(id=10, s=200)に対し、StopYieldSignAwareがL2意味論フォールバック(`priority type="stopLine"`)によりSTOPとして扱う。
3. road 1 の s=190〜201の範囲で1.0 s以上、速度0.3 m/s以下を維持して完全停止する。
4. sim_time > 22.0 sの時点で速度3.0 m/sを超えて再加速している。
5. ルートは s=480 まで続き、sim_time > 40 sでStopTriggerが成立してシナリオが終了する。

## 期待する挙動

- カタログ未分類の標識でも、意味論(`priority type="stopLine"`)由来でstop_sign_full_stopと同じ停止線位置(road 1, s=190〜201)に1.0 s以上、速度0.3 m/s以下の完全停止をする。
- 完全停止後、sim_time > 22.0 sでは速度3.0 m/sを超えて再加速している。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| stopped_at_stop_sign | sign_id=10, road_id=1, s_range=[190, 201], min_duration=1.0, stop_speed=0.3 | semantics(priority=stopLine)由来のSTOP停止線で完全停止 |
| speed_above | threshold=3.0, after sim_time=22.0 | 安全確認後に発進(再加速) |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`(policies: [stop_yield])
- 期待値: `semantic_stop_sign_full_stop.expectations.yaml`
