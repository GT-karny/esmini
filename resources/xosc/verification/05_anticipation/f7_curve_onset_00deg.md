# カーブオンセット誤オーバーライド再現（0度・直進コントロール）

feature:F7のカーブオンセット再現アセット群のひとつ。走行中に道路曲率だけでAD操舵デマンドが急変する状況を作るための素材で、このファイルは曲率0度（直進）のコントロールにあたる。

## 検証の狙い

f7_curve_onset.xodrの5本の道路(id=0..4)はいずれも独立した行き止まり道路で、自車を各道路上のs=50へ直接テレポートすることで、テレポート直後(t=0)のAD操舵デマンドを道路曲率だけで決め打ちする。
`GT_esmini/test/headless/f7_curve_onset_probe.py`がこの5本の道路を読み込み、`--mode sweep`（8シナリオ×3 plant variantの曲率スイープ）で、ステアリングホイールが中立のまま操舵デマンドだけが急変したときに、シャドウ/残差方式の手動オーバーライド誤検知が起きないかを比較する。
xosc内コメントによれば、このファイル(road id=0)は直進のみのin-network直進コントロールで、AD操舵目標は走行全体を通じて約0のままであるべきとされる。virtual_driver_basic.xosc / decelerate_for_right_turn.xosc / traffic_lights_junction.xoscという既存の3つのF7シナリオも同様に直進から始まる点と対応している。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `f7_curve_onset.xodr`（road id=0: 曲率のない直進道路、全長180 m） |
| 自車 | road0 lane-1 s=50へ直接テレポート。Init に速度指定なし。Story Eventで目標速度13.889 m/sへ3.0 s linearで加速 |
| 他エンティティ | なし |
| 走行時間 | StopTrigger: シミュレーション時間25 s超過 |

## 進行

1. t=0: 自車がroad0 lane-1 s=50に直接テレポートされる（別区間から走行して進入するのではない）。同時にStory Event「Cruise」が開始し、目標速度13.889 m/sへ3.0 sかけてlinearに加速。
2. route上をroad0 s=50からs=170まで(shortest)走行する。
3. t=25 s到達でStopTrigger成立、シナリオ終了。

## 期待する挙動

このxosc単体にexpectations.yamlによる合否判定は無い。`f7_curve_onset_probe.py`が読み取る観測値は次の通り。

- 道路に曲率が無いため、AD操舵目標(`ffb.target_norm`)はt=0から走行を通じて約0のまま推移するはずである(xosc内コメント)。
- `--mode sweep`のanalyze()関数は、オンセットウィンドウ(既定1.0 s)内でshadow_normがactual_normよりtargetに近づく(overtakeする)方向と、residualの増加傾向からSUPPORTED/REFUTED/INCONCLUSIVEを判定する。曲率デマンドがほぼ0のこのシナリオでは、targetがactualから動かないため判定条件`n_valid<3`によりINCONCLUSIVEになりやすいと考えられる（推測。スクリプトのしきい値ロジックから読み取れる範囲）。
- `--mode 2x2`（既定のプライマリ診断）にはこのファイルは含まれない。2x2で使われる曲率メンバーはcurve_onset_30degのみ。

## 関連

- バッチ: 常設バッチ(`anticipation_driving_batch.yaml`)には未所属。個別実行用（`GT_esmini/test/headless/f7_curve_onset_probe.py`の`--mode sweep`診断から使用される）
- 関連ID: feature:F7
