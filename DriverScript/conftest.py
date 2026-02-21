# Root conftest.py - loaded before test collection
import sys
import os

# Patch protobuf version check BEFORE any test imports
try:
    from google.protobuf import runtime_version as _runtime_version

    def _ignore_version_check(*_args, **_kwargs):
        return None

    _runtime_version.ValidateProtobufRuntimeVersion = _ignore_version_check
except Exception:
    pass

# Ensure local package imports resolve
_THIS_DIR = os.path.dirname(__file__)
if _THIS_DIR not in sys.path:
    sys.path.insert(0, _THIS_DIR)
