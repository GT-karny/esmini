#!/usr/bin/env python3
"""Start the GT_Sim Web API server.

Usage:
    python GT_esmini/web/start_server.py [--port 8000] [--host 127.0.0.1] [--reload]

Or with the DriverScript venv:
    DriverScript/.venv/Scripts/python.exe GT_esmini/web/start_server.py
"""

import argparse
import os
import sys
from pathlib import Path

# Ensure repo root is in PYTHONPATH
repo_root = Path(__file__).resolve().parents[2]
if str(repo_root) not in sys.path:
    sys.path.insert(0, str(repo_root))
os.chdir(str(repo_root))


def main():
    parser = argparse.ArgumentParser(description="GT_Sim Web API Server")
    parser.add_argument("--host", default="127.0.0.1", help="Host to bind (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8000, help="Port to bind (default: 8000)")
    parser.add_argument("--reload", action="store_true", help="Enable auto-reload for development")
    args = parser.parse_args()

    import uvicorn

    print(f"Starting GT_Sim Web API at http://{args.host}:{args.port}")
    print(f"  Swagger UI: http://{args.host}:{args.port}/docs")
    print(f"  ReDoc:      http://{args.host}:{args.port}/redoc")
    print(f"  Web UI:     http://{args.host}:{args.port}/")
    print()

    uvicorn.run(
        "GT_esmini.web.backend.main:app",
        host=args.host,
        port=args.port,
        reload=args.reload,
    )


if __name__ == "__main__":
    main()
