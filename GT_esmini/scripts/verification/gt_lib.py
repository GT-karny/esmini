"""ctypes wrapper for the GT_esmini C-API (GT_esminiLib.dll).

Runs the simulation *in-process* (GT_InitWithArgs -> GT_Step loop -> GT_Close)
and pulls per-frame VirtualDriver telemetry via GT_GetVirtualDriverTelemetry().
This is how the verification harness (gt_sim_test) records telemetry without
GT_Sim having to stream it over a socket.

The DLL depends on python312.dll (embedded Python) and the Release-dir DLLs, so
the loader adds those directories to the search path first (see common-pitfalls:
missing python-embed on PATH -> 0xC0000135). Run via DriverScript/.venv.
"""
from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path

# .../esmini  (this file is GT_esmini/scripts/verification/gt_lib.py)
REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_RELEASE = REPO_ROOT / "build" / "GT_esmini" / "Release"
DEFAULT_DLL = DEFAULT_RELEASE / "GT_esminiLib.dll"
DEFAULT_PY_EMBED = REPO_ROOT / "thirdparty" / "python-embed" / "python-3.12.10-embed-amd64"


class GtLib:
    """Thin ctypes binding around the subset of the GT C-API the harness needs."""

    def __init__(self, dll_path: str | Path = DEFAULT_DLL,
                 release_dir: str | Path = DEFAULT_RELEASE,
                 py_embed: str | Path = DEFAULT_PY_EMBED,
                 buf_size: int = 1 << 16):
        dll_path = Path(dll_path)
        if not dll_path.is_file():
            raise FileNotFoundError(
                f"{dll_path} not found - build Protocol A first (cmake --build build --config Release)"
            )

        # Make dependent DLLs (python312.dll, OSG, etc.) resolvable.
        for d in (Path(release_dir), Path(py_embed)):
            if d.is_dir():
                os.add_dll_directory(str(d))
                os.environ["PATH"] = str(d) + os.pathsep + os.environ.get("PATH", "")

        self.lib = ctypes.CDLL(str(dll_path))

        self.lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
        self.lib.GT_InitWithArgs.restype = ctypes.c_int
        self.lib.GT_Step.argtypes = [ctypes.c_double]
        self.lib.GT_Close.argtypes = []
        self.lib.GT_GetVirtualDriverTelemetry.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        self.lib.GT_GetVirtualDriverTelemetry.restype = ctypes.c_int
        # esmini OSI UDP streaming. GT_InitWithArgs parses --osi but (unlike
        # GT_Sim.exe) never opens the groundtruth socket, so the harness opens it
        # explicitly after init; GT_Step then emits OSI to 127.0.0.1:48198 each
        # frame. (SE_UpdateOSIGroundTruth/SE_CloseOSISocket are not exported.)
        # GT_OpenOSISocket is the GT-flavored variant: it auto-sets the OSI
        # frequency to 1 (send every frame) when unset, which the in-process
        # harness depends on. Core SE_OpenOSISocket is vanilla (audit BND-2/R5-U1).
        self.lib.GT_OpenOSISocket.argtypes = [ctypes.c_char_p]
        self.lib.GT_OpenOSISocket.restype = ctypes.c_int

        self._buf = ctypes.create_string_buffer(buf_size)
        self._open = False

    def open_osi_socket(self, ip: str = "127.0.0.1") -> int:
        """Open the OSI groundtruth UDP socket (sends to ip:48198 per frame).
        Call after init_with_args. Returns 0 on success.

        Uses GT_OpenOSISocket (the GT-flavored variant) so the OSI frequency is
        auto-set to 1 (every frame) when unset — core SE_OpenOSISocket is vanilla."""
        return self.lib.GT_OpenOSISocket(ip.encode("utf-8"))

    def init_with_args(self, args: list[str]) -> int:
        """args: everything after argv[0], e.g. ['--osc', path, '--headless', ...].
        A program name is prepended automatically. Returns 0 on success."""
        argv_list = [b"gt_sim_test"] + [a.encode("utf-8") for a in args]
        argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
        rc = self.lib.GT_InitWithArgs(len(argv_list), argv)
        self._open = rc == 0
        return rc

    def step(self, dt: float) -> None:
        self.lib.GT_Step(dt)

    def get_vd_telemetry(self, vehicle_id: int = -1) -> dict | None:
        """Returns the telemetry dict, or None if the vehicle has no
        VirtualDriverController active (rc < 0) - e.g. before/after the scenario."""
        n = self.lib.GT_GetVirtualDriverTelemetry(vehicle_id, self._buf, len(self._buf))
        if n <= 0:
            return None
        try:
            return json.loads(self._buf.value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return None

    def close(self) -> None:
        if self._open:
            self.lib.GT_Close()
            self._open = False

    def __enter__(self) -> "GtLib":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
