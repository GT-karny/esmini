#!/usr/bin/env python3
"""XOSCシナリオバリアントジェネレーター

ベースラインXOSCファイルから2種類のバリアントを生成:
1. DefaultControllerバリアント: <ObjectController>を削除してesminiのdefaultController()を使用
2. PythonDriverControllerバリアント: PythonDriverControllerの<ObjectController>を挿入
"""

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Optional


def register_namespaces():
    """XML名前空間を登録してプリフィックスを保持"""
    # OpenSCENARIO名前空間はデフォルト名前空間として扱う
    ET.register_namespace('', 'http://www.asam.net/xosc')


def remove_element_safe(parent: ET.Element, child: ET.Element) -> bool:
    """要素を安全に削除"""
    try:
        parent.remove(child)
        return True
    except ValueError:
        return False


def generate_default_variant(
    baseline_xosc: Path,
    output_path: Path,
    verbose: bool = False
) -> None:
    """DefaultControllerバリアントを生成

    <ObjectController>と<ActivateControllerAction>を削除して、
    esminiのdefaultController()を使用するバリアントを作成。

    Args:
        baseline_xosc: ベースラインXOSCファイルパス
        output_path: 出力XOSCファイルパス
        verbose: 詳細ログを出力
    """
    register_namespaces()

    try:
        tree = ET.parse(baseline_xosc)
        root = tree.getroot()
    except ET.ParseError as e:
        raise ValueError(f"XMLパースエラー: {baseline_xosc}: {e}")

    removed_controllers = 0
    removed_activate_actions = 0

    # <ObjectController>を検索して削除
    # 名前空間を考慮して検索
    ns = {'osc': 'http://www.asam.net/xosc'}

    # 名前空間なしで検索（多くのファイルは名前空間を使用していない）
    for entity in root.findall(".//ScenarioObject"):
        controller = entity.find("ObjectController")
        if controller is not None:
            entity.remove(controller)
            removed_controllers += 1
            if verbose:
                entity_name = entity.get("name", "Unknown")
                print(f"  削除: <ObjectController> from entity '{entity_name}'")

    # 名前空間ありで検索（一部のファイルで使用）
    for entity in root.findall(".//osc:ScenarioObject", ns):
        controller = entity.find("osc:ObjectController", ns)
        if controller is not None:
            entity.remove(controller)
            removed_controllers += 1
            if verbose:
                entity_name = entity.get("name", "Unknown")
                print(f"  削除: <ObjectController> from entity '{entity_name}'")

    # <ActivateControllerAction>を削除
    for action in root.findall(".//ActivateControllerAction"):
        parent = find_parent(root, action)
        if parent is not None:
            parent.remove(action)
            removed_activate_actions += 1
            if verbose:
                print(f"  削除: <ActivateControllerAction>")

    # 名前空間ありバージョン
    for action in root.findall(".//osc:ActivateControllerAction", ns):
        parent = find_parent(root, action)
        if parent is not None:
            parent.remove(action)
            removed_activate_actions += 1
            if verbose:
                print(f"  削除: <ActivateControllerAction>")

    if verbose:
        print(f"  合計削除: Controllers={removed_controllers}, ActivateActions={removed_activate_actions}")

    # 出力ディレクトリを作成
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # XML宣言とエンコーディングを保持して書き込み
    tree.write(
        output_path,
        encoding='UTF-8',
        xml_declaration=True,
        method='xml'
    )

    if verbose:
        print(f"[OK] DefaultControllerバリアント生成: {output_path}")


def find_parent(root: ET.Element, child: ET.Element) -> Optional[ET.Element]:
    """子要素の親要素を検索"""
    for parent in root.iter():
        if child in parent:
            return parent
    return None


