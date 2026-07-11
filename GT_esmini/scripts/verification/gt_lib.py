"""ctypes wrapper for the GT_esmini C-API (GT_esminiLib.dll).

Runs the simulation *in-process* (GT_InitWithArgs -> GT_Step loop -> GT_Close)
and pulls per-frame VirtualDriver telemetry via GT_GetVirtualDriverTelemetry().
This is how the verification harness (gt_sim_test) records telemetry without
GT_Sim having to stream it over a socket.

DLL log lines are received via GT_SetLogCallback (leveled) and relayed to the
Python logger "gt_esmini.dll" (default: stderr StreamHandler when the caller
has not configured logging); the DLL is started with --disable_stdout so its
console writes stop mixing into fd1 (audit PY-1 / CORE-1/5). On an older DLL
without the log API the wrapper degrades to the legacy console behavior.

The DLL depends on python312.dll (embedded Python) and the Release-dir DLLs, so
the loader adds those directories to the search path first (see common-pitfalls:
missing python-embed on PATH -> 0xC0000135). Run via DriverScript/.venv.
"""
from __future__ import annotations

import ctypes
import json
import logging
import os
import re
import sys
from pathlib import Path

# .../esmini  (this file is GT_esmini/scripts/verification/gt_lib.py)
REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_RELEASE = REPO_ROOT / "build" / "GT_esmini" / "Release"
DEFAULT_DLL = DEFAULT_RELEASE / "GT_esminiLib.dll"
DEFAULT_PY_EMBED = REPO_ROOT / "thirdparty" / "python-embed" / "python-3.12.10-embed-amd64"

_LOG = logging.getLogger("gt_esmini.dll")

# GT_LogCallbackFn: void (*)(int level, const char* message, void* user_data)
_GT_LOG_CALLBACK_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p)

# DLL level (0=unknown, 1=debug, 2=info, 3=warn, 4=error) -> Python logging level
_LEVEL_MAP = {
    0: logging.INFO,
    1: logging.DEBUG,
    2: logging.INFO,
    3: logging.WARNING,
    4: logging.ERROR,
}

# pugixml parse failures report a character offset only (audit CORE-12)
_OFFSET_RE = re.compile(r"at offset \(character position\): (\d+)")


def _ensure_default_handler() -> None:
    """Attach a stderr StreamHandler if the caller configured no logging at all."""
    if not _LOG.hasHandlers():
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter("[dll] %(message)s"))
        _LOG.addHandler(handler)
        _LOG.setLevel(logging.INFO)


def _offset_to_line_col(path: Path, offset: int) -> tuple[int, int] | None:
    """1-based (line, col) of a byte offset in path, or None if not computable."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if offset < 0 or offset > len(data):
        return None
    line = data.count(b"\n", 0, offset) + 1
    col = offset - data.rfind(b"\n", 0, offset)
    return line, col


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
        self._xml_candidates: list[Path] = []

        # Leveled log relay (GT_SetLogCallback / GT_GetLastError). Older DLLs
        # lack these exports -> graceful degrade to legacy console output.
        self._log_cb = None
        self._has_log_api = False
        try:
            self.lib.GT_SetLogCallback.argtypes = [_GT_LOG_CALLBACK_TYPE, ctypes.c_void_p]
            self.lib.GT_SetLogCallback.restype = None
            self.lib.GT_GetLastError.argtypes = [ctypes.c_char_p, ctypes.c_int]
            self.lib.GT_GetLastError.restype = ctypes.c_int
        except AttributeError:
            return
        _ensure_default_handler()
        # The CFUNCTYPE object must outlive the DLL registration: the DLL calls
        # this pointer on every log line, and a GC'd callback is a hard crash.
        self._log_cb = _GT_LOG_CALLBACK_TYPE(self._on_dll_log)
        self.lib.GT_SetLogCallback(self._log_cb, None)
        self._has_log_api = True

    def _on_dll_log(self, level: int, message: bytes, _user_data) -> None:
        # Called synchronously from inside the DLL logger - must never raise.
        try:
            text = message.decode("utf-8", errors="replace") if message else ""
            if level == 4:
                text = self._annotate_parse_offset(text)
            _LOG.log(_LEVEL_MAP.get(level, logging.INFO), "%s", text)
        except Exception:
            pass

    def _annotate_parse_offset(self, text: str) -> str:
        """Append ' (line X, col Y of <file>)' to pugixml offset-only parse errors
        when the offset resolves inside one of the known input files."""
        if "Error parsing" not in text:
            return text
        m = _OFFSET_RE.search(text)
        if not m:
            return text
        offset = int(m.group(1))
        candidates = [p for p in self._xml_candidates if p.name in text] or self._xml_candidates
        for p in candidates:
            lc = _offset_to_line_col(p, offset)
            if lc is not None:
                return f"{text} (line {lc[0]}, col {lc[1]} of {p.name})"
        return text

    def _collect_xml_candidates(self, args: list[str]) -> None:
        """Remember the scenario (and its LogicFile xodr) for offset->line/col."""
        self._xml_candidates = []
        scen: Path | None = None
        if args and not args[0].startswith("--"):
            scen = Path(args[0])
        else:
            for i, a in enumerate(args):
                if a == "--osc" and i + 1 < len(args):
                    scen = Path(args[i + 1])
                    break
        if scen is None or not scen.is_file():
            return
        self._xml_candidates.append(scen)
        try:
            m = re.search(r'LogicFile[^>]*\bfilepath="([^"]+)"',
                          scen.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            return
        if m:
            xodr = Path(m.group(1))
            if not xodr.is_absolute():
                xodr = scen.parent / xodr
            if xodr.is_file():
                self._xml_candidates.append(xodr)

    def get_last_error(self) -> str:
        """Last error-level DLL log line (via GT_GetLastError), '' if none/unsupported."""
        if not self._has_log_api:
            return ""
        buf = ctypes.create_string_buffer(4096)
        n = self.lib.GT_GetLastError(buf, len(buf))
        if n <= 0:
            return ""
        return buf.value.decode("utf-8", errors="replace")

    def open_osi_socket(self, ip: str = "127.0.0.1") -> int:
        """Open the OSI groundtruth UDP socket (sends to ip:48198 per frame).
        Call after init_with_args. Returns 0 on success.

        Uses GT_OpenOSISocket (the GT-flavored variant) so the OSI frequency is
        auto-set to 1 (every frame) when unset — core SE_OpenOSISocket is vanilla."""
        return self.lib.GT_OpenOSISocket(ip.encode("utf-8"))

    def init_with_args(self, args: list[str]) -> int:
        """args: everything after argv[0], e.g. ['--osc', path, '--headless', ...].
        A program name is prepended automatically. Returns 0 on success."""
        self._collect_xml_candidates(args)
        if self._has_log_api and "--disable_stdout" not in args:
            # Log lines arrive via the callback regardless of --disable_stdout;
            # this only stops the DLL's duplicate console writes on fd1.
            args = list(args) + ["--disable_stdout"]
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
        if self._has_log_api:
            # Detach before this wrapper (and its CFUNCTYPE) can be GC'd - the
            # DLL must never call a dead Python callback.
            self.lib.GT_SetLogCallback(ctypes.cast(None, _GT_LOG_CALLBACK_TYPE), None)
            self._has_log_api = False
            self._log_cb = None

    def __enter__(self) -> "GtLib":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
