# カーブオンセット誤オーバーライド再現（20度）

feature:F7のカーブオンセット再現アセット群のひとつ。走行中に道路曲率だけでAD操舵デマンドが急変する状況を作るための素材で、このファイルはホイール回転換算で20度分の曲率デマンドを課すメンバーにあたる。

## 検証の狙い

f7_curve_onset.xodrの5本の道路(id=0..4)はいずれも独立した行き止まり道路で、自車を各道路上のs=50へ直接テレポートすることで、テレポート直後(t=0)のAD操舵デマンドを道路曲率だけで決め打ちする。
`GT_esmini/test/headless/f7_curve_onset_probe.py`がこの5本の道路を読み込み、`--mode sweep`（8シナリオ×3 plant variantの曲率スイープ）で、ステアリングホイールが中立のまま操舵デマンドだけが急変したときに、シャドウ/残差方式の手動オーバーライド誤検知が起きないかを比較する。
xosc内コメントによれば、road id=2は30 m直線+150 mの定曲率右カーブ(R=110.629 m)で、自車はs=50（円弧開始s=30から20 m地点）にテレポートされるため、t=0の時点で道路自体がステアリングホイール換算で約20度の回転(axis_frac約0.04444、タイヤ角約1.553度)を要求する。
このファイルは`--mode sweep`でのみ使用され、`--mode 2x2`（既定のプライマリ診断、6セル構成）には含まれない。2x2で使われる曲率メンバーはcurve_onset_30degのみ（ユーザーの実機バグ報告のスケールに一致するため）。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `f7_curve_onset.xodr`（road id=2: 30 m直線+150 m定曲率右カーブ、R=110.629 m） |
| 自車 | road2 lane-1 s=50へ直接テレポート（円弧区間20 m地点）。Init に速度指定なし。Story Eventで目標速度13.889 m/sへ3.0 s linearで加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間25 s超過 |

## 進行

1. t=0: 自車がroad2 lane-1 s=50に直接テレポートされる（円弧区間の途中）。同時にStory Event「Cruise」が開始し、目標速度13.889 m/sへ3.0 sかけてlinearに加速。
2. route上をroad2 s=50からs=170まで(shortest)走行する。
3. t=25 s到達でStopTrigger成立、シナリオ終了。

## 期待する挙動

このxosc単体にexpectations.yamlによる合否判定は無い。`f7_curve_onset_probe.py`が読み取る観測値は次の通り。

- テレポート直後、道路曲率によりAD操舵目標(`ffb.target_norm`)がホイール換算約20度相当(axis_frac約0.04444)へ跳ね上がるはずである(xosc/xodr内コメントの構築上の値)。
- `--mode sweep`のanalyze()関数は、オンセットウィンドウ(既定1.0 s)内でshadow_normがactual_normよりtargetに近づく(overtakeする)方向と、residualの増加傾向からSUPPORTED/REFUTED/INCONCLUSIVEを判定する。

## 関連

- バッチ: 常設バッチ(`anticipation_driving_batch.yaml`)には未所属。個別実行用（`GT_esmini/test/headless/f7_curve_onset_probe.py`の`--mode sweep`診断から使用される）
- 関連ID: feature:F7
