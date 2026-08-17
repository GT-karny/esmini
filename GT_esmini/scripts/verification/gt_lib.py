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
DEFAULT_PY_EMBED = (
    REPO_ROOT / "thirdparty" / "python-embed" / "python-3.12.10-embed-amd64"
)

_LOG = logging.getLogger("gt_esmini.dll")

# GT_LogCallbackFn: void (*)(int level, const char* message, void* user_data)
_GT_LOG_CALLBACK_TYPE = ctypes.CFUNCTYPE(
    None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p
)

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

    def __init__(
        self,
        dll_path: str | Path = DEFAULT_DLL,
        release_dir: str | Path = DEFAULT_RELEASE,
        py_embed: str | Path = DEFAULT_PY_EMBED,
        buf_size: int = 1 << 16,
    ):
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
        # Kept for diagnostics -- e.g. naming the DLL in the
        # get_osi_host_vehicle_data() RuntimeError when this DLL predates
        # GT_GetOSIHostVehicleData.
        self.dll_path = dll_path

        self.lib.GT_InitWithArgs.argtypes = [
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_char_p),
        ]
        self.lib.GT_InitWithArgs.restype = ctypes.c_int
        self.lib.GT_Step.argtypes = [ctypes.c_double]
        self.lib.GT_Close.argtypes = []
        self.lib.GT_GetVirtualDriverTelemetry.argtypes = [
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_int,
        ]
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
        # In-process OSI GroundTruth retrieval (feature:F7 gate hardening,
        # 2026-07-28): gt_sim_test.py used to open the UDP socket above and
        # reassemble GT_Step's wire-format emission from a loopback socket
        # (_OsiCapture). Under loopback UDP buffer pressure that silently
        # dropped frames (confirmed: normal_following captured only 403/440
        # frames in one run) with no distinction from "the world really was
        # empty this frame". SE_SetOSIFrequency + SE_GetOSIGroundTruth are
        # vanilla upstream esminiLib exports (confirmed present in
        # GT_esminiLib.dll) that serialize and return the SAME internal
        # GroundTruth object synchronously, in-process -- no socket, nothing
        # to drop packets. GetOSIGroundTruth's own C++ side only does the
        # (re-)serialization work when neither UDP nor file logging already
        # did it this frame (GT_OSIReporter_Api.cpp), so as long as
        # open_osi_socket() is never called, every call here is guaranteed
        # fresh for the frame just stepped.
        self.lib.SE_SetOSIFrequency.argtypes = [ctypes.c_int]
        self.lib.SE_SetOSIFrequency.restype = ctypes.c_int
        self.lib.SE_GetOSIGroundTruth.argtypes = [ctypes.POINTER(ctypes.c_int)]
        self.lib.SE_GetOSIGroundTruth.restype = ctypes.c_void_p

        # GT_GetOSIHostVehicleData (req-vd-ad:REQ-AD-028 段c, vd-func:FUNC-075):
        # in-process counterpart of SE_GetOSIGroundTruth above, but for HVD
        # instead of GroundTruth.
        #
        # This WAS an eager, unwrapped binding (hard AttributeError at
        # construction against a DLL that lacks the export). That reasoning
        # was sound for one failure mode -- a stale DLL silently masking a
        # capture bug -- but missed a different one: it makes GtLib
        # structurally incapable of loading ANY older DLL at all, including
        # via the harness's own `--dll` override. Concrete incident: while
        # bisecting an intermittent native access-violation (dangling-pointer
        # fingerprint, not null-deref) the decisive experiment was to rerun
        # the same batch against the pre-session, known-good
        # dist/GT_Sim_v0.14.3/bin/GT_esminiLib.dll. Every scenario died at
        # GtLib.__init__ with "function 'GT_GetOSIHostVehicleData' not found"
        # -- the eager binding blocked the one tool needed to attribute the
        # regression to a change, before a single scenario could even run.
        # A VirtualDriver batch never calls this API at all, so it should
        # never pay for HVD's absence.
        #
        # The trade-off is therefore not "loud vs quiet" -- it is "loud at
        # construction vs able to bisect at all". Resolved the same way
        # GT_SetLogCallback below already resolves it for older DLLs: try the
        # binding, record a flag, and defer the loud failure to first use
        # (get_osi_host_vehicle_data), where it can name exactly what's
        # missing and why, instead of a bare AttributeError with no context.
        self._has_hvd_api = False
        try:
            self.lib.GT_GetOSIHostVehicleData.argtypes = [
                ctypes.c_int,
                ctypes.POINTER(ctypes.c_int),
            ]
            self.lib.GT_GetOSIHostVehicleData.restype = ctypes.c_void_p
            self._has_hvd_api = True
        except AttributeError:
            pass

        self._buf = ctypes.create_string_buffer(buf_size)
        self._open = False
        self._xml_candidates: list[Path] = []

        # Leveled log relay (GT_SetLogCallback / GT_GetLastError). Older DLLs
        # lack these exports -> graceful degrade to legacy console output.
        self._log_cb = None
        self._has_log_api = False
        try:
            self.lib.GT_SetLogCallback.argtypes = [
                _GT_LOG_CALLBACK_TYPE,
                ctypes.c_void_p,
            ]
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
        candidates = [
            p for p in self._xml_candidates if p.name in text
        ] or self._xml_candidates
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
            m = re.search(
                r'LogicFile[^>]*\bfilepath="([^"]+)"',
                scen.read_text(encoding="utf-8", errors="replace"),
            )
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

    def set_osi_frequency(self, freq: int = 1) -> int:
        """Enable per-frame OSI GroundTruth updates (SE_SetOSIFrequency) WITHOUT
        opening any socket. Call once after init_with_args, before the first
        get_osi_ground_truth(). Returns 0 on success."""
        return self.lib.SE_SetOSIFrequency(freq)

    def get_osi_ground_truth(self) -> bytes | None:
        """In-process retrieval of this frame's serialized OSI GroundTruth
        (SE_GetOSIGroundTruth) — no socket, no transport to drop packets over.
        Call after step(); requires set_osi_frequency() to have been called
        first (otherwise the DLL never refreshes the internal GroundTruth
        object and this returns the same stale/empty buffer every call).
        Returns None if the DLL reports zero bytes (nothing to distinguish
        from a genuinely empty capture — the caller must treat this as a
        capture failure, not an empty-but-valid scene).

        restype is c_void_p (not c_char_p): the payload is a serialized
        protobuf message that can contain embedded NUL bytes, which c_char_p
        would silently truncate at. ctypes.string_at(ptr, size) copies
        exactly `size` bytes regardless of content."""
        size = ctypes.c_int(0)
        ptr = self.lib.SE_GetOSIGroundTruth(ctypes.byref(size))
        if not ptr or size.value <= 0:
            return None
        return ctypes.string_at(ptr, size.value)

    def get_osi_host_vehicle_data(self, vehicle_id: int = -1) -> bytes | None:
        """In-process retrieval of this frame's serialized OSI HostVehicleData
        (GT_GetOSIHostVehicleData) for one vehicle. HVD's real transport is UDP
        48199, which does not fit an in-process harness, so this mirrors
        get_osi_ground_truth()'s no-socket approach for HVD instead. Unlike
        get_osi_ground_truth(), there is no set_osi_frequency() equivalent to
        call first: GT_Step keeps the DLL-side HVD buffer current every frame
        unconditionally (HVD, unlike GroundTruth, is not frequency-gated).

        vehicle_id=-1 resolves to the first/ego vehicle, same rule as
        set_host_vehicle_inputs(). The DLL keeps exactly ONE HVD serialization
        buffer -- whichever vehicle GT_Step most recently resolved as
        ego/target -- so requesting any OTHER vehicle_id is refused by the DLL
        (returns None here) rather than silently handed back a different
        vehicle's bytes mislabeled as the one asked for.

        Returns None both when the DLL reports zero bytes and when the
        requested vehicle could not be served (wrong vehicle_id, HVD reporter
        not initialized, _USE_OSI not built in). Nothing distinguishes these
        cases at this layer -- treat None as a CAPTURE FAILURE for vehicle_id,
        exactly as get_osi_ground_truth() documents for its own None, never as
        "this vehicle legitimately has no HVD". None must never mean "this DLL
        doesn't have the export" either -- that ambiguity is exactly what the
        RuntimeError below exists to prevent; a caller that only checks for
        None would otherwise silently treat "wrong DLL" as "no HVD this frame".

        Raises RuntimeError, not a bare AttributeError, if this DLL predates
        the export (binding is lazy/tolerant -- see __init__ -- specifically
        so an older DLL can still be loaded and used for everything that
        isn't ManualDrive ADAS, e.g. an A/B bisect against a historical
        build). The message names the DLL path and states plainly that the
        DLL needs rebuilding or replacing; a VirtualDriver caller that never
        touches this method is unaffected either way.

        restype is c_void_p (not c_char_p): the payload is a serialized
        protobuf message that can contain embedded NUL bytes, which c_char_p
        would silently truncate at. ctypes.string_at(ptr, size) copies exactly
        `size` bytes regardless of content -- same reasoning as
        get_osi_ground_truth()."""
        if not self._has_hvd_api:
            raise RuntimeError(
                f"GT_GetOSIHostVehicleData is not exported by this DLL "
                f"({self.dll_path}). This DLL predates the ManualDrive-ADAS "
                f"C API (req-vd-ad:REQ-AD-028 段c) -- rebuild it, or point "
                f"--dll at a newer one, before calling get_osi_host_vehicle_data()."
            )
        size = ctypes.c_int(0)
        ptr = self.lib.GT_GetOSIHostVehicleData(vehicle_id, ctypes.byref(size))
        if not ptr or size.value <= 0:
            return None
        return ctypes.string_at(ptr, size.value)

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
