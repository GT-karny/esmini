#!/usr/bin/env python3
import argparse
import json
from typing import Dict, Iterable, List, Optional


def _infer_type_name(action: argparse.Action) -> str:
    if isinstance(action, (argparse._StoreTrueAction, argparse._StoreFalseAction)):
        return "bool"
    arg_type = getattr(action, "type", None)
    if arg_type is int:
        return "int"
    if arg_type is float:
        return "float"
    if arg_type is bool:
        return "bool"
    return "str"


def add_dump_argspec_option(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--dump-argspec", action="store_true", help=argparse.SUPPRESS)


def build_argspec(
    parser: argparse.ArgumentParser,
    descriptions: Optional[Dict[str, str]] = None,
    ui_hints: Optional[Dict[str, Dict[str, str]]] = None,
    exclude_names: Optional[Iterable[str]] = None,
) -> List[Dict]:
    descriptions = descriptions or {}
    ui_hints = ui_hints or {}
    exclude = set(exclude_names or [])
    specs: List[Dict] = []

    for action in parser._actions:
        if isinstance(action, argparse._HelpAction):
            continue
        if not action.option_strings:
            continue
        name = next(
            (opt for opt in action.option_strings if opt.startswith("--")),
            action.option_strings[0],
        )
        if name in exclude or name == "--dump-argspec":
            continue

        default = None if action.default is argparse.SUPPRESS else action.default
        help_text = action.help or ""
        spec: Dict = {
            "name": name,
            "type": _infer_type_name(action),
            "default": default,
            "required": bool(getattr(action, "required", False)),
            "help": help_text,
            "description": descriptions.get(name, help_text),
        }
        if action.choices is not None:
            spec["choices"] = list(action.choices)

        hint = ui_hints.get(name)
        if isinstance(hint, dict):
            spec.update(hint)
        specs.append(spec)

    return specs


def maybe_dump_argspec(
    args,
    parser: argparse.ArgumentParser,
    descriptions: Optional[Dict[str, str]] = None,
    ui_hints: Optional[Dict[str, Dict[str, str]]] = None,
    exclude_names: Optional[Iterable[str]] = None,
) -> bool:
    if not getattr(args, "dump_argspec", False):
        return False
    specs = build_argspec(
        parser=parser,
        descriptions=descriptions,
        ui_hints=ui_hints,
        exclude_names=exclude_names,
    )
    print(json.dumps(specs, ensure_ascii=True))
    return True