def generate_python_variant(
    baseline_xosc: Path,
    output_path: Path,
    python_script: str = "DriverScript/pythondriver/scenario_drive_embedded.py",
    python_class: str = "EmbeddedController",
    python_home: str = "",
    python_trace: bool = True,
    python_trace_dir: str = "",
    verbose: bool = False
) -> None:
    """PythonDriverControllerバリアントを生成

    <ObjectController>にPythonDriverControllerを挿入し、
    <ActivateControllerAction>を追加。

    Args:
        baseline_xosc: ベースラインXOSCファイルパス
        output_path: 出力XOSCファイルパス
        python_script: Pythonスクリプトパス
        python_class: Pythonクラス名
        python_home: Python_HOME（空文字列で自動検出）
        python_trace: トレース有効化
        python_trace_dir: トレース出力ディレクトリ
        verbose: 詳細ログを出力
    """
    register_namespaces()

    try:
        tree = ET.parse(baseline_xosc)
        root = tree.getroot()
    except ET.ParseError as e:
        raise ValueError(f"XMLパースエラー: {baseline_xosc}: {e}")

    added_controllers = 0
    added_activate_actions = 0

    # 既存の<ObjectController>を削除（もしあれば）
    for entity in root.findall(".//ScenarioObject"):
        controller = entity.find("ObjectController")
        if controller is not None:
            entity.remove(controller)

    # 各ScenarioObjectにPythonDriverControllerを追加
    for entity in root.findall(".//ScenarioObject"):
        entity_name = entity.get("name", "Unknown")

        # <ObjectController>要素を作成
        controller = ET.SubElement(entity, "Controller")
        controller.set("name", "PythonDriverController")

        # <Properties>
        properties = ET.SubElement(controller, "Properties")

        # Property要素を追加
        prop_esmini = ET.SubElement(properties, "Property")
        prop_esmini.set("name", "esminiController")
        prop_esmini.set("value", "PythonDriverController")

        prop_script = ET.SubElement(properties, "Property")
        prop_script.set("name", "PythonScript")
        prop_script.set("value", python_script)

        prop_class = ET.SubElement(properties, "Property")
        prop_class.set("name", "PythonClass")
        prop_class.set("value", python_class)

        prop_home = ET.SubElement(properties, "Property")
        prop_home.set("name", "PythonHome")
        prop_home.set("value", python_home)

        prop_trace = ET.SubElement(properties, "Property")
        prop_trace.set("name", "PythonTrace")
        prop_trace.set("value", "on" if python_trace else "off")

        prop_trace_dir = ET.SubElement(properties, "Property")
        prop_trace_dir.set("name", "PythonTraceDir")
        prop_trace_dir.set("value", python_trace_dir)

        # <ObjectController>でラップ
        obj_controller = ET.Element("ObjectController")
        obj_controller.append(controller)

        # VehicleまたはCatalogReferenceの後に挿入
        insert_pos = None
        for i, child in enumerate(entity):
            if child.tag in ["Vehicle", "CatalogReference"]:
                insert_pos = i + 1
                break

        if insert_pos is not None:
            entity.insert(insert_pos, obj_controller)
        else:
            entity.append(obj_controller)

        added_controllers += 1

        if verbose:
            print(f"  追加: <ObjectController> to entity '{entity_name}'")

    # Init/Actions/Private配下に<ActivateControllerAction>を追加
    # 各Privateセクションを検索
    for private in root.findall(".//Init/Actions/Private"):
        entity_ref = private.get("entityRef", "Unknown")

        # 既存の<ActivateControllerAction>を確認
        existing_activate = private.find(".//ActivateControllerAction")
        if existing_activate is None:
            # <PrivateAction>を作成
            private_action = ET.SubElement(private, "PrivateAction")
            activate_action = ET.SubElement(private_action, "ActivateControllerAction")
            activate_action.set("longitudinal", "true")
            activate_action.set("lateral", "true")

            added_activate_actions += 1

            if verbose:
                print(f"  追加: <ActivateControllerAction> for entity '{entity_ref}'")

    if verbose:
        print(f"  合計追加: Controllers={added_controllers}, ActivateActions={added_activate_actions}")

    # 出力ディレクトリを作成
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # XML宣言とエンコーディングを保持して書き込み
    tree.write(
        output_path,
        encoding='UTF-8',
        xml_declaration=True,
        method='xml'
    )

    if verbose:
        print(f"[OK] PythonDriverControllerバリアント生成: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="XOSCシナリオバリアントジェネレーター"
    )
    parser.add_argument(
        "baseline_xosc",
        type=Path,
        help="ベースラインXOSCファイルパス"
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("test_results/comparison_xosc"),
        help="出力ディレクトリ (デフォルト: test_results/comparison_xosc)"
    )
    parser.add_argument(
        "--python-script",
        type=str,
        default="DriverScript/pythondriver/scenario_drive_embedded.py",
        help="Pythonスクリプトパス"
    )
    parser.add_argument(
        "--python-class",
        type=str,
        default="EmbeddedController",
        help="Pythonクラス名"
    )
    parser.add_argument(
        "--python-home",
        type=str,
        default="",
        help="PYTHON_HOME（空で自動検出）"
    )
    parser.add_argument(
        "--no-trace",
        action="store_true",
        help="Pythonトレースを無効化"
    )
    parser.add_argument(
        "--python-trace-dir",
        type=str,
        default="",
        help="Pythonトレース出力ディレクトリ"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="詳細ログを出力"
    )

    args = parser.parse_args()

    if not args.baseline_xosc.exists():
        print(f"エラー: ベースラインXOSCファイルが見つかりません: {args.baseline_xosc}")
        return 1

    # ファイル名からバリアント名を生成
    baseline_name = args.baseline_xosc.stem
    default_output = args.output_dir / f"{baseline_name}_default.xosc"
    python_output = args.output_dir / f"{baseline_name}_python.xosc"

    print(f"ベースライン: {args.baseline_xosc}")
    print(f"出力ディレクトリ: {args.output_dir}")
    print()

    # DefaultControllerバリアント生成
    print("DefaultControllerバリアント生成中...")
    generate_default_variant(
        args.baseline_xosc,
        default_output,
        verbose=args.verbose
    )

    # PythonDriverControllerバリアント生成
    print("\nPythonDriverControllerバリアント生成中...")
    generate_python_variant(
        args.baseline_xosc,
        python_output,
        python_script=args.python_script,
        python_class=args.python_class,
        python_home=args.python_home,
        python_trace=not args.no_trace,
        python_trace_dir=args.python_trace_dir,
        verbose=args.verbose
    )

    print(f"\n[OK] バリアント生成完了")
    print(f"  Default: {default_output}")
    print(f"  Python:  {python_output}")

    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
