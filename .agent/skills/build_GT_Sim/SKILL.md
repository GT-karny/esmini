---
name: build_GT_Sim
description: GT_Simをビルドするスキル。
---

# ビルドディレクトリに移動
cd e:\Repository\GT_esmini\esmini\build

# Releaseビルド
cmake --build . --config Release --target GT_Sim

# または、Visual Studioを使用している場合
# GT_esmini.slnを開いてGT_Simプロジェクトをビルド

このコマンドでビルドできます。