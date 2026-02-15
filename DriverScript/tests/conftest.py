import os
import sys

# Ensure local package imports resolve in test runs.
_THIS_DIR = os.path.dirname(__file__)
_DRIVER_ROOT = os.path.abspath(os.path.join(_THIS_DIR, ".."))
if _DRIVER_ROOT not in sys.path:
    sys.path.insert(0, _DRIVER_ROOT)

# Allow tests to run in environments where protobuf runtime/gencode versions differ.
try:
    from google.protobuf import runtime_version as _runtime_version

    def _ignore_version_check(*_args, **_kwargs):
        return None

    _runtime_version.ValidateProtobufRuntimeVersion = _ignore_version_check
except Exception:
    pass
