# カーブオンセット誤オーバーライド再現（30度・実機バグ報告スケール）

feature:F7のカーブオンセット再現アセット群のひとつ。走行中に道路曲率だけでAD操舵デマンドが急変する状況を作るための素材で、このファイルはユーザーの実機バグ報告と同じスケール（ホイール回転換算30度）の曲率デマンドを課すメンバーにあたる。

## 検証の狙い

f7_curve_onset.xodrの5本の道路(id=0..4)はいずれも独立した行き止まり道路で、自車を各道路上のs=50へ直接テレポートすることで、テレポート直後(t=0)のAD操舵デマンドを道路曲率だけで決め打ちする。
xosc内コメントによれば、road id=3は30 m直線+150 mの定曲率右カーブ(R=73.730 m)で、自車はs=50（円弧開始s=30から20 m地点）にテレポートされるため、t=0の時点で道路自体がステアリングホイール換算で約30度の回転(axis_frac約0.06667、タイヤ角約2.330度)を要求する。
このaxis_fracは、追跡ギャップを勘案するとresidual_threshold=0.08の近傍にすでに達している、とコメントされている。

このファイルは`--mode sweep`（8シナリオ×3 plant variantの曲率スイープ）に加えて、`--mode 2x2`（既定のプライマリ診断、6セル構成）でも使われる唯一の曲率メンバーである。2x2では`curve_neutral`（ホイール中立a0=0）と`curve_nonneutral`（非中立a0=-0.13667、判定には使わない）の2セルの元になる。`curve_neutral`セルの結果は、ユーザー実機報告の症状がカーブそのもの(shadow/残差経路)から来るのか、それとも既に非中立だったホイール(direct-axis経路、Sec.5-5と同じ機序)から来るのかを切り分ける、スクリプトの主目的にあたる。ただしこの判定は、モジュールが定める3つのacceptance conditionすべてがPASSした場合にのみ信頼される設計になっている。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `f7_curve_onset.xodr`（road id=3: 30 m直線+150 m定曲率右カーブ、R=73.730 m） |
| 自車 | road3 lane-1 s=50へ直接テレポート（円弧区間20 m地点）。Init に速度指定なし。Story Eventで目標速度13.889 m/sへ3.0 s linearで加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間25 s超過 |

## 進行

1. t=0: 自車がroad3 lane-1 s=50に直接テレポートされる（円弧区間の途中）。同時にStory Event「Cruise」が開始し、目標速度13.889 m/sへ3.0 sかけてlinearに加速。
2. route上をroad3 s=50からs=170まで(shortest)走行する。
3. t=25 s到達でStopTrigger成立、シナリオ終了。

## 期待する挙動

このxosc単体にexpectations.yamlによる合否判定は無い。`f7_curve_onset_probe.py`が読み取る観測値は次の通り。

- テレポート直後、道路曲率によりAD操舵目標(`ffb.target_norm`)がホイール換算約30度相当(axis_frac約0.06667)へ跳ね上がるはずである(xosc/xodr内コメントの構築上の値)。
- `--mode sweep`実行時は、analyze()関数がオンセットウィンドウ(既定1.0 s)内のshadow_norm/actual_norm/residualの推移からSUPPORTED/REFUTED/INCONCLUSIVEを判定する。
- `--mode 2x2`実行時は、`curve_neutral`セルの結果がNO_LATCH（曲率単独では再現しない、Sec.5-5と同じ機序の可能性を示唆）かRESIDUAL_PATH_LATCH（曲率由来の独立した欠陥）かに分類される。ただしこの分類は、`straight_nonneutral`・`straight_nonneutral_sub`・`straight_held`の3セルすべてがacceptance conditionを満たした場合にのみ採用される。

## 関連

- バッチ: 常設バッチ(`anticipation_driving_batch.yaml`)には未所属。個別実行用（`GT_esmini/test/headless/f7_curve_onset_probe.py`の`--mode sweep`／`--mode 2x2`の両方から使用される）
- 関連ID: feature:F7
