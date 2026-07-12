"""Frozen entry point for GT_Sim Web (PyInstaller).

Sets up the packaged environment and launches the uvicorn web server.

Usage (from the package root):
    server\\gt_sim_web.exe [--host 127.0.0.1] [--port 8000] [--no-browser]
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path


def get_package_root() -> Path:
    """Resolve the package root directory.

    In PyInstaller --onedir mode, sys.executable is at:
        GT_Sim_Web_v{VERSION}/server/gt_sim_web.exe
    So the package root is the parent of server/.
    """
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent.parent
    # Dev mode fallback (should not normally be reached)
    return Path(__file__).resolve().parents[3]


def setup_environment() -> Path:
    """Configure paths and environment for packaged mode.

    Returns the package root path.
    """
    pkg_root = get_package_root()

    # Set env var so config.py can detect packaged mode and resolve paths
    os.environ["GT_SIM_WEB_PACKAGE_ROOT"] = str(pkg_root)

    # Add external script directories to sys.path
    for subdir in ("scripts", "DriverScript"):
        d = str(pkg_root / subdir)
        if d not in sys.path:
            sys.path.insert(0, d)

    # Ensure writable data directories exist
    data_dir = pkg_root / "data"
    data_dir.mkdir(exist_ok=True)
    (data_dir / "results").mkdir(exist_ok=True)

    return pkg_root


def main() -> None:
    parser = argparse.ArgumentParser(description="GT_Sim Web Server")
    parser.add_argument("--host", default="127.0.0.1", help="Host to bind (default: 127.0.0.1)")
    # Default port resolves to config.HTTP_PORT (8000 unless GT_SIM_HTTP_PORT is set).
    # Left as None here because config.py must be imported AFTER setup_environment()
    # sets GT_SIM_WEB_PACKAGE_ROOT (see below).
    parser.add_argument("--port", type=int, default=None, help="Port to bind (default: config HTTP_PORT, 8000)")
    parser.add_argument("--no-browser", action="store_true", help="Do not open browser on start")
    args = parser.parse_args()

    pkg_root = setup_environment()

    if args.port is None:
        from GT_esmini.web.backend.config import HTTP_PORT
        args.port = HTTP_PORT

    import uvicorn

    print("=" * 50)
    print("  GT_Sim Web Server")
    print("=" * 50)
    print()
    print(f"  Package root: {pkg_root}")
    print(f"  Web UI:       http://{args.host}:{args.port}/")
    print(f"  Swagger API:  http://{args.host}:{args.port}/docs")
    print()
    print("  Press Ctrl+C to stop.")
    print()

    # Open browser after a short delay
    if not args.no_browser:
        import threading
        import webbrowser

        url = f"http://{args.host}:{args.port}/"
        threading.Timer(1.5, lambda: webbrowser.open(url)).start()

    # --- Signal handling for clean shutdown ---
    import atexit
    import signal

    # Convert SIGBREAK (Windows CTRL_CLOSE_EVENT) to SIGINT
    # so uvicorn's graceful shutdown path is triggered.
    if hasattr(signal, "SIGBREAK"):
        def _on_sigbreak(signum, frame):
            os.kill(os.getpid(), signal.SIGINT)

        signal.signal(signal.SIGBREAK, _on_sigbreak)

    # atexit fallback: kill orphaned subprocesses if lifespan didn't complete
    def _atexit_cleanup():
        try:
            from GT_esmini.web.backend.services.simulation_runner import kill_all_running

            killed = kill_all_running()
            if killed:
                print(f"[atexit] Killed {killed} orphaned GT_Sim process(es)")
        except Exception:
            pass  # Module may already be torn down

    atexit.register(_atexit_cleanup)

    # setup_environment() has set GT_SIM_WEB_PACKAGE_ROOT, so config.py resolves
    # the packaged data dir before we build the file-handler path.
    from GT_esmini.web.backend.logging_config import setup_logging
    log_config = setup_logging()

    uvicorn.run(
        "GT_esmini.web.backend.main:app",
        host=args.host,
        port=args.port,
        reload=False,  # Never reload in frozen mode
        log_config=log_config,
        log_level="info",
    )


if __name__ == "__main__":
    main()
