# invalidated指定されたSTOP標識の無視

semantic_stop_sign_full_stopの双子シナリオで、かつその逆(負例)にあたる。
道路上の標識はsemantic_stop_sign_full_stopと同じtype=9001と`priority type="stopLine"`を持つが、`invalidated="true"`(OpenDRIVE 1.9)が付与されている。
invalidatedフィルタ(D4/D5)がOSI論理グラウンドトゥルースとVirtualDriverのルート標識スキャンの両方からこの標識を除外するため、自車は停止しない。

## 検証の狙い

semantic_stop_sign_full_stopが「意味論フォールバックがSTOPとして正しく拾う」正例であるのに対し、本シナリオは「invalidated指定された標識はフィルタで除外され、STOPとして拾ってはいけない」負例にあたる。
両者を対にすることで、invalidatedフィルタが意味論フォールバックより手前で正しく効いているかを切り分ける。
フィルタが退行し無効化された標識を依然として拾ってしまうと、自車は停止線で完全停止してしまい`min_speed_above`が不成立(FAIL)になる。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_invalidated_stop_sign.xodr`(500 m直線2車線。標識 id=10, type=9001, s=200, t=-3.57, lane -1のみ有効, `<semantics><priority type="stopLine"/></semantics>`付き, `invalidated="true"`) |
| 自車 | road 1 / lane -1 / s=30 にテレポート。Cruiseイベントで目標速度13.889 m/sへ3.0 s(linear)で加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間 40 s 超過 |

## 進行

1. sim_time > 0でCruiseイベントが起動し、目標速度13.889 m/sへ3.0 s(linear)で加速する。
2. invalidatedフィルタ(D4/D5)により標識(id=10, s=200)がOSI論理グラウンドトゥルースとルート標識スキャンの双方から除外され、StopYieldSignAwareはSTOPと判定しない。
3. 自車は減速せず、標識付近(s=200)を巡航速度のまま通過する。
4. s=480までルートを走行し続ける。
5. sim_time > 40 sでStopTriggerが成立してシナリオが終了する。

## 期待する挙動

- 標識区間(road 1)でsim_time > 5.0 s以降、速度が8.0 m/s以上を維持する(停止線で減速・停止しない)。
- sim_time > 18.0 s以降(標識通過後)も速度12.0 m/s以上を維持し、巡航を継続する。
- フィルタが退行した場合、自車は停止線で完全停止し`min_speed_above`が不成立になる(この不一致が判別点)。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| min_speed_above | threshold=8.0, road_id=1, after sim_time=5.0 | invalidated標識は無視 → 停止線で減速・停止しない(巡航速度を維持) |
| speed_above | threshold=12.0, after sim_time=18.0 | 標識通過後(s>200)も等速巡航を継続していること |

## 関連

- バッチ: `car_following_traffic_control_batch.yaml`(policies: [stop_yield])
- 期待値: `invalidated_stop_sign_ignored.expectations.yaml`
