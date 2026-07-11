"""Backend logging configuration (audit WEB-1 / WEB-4).

The backend had no logging config, so PyInstaller/Electron startups — which reach
the app via ``uvicorn.run("...backend.main:app")`` rather than a dev shell — had
only uvicorn's default sink, and job start/failure/kill diagnostics could be
dropped. ``setup_logging`` installs a stderr StreamHandler plus a rotating file
handler on the root and uvicorn loggers, and is called at import of ``backend.main``
so every startup path (main.__main__, start_server.py, pyinstaller entry) gets it.
"""

from __future__ import annotations

import logging.config
from pathlib import Path

_configured = False


def _log_dir() -> Path:
    """Resolve the log directory next to the results/data directory (config.py)."""
    from GT_esmini.web.backend.config import RESULTS_DIR

    return RESULTS_DIR.parent / "logs"


def build_config(log_file: Path, level: str = "INFO") -> dict:
    """Build the dictConfig applied to root + uvicorn loggers."""
    handlers = {
        "stderr": {
            "class": "logging.StreamHandler",
            "stream": "ext://sys.stderr",
            "formatter": "default",
            "level": level,
        },
        "file": {
            "class": "logging.handlers.RotatingFileHandler",
            "filename": str(log_file),
            "maxBytes": 5 * 1024 * 1024,
            "backupCount": 3,
            "encoding": "utf-8",
            "formatter": "default",
            "level": level,
        },
    }
    logger = {"level": level, "handlers": ["stderr", "file"], "propagate": False}
    return {
        "version": 1,
        "disable_existing_loggers": False,
        "formatters": {
            "default": {"format": "%(asctime)s %(levelname)s %(name)s: %(message)s"},
        },
        "handlers": handlers,
        "root": {"level": level, "handlers": ["stderr", "file"]},
        "loggers": {
            "uvicorn": dict(logger),
            "uvicorn.error": dict(logger),
            "uvicorn.access": dict(logger),
        },
    }


def setup_logging(level: str = "INFO") -> dict:
    """Apply the backend logging config (idempotent) and return the dict.

    The returned dict is passed to ``uvicorn.run(log_config=...)`` so uvicorn's
    own access/error logs route through the same stderr + rotating-file sinks.
    """
    global _configured
    log_file = _log_dir() / "backend.log"
    try:
        log_file.parent.mkdir(parents=True, exist_ok=True)
    except OSError:
        # Read-only layout: fall back to stderr-only so startup still logs.
        config = build_config(log_file, level)
        config["handlers"].pop("file", None)
        config["root"]["handlers"] = ["stderr"]
        for name in ("uvicorn", "uvicorn.error", "uvicorn.access"):
            config["loggers"][name]["handlers"] = ["stderr"]
        logging.config.dictConfig(config)
        _configured = True
        return config
    config = build_config(log_file, level)
    logging.config.dictConfig(config)
    _configured = True
    return config
