# compare-controllers

> **DEPRECATED (audit SCR-2/WEB-5)** — This skill's scripts have been moved to
> `archive/frozen_python_verification/`. PythonDriver features are dev-frozen since
> v0.8; the toolchain is non-functional (dat.py loss, stale imports). Do not invoke.

PythonDriverControllerとDefaultControllerの比較テストを実行します。

## 使用方法

```
/compare-controllers [シナリオID]
```

シナリオIDを指定しない場合は全シナリオを実行します。

## 実行内容

1. venv有効化（DriverScript/.venv）
2. Python依存関係確認（pyyaml, matplotlib, jinja2）
3. 比較テスト実行
4. 結果レポート生成（HTML + JSON）

## 出力

- `test_results/comparison_YYYYMMDD_HHMMSS/comparison_report.html` - HTMLレポート
- `test_results/comparison_YYYYMMDD_HHMMSS/comparison_summary.json` - JSON結果
- `test_results/comparison_YYYYMMDD_HHMMSS/plots/` - 比較プロット

## 利用可能なシナリオ

- `straight_500m` - 直進路での基本的な縦方向制御
- `lane_change_simple` - 車線変更中の横方向制御
- `speed_profile` - 速度プロファイル追従
- `lane_change` - 標準車線変更シナリオ

---

## 実装詳細

```bash
# venv有効化
cd DriverScript && .venv/Scripts/activate && cd ..

# 依存関係確認とインストール
python -c "import yaml; import matplotlib; import jinja2" 2>/dev/null || pip install pyyaml matplotlib jinja2 --quiet

# 比較テスト実行
python archive/frozen_python_verification/scripts/compare_python_vs_default.py \
  --scenario ${1:-}  # 引数があればそれを使用、なければ全シナリオ \
  --gt-sim build/GT_esmini/Release/GT_Sim.exe \
  --matrix archive/frozen_python_verification/test/comparison_matrix.yaml \
  --thresholds GT_esmini/test/comparison_thresholds.yaml

# 結果表示
echo ""
echo "=== 結果ファイル ==="
ls -lh test_results/comparison_*/comparison_report.html | tail -1
ls -lh test_results/comparison_*/comparison_summary.json | tail -1
echo ""
echo "HTMLレポートをブラウザで開く:"
echo "  explorer test_results/comparison_*/comparison_report.html"
```

## トラブルシューティング

### GT_Simが見つからない

```bash
# GT_Simをビルド
cmake --build "e:\Repository\GT_esmini\esmini\build" --config Release
```

### PythonDriverControllerエラー

埋め込みPython環境の問題の可能性があります。
DefaultControllerとの比較は正常に動作します。

### Unicode文字エラー

Windows環境でUnicode文字（絵文字）が表示できない場合があります。
スクリプト内で`[OK]`/`[NG]`などのASCII文字に置換済みです。

## 関連ファイル（アーカイブ済み）

- `archive/frozen_python_verification/scripts/compare_python_vs_default.py` - メインオーケストレーター（旧: `scripts/compare_python_vs_default.py`）
- `archive/frozen_python_verification/scripts/comparison_kpis.py` - メトリクス計算（旧: `scripts/comparison_kpis.py`）
- `scripts/scenario_generator.py` - XOSCバリアント生成
- `archive/frozen_python_verification/scripts/plot_comparison.py` - プロット生成（旧: `scripts/plot_comparison.py`）
- `archive/frozen_python_verification/test/comparison_matrix.yaml` - テスト設定（旧: `GT_esmini/test/comparison_matrix.yaml`）
- `GT_esmini/test/comparison_thresholds.yaml` - 合格基準（アクティブ: webバックエンドが読み書き）
